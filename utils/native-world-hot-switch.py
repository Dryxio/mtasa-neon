#!/usr/bin/env python3
"""Headless Parallels driver for the native-world runtime lifecycle.

The client-side command channel is disabled unless the launched Core inherited
MTA_NATIVE_WORLD_HARNESS=1. This driver is intentionally strict: it accepts
numeric IPv4 endpoints only, bounds every wait, keeps the GTA PID stable after
the initial authorization restart, and writes exactly one terminal PASS/FAIL
record to its JSONL trace.
"""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import json
import pathlib
import re
import subprocess
import sys
import time
import uuid


VM_NAME = "Windows 11"
BIN = r"C:\dev\mtasa-vm-custom\Bin"
GTA = r"C:\dev\GTA-SA\gta_sa.exe"
LOG = BIN + r"\MTA\logs\logfile.txt"
COMMAND = BIN + r"\MTA\logs\native-world-hot-switch.command"
COMMAND_TMP = BIN + r"\MTA\logs\native-world-hot-switch.tmp"
EXPECTED_GTA_SHA256 = "A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26"
FATAL = re.compile(
    r"(?:\[FATAL\]|registrar=fatal|Unhandled exception|Assertion failed|CCrashDumpWriter::HandleExceptionGlobal|SEH:AccessViolation|Fatal Exception:\s*Yes)",
    re.IGNORECASE,
)
ENDPOINT = re.compile(r"^(?P<host>(?:\d{1,3}\.){3}\d{1,3}):(?P<port>\d{1,5})$")


class HarnessFailure(RuntimeError):
    pass


def powershell_encoded(script: str) -> str:
    return base64.b64encode(script.encode("utf-16le")).decode("ascii")


def vm_exec(argv: list[str], *, current_user: bool = False, timeout: float = 30.0) -> str:
    command = ["prlctl", "exec", VM_NAME]
    if current_user:
        command.append("--current-user")
    command.extend(argv)
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, check=False)
    output = result.stdout.decode("utf-8", errors="replace")
    if result.returncode:
        raise HarnessFailure(f"VM command failed ({result.returncode}): {output.strip()}")
    return output


def ps(script: str, *, current_user: bool = False, timeout: float = 30.0) -> str:
    quiet_script = "$ProgressPreference='SilentlyContinue'; " + script
    return vm_exec(["powershell.exe", "-NoProfile", "-EncodedCommand", powershell_encoded(quiet_script)], current_user=current_user, timeout=timeout)


def parse_endpoint(value: str) -> tuple[str, int]:
    match = ENDPOINT.fullmatch(value)
    if not match:
        raise argparse.ArgumentTypeError("endpoint must be a numeric IPv4 address followed by :port")
    octets = [int(part) for part in match.group("host").split(".")]
    port = int(match.group("port"))
    if any(part > 255 for part in octets) or not 1 <= port <= 65535:
        raise argparse.ArgumentTypeError("endpoint is outside the numeric IPv4/port range")
    return match.group("host"), port


class Trace:
    def __init__(self, path: pathlib.Path, run_id: str) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self._file = path.open("x", encoding="utf-8")
        self._run_id = run_id
        self._seq = 0
        self._start = time.monotonic()
        self.terminal = False

    def emit(self, phase: str, event: str, outcome: str, **fields: object) -> None:
        self._seq += 1
        record = {
            "runId": self._run_id,
            "seq": self._seq,
            "utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "monotonicMs": round((time.monotonic() - self._start) * 1000),
            "phase": phase,
            "event": event,
            "outcome": outcome,
            **fields,
        }
        self._file.write(json.dumps(record, sort_keys=True) + "\n")
        self._file.flush()

    def verdict(self, verdict: str, reason: str, **fields: object) -> None:
        if self.terminal:
            return
        self.terminal = True
        self.emit("terminal", "verdict", verdict, verdict=verdict, reason=reason, **fields)

    def close(self) -> None:
        self._file.close()


