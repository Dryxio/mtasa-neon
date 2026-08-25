from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any

from .components import file_sha256
from .jsonio import JsonDocumentError, canonical_json, load_json, sha256_bytes
from .schema import SchemaStore, schema_major


STATIC_ACTIONS = {"check", "project.resolve", "generate.project", "context.verify", "api.search"}
RUNTIME_ACTIONS = {"build", "resource.start", "resource.stop", "resource.restart", "scenario.execute", "client.launch"}
PATH_INPUTS = {"project", "catalogue", "output", "context"}
ACTION_INPUTS = {
    "check": {"project", "catalogue"},
    "project.resolve": {"project", "catalogue"},
    "generate.project": {"project", "catalogue", "output"},
    "context.verify": {"project", "catalogue", "context"},
    "api.search": {"query", "catalogue", "kind", "origin", "state", "side", "profile", "limit", "full"},
}
MAX_CONTROL_BYTES = 16 * 1024 * 1024


def _diagnostic(code: str, message: str, path: str = ".", *, step: str | None = None) -> dict[str, Any]:
    result: dict[str, Any] = {"code": code, "severity": "error", "message": message, "path": path}
    if step is not None:
        result["step"] = step
    return result


def _action_failure(action: str, code: str, message: str) -> dict[str, Any]:
    return {
        "schemaVersion": "1.0.0", "command": action, "status": "fail",
        "summary": {"errors": 1, "warnings": 0},
        "diagnostics": [_diagnostic(code, message)],
    }


def _normalize_workspace_values(value: Any, workspace: Path) -> Any:
    if isinstance(value, dict):
        return {key: _normalize_workspace_values(item, workspace) for key, item in value.items()}
    if isinstance(value, list):
        return [_normalize_workspace_values(item, workspace) for item in value]
    if isinstance(value, str):
        root = os.fspath(workspace.resolve())
        normalized = value.replace(root + os.sep, "workspace:/")
        return "workspace:" if normalized == root else normalized
    return value


def _inside(root: Path, value: str, *, allow_missing: bool = False) -> Path:
    relative = PurePosixPath(value.replace("\\", "/"))
    if not value or relative.is_absolute() or ".." in relative.parts or "." in relative.parts:
        raise ValueError(f"path must be workspace-relative without traversal: {value!r}")
    root = root.resolve()
    candidate = root.joinpath(*relative.parts)
    current = root
    for part in relative.parts:
        current /= part
        if current.is_symlink():
            raise ValueError(f"path contains a symbolic link: {value}")
        if not current.exists() and allow_missing:
            break
    try:
        candidate.resolve(strict=False).relative_to(root)
    except (OSError, ValueError) as exc:
        raise ValueError(f"path escapes workspace: {value}") from exc
    return candidate


def _action_command(step: dict[str, Any], workspace: Path, tool: Path, scenario_profile: str) -> list[str]:
    action = step["action"]
    inputs = step["inputs"]
    allowed = ACTION_INPUTS[action]
    unknown = sorted(set(inputs) - allowed)
    if unknown:
        raise ValueError(f"unknown {action} input: {unknown[0]}")
    for key in PATH_INPUTS.intersection(inputs):
        if not isinstance(inputs[key], str):
            raise ValueError(f"{action} input {key} must be a string")
        _inside(workspace, inputs[key], allow_missing=key in {"output", "context"})

    command = [sys.executable, os.fspath(tool)]
    if action == "check":
        command.append("check")
    elif action == "project.resolve":
        command.extend(("project", "resolve"))
    elif action == "generate.project":
        command.extend(("generate", "project"))
    elif action == "context.verify":
        command.extend(("context", "verify"))
    elif action == "api.search":
        query = inputs.get("query")
        if not isinstance(query, str) or not query.strip():
            raise ValueError("api.search input query must be a non-empty string")
        command.extend(("api", "search", query))
        enums = {
            "kind": {"function", "event", "element", "type", "class", "enum"},
            "origin": {"mta", "neon"},
            "state": {"verified", "documented-only", "runtime-only", "opaque", "conflict", "unavailable"},
            "side": {"client", "server"},
            "profile": {"mta-upstream", "neon-client", "neon-server", "neon-pair", "neon-multiclient"},
        }
        for key, values in enums.items():
            if key in inputs:
                if inputs[key] not in values:
                    raise ValueError(f"api.search input {key} is invalid")
                command.extend((f"--{key}", inputs[key]))
        requested_profile = inputs.get("profile")
        if requested_profile is not None and requested_profile != scenario_profile:
            raise ValueError(f"api.search profile {requested_profile} does not match scenario profile {scenario_profile}")
        if requested_profile is None:
            command.extend(("--profile", scenario_profile))
        if "limit" in inputs:
            if not isinstance(inputs["limit"], int) or isinstance(inputs["limit"], bool) or not 1 <= inputs["limit"] <= 100:
                raise ValueError("api.search input limit must be an integer from 1 to 100")
            command.extend(("--limit", str(inputs["limit"])))
        if "full" in inputs:
            if not isinstance(inputs["full"], bool):
                raise ValueError("api.search input full must be boolean")
            if inputs["full"]:
                command.append("--full")

    for key in ("project", "catalogue", "output", "context"):
        if key in inputs:
            command.extend((f"--{key}", os.fspath(_inside(workspace, inputs[key], allow_missing=key in {"output", "context"}))))
    command.append("--json")
    return command