class Driver:
    def __init__(self, trace: Trace, poll_seconds: float, allow_foreign_processes: bool = False) -> None:
        self.trace = trace
        self.poll_seconds = poll_seconds
        self.allow_foreign_processes = allow_foreign_processes
        self.log_text = ""
        self.cursor = 0
        self.stable_pid: int | None = None
        self.loader_pid: int | None = None
        self.initial_dumps = crash_dump_fingerprint()

    def refresh_log(self) -> str:
        rotated = LOG + ".1"
        script = (
            "$utf8 = [Text.UTF8Encoding]::new($false); "
            "[Console]::OutputEncoding = $utf8; "
            f"$paths = @('{rotated}', '{LOG}'); "
            "$paths | Where-Object { [IO.File]::Exists($_) } | "
            "ForEach-Object { [Console]::Write([IO.File]::ReadAllText($_, $utf8)) }"
        )
        self.log_text = ps(script, timeout=45.0)
        if self.cursor > len(self.log_text):
            self.cursor = 0
        return self.log_text

    def stable_process_is_alive(self) -> tuple[bool, list[int]]:
        current, loaders, parents = process_topology()
        if self.stable_pid is None:
            return True, current
        if self.allow_foreign_processes:
            alive = self.stable_pid in current and self.loader_pid in loaders and parents.get(self.stable_pid) == self.loader_pid
        else:
            alive = current == [self.stable_pid] and loaders == [self.loader_pid] and parents.get(self.stable_pid) == self.loader_pid
        return alive, current

    def wait(self, phase: str, pattern: str, timeout: float, *, start: int | None = None) -> re.Match[str]:
        expression = re.compile(pattern, re.IGNORECASE)
        deadline = time.monotonic() + timeout
        search_from = self.cursor if start is None else start
        while time.monotonic() < deadline:
            if self.loader_pid is not None:
                dismissed = dismiss_loader_startup_false_positive(self.loader_pid)
                if dismissed:
                    self.trace.emit(phase, "loader-slow-start-dialog", "dismissed", launcherPids=dismissed, response="no")
            if self.stable_pid is not None:
                stable_alive, current = self.stable_process_is_alive()
                if not stable_alive:
                    dumps = crash_dump_fingerprint()
                    _, failure_text = rotated_loader_log()
                    fatal_matches = list(FATAL.finditer(failure_text))
                    fatal_line = ""
                    if fatal_matches:
                        fatal = fatal_matches[-1]
                        line_end = failure_text.find("\n", fatal.start())
                        fatal_line = failure_text[fatal.start() : line_end if line_end >= 0 else len(failure_text)].strip()
                    if dumps != self.initial_dumps:
                        self.trace.emit(
                            phase,
                            "crash-dump",
                            "FAIL",
                            expected=self.initial_dumps,
                            actual=dumps,
                            matchedLine=fatal_line,
                        )
                    else:
                        fatal_line = ""
                    self.trace.emit(phase, "process-stability", "FAIL", expectedPid=self.stable_pid, actualPids=current)
                    detail = f"; crash evidence: {fatal_line}" if fatal_line else ""
                    raise HarnessFailure(f"GTA process changed during {phase}: expected {self.stable_pid}, got {current}{detail}")
            text = self.refresh_log()
            # A newly launched loader rotates logfile.txt. The combined
            # `.1 + current` stream can then be shorter than the previous
            # absolute offset even though it still contains the unread tail.
            # refresh_log resets self.cursor in that case; keep this wait's
            # local search origin in sync so the terminal fallback markers
            # written immediately before process exit remain observable.
            if search_from > len(text):
                search_from = self.cursor
            fatal = FATAL.search(text, search_from)
            match = expression.search(text, search_from)
            if fatal and (not match or fatal.start() <= match.start()):
                line = text[fatal.start() : text.find("\n", fatal.start()) if "\n" in text[fatal.start() :] else len(text)]
                self.trace.emit(phase, "fatal-log", "FAIL", matchedLine=line.strip())
                raise HarnessFailure(f"fatal client log during {phase}: {line.strip()}")
            if match:
                line_end = text.find("\n", match.start())
                if line_end < 0:
                    line_end = len(text)
                self.cursor = line_end
                line = text[text.rfind("\n", 0, match.start()) + 1 : line_end].strip()
                self.trace.emit(phase, "marker", "ok", matchedLine=line)
                return match
            time.sleep(self.poll_seconds)
        self.trace.emit(phase, "timeout", "FAIL", timeoutSeconds=timeout, pattern=pattern)
        raise HarnessFailure(f"timeout waiting for {phase}: {pattern}")

    def send(self, command: str) -> None:
        if not re.fullmatch(
            r"(?:status|begin|teardown|disconnect|auth-restart|refuse-next-drain|disarm-drain-refusal|connect (?:\d{1,3}\.){3}\d{1,3} \d{1,5})",
            command,
        ):
            raise HarnessFailure(f"refusing command outside the closed grammar: {command}")
        escaped = command.replace("'", "''")
        script = (
            f"[IO.File]::WriteAllText('{COMMAND_TMP}', '{escaped}', [Text.Encoding]::ASCII); "
            f"Move-Item -LiteralPath '{COMMAND_TMP}' -Destination '{COMMAND}' -Force"
        )
        ps(script, current_user=True)
        self.trace.emit("command", command.split()[0], "sent", command=command)


def gta_pids() -> list[int]:
    output = ps("@(Get-Process gta_sa -ErrorAction SilentlyContinue) | ForEach-Object { [Console]::WriteLine($_.Id) }; exit 0")
    return [int(line) for line in output.splitlines() if line.strip().isdigit()]


def process_topology() -> tuple[list[int], list[int], dict[int, int]]:
    script = (
        "Get-CimInstance Win32_Process | Where-Object { $_.Name -in @('gta_sa.exe', 'Multi Theft Auto.exe') } | "
        "ForEach-Object { [Console]::WriteLine(('{0}|{1}|{2}' -f $_.Name,$_.ProcessId,$_.ParentProcessId)) }"
    )
    gta: list[int] = []
    loaders: list[int] = []
    parents: dict[int, int] = {}
    for line in ps(script).splitlines():
        parts = line.strip().split("|")
        if len(parts) != 3 or not parts[1].isdigit() or not parts[2].isdigit():
            continue
        name, pid, parent = parts[0].lower(), int(parts[1]), int(parts[2])
        if name == "gta_sa.exe":
            gta.append(pid)
            parents[pid] = parent
        elif name == "multi theft auto.exe":
            loaders.append(pid)
    return sorted(gta), sorted(loaders), parents


def crash_dump_fingerprint() -> list[str]:
    script = (
        f"Get-ChildItem -LiteralPath '{BIN}' -Filter '*.dmp' -File -Recurse -ErrorAction SilentlyContinue | "
        "Sort-Object FullName | ForEach-Object { [Console]::WriteLine(('{0}|{1}|{2}' -f $_.FullName,$_.Length,$_.LastWriteTimeUtc.Ticks)) }"
    )
    return [line.strip() for line in ps(script, timeout=60.0).splitlines() if "|" in line]