def _run_step(step: dict[str, Any], workspace: Path, tool: Path, profile: str) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    action = step["action"]
    expected = step.get("expectedStatus", "pass")
    diagnostics: list[dict[str, Any]] = []
    if action in RUNTIME_ACTIONS:
        result = _action_failure(action, "SCENARIO_ACTION_UNAVAILABLE", f"{action} is reserved for a later runtime checkpoint")
        diagnostics.append(_diagnostic("SCENARIO_ACTION_UNAVAILABLE", f"{action} is not enabled by the runtime-free runner", step=step["id"]))
        return _finish_step(step, expected, 1, result, diagnostics)
    if action not in STATIC_ACTIONS:
        result = _action_failure(action, "SCENARIO_ACTION_UNKNOWN", f"unsupported action {action}")
        diagnostics.append(_diagnostic("SCENARIO_ACTION_UNKNOWN", f"unsupported action {action}", step=step["id"]))
        return _finish_step(step, expected, 1, result, diagnostics)
    if action in {"check", "project.resolve", "generate.project", "context.verify"}:
        project_value = step["inputs"].get("project", "neon.project.json")
        try:
            project_path = _inside(workspace, project_value)
            project = load_json(project_path)
        except (JsonDocumentError, OSError, ValueError):
            project = None  # The action itself emits the authoritative malformed-project diagnostic.
        if isinstance(project, dict) and isinstance(project.get("profile"), str) and project["profile"] != profile:
            message = f"scenario profile {profile} does not match project profile {project['profile']}"
            result = _action_failure(action, "SCENARIO_PROFILE_MISMATCH", message)
            diagnostics.append(_diagnostic(
                "SCENARIO_PROFILE_MISMATCH", message,
                step=step["id"],
            ))
            return _finish_step(step, expected, 1, result, diagnostics)
    try:
        command = _action_command(step, workspace, tool, profile)
    except ValueError as exc:
        result = _action_failure(action, "SCENARIO_INPUT_INVALID", str(exc))
        diagnostics.append(_diagnostic("SCENARIO_INPUT_INVALID", str(exc), step=step["id"]))
        return _finish_step(step, expected, 1, result, diagnostics)
    try:
        completed = subprocess.run(
            command, cwd=workspace, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=step["timeoutMs"] / 1000, check=False,
        )
    except subprocess.TimeoutExpired:
        message = f"step exceeded {step['timeoutMs']} ms"
        result = _action_failure(action, "SCENARIO_STEP_TIMEOUT", message)
        diagnostics.append(_diagnostic("SCENARIO_STEP_TIMEOUT", message, step=step["id"]))
        return _finish_step(step, expected, 124, result, diagnostics)
    except OSError as exc:
        result = _action_failure(action, "SCENARIO_STEP_EXEC_FAILED", str(exc))
        diagnostics.append(_diagnostic("SCENARIO_STEP_EXEC_FAILED", str(exc), step=step["id"]))
        return _finish_step(step, expected, 1, result, diagnostics)
    if completed.stderr:
        stderr = _normalize_workspace_values(completed.stderr.strip(), workspace)
        diagnostics.append(_diagnostic("SCENARIO_STEP_STDERR", stderr, step=step["id"]))
    try:
        result = load_json_text(completed.stdout)
        if not isinstance(result, dict):
            raise JsonDocumentError("command JSON must be an object")
    except JsonDocumentError as exc:
        result = _action_failure(action, "SCENARIO_STEP_OUTPUT_INVALID", str(exc))
        diagnostics.append(_diagnostic("SCENARIO_STEP_OUTPUT_INVALID", str(exc), step=step["id"]))
    result = _normalize_workspace_values(result, workspace)
    return _finish_step(step, expected, completed.returncode, result, diagnostics)


def _finish_step(
    step: dict[str, Any], expected: str, exit_code: int, result: dict[str, Any], diagnostics: list[dict[str, Any]],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    record = _step_record(step, expected, max(0, exit_code), result)
    exit_matches = (exit_code == 0) == (record["actualStatus"] == "pass")
    if not exit_matches:
        diagnostics.append(_diagnostic(
            "SCENARIO_STEP_EXIT_MISMATCH",
            f"exit {exit_code} contradicts result status {record['actualStatus']}", step=step["id"],
        ))
    if record["status"] == "fail":
        diagnostics.append(_diagnostic(
            "SCENARIO_STEP_STATUS_MISMATCH",
            f"expected {expected}, got {record['actualStatus']} (exit {exit_code})", step=step["id"],
        ))
    return record, diagnostics


def load_json_text(text: str) -> Any:
    if len(text.encode("utf-8")) > MAX_CONTROL_BYTES:
        raise JsonDocumentError(f"command JSON exceeds {MAX_CONTROL_BYTES} bytes")
    import json

    try:
        return json.loads(text, object_pairs_hook=_reject_duplicate_keys, parse_constant=_reject_nonfinite)
    except JsonDocumentError:
        raise
    except (json.JSONDecodeError, UnicodeError) as exc:
        raise JsonDocumentError(str(exc)) from exc


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise JsonDocumentError(f"duplicate object key: {key}")
        result[key] = value
    return result


def _reject_nonfinite(value: str) -> None:
    raise JsonDocumentError(f"non-finite JSON number is forbidden: {value}")


def _step_record(step: dict[str, Any], expected: str, exit_code: int, result: dict[str, Any]) -> dict[str, Any]:
    actual = result.get("status") if result.get("status") in {"pass", "fail"} else "fail"
    return {
        "id": step["id"], "action": step["action"], "status": "pass" if actual == expected else "fail",
        "expectedStatus": expected, "actualStatus": actual, "exitCode": exit_code, "result": result,
    }


def _pointer(document: Any, pointer: str) -> Any:
    if pointer == "":
        return document
    if not pointer.startswith("/"):
        raise ValueError("JSON pointer must be empty or start with /")
    current = document
    for raw in pointer[1:].split("/"):
        if "~" in raw and any(part[:1] not in {"0", "1"} for part in raw.split("~")[1:]):
            raise ValueError(f"JSON pointer contains an invalid escape: {pointer}")
        token = raw.replace("~1", "/").replace("~0", "~")
        if isinstance(current, dict) and token in current:
            current = current[token]
        elif isinstance(current, list) and token.isdigit() and int(token) < len(current):
            current = current[int(token)]
        else:
            raise ValueError(f"JSON pointer does not exist: {pointer}")
    return current


def _selector(assertion: dict[str, Any], steps: list[dict[str, Any]], scenario_view: dict[str, Any]) -> Any:
    actual = assertion["actual"]
    if actual.startswith("step:"):
        target, separator, pointer = actual.partition("#")
        if not separator:
            raise ValueError("step selector must contain # before its JSON pointer")
        step_id = target
        step = next((item for item in steps if item["id"] == step_id), None)
        if step is None:
            raise ValueError(f"unknown step selector: {step_id}")
        return _pointer(step["result"], pointer)
    if actual.startswith("scenario#"):
        return _pointer(scenario_view, actual.removeprefix("scenario#"))
    if not steps:
        raise ValueError("short assertion selector requires at least one step")
    return _pointer(steps[-1]["result"], actual)


def _evaluate_assertion(
    assertion: dict[str, Any], steps: list[dict[str, Any]], scenario_view: dict[str, Any], workspace: Path,
) -> tuple[dict[str, Any], dict[str, Any] | None]:
    kind = assertion["kind"]
    expected_present = "expected" in assertion
    if kind in {"equals", "not-equals", "contains"} and not expected_present:
        return _assertion_failure(assertion, None), _diagnostic("ASSERTION_CONTRACT_INVALID", f"{kind} requires expected", assertion["id"])
    try:
        if kind == "file-exists":
            path = _inside(workspace, assertion["actual"])
            actual: Any = path.is_file() and not path.is_symlink()
            passed = actual
        else:
            actual = _selector(assertion, steps, scenario_view)
            if kind == "equals":
                passed = _json_equal(actual, assertion["expected"])
            elif kind == "not-equals":
                passed = not _json_equal(actual, assertion["expected"])
            elif kind == "truthy":
                passed = bool(actual)
            elif kind == "falsy":
                passed = not actual
            elif kind == "contains":
                expected = assertion["expected"]
                passed = expected in actual if isinstance(actual, (str, list, dict)) else False
            elif kind == "diagnostic-absent":
                if not isinstance(actual, list):
                    raise ValueError("diagnostic-absent selector must resolve to an array")
                code = assertion.get("expected")
                passed = not any(isinstance(item, dict) and (code is None or item.get("code") == code) for item in actual)
            else:
                raise ValueError(f"unsupported assertion kind: {kind}")
    except (TypeError, ValueError) as exc:
        return _assertion_failure(assertion, None), _diagnostic("ASSERTION_EVALUATION_FAILED", str(exc), assertion["id"])
    record = {
        "id": assertion["id"], "kind": kind, "status": "pass" if passed else "fail",
        "message": assertion["message"], "actual": actual,
    }
    if expected_present:
        record["expected"] = assertion["expected"]
    diagnostic = None if passed else _diagnostic("ASSERTION_FAILED", assertion["message"], assertion["id"])
    return record, diagnostic


def _json_equal(left: Any, right: Any) -> bool:
    # JSON booleans are a distinct type even though Python bool subclasses int.
    if isinstance(left, bool) or isinstance(right, bool):
        return isinstance(left, bool) and isinstance(right, bool) and left is right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return left == right
    if type(left) is not type(right):
        return False
    if isinstance(left, list):
        return len(left) == len(right) and all(_json_equal(a, b) for a, b in zip(left, right, strict=True))
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(_json_equal(left[key], right[key]) for key in left)
    return left == right


def _assertion_failure(assertion: dict[str, Any], actual: Any) -> dict[str, Any]:
    result = {"id": assertion["id"], "kind": assertion["kind"], "status": "fail", "message": assertion["message"], "actual": actual}
    if "expected" in assertion:
        result["expected"] = assertion["expected"]
    return result


def _atomic_write(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _workspace_relative(workspace: Path, value: Path, original_workspace: Path) -> str:
    if not value.is_absolute():
        return value.as_posix()
    absolute = value.absolute()
    candidates = [absolute]
    text = absolute.as_posix()
    if text == "/var" or text.startswith("/var/") or text == "/tmp" or text.startswith("/tmp/"):
        candidates.append(Path("/private" + text))
    elif text == "/private/var" or text.startswith("/private/var/") or text == "/private/tmp" or text.startswith("/private/tmp/"):
        candidates.append(Path(text.removeprefix("/private")))
    for candidate in candidates:
        for root in (original_workspace.absolute(), workspace.resolve()):
            try:
                return candidate.relative_to(root).as_posix()
            except ValueError:
                continue
    raise ValueError(f"path is outside the approved workspace: {value}")


def _validate_output(workspace: Path, output: Path, original_workspace: Path) -> Path:
    root = workspace.resolve()
    try:
        relative = _workspace_relative(root, output, original_workspace)
    except ValueError as exc:
        raise ValueError("scenario output must be inside the approved workspace") from exc
    target = _inside(root, relative, allow_missing=True)
    if target.exists() and (not target.is_dir() or target.is_symlink() or any(target.iterdir())):
        raise ValueError("scenario output must be a new or empty real directory")
    return target


def _approved_document(workspace: Path, path: Path, original_workspace: Path) -> Path:
    root = workspace.resolve()
    try:
        relative = _workspace_relative(root, path, original_workspace)
    except ValueError as exc:
        raise ValueError(f"control document is outside the approved workspace: {path}") from exc
    approved = _inside(root, relative)
    if not approved.is_file():
        raise ValueError(f"control document is not a regular file: {relative}")
    return approved


def _capture_json(path: Path) -> tuple[bytes, Any]:
    if path.stat().st_size > MAX_CONTROL_BYTES:
        raise JsonDocumentError(f"JSON document exceeds {MAX_CONTROL_BYTES} bytes")
    payload = path.read_bytes()
    return payload, load_json_text(payload.decode("utf-8"))


def _relative_to_workspace(workspace: Path, path: Path) -> PurePosixPath:
    absolute = path if path.is_absolute() else workspace / path
    return PurePosixPath(absolute.relative_to(workspace.resolve()).as_posix())


def _overlaps(left: PurePosixPath, right: PurePosixPath) -> bool:
    return left == right or left in right.parents or right in left.parents


def _generated_output(workspace: Path, step: dict[str, Any]) -> Path:
    inputs = step["inputs"]
    if "output" in inputs:
        if not isinstance(inputs["output"], str):
            raise ValueError("generate.project input output must be a string")
        return _inside(workspace, inputs["output"], allow_missing=True)
    project = inputs.get("project", "neon.project.json")
    if not isinstance(project, str):
        raise ValueError("generate.project input project must be a string")
    return _inside(workspace, project, allow_missing=True).parent / ".neon"


def _artifact(output: Path, relative: str, artifact_id: str, kind: str, media: str, payload: bytes) -> dict[str, Any]:
    path = output / relative
    _atomic_write(path, payload)
    return {
        "schemaVersion": "1.0.0", "id": artifact_id, "kind": kind, "path": relative,
        "mediaType": media, "size": len(payload), "sha256": sha256_bytes(payload),
    }


def run_scenario(
    scenario_path: Path,
    assertion_paths: list[Path],
    workspace: Path,
    schema_store: SchemaStore,
    tool: Path,
    output: Path | None = None,
    observed_at: str | None = None,
) -> dict[str, Any]:
    started = time.monotonic()
    original_workspace = workspace.absolute()
    workspace = workspace.resolve()
    scenario_path = _approved_document(workspace, scenario_path, original_workspace)
    assertion_paths = [_approved_document(workspace, path, original_workspace) for path in assertion_paths]
    scenario_payload, scenario = _capture_json(scenario_path)
    issues = schema_store.validate("neon-test", scenario)
    if issues:
        raise ValueError("; ".join(f"{issue.pointer}: {issue.message}" for issue in issues))
    if schema_major(scenario["schemaVersion"]) != 1:
        raise ValueError(f"unsupported scenario schema {scenario['schemaVersion']}")
    if len({step["id"] for step in scenario["steps"]}) != len(scenario["steps"]):
        raise ValueError("scenario step ids must be unique")
    if sum(step["timeoutMs"] for step in scenario["steps"]) > 600000:
        raise ValueError("scenario timeout budget exceeds 600000 ms")
    if observed_at is not None:
        try:
            datetime.strptime(observed_at, "%Y-%m-%dT%H:%M:%SZ")
        except ValueError as exc:
            raise ValueError("observed-at must be a valid UTC time in YYYY-MM-DDTHH:MM:SSZ form") from exc
    assertions: dict[str, dict[str, Any]] = {}
    assertion_payloads: list[bytes] = []
    for path in assertion_paths:
        payload, document = _capture_json(path)
        assertion_issues = schema_store.validate("neon-assertion", document)
        if assertion_issues:
            raise ValueError(f"{path.name}: " + "; ".join(f"{issue.pointer}: {issue.message}" for issue in assertion_issues))
        if schema_major(document["schemaVersion"]) != 1:
            raise ValueError(f"unsupported assertion schema {document['schemaVersion']}")
        if document["id"] in assertions:
            raise ValueError(f"duplicate assertion id: {document['id']}")
        assertions[document["id"]] = document
        assertion_payloads.append(payload)
    required = set(scenario["assertions"])
    if set(assertions) != required:
        missing = sorted(required - set(assertions))
        extra = sorted(set(assertions) - required)
        raise ValueError(f"assertion set mismatch; missing={missing}, extra={extra}")

    output_root = _validate_output(workspace, output, original_workspace) if output is not None else None
    if output_root is not None:
        runner_relative = _relative_to_workspace(workspace, output_root)
        for step in scenario["steps"]:
            if step["action"] != "generate.project":
                continue
            generated = _generated_output(workspace, step)
            generated_relative = _relative_to_workspace(workspace, generated)
            if _overlaps(runner_relative, generated_relative):
                raise ValueError("scenario result output overlaps a generate.project output")
        output_root.mkdir(parents=True, exist_ok=True)
    step_records: list[dict[str, Any]] = []
    diagnostics: list[dict[str, Any]] = []
    for step in scenario["steps"]:
        record, step_diagnostics = _run_step(step, workspace, tool, scenario["profile"])
        step_records.append(record)
        diagnostics.extend(step_diagnostics)

    scenario_view = {"id": scenario["id"], "profile": scenario["profile"], "steps": step_records}
    assertion_records: list[dict[str, Any]] = []
    for identifier in scenario["assertions"]:
        record, assertion_diagnostic = _evaluate_assertion(assertions[identifier], step_records, scenario_view, workspace)
        assertion_records.append(record)
        if assertion_diagnostic is not None:
            diagnostics.append(assertion_diagnostic)

    artifacts: list[dict[str, Any]] = []
    if output_root is not None:
        artifacts.append(_artifact(
            output_root, "inputs/scenario.json", "artifact:scenario-definition", "json", "application/json",
            scenario_payload,
        ))
        for index, payload in enumerate(assertion_payloads, 1):
            artifacts.append(_artifact(
                output_root, f"inputs/assertions/{index:03d}.json", f"artifact:assertion-{index:03d}",
                "json", "application/json", payload,
            ))
        for index, step in enumerate(step_records, 1):
            artifacts.append(_artifact(
                output_root, f"steps/{index:03d}.json", f"artifact:scenario-step-{index:03d}", "json", "application/json",
                canonical_json(step["result"]).encode("utf-8"),
            ))
        events = []
        sequence = 1
        for step in step_records:
            events.append({"schemaVersion": "1.0.0", "sequence": sequence, "type": "step.result", "id": step["id"], "status": step["status"]})
            sequence += 1
        for assertion in assertion_records:
            events.append({"schemaVersion": "1.0.0", "sequence": sequence, "type": "assertion.result", "id": assertion["id"], "status": assertion["status"]})
            sequence += 1
        event_payload = b"".join(canonical_json(event).encode("utf-8") for event in events)
        artifacts.append(_artifact(output_root, "events.jsonl", "artifact:scenario-events", "jsonl", "application/x-ndjson", event_payload))
        artifacts.sort(key=lambda item: item["id"])

    scenario_hash = sha256_bytes(scenario_payload)
    identity_payload = canonical_json({
        "scenario": scenario, "assertions": [assertions[item] for item in scenario["assertions"]],
        "steps": [{"id": item["id"], "status": item["status"], "result": item["result"]} for item in step_records],
    }).encode("utf-8")
    run_id = f"run:{scenario['id']}:{sha256_bytes(identity_payload)[:20]}"
    if observed_at is None:
        observed_at = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    evidence = {
        "schemaVersion": "1.0.0", "runId": run_id, "profile": scenario["profile"], "observedAt": observed_at,
        "durationMs": max(0, round((time.monotonic() - started) * 1000)), "labels": ["static-checked"],
        "scenario": {"id": scenario["id"], "sha256": scenario_hash},
        "assertions": [{"id": item["id"], "status": item["status"]} for item in assertion_records],
        "artifacts": [{"id": item["id"], "path": item["path"], "sha256": item["sha256"]} for item in artifacts],
    }
    evidence_issues = schema_store.validate("neon-evidence", evidence)
    if evidence_issues:
        diagnostics.append(_diagnostic("SCENARIO_EVIDENCE_INVALID", "; ".join(f"{issue.pointer}: {issue.message}" for issue in evidence_issues)))
    diagnostics.sort(key=lambda item: (item["severity"], item["code"], item["path"], item.get("step", ""), item["message"]))
    errors = sum(item["severity"] == "error" for item in diagnostics)
    warnings = sum(item["severity"] == "warning" for item in diagnostics)
    result = {
        "schemaVersion": "1.0.0", "command": "scenario.run", "status": "pass" if errors == 0 else "fail",
        "summary": {
            "errors": errors, "warnings": warnings, "steps": len(step_records), "assertions": len(assertion_records),
            "passedAssertions": sum(item["status"] == "pass" for item in assertion_records),
        },
        "diagnostics": diagnostics, "steps": step_records, "assertions": assertion_records, "evidence": evidence,
    }
    result_issues = schema_store.validate("neon-test-result", result)
    if result_issues:
        raise ValueError("generated scenario result is invalid: " + "; ".join(f"{issue.pointer}: {issue.message}" for issue in result_issues))
    if output_root is not None:
        _atomic_write(output_root / "evidence.json", canonical_json(evidence).encode("utf-8"))
        _atomic_write(output_root / "artifacts.json", canonical_json({"schemaVersion": "1.0.0", "artifacts": artifacts}).encode("utf-8"))
        _atomic_write(output_root / "result.json", canonical_json(result).encode("utf-8"))
    return result


def _verify_diagnostic(code: str, message: str, path: str = ".") -> dict[str, Any]:
    return {"code": code, "severity": "error", "message": message, "path": path}


def _scenario_semantic_issues(result: dict[str, Any], evidence: dict[str, Any]) -> list[dict[str, Any]]:
    diagnostics: list[dict[str, Any]] = []
    embedded = result.get("evidence")
    if embedded != evidence:
        diagnostics.append(_verify_diagnostic(
            "SCENARIO_EVIDENCE_MISMATCH", "result.json does not embed the exact evidence.json document", "result.json",
        ))
    result_diagnostics = result.get("diagnostics", [])
    steps = result.get("steps", [])
    assertions = result.get("assertions", [])
    summary = result.get("summary", {})
    expected_summary = {
        "errors": sum(item.get("severity") == "error" for item in result_diagnostics if isinstance(item, dict)),
        "warnings": sum(item.get("severity") == "warning" for item in result_diagnostics if isinstance(item, dict)),
        "steps": len(steps),
        "assertions": len(assertions),
        "passedAssertions": sum(item.get("status") == "pass" for item in assertions if isinstance(item, dict)),
    }
    if summary != expected_summary:
        diagnostics.append(_verify_diagnostic(
            "SCENARIO_SUMMARY_MISMATCH", "result summary does not match its diagnostics, steps, and assertions", "result.json",
        ))
    expected_status = "pass" if expected_summary["errors"] == 0 else "fail"
    if result.get("status") != expected_status:
        diagnostics.append(_verify_diagnostic(
            "SCENARIO_STATUS_MISMATCH", f"result status must be {expected_status}", "result.json",
        ))
    expected_assertions = [
        {"id": item.get("id"), "status": item.get("status")} for item in assertions if isinstance(item, dict)
    ]
    if evidence.get("assertions") != expected_assertions:
        diagnostics.append(_verify_diagnostic(
            "SCENARIO_ASSERTION_EVIDENCE_MISMATCH", "evidence assertions do not match result assertions", "evidence.json",
        ))
    if evidence.get("labels") != ["static-checked"]:
        diagnostics.append(_verify_diagnostic(
            "SCENARIO_EVIDENCE_SCOPE_INVALID", "a static scenario run may grant only the static-checked label", "evidence.json",
        ))
    for index, step in enumerate(steps):
        if not isinstance(step, dict):
            continue
        expected_step_status = "pass" if step.get("actualStatus") == step.get("expectedStatus") else "fail"
        if step.get("status") != expected_step_status:
            diagnostics.append(_verify_diagnostic(
                "SCENARIO_STEP_RECORD_MISMATCH", "step status contradicts expectedStatus and actualStatus", f"result.json#/steps/{index}",
            ))
        exit_matches = (step.get("exitCode") == 0) == (step.get("actualStatus") == "pass")
        if not exit_matches:
            diagnostics.append(_verify_diagnostic(
                "SCENARIO_STEP_EXIT_MISMATCH", "step exitCode contradicts actualStatus", f"result.json#/steps/{index}",
            ))
        child_diagnostics = step.get("result", {}).get("diagnostics", [])
        child_summary = step.get("result", {}).get("summary")
        if not isinstance(child_diagnostics, list) or not isinstance(child_summary, dict):
            diagnostics.append(_verify_diagnostic(
                "SCENARIO_STEP_RESULT_INVALID", "step result lacks diagnostics or summary contracts", f"result.json#/steps/{index}",
            ))
            child_diagnostics = []
            child_summary = {}
        child_errors = sum(
            item.get("severity") == "error" for item in child_diagnostics if isinstance(item, dict)
        )
        child_warnings = sum(
            item.get("severity") == "warning" for item in child_diagnostics if isinstance(item, dict)
        )
        if child_summary.get("errors") != child_errors or child_summary.get("warnings") != child_warnings:
            diagnostics.append(_verify_diagnostic(
                "SCENARIO_STEP_SUMMARY_MISMATCH",
                "step summary error/warning counts do not match its diagnostics",
                f"result.json#/steps/{index}",
            ))
        expected_child_status = "pass" if child_errors == 0 else "fail"
        if step.get("result", {}).get("status") != expected_child_status:
            diagnostics.append(_verify_diagnostic(
                "SCENARIO_STEP_RESULT_STATUS_MISMATCH",
                f"step result status must be {expected_child_status}",
                f"result.json#/steps/{index}",
            ))
        child_codes = {
            item.get("code") for item in child_diagnostics if isinstance(item, dict)
        }
        infrastructure_codes = {
            "SCENARIO_ACTION_UNAVAILABLE", "SCENARIO_ACTION_UNKNOWN", "SCENARIO_PROFILE_MISMATCH",
            "SCENARIO_INPUT_INVALID", "SCENARIO_STEP_TIMEOUT", "SCENARIO_STEP_EXEC_FAILED",
            "SCENARIO_STEP_OUTPUT_INVALID",
        }
        required_codes = child_codes.intersection(infrastructure_codes)
        if step.get("status") == "fail":
            required_codes.add("SCENARIO_STEP_STATUS_MISMATCH")
        if not exit_matches:
            required_codes.add("SCENARIO_STEP_EXIT_MISMATCH")
        if step.get("action") in RUNTIME_ACTIONS:
            required_codes.add("SCENARIO_ACTION_UNAVAILABLE")
        if step.get("exitCode") == 124:
            required_codes.add("SCENARIO_STEP_TIMEOUT")
        root_codes = {
            item.get("code") for item in result_diagnostics
            if isinstance(item, dict) and item.get("step") == step.get("id")
        }
        missing_codes = sorted(required_codes - root_codes)
        if missing_codes:
            diagnostics.append(_verify_diagnostic(
                "SCENARIO_STEP_DIAGNOSTIC_MISSING",
                f"top-level diagnostics omit required step diagnostics: {', '.join(missing_codes)}",
                f"result.json#/steps/{index}",
            ))
    return diagnostics


def verify_scenario_run(workspace: Path, run_directory: Path, schema_store: SchemaStore) -> dict[str, Any]:
    diagnostics: list[dict[str, Any]] = []
    try:
        original_workspace = workspace.absolute()
        workspace = workspace.resolve()
        relative = _workspace_relative(workspace, run_directory, original_workspace)
        run_root = _inside(workspace, relative)
        if run_root == workspace or not run_root.is_dir() or run_root.is_symlink():
            raise ValueError("scenario run must be a real directory inside the approved workspace")
    except (OSError, ValueError) as exc:
        diagnostics.append(_verify_diagnostic("SCENARIO_RUN_UNSAFE", str(exc)))
        return _scenario_verify_result(diagnostics, 0)

    documents: dict[str, Any] = {}
    for name in ("result.json", "evidence.json", "artifacts.json"):
        try:
            path = _inside(run_root, name)
            if not path.is_file() or path.is_symlink():
                raise ValueError("required control document is not a regular file")
            documents[name] = load_json(path)
        except (JsonDocumentError, OSError, ValueError) as exc:
            diagnostics.append(_verify_diagnostic("SCENARIO_CONTROL_INVALID", str(exc), name))
    if diagnostics:
        return _scenario_verify_result(diagnostics, 0)

    result = documents["result.json"]
    evidence = documents["evidence.json"]
    artifact_index = documents["artifacts.json"]
    for schema_name, document_name, document in (
        ("neon-test-result", "result.json", result),
        ("neon-evidence", "evidence.json", evidence),
        ("neon-artifact-index", "artifacts.json", artifact_index),
    ):
        for issue in schema_store.validate(schema_name, document):
            diagnostics.append(_verify_diagnostic(
                "SCENARIO_CONTRACT_INVALID", f"{issue.pointer}: {issue.message}", document_name,
            ))
        if isinstance(document, dict) and schema_major(document.get("schemaVersion", "")) != 1:
            diagnostics.append(_verify_diagnostic(
                "SCENARIO_CONTRACT_UNSUPPORTED", f"unsupported schema {document.get('schemaVersion')!r}", document_name,
            ))
    if diagnostics:
        return _scenario_verify_result(diagnostics, 0)

    diagnostics.extend(_scenario_semantic_issues(result, evidence))
    artifacts = artifact_index["artifacts"]
    ids = [item["id"] for item in artifacts]
    paths = [item["path"] for item in artifacts]
    if len(set(ids)) != len(ids):
        diagnostics.append(_verify_diagnostic("SCENARIO_ARTIFACT_ID_DUPLICATE", "artifact ids must be unique", "artifacts.json"))
    if len(set(paths)) != len(paths):
        diagnostics.append(_verify_diagnostic("SCENARIO_ARTIFACT_PATH_DUPLICATE", "artifact paths must be unique", "artifacts.json"))
    if ids != sorted(ids):
        diagnostics.append(_verify_diagnostic("SCENARIO_ARTIFACT_ORDER_INVALID", "artifacts must be sorted by id", "artifacts.json"))

    indexed_files = {"result.json", "evidence.json", "artifacts.json"}
    for artifact in artifacts:
        relative_path = artifact["path"]
        indexed_files.add(relative_path)
        if schema_major(artifact["schemaVersion"]) != 1:
            diagnostics.append(_verify_diagnostic(
                "SCENARIO_CONTRACT_UNSUPPORTED", f"unsupported artifact schema {artifact['schemaVersion']!r}", "artifacts.json",
            ))
        try:
            path = _inside(run_root, relative_path)
            if not path.is_file() or path.is_symlink():
                raise ValueError("indexed artifact is not a regular file")
            payload = path.read_bytes()
        except (OSError, ValueError) as exc:
            diagnostics.append(_verify_diagnostic("SCENARIO_ARTIFACT_UNSAFE", str(exc), relative_path))
            continue
        if len(payload) != artifact["size"] or sha256_bytes(payload) != artifact["sha256"]:
            diagnostics.append(_verify_diagnostic(
                "SCENARIO_ARTIFACT_TAMPERED", "artifact size or SHA-256 does not match its index", relative_path,
            ))

    evidence_refs = [{"id": item["id"], "path": item["path"], "sha256": item["sha256"]} for item in artifacts]
    if evidence["artifacts"] != evidence_refs:
        diagnostics.append(_verify_diagnostic(
            "SCENARIO_ARTIFACT_EVIDENCE_MISMATCH", "evidence artifact references do not match artifacts.json", "evidence.json",
        ))

    discovered_paths: list[Path] = []
    too_many_paths = False
    for path in run_root.rglob("*"):
        if len(discovered_paths) == 10000:
            too_many_paths = True
            break
        discovered_paths.append(path)
    if too_many_paths:
        diagnostics.append(_verify_diagnostic(
            "SCENARIO_RUN_TOO_LARGE", "scenario run contains more than 10000 filesystem entries", ".",
        ))
    for path in discovered_paths:
        relative_path = path.relative_to(run_root).as_posix()
        if path.is_symlink():
            diagnostics.append(_verify_diagnostic("SCENARIO_RUN_SYMLINK", "scenario runs cannot contain symbolic links", relative_path))
        elif path.is_file() and relative_path not in indexed_files:
            diagnostics.append(_verify_diagnostic("SCENARIO_FILE_UNINDEXED", "run contains a file absent from artifacts.json", relative_path))

    scenario_artifact = next((item for item in artifacts if item["id"] == "artifact:scenario-definition"), None)
    assertion_artifacts = [item for item in artifacts if item["id"].startswith("artifact:assertion-")]
    try:
        if scenario_artifact is None:
            raise ValueError("scenario definition artifact is missing")
        scenario_document = load_json(_inside(run_root, scenario_artifact["path"]))
        assertion_documents = [load_json(_inside(run_root, item["path"])) for item in assertion_artifacts]
        scenario_issues = schema_store.validate("neon-test", scenario_document)
        if scenario_issues or schema_major(scenario_document.get("schemaVersion", "")) != 1:
            raise ValueError("indexed scenario definition has an invalid or unsupported contract")
        for document in assertion_documents:
            assertion_issues = schema_store.validate("neon-assertion", document)
            if assertion_issues or schema_major(document.get("schemaVersion", "")) != 1:
                raise ValueError("indexed assertion definition has an invalid or unsupported contract")
        if file_sha256(_inside(run_root, scenario_artifact["path"])) != evidence["scenario"]["sha256"]:
            raise ValueError("scenario definition hash does not match evidence")
        if scenario_document.get("id") != evidence["scenario"]["id"]:
            raise ValueError("scenario definition id does not match evidence")
        if scenario_document.get("profile") != evidence["profile"]:
            raise ValueError("scenario definition profile does not match evidence")
        by_id = {item.get("id"): item for item in assertion_documents if isinstance(item, dict)}
        if len(by_id) != len(assertion_documents) or len(assertion_documents) != len(scenario_document.get("assertions", [])):
            raise ValueError("indexed assertion inputs do not exactly match the scenario")
        ordered_assertions = [by_id[identifier] for identifier in scenario_document.get("assertions", [])]
        scenario_steps = scenario_document["steps"]
        result_steps = result["steps"]
        expected_artifact_paths = {
            "artifact:scenario-definition": "inputs/scenario.json",
            "artifact:scenario-events": "events.jsonl",
            **{
                f"artifact:assertion-{index:03d}": f"inputs/assertions/{index:03d}.json"
                for index in range(1, len(ordered_assertions) + 1)
            },
            **{
                f"artifact:scenario-step-{index:03d}": f"steps/{index:03d}.json"
                for index in range(1, len(result_steps) + 1)
            },
        }
        actual_artifact_paths = {item["id"]: item["path"] for item in artifacts}
        if actual_artifact_paths != expected_artifact_paths:
            raise ValueError("artifact ids and paths do not exactly match the static scenario contract")
        if len(scenario_steps) != len(result_steps):
            raise ValueError("result step count does not match the scenario")
        artifacts_by_id = {item["id"]: item for item in artifacts}
        for index, (definition, record) in enumerate(zip(scenario_steps, result_steps, strict=True), 1):
            expected_status = definition.get("expectedStatus", "pass")
            if (
                record["id"] != definition["id"]
                or record["action"] != definition["action"]
                or record["expectedStatus"] != expected_status
                or record["actualStatus"] != record["result"].get("status")
                or record["result"].get("command") != definition["action"]
            ):
                raise ValueError(f"result step {record['id']} does not match its scenario definition")
            saved_step = load_json(_inside(run_root, artifacts_by_id[f"artifact:scenario-step-{index:03d}"]["path"]))
            if saved_step != record["result"]:
                raise ValueError(f"saved step artifact {index:03d} does not match result.json")
        if len(ordered_assertions) != len(result["assertions"]):
            raise ValueError("result assertion count does not match the scenario")
        for definition, record in zip(ordered_assertions, result["assertions"], strict=True):
            if (
                record["id"] != definition["id"]
                or record["kind"] != definition["kind"]
                or record["message"] != definition["message"]
            ):
                raise ValueError(f"result assertion {record['id']} does not match its definition")
            scenario_view = {"id": scenario_document["id"], "profile": scenario_document["profile"], "steps": result_steps}
            recalculated, recalculated_diagnostic = _evaluate_assertion(
                definition, result_steps, scenario_view, workspace,
            )
            if recalculated != record:
                raise ValueError(f"result assertion {record['id']} cannot be reproduced from the saved steps and workspace")
            if recalculated_diagnostic is not None and not any(
                item.get("code") == recalculated_diagnostic["code"] and item.get("path") == definition["id"]
                for item in result["diagnostics"] if isinstance(item, dict)
            ):
                raise ValueError(f"failed assertion {record['id']} is missing its top-level diagnostic")
        expected_events: list[dict[str, Any]] = []
        sequence = 1
        for step in result_steps:
            expected_events.append({
                "schemaVersion": "1.0.0", "sequence": sequence, "type": "step.result",
                "id": step["id"], "status": step["status"],
            })
            sequence += 1
        for assertion in result["assertions"]:
            expected_events.append({
                "schemaVersion": "1.0.0", "sequence": sequence, "type": "assertion.result",
                "id": assertion["id"], "status": assertion["status"],
            })
            sequence += 1
        expected_event_payload = b"".join(canonical_json(event).encode("utf-8") for event in expected_events)
        event_path = _inside(run_root, artifacts_by_id["artifact:scenario-events"]["path"])
        if event_path.read_bytes() != expected_event_payload:
            raise ValueError("events.jsonl cannot be reproduced from result.json")
        identity_payload = canonical_json({
            "scenario": scenario_document,
            "assertions": ordered_assertions,
            "steps": [{"id": item["id"], "status": item["status"], "result": item["result"]} for item in result["steps"]],
        }).encode("utf-8")
        expected_run_id = f"run:{scenario_document['id']}:{sha256_bytes(identity_payload)[:20]}"
        if evidence["runId"] != expected_run_id:
            raise ValueError("run identity cannot be reproduced from the indexed inputs and results")
    except (JsonDocumentError, KeyError, OSError, TypeError, ValueError) as exc:
        diagnostics.append(_verify_diagnostic("SCENARIO_IDENTITY_INVALID", str(exc), "evidence.json"))

    return _scenario_verify_result(diagnostics, len(artifacts))


def _scenario_verify_result(diagnostics: list[dict[str, Any]], artifacts: int) -> dict[str, Any]:
    diagnostics.sort(key=lambda item: (item["code"], item["path"], item["message"]))
    return {
        "schemaVersion": "1.0.0", "command": "scenario.verify", "status": "pass" if not diagnostics else "fail",
        "summary": {"errors": len(diagnostics), "warnings": 0, "artifacts": artifacts},
        "diagnostics": diagnostics,
    }