def rotated_loader_log() -> tuple[str, str]:
    rotated = LOG + ".1"
    script = (
        "$utf8 = [Text.UTF8Encoding]::new($false); "
        f"$paths = @('{rotated}', '{LOG}'); "
        "$stamp = ($paths | Where-Object { [IO.File]::Exists($_) } | ForEach-Object { (Get-Item -LiteralPath $_).LastWriteTimeUtc.Ticks }) -join ','; "
        "[Console]::WriteLine($stamp); "
        "$paths | Where-Object { [IO.File]::Exists($_) } | ForEach-Object { [Console]::Write([IO.File]::ReadAllText($_, $utf8)) }"
    )
    output = ps(script, timeout=45.0)
    stamp, _, text = output.partition("\n")
    return stamp.strip(), text


def dismiss_loader_startup_false_positive(loader_pid: int) -> list[int]:
    """Answer No to the loader's CL25 slow-start warning, if present.

    Large native-world sets can legitimately keep L3 open past the loader's
    historical stuck-process threshold.  A modal CL25 then blocks the loader
    from observing a later clean GTA exit and executing OnQuitCommand.  The
    harness is already exclusive, so it may close only this exact dialog while
    leaving every other window and every game process untouched.
    """
    script = r"""
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NativeWorldHarnessWindows {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
}
'@
$processes = Get-Process -Id __LOADER_PID__ -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowTitle -like '*[[]CL25]' -and $_.MainWindowHandle -ne 0 }
foreach ($process in $processes) {
    if ([NativeWorldHarnessWindows]::PostMessage($process.MainWindowHandle, 0x0111, [IntPtr]7, [IntPtr]::Zero)) {
            [Console]::WriteLine($process.Id)
        }
}
""".replace("__LOADER_PID__", str(loader_pid))
    try:
        output = ps(script, current_user=True)
    except HarnessFailure:
        # Parallels can briefly reject --current-user commands while the
        # loader replaces its GTA child. This probe is repeated every poll;
        # lifecycle commands and log assertions remain fail-closed.
        return []
    return [int(line) for line in output.splitlines() if line.strip().isdigit()]


def launch_client(endpoint: tuple[str, int]) -> int:
    host, port = endpoint
    script = (
        "$env:MTA_NATIVE_WORLD_HARNESS='1'; "
        f"$process = Start-Process -FilePath '{BIN}\\Multi Theft Auto.exe' -ArgumentList 'mtasa://{host}:{port}' "
        f"-WorkingDirectory '{BIN}' -PassThru; [Console]::WriteLine($process.Id)"
    )
    output = ps(script, current_user=True)
    pids = [int(line) for line in output.splitlines() if line.strip().isdigit()]
    if len(pids) != 1:
        raise HarnessFailure(f"client launch did not return one loader PID: {output.strip()}")
    return pids[0]


def preflight(trace: Trace, attach: bool, attach_pid: int | None) -> None:
    status = subprocess.run(["prlctl", "status", VM_NAME], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    if status.returncode or "running" not in status.stdout.decode("utf-8", errors="replace").lower():
        raise HarnessFailure(f"{VM_NAME} is not running")
    sha_output = ps(f"(Get-FileHash -Algorithm SHA256 -LiteralPath '{GTA}').Hash")
    sha_match = re.search(r"\b[0-9A-Fa-f]{64}\b", sha_output)
    sha = sha_match.group(0).upper() if sha_match else ""
    if sha != EXPECTED_GTA_SHA256:
        raise HarnessFailure(f"unexpected GTA executable hash: {sha_output.strip()}")
    owner = ps(
        "$path = Join-Path $env:LOCALAPPDATA 'MTA San Andreas All'; "
        "$expected = $env:USERDOMAIN + '\\' + $env:USERNAME; "
        "$actual = (Get-Acl -LiteralPath $path).Owner; "
        "[Console]::WriteLine($expected); [Console]::WriteLine($actual)"
    , current_user=True).splitlines()
    clean_owner = [line.strip() for line in owner if "\\" in line]
    if len(clean_owner) < 2 or clean_owner[-2].lower() != clean_owner[-1].lower():
        raise HarnessFailure(f"authorization store owner mismatch: {clean_owner}")
    pids, loaders, parents = process_topology()
    if attach_pid is not None:
        parent = parents.get(attach_pid)
        if attach_pid not in pids or parent not in loaders:
            raise HarnessFailure(
                f"attach PID does not identify a live loader/GTA pair: requested={attach_pid} gta={pids} loaders={loaders} parents={parents}"
            )
    elif attach and (len(pids) != 1 or len(loaders) != 1 or parents.get(pids[0]) != loaders[0]):
        raise HarnessFailure(f"attach mode requires one exclusive loader/GTA pair, found gta={pids} loaders={loaders} parents={parents}")
    if not attach and attach_pid is None and (pids or loaders):
        raise HarnessFailure(f"launch mode refuses existing MTA processes: gta={pids} loaders={loaders}")
    trace.emit(
        "preflight",
        "environment",
        "ok",
        gtaSha256=sha,
        authorizationOwner=clean_owner[-1],
        existingPids=pids,
        existingLoaders=loaders,
        attachedPid=attach_pid,
    )


def neutral_signature(text: str, start: int) -> dict[str, object]:
    segment = text[start:]

    def last_match(pattern: str) -> re.Match[str] | None:
        matches = list(re.finditer(pattern, segment, re.IGNORECASE))
        return matches[-1] if matches else None

    # WriteDebugEvent may shed individual telemetry records when per-field
    # baseline instrumentation is especially verbose. Prefer the final
    # session-release sample; otherwise use the exact Detached postcondition
    # which precedes Core's independently required endpoint-release marker.
    context = "session-release-neutral-postcondition"
    sample = last_match(rf"state=sample context={context}[^\n]*")
    if not sample:
        context = "teardown-detached-postcondition"
        sample = last_match(rf"state=sample context={context}[^\n]*")
    capacity = last_match(rf"state=capacity context={context}[^\n]*")
    pools = last_match(rf"state=pools context={context}[^\n]*")
    streaming = last_match(rf"state=streaming context={context}[^\n]*")
    if not all((sample, capacity, pools, streaming)):
        raise HarnessFailure("neutral telemetry record is incomplete")

    def find(pattern: str, value: str) -> str:
        match = re.search(pattern, value)
        if not match:
            raise HarnessFailure(f"neutral telemetry omitted {pattern}")
        return match.group(1)

    pool_values: dict[str, dict[str, int]] = {}
    for name, occupied, capacity_value, first_free, high in re.findall(
        r"(txd|col|ipl|building|colModel|quadTreeNode|ptrNodeSingleLink)=([0-9]+)/([0-9]+) firstFree=(-?[0-9]+) high=(-?[0-9]+)",
        pools.group(0),
    ):
        pool_values[name] = {
            "occupied": int(occupied),
            "capacity": int(capacity_value),
            "firstFree": int(first_free),
            "high": int(high),
        }
    if len(pool_values) != 7:
        raise HarnessFailure(f"neutral telemetry omitted pool detail: {pools.group(0)}")

    return {
        "stores": find(r"stores=([0-9]+/[0-9]+,[0-9]+/[0-9]+,[0-9]+/[0-9]+)", capacity.group(0)),
        "pools": pool_values,
        "boundArchives": int(find(r"bound=([0-9]+)", streaming.group(0))),
        "streamHandles": int(find(r"streamHandles=([0-9]+)", streaming.group(0))),
        "archiveHash": find(r"archiveHash=([0-9a-f]+)", streaming.group(0)),
        "handleHash": find(r"handleHash=([0-9a-f]+)", streaming.group(0)),
        "streamRequests": int(find(r"requests=([0-9]+)", streaming.group(0))),
        "workerBusy": int(find(r"workerBusy=([0-9]+)", streaming.group(0))),
        "memory": int(find(r"memory=([0-9]+)", streaming.group(0))),
        "ioQuiescent": find(r"io-quiescent=(yes|no)", sample.group(0)),
        "leaseObjects": int(find(r"leaseObjects=([0-9]+)", sample.group(0))),
        "cacheHandles": find(r"cacheHandles=([0-9]+,[0-9]+)", sample.group(0)),
        "contentNeutral": find(r"content-neutral=(yes|no)", sample.group(0)),
        # A Detached sample deliberately reports session-neutral=no. The
        # caller records this signature only after Core has logged exact
        # endpoint release, which is the independent session-neutral proof.
        "sessionNeutral": "yes",
    }


def invariant_neutral_signature(signature: dict[str, object]) -> dict[str, object]:
    pools = signature["pools"]
    assert isinstance(pools, dict)
    result = {key: value for key, value in signature.items() if key not in ("pools", "memory")}
    # GTA resources legitimately churn Building/ColModel slots during a
    # session. PtrNodeSingleLink also backs their sector lists, so its
    # occupied high-water is bounded separately below. Native TXD/COL/IPL
    # ownership and the persistent quadtree topology must be exact.
    result["pools"] = {name: pools[name] for name in ("txd", "col", "ipl", "quadTreeNode")}
    result["dynamicPoolCapacities"] = {name: pools[name]["capacity"] for name in ("building", "colModel", "ptrNodeSingleLink")}
    return result


def dynamic_neutral_metrics(signature: dict[str, object]) -> dict[str, int]:
    pools = signature["pools"]
    assert isinstance(pools, dict)
    return {
        "buildingOccupied": pools["building"]["occupied"],
        "colModelOccupied": pools["colModel"]["occupied"],
        "ptrNodeSingleLinkOccupied": pools["ptrNodeSingleLink"]["occupied"],
        "streamingMemory": signature["memory"],
    }


def run(args: argparse.Namespace, trace: Trace) -> None:
    driver = Driver(trace, args.poll, allow_foreign_processes=args.attach_pid is not None)
    preflight(trace, args.attach, args.attach_pid)
    endpoints = args.endpoint
    attached_active = False
    if args.attach or args.attach_pid is not None:
        gta, loaders, parents = process_topology()
        attached_pid = args.attach_pid if args.attach_pid is not None else gta[0]
        driver.loader_pid = parents[attached_pid]
        driver.refresh_log()
        driver.cursor = len(driver.log_text)
        status_start = driver.cursor
        driver.send("status")
        driver.wait("attach-status", r"\[NativeWorldHarness\].*command=nativeworlddrain status", args.command_timeout, start=status_start)
        driver.wait("attach-executed", r"\[NativeWorldHarness\] state=executed.*command=nativeworlddrain status", args.command_timeout, start=status_start)
        attached_active = True
    else:
        previous_write = ps(f"if (Test-Path -LiteralPath '{LOG}') {{ (Get-Item -LiteralPath '{LOG}').LastWriteTimeUtc.Ticks }}").strip()
        driver.loader_pid = launch_client(endpoints[0])
        trace.emit("bootstrap", "client-launch", "ok", endpoint=f"{endpoints[0][0]}:{endpoints[0][1]}")
        deadline = time.monotonic() + 90.0
        while time.monotonic() < deadline:
            dismissed = dismiss_loader_startup_false_positive(driver.loader_pid)
            if dismissed:
                trace.emit("bootstrap", "loader-slow-start-dialog", "dismissed", launcherPids=dismissed, response="no")
            pids = gta_pids()
            current_write = ps(f"if (Test-Path -LiteralPath '{LOG}') {{ (Get-Item -LiteralPath '{LOG}').LastWriteTimeUtc.Ticks }}").strip()
            if len(pids) == 1 and current_write and current_write != previous_write:
                driver.cursor = 0
                break
            time.sleep(args.poll)
        else:
            raise HarnessFailure("launched client did not create one GTA process and a fresh log")

    bootstrap_start = driver.cursor
    deadline = time.monotonic() + args.bootstrap_timeout
    initial_active = attached_active
    while not initial_active and time.monotonic() < deadline:
        if driver.loader_pid is not None:
            dismissed = dismiss_loader_startup_false_positive(driver.loader_pid)
            if dismissed:
                trace.emit("bootstrap", "loader-slow-start-dialog", "dismissed", launcherPids=dismissed, response="no")
        current_pids = gta_pids()
        if len(current_pids) != 1:
            raise HarnessFailure(f"GTA process changed during bootstrap: expected one process, got {current_pids}")
        text = driver.refresh_log()
        active = re.search(r"\[NativeWorld\] registrar=active", text[bootstrap_start:], re.IGNORECASE)
        pending = re.search(r"\[NativeWorldAuthorization\] state=pending.*restart-required=yes", text[bootstrap_start:], re.IGNORECASE)
        fatal = FATAL.search(text, bootstrap_start)
        if fatal:
            raise HarnessFailure("fatal client log during bootstrap")
        if active:
            driver.cursor = bootstrap_start + active.end()
            initial_active = True
            break
        if pending:
            before = gta_pids()
            if len(before) != 1:
                raise HarnessFailure(f"authorization restart has ambiguous GTA process set: {before}")
            driver.send("auth-restart")
            trace.emit("bootstrap", "authorization-restart", "ok", oldPid=before[0])
            bootstrap_start = 0
            driver.cursor = 0
            driver.wait("authorized-selection", r"\[NativeWorldAuthorization\] state=selected", args.bootstrap_timeout)
            driver.wait("initial-active", r"\[NativeWorld\] registrar=active", args.bootstrap_timeout)
            initial_active = True
            break
        time.sleep(args.poll)
    if not initial_active:
        raise HarnessFailure("bootstrap did not reach Active")

    stable = gta_pids()
    if args.attach_pid is not None:
        if args.attach_pid not in stable:
            raise HarnessFailure(f"attached GTA process disappeared before Active A: expected {args.attach_pid}, got {stable}")
        stable_pid = args.attach_pid
    else:
        if len(stable) != 1:
            raise HarnessFailure(f"Active A has ambiguous GTA process set: {stable}")
        stable_pid = stable[0]
    topology_gta, topology_loaders, topology_parents = process_topology()
    stable_loader = topology_parents.get(stable_pid)
    topology_valid = stable_loader in topology_loaders
    if args.attach_pid is None:
        topology_valid = topology_valid and topology_gta == [stable_pid] and topology_loaders == [stable_loader]
    if not topology_valid:
        raise HarnessFailure(
            f"Active A does not have the required loader parent: gta={topology_gta} loaders={topology_loaders} parents={topology_parents}"
        )
    driver.loader_pid = stable_loader
    driver.stable_pid = stable_pid
    trace.emit("active-a", "pid-locked", "ok", pid=stable_pid)
    dismissed = dismiss_loader_startup_false_positive(driver.loader_pid)
    if dismissed:
        trace.emit("active-a", "loader-slow-start-dialog", "dismissed", launcherPids=dismissed, response="no")

    if args.restart_fallback:
        target = endpoints[1]
        target_text = f"{target[0]}:{target[1]}"
        cycle_start = len(driver.refresh_log())
        driver.cursor = cycle_start
        loader_stamp, loader_text_before = rotated_loader_log()
        old_loader_executions = set(
            re.findall(r"[^\n]*Executing OnQuitCommand: op=open, file=[^\n]*Multi Theft Auto\.exe[^\n]*", loader_text_before, re.IGNORECASE)
        )
        driver.send("refuse-next-drain")
        driver.wait("restart-fallback-arm", r"\[NativeWorldHarness\] state=armed action=refuse-next-drain", args.command_timeout)
        # A verified fallback calls Quit immediately after its exact registry
        # readback. The old process can therefore exit before the next log
        # poll observes the queued marker; the required/scheduled proof below
        # remains the authority for distinguishing this from an early crash.
        driver.stable_pid = None
        try:
            driver.send(f"connect {target[0]} {target[1]}")
            driver.wait(
                "restart-fallback-queued",
                rf"\[NativeWorldHotSwitch\] state=connection-queued target={re.escape(target_text)}",
                args.command_timeout,
            )
        except BaseException:
            try:
                driver.send("disarm-drain-refusal")
            except (HarnessFailure, subprocess.TimeoutExpired, OSError):
                pass
            raise
        # Quit follows the verified registry readback immediately. The old GTA
        # process can disappear before the next one-second poll, so preserve
        # the proof from logfile.txt or its first rotated generation instead
        # of treating the expected exit as instability.
        deadline = time.monotonic() + args.command_timeout
        proof_text = ""
        required: re.Match[str] | None = None
        scheduled: re.Match[str] | None = None
        while time.monotonic() < deadline:
            _, proof_text = rotated_loader_log()
            armed_matches = list(re.finditer(r"[^\n]*state=armed action=refuse-next-drain[^\n]*", proof_text, re.IGNORECASE))
            proof_start = armed_matches[-1].end() if armed_matches else len(proof_text)
            required_expression = re.compile(
                r"[^\n]*state=restart-fallback-required[^\n]*session=active-preserved[^\n]*", re.IGNORECASE
            )
            scheduled_expression = re.compile(
                rf"[^\n]*state=restart-fallback-scheduled endpoint={re.escape(target_text)}[^\n]*readback=exact[^\n]*", re.IGNORECASE
            )
            required = required_expression.search(proof_text, proof_start)
            scheduled = scheduled_expression.search(proof_text, required.end() if required else proof_start)
            if required and scheduled:
                break
            time.sleep(args.poll)
        if not required or not scheduled:
            raise HarnessFailure("restart fallback exited without durable required/scheduled proof")
        trace.emit("restart-fallback-required", "marker", "ok", matchedLine=required.group(0).strip())
        trace.emit("restart-fallback-scheduled", "marker", "ok", matchedLine=scheduled.group(0).strip())
        segment = proof_text[required.start():scheduled.end()]
        if re.search(r"drain=quiescent|teardown=begin|teardown=detached|session-release=neutral", segment, re.IGNORECASE):
            raise HarnessFailure("forced drain refusal mutated the reusable lifecycle before restart scheduling")
        trace.emit("restart-fallback", "pre-exit-invariants", "ok", pid=stable_pid, target=target_text)

        # Process replacement is the expected result only after the exact
        # loader command has been read back and logged as scheduled.
        deadline = time.monotonic() + 90.0
        replacement_pid: int | None = None
        while time.monotonic() < deadline:
            dismissed = dismiss_loader_startup_false_positive(driver.loader_pid)
            if dismissed:
                trace.emit("restart-fallback", "loader-slow-start-dialog", "dismissed", launcherPids=dismissed, response="no")
            current = gta_pids()
            if len(current) == 1 and current[0] != stable_pid:
                replacement_pid = current[0]
                break
            if len(current) > 1:
                raise HarnessFailure(f"restart fallback created ambiguous GTA processes: {current}")
            time.sleep(args.poll)
        if replacement_pid is None:
            raise HarnessFailure("verified restart fallback did not replace the GTA process")
        _, replacement_loaders, replacement_parents = process_topology()
        if len(replacement_loaders) != 1 or replacement_parents.get(replacement_pid) != replacement_loaders[0]:
            raise HarnessFailure(
                f"replacement GTA does not have one exclusive loader parent: loaders={replacement_loaders} parents={replacement_parents}"
            )
        driver.loader_pid = replacement_loaders[0]

        deadline = time.monotonic() + 30.0
        loader_proof = ""
        while time.monotonic() < deadline:
            current_stamp, loader_text = rotated_loader_log()
            if current_stamp != loader_stamp:
                # The loader validates the registry verb `restart`, resolves
                # it to the trusted installed executable, then deliberately
                # logs the ShellExecute verb as `open`.
                matches = re.findall(
                    r"[^\n]*Executing OnQuitCommand: op=open, file=[^\n]*Multi Theft Auto\.exe[^\n]*", loader_text, re.IGNORECASE
                )
                new_matches = [line for line in matches if line not in old_loader_executions]
                if new_matches:
                    loader_proof = new_matches[-1].strip()
                    break
            time.sleep(args.poll)
        if not loader_proof:
            raise HarnessFailure("loader restart execution was not preserved in the rotated log")
        trace.emit("restart-fallback", "process-replaced", "ok", oldPid=stable_pid, newPid=replacement_pid, loaderLine=loader_proof)

        # A fallback starts from a clean process. If the target publishes a
        # fresh authorization, complete its normal one-time authorization
        # restart before requiring Active again.
        replacement_start: int | None = None
        replacement_pattern = re.compile(rf"Loader - Process ID:\s*{replacement_pid}\b", re.IGNORECASE)
        deadline = time.monotonic() + 30.0
        while time.monotonic() < deadline:
            text = driver.refresh_log()
            matches = list(replacement_pattern.finditer(text))
            if matches:
                replacement_start = matches[-1].start()
                break
            time.sleep(args.poll)
        if replacement_start is None:
            raise HarnessFailure(f"replacement GTA {replacement_pid} has no current-run loader marker")
        driver.cursor = replacement_start
        deadline = time.monotonic() + args.bootstrap_timeout
        final_pid = replacement_pid
        final_ticket: str | None = None
        authorization_restart_sent = False
        while time.monotonic() < deadline:
            dismissed = dismiss_loader_startup_false_positive(driver.loader_pid)
            if dismissed:
                trace.emit("restart-fallback", "loader-slow-start-dialog", "dismissed", launcherPids=dismissed, response="no")
            text = driver.refresh_log()
            current_run_text = text[replacement_start:]
            fatal = FATAL.search(current_run_text)
            if fatal:
                raise HarnessFailure("fatal client log after restart fallback")
            active = re.search(r"\[NativeWorldAuthorization\] state=active.*lease=process", current_run_text, re.IGNORECASE)
            no_restart = active and re.search(
                r"\[NativeWorldAuthorization\].*activation=active.*restart-required=no", current_run_text[active.end() :], re.IGNORECASE
            )
            if active and no_restart:
                ticket = re.search(r"\bticket=([0-9a-f]+)\b", active.group(0), re.IGNORECASE)
                final_ticket = ticket.group(1) if ticket else None
                break
            pending = re.search(r"\[NativeWorldAuthorization\] state=pending.*restart-required=yes", current_run_text, re.IGNORECASE)
            if pending and not authorization_restart_sent:
                driver.send("auth-restart")
                trace.emit("restart-fallback", "authorization-restart", "ok", pid=final_pid)
                authorization_restart_sent = True
                deadline = time.monotonic() + args.bootstrap_timeout
                time.sleep(args.poll)
                current = gta_pids()
                if len(current) == 1:
                    final_pid = current[0]
                    _, replacement_loaders, replacement_parents = process_topology()
                    if len(replacement_loaders) == 1 and replacement_parents.get(final_pid) == replacement_loaders[0]:
                        driver.loader_pid = replacement_loaders[0]
            time.sleep(args.poll)
        else:
            raise HarnessFailure("restart fallback target did not return to Active")
        current = gta_pids()
        if len(current) != 1:
            raise HarnessFailure(f"restart fallback ended with ambiguous GTA processes: {current}")
        _, final_proof_text = rotated_loader_log()
        selected_for_ticket = final_ticket and re.search(
            rf"\[NativeWorldAuthorization\] state=(?:selected|runtime-selected) endpoint={re.escape(target_text)}[^\n]*ticket={re.escape(final_ticket)}\b",
            final_proof_text,
            re.IGNORECASE,
        )
        if not selected_for_ticket:
            raise HarnessFailure(f"restart fallback Active state was not selected for target {target_text}")
        trace.emit("restart-fallback", "active", "ok", pid=current[0], target=target_text, ticket=final_ticket)
        return

    baseline: dict[str, object] | None = None
    dynamic_baseline: dict[str, int] | None = None
    dynamic_history: list[dict[str, int]] = []
    total_cycles = args.warmup_cycles + args.cycles
    for cycle in range(1, total_cycles + 1):
        target = endpoints[cycle % len(endpoints)]
        target_text = f"{target[0]}:{target[1]}"
        measured_cycle = cycle - args.warmup_cycles
        cycle_name = f"warmup-{cycle}" if measured_cycle <= 0 else f"cycle-{measured_cycle}"
        cycle_start = len(driver.refresh_log())
        driver.cursor = cycle_start
        driver.send(f"connect {target[0]} {target[1]}")
        driver.wait(f"{cycle_name}-queued", rf"\[NativeWorldHotSwitch\] state=connection-queued target={re.escape(target_text)}", args.command_timeout)
        driver.wait(f"{cycle_name}-quiescent", r"\[NativeWorld\] drain=quiescent", args.transition_timeout)
        driver.wait(f"{cycle_name}-detached", r"\[NativeWorld\] teardown=detached", args.transition_timeout)
        neutral_or_release = driver.wait(
            f"{cycle_name}-neutral",
            r"(?:context=session-release-neutral-postcondition.*session-neutral=yes|\[NativeWorldHotSwitch\] state=connection-released|"
            r"\[NativeWorldAuthorization\] state=release-refused)",
            args.transition_timeout,
        )
        neutral_line = neutral_or_release.group(0).lower()
        if "state=release-refused" in neutral_line:
            raise HarnessFailure(f"native-world session release was refused at {cycle_name}")
        signature = neutral_signature(driver.log_text, cycle_start)
        invariant_signature = invariant_neutral_signature(signature)
        dynamic_metrics = dynamic_neutral_metrics(signature)
        if measured_cycle == 1:
            baseline = invariant_signature
            dynamic_baseline = dynamic_metrics
        elif measured_cycle > 1 and invariant_signature != baseline:
            raise HarnessFailure(
                f"neutral ownership signature changed at measured cycle {measured_cycle}: expected {baseline}, got {invariant_signature}"
            )
        if measured_cycle > 0:
            assert dynamic_baseline is not None
            limits = {
                "buildingOccupied": dynamic_baseline["buildingOccupied"] + 128,
                "colModelOccupied": dynamic_baseline["colModelOccupied"] + 128,
                "ptrNodeSingleLinkOccupied": dynamic_baseline["ptrNodeSingleLinkOccupied"] + 128,
                "streamingMemory": dynamic_baseline["streamingMemory"] + 16 * 1024 * 1024,
            }
            if any(dynamic_metrics[name] > limit for name, limit in limits.items()):
                raise HarnessFailure(f"dynamic neutral high-water exceeded its bound at cycle {measured_cycle}: values={dynamic_metrics} limits={limits}")
            dynamic_history.append(dynamic_metrics)
        trace.emit(
            cycle_name,
            "neutral-signature",
            "ok",
            pid=stable_pid,
            endpoint=target_text,
            measured=measured_cycle > 0,
            invariantSignature=invariant_signature,
            dynamicMetrics=dynamic_metrics,
        )
        if "state=connection-released" not in neutral_line:
            driver.wait(f"{cycle_name}-released", r"\[NativeWorldHotSwitch\] state=connection-released", args.transition_timeout)
        driver.wait(f"{cycle_name}-fence", r"context=runtime-admission-fence-after.*admission-baseline-match=yes", args.activation_timeout)
        driver.wait(
            f"{cycle_name}-selected",
            rf"\[NativeWorldAuthorization\] state=runtime-selected endpoint={re.escape(target_text)}",
            args.activation_timeout,
        )
        driver.wait(f"{cycle_name}-preflight", r"state=transaction-preflight-proved", args.activation_timeout)
        driver.wait(f"{cycle_name}-active", r"\[NativeWorldAuthorization\] state=runtime-active.*restart-required=no", args.activation_timeout)
        heartbeat_start = len(driver.refresh_log())
        driver.cursor = heartbeat_start
        driver.send("status")
        driver.wait(
            f"{cycle_name}-heartbeat",
            r"\[NativeWorldHarness\] state=executed.*command=nativeworlddrain status",
            args.command_timeout,
            start=heartbeat_start,
        )
        stable_alive, current = driver.stable_process_is_alive()
        if not stable_alive:
            raise HarnessFailure(f"GTA process changed during {cycle_name}: expected {stable_pid}, got {current}")
        dumps = crash_dump_fingerprint()
        if dumps != driver.initial_dumps:
            raise HarnessFailure(f"crash dump set changed during {cycle_name}: expected {driver.initial_dumps}, got {dumps}")
        trace.emit(cycle_name, "active", "ok", pid=stable_pid, endpoint=target_text, heartbeat="main-thread", dumps="unchanged")

    for metric in ("buildingOccupied", "colModelOccupied", "ptrNodeSingleLinkOccupied", "streamingMemory"):
        values = [sample[metric] for sample in dynamic_history]
        if len(values) >= 2 and all(right > left for left, right in zip(values, values[1:])):
            raise HarnessFailure(f"dynamic neutral metric grew monotonically across every measured cycle: {metric}={values}")
    trace.emit("endurance", "dynamic-high-water", "ok", samples=dynamic_history, measuredCycles=args.cycles)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", action="append", required=True, type=parse_endpoint, help="numeric IPv4 endpoint; repeat to alternate sets")
    parser.add_argument("--cycles", type=int, default=10, help="measured cycles after warm-up")
    parser.add_argument("--warmup-cycles", type=int, default=2, help="unmeasured topology/cache warm-up cycles")
    attach_group = parser.add_mutually_exclusive_group()
    attach_group.add_argument("--attach", action="store_true", help="attach to one exclusive already-running harness-enabled client")
    attach_group.add_argument(
        "--attach-pid",
        type=int,
        help="attach to this harness-enabled GTA PID while tolerating unrelated multi-client pairs",
    )
    parser.add_argument("--restart-fallback", action="store_true", help="force one drain refusal and verify the bounded loader restart fallback")
    parser.add_argument(
        "--allow-same-endpoint-fallback",
        action="store_true",
        help="permit an end-to-end fallback restart back to endpoint A; default fallback validation requires endpoint B",
    )
    parser.add_argument("--poll", type=float, default=1.0)
    parser.add_argument("--command-timeout", type=float, default=30.0)
    parser.add_argument("--transition-timeout", type=float, default=150.0)
    parser.add_argument("--activation-timeout", type=float, default=1200.0)
    parser.add_argument("--bootstrap-timeout", type=float, default=1200.0)
    parser.add_argument("--trace", type=pathlib.Path)
    result = parser.parse_args()
    if result.cycles < 1 or result.cycles > 100:
        parser.error("--cycles must be between 1 and 100")
    if result.warmup_cycles < 0 or result.warmup_cycles > 20:
        parser.error("--warmup-cycles must be between 0 and 20")
    for name in ("poll", "command_timeout", "transition_timeout", "activation_timeout", "bootstrap_timeout"):
        if getattr(result, name) <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if result.restart_fallback and len(result.endpoint) < 2:
        parser.error("--restart-fallback requires distinct launch and fallback endpoints")
    if result.restart_fallback and result.endpoint[0] == result.endpoint[1] and not result.allow_same_endpoint_fallback:
        parser.error("--restart-fallback requires endpoint B to differ from endpoint A")
    if result.allow_same_endpoint_fallback and not result.restart_fallback:
        parser.error("--allow-same-endpoint-fallback requires --restart-fallback")
    if result.restart_fallback and result.attach_pid is not None:
        parser.error("--restart-fallback cannot coexist with unrelated clients because OnQuitCommand is profile-global")
    if result.attach_pid is not None and result.attach_pid <= 0:
        parser.error("--attach-pid must be positive")
    return result


def main() -> int:
    args = arguments()
    run_id = uuid.uuid4().hex
    trace_path = args.trace or pathlib.Path("Build") / f"native-world-hot-switch-{run_id}.jsonl"
    try:
        trace = Trace(trace_path, run_id)
    except OSError as exc:
        failure_path = trace_path.with_name(f"{trace_path.name}.failure-{run_id}.jsonl")
        trace = Trace(failure_path, run_id)
        reason = f"could not create requested trace {trace_path}: {exc}"
        trace.verdict("FAIL", reason)
        trace.close()
        print(f"FAIL: {reason}\ntrace: {failure_path}", file=sys.stderr)
        return 1
    try:
        run(args, trace)
    except (HarnessFailure, subprocess.TimeoutExpired, OSError, KeyboardInterrupt) as exc:
        reason = str(exc) or type(exc).__name__
        trace.verdict("FAIL", reason)
        print(f"FAIL: {reason}\ntrace: {trace_path}", file=sys.stderr)
        return 1
    else:
        reason = "verified restart fallback completed" if args.restart_fallback else f"{args.cycles} hot-switch cycle(s) completed"
        trace.verdict("PASS", reason)
        print(f"PASS: {reason}\ntrace: {trace_path}")
        return 0
    finally:
        trace.close()


if __name__ == "__main__":
    raise SystemExit(main())
