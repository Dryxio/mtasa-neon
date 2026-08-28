from __future__ import annotations

import hashlib
import hmac
import os
import secrets
import stat
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .jsonio import canonical_json, sha256_bytes
from .runtime import compare_runtime_snapshot
from .schema import SchemaStore


PROBE_RESOURCE = "neon-agent-probe"
PROBE_FILES = ("meta.xml", "server.lua", "client.lua")
CONFIG_FILE = "neon-agent-proof-config.json"
REPORT_FILE = "neon-agent-proof-report.json"
MAX_PROBE_DOCUMENT = 64 * 1024
FILE_ATTRIBUTE_REPARSE_POINT = 0x400
_DIRECTORY_FLAGS = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) | getattr(os, "O_NOFOLLOW", 0)


def _probe_source() -> Path:
    return Path(__file__).resolve().parents[1] / "runtime-probe"


def probe_assets() -> dict[str, bytes]:
    assets: dict[str, bytes] = {}
    for name in PROBE_FILES:
        path = _probe_source() / name
        payload = path.read_bytes()
        if not payload or len(payload) > 256 * 1024:
            raise ValueError(f"bundled runtime probe asset {name} is empty or oversized")
        assets[name] = payload
    return assets


def probe_source_sha256(assets: dict[str, bytes] | None = None) -> str:
    selected = probe_assets() if assets is None else assets
    manifest = [{"path": name, "sha256": sha256_bytes(selected[name])} for name in PROBE_FILES]
    return sha256_bytes(canonical_json(manifest).encode("utf-8"))


def _is_link_like(path: Path) -> bool:
    if path.is_symlink():
        return True
    try:
        attributes = getattr(path.lstat(), "st_file_attributes", 0)
    except OSError:
        return False
    return bool(attributes & FILE_ATTRIBUTE_REPARSE_POINT)


def _real_directory(path: Path, label: str) -> Path:
    original = path.absolute()
    if _is_link_like(original) or not original.is_dir():
        raise ValueError(f"{label} must be a real directory")
    # The explicitly selected root is canonicalized once. System ancestors
    # such as macOS /var -> /private/var are outside the relative resource
    # boundary; every descendant used below is checked independently.
    return original.resolve(strict=True)


def _open_probe_directory(server_root: Path, *, create: bool = False, writable: bool = False) -> tuple[Path, int]:
    root = _real_directory(server_root, "MTA server root")
    components = ("mods", "deathmatch", "resources", PROBE_RESOURCE)
    if os.name == "nt":
        from . import winfs

        handle = winfs.open_directory(root)
        try:
            for index, name in enumerate(components):
                child = winfs.open_directory_at(
                    handle, name, create=create and index == len(components) - 1,
                    writable=writable or create,
                )
                winfs.close(handle)
                handle = child
            return root.joinpath(*components), handle
        except Exception:
            winfs.close(handle)
            raise
    handle = os.open(root, _DIRECTORY_FLAGS)
    try:
        for index, name in enumerate(components):
            if create and index == len(components) - 1:
                try:
                    os.mkdir(name, 0o700, dir_fd=handle)
                except FileExistsError:
                    pass
            child = os.open(name, _DIRECTORY_FLAGS, dir_fd=handle)
            os.close(handle)
            handle = child
        return root.joinpath(*components), handle
    except Exception:
        os.close(handle)
        raise


def _close_probe_directory(handle: int) -> None:
    if os.name == "nt":
        from . import winfs

        winfs.close(handle)
    else:
        os.close(handle)


def _write_probe_file(handle: int, name: str, payload: bytes) -> None:
    temporary = f".{name}.{secrets.token_hex(8)}.tmp"
    if os.name == "nt":
        from . import winfs

        winfs.atomic_write_at(handle, name, payload, temporary)
        return
    descriptor = os.open(
        temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
        0o600, dir_fd=handle,
    )
    try:
        with os.fdopen(descriptor, "wb", closefd=False) as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
    finally:
        os.close(descriptor)
    os.replace(temporary, name, src_dir_fd=handle, dst_dir_fd=handle)


def _read_probe_file(handle: int, name: str, maximum: int) -> bytes:
    if os.name == "nt":
        from . import winfs

        return winfs.read_regular_at(handle, name, maximum)
    descriptor = os.open(name, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0), dir_fd=handle)
    try:
        details = os.fstat(descriptor)
        if not stat.S_ISREG(details.st_mode):
            raise ValueError(f"runtime probe entry {name} is not a regular file")
        if details.st_size > maximum:
            raise ValueError(f"runtime probe entry {name} exceeds {maximum} bytes")
        payload = os.read(descriptor, maximum + 1)
        if len(payload) != details.st_size:
            raise OSError(f"runtime probe entry {name} changed while being read")
        return payload
    finally:
        os.close(descriptor)


def _unlink_probe_file(handle: int, name: str) -> None:
    if os.name == "nt":
        from . import winfs

        winfs.unlink_at(handle, name)
        return
    try:
        os.unlink(name, dir_fd=handle)
    except FileNotFoundError:
        pass


def install_runtime_probe(server_root: Path) -> dict[str, Any]:
    _, handle = _open_probe_directory(server_root, create=True, writable=True)
    assets = probe_assets()
    try:
        for name, payload in assets.items():
            _write_probe_file(handle, name, payload)
    finally:
        _close_probe_directory(handle)
    verified = verify_runtime_probe(server_root)
    return {
        "schemaVersion": "1.0.0",
        "command": "runtime.probe.install",
        "status": "pass",
        "summary": {"errors": 0, "warnings": 0, "files": len(assets)},
        "diagnostics": [],
        "probe": verified,
    }


def probe_install_failure(code: str, message: str) -> dict[str, Any]:
    return {
        "schemaVersion": "1.0.0", "command": "runtime.probe.install", "status": "fail",
        "summary": {"errors": 1, "warnings": 0, "files": 0},
        "diagnostics": [{"code": code, "severity": "error", "message": message[:1024], "path": "."}],
        "probe": None,
    }


def arm_runtime_probe(server_root: Path, config: dict[str, Any], schema_store: SchemaStore) -> Path:
    if schema_store.validate("neon-probe-config", config):
        raise ValueError("generated runtime probe configuration is invalid")
    verify_runtime_probe(server_root)
    target, handle = _open_probe_directory(server_root, writable=True)
    try:
        _unlink_probe_file(handle, REPORT_FILE)
        _write_probe_file(handle, CONFIG_FILE, canonical_json(config).encode("utf-8"))
    finally:
        _close_probe_directory(handle)
    return target


def load_probe_report(server_root: Path) -> dict[str, Any]:
    _, handle = _open_probe_directory(server_root)
    try:
        payload = _read_probe_file(handle, REPORT_FILE, MAX_PROBE_DOCUMENT)
    except FileNotFoundError as exc:
        raise FileNotFoundError("authenticated runtime probe report is not ready") from exc
    finally:
        _close_probe_directory(handle)
    from .scenario import load_json_text

    return load_json_text(payload.decode("utf-8"))


def verify_runtime_probe(server_root: Path) -> dict[str, Any]:
    _, handle = _open_probe_directory(server_root)
    assets = probe_assets()
    files = []
    try:
        for name in PROBE_FILES:
            payload = _read_probe_file(handle, name, 256 * 1024)
            expected = assets[name]
            if not hmac.compare_digest(hashlib.sha256(payload).digest(), hashlib.sha256(expected).digest()):
                raise ValueError(f"runtime probe asset {name} differs from the trusted bundled asset")
            files.append({"path": name, "sha256": sha256_bytes(payload)})
    finally:
        _close_probe_directory(handle)
    return {"resource": PROBE_RESOURCE, "sourceSha256": probe_source_sha256(assets), "files": files}


def client_executable(client_root: Path, test_adapter: bool = False) -> tuple[Path, str]:
    root = _real_directory(client_root, "MTA client root")
    if os.name != "nt" and not test_adapter:
        raise ValueError("client.launch is supported only for the Windows MTA client")
    names = ("Multi Theft Auto.exe",) if os.name == "nt" else ("mta-client",)
    executable = next((root / name for name in names if (root / name).is_file() and not _is_link_like(root / name)), None)
    if executable is None:
        raise ValueError("MTA client root has no approved client executable")
    digest = hashlib.sha256()
    size = 0
    with executable.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(chunk)
            if size > 512 * 1024 * 1024:
                raise ValueError("MTA client executable exceeds 536870912 bytes")
            digest.update(chunk)
    return executable, digest.hexdigest()


def expected_clients(profile: str) -> int:
    if profile == "neon-pair":
        return 1
    if profile == "neon-multiclient":
        return 2
    raise ValueError("authenticated client proof requires neon-pair or neon-multiclient")


def create_probe_config(session: dict[str, Any], secret: str, now: datetime | None = None) -> dict[str, Any]:
    current = (now or datetime.now(timezone.utc)).replace(microsecond=0)
    expires = datetime.strptime(session["expiresAt"], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
    return {
        "schemaVersion": "1.0.0",
        "sessionId": session["sessionId"],
        "challenge": secrets.token_hex(32),
        "secret": secret,
        "profile": session["profile"],
        "projectSha256": session["project"]["sha256"],
        "catalogueSha256": session["catalogue"]["sha256"],
        "expectedClients": expected_clients(session["profile"]),
        "issuedUnix": int(current.timestamp()),
        "expiresUnix": int(expires.timestamp()),
    }


def signature_payload(report: dict[str, Any]) -> bytes:
    fields = [
        report["sessionId"], report["challenge"], report["profile"],
        report["projectSha256"], report["catalogueSha256"], str(report["observedUnix"]),
        str(report["expectedClients"]), report["server"]["engineVersion"], report["server"]["buildId"],
    ]
    for client in report["clients"]:
        fields.extend((str(client["ordinal"]), client["engineVersion"], client["buildId"], client["nonce"]))
    return "\n".join(fields).encode("utf-8")


def authorize_report(report: dict[str, Any], secret: str) -> str:
    return hmac.digest(secret.encode("ascii"), signature_payload(report), "sha256").hex()


def proof_failure(code: str, message: str, session: dict[str, Any]) -> dict[str, Any]:
    return {
        "schemaVersion": "1.0.0", "command": "runtime.prove", "status": "fail",
        "summary": {"errors": 1, "warnings": 0, "observations": 0},
        "diagnostics": [{"code": code, "severity": "error", "message": message[:1024], "path": "."}],
        "proof": {
            "sessionId": session["sessionId"], "profile": session["profile"],
            "scope": "authenticated-runtime-proof", "grantedEvidenceLabels": [],
            "snapshotSha256": "0" * 64, "evidence": None,
        },
    }


def prove_runtime(
    report: dict[str, Any], config: dict[str, Any], secret: str, session: dict[str, Any],
    project: dict[str, Any], catalogue: dict[str, Any], project_api: dict[str, Any],
    schema_store: SchemaStore,
) -> tuple[dict[str, Any], dict[str, Any] | None]:
    issues = schema_store.validate("neon-probe-report", report)
    if issues:
        return proof_failure("PROBE_REPORT_INVALID", f"{issues[0].pointer}: {issues[0].message}", session), None
    bindings = {
        "sessionId": session["sessionId"], "challenge": config["challenge"], "profile": session["profile"],
        "projectSha256": session["project"]["sha256"], "catalogueSha256": session["catalogue"]["sha256"],
        "expectedClients": config["expectedClients"],
    }
    for field, expected in bindings.items():
        if report.get(field) != expected:
            return proof_failure("PROBE_BINDING_MISMATCH", f"probe report {field} differs from the active session", session), None
    if not hmac.compare_digest(report["authorization"], authorize_report({k: v for k, v in report.items() if k != "authorization"}, secret)):
        return proof_failure("PROBE_AUTHORIZATION_INVALID", "probe report authentication failed", session), None
    now = int(datetime.now(timezone.utc).timestamp())
    if (
        not config["issuedUnix"] <= report["observedUnix"] <= config["expiresUnix"]
        or report["observedUnix"] > now + 5 or report["observedUnix"] < now - 10
    ):
        return proof_failure("PROBE_TIME_INVALID", "probe report is outside the active session window", session), None
    clients = report["clients"]
    if len(clients) != config["expectedClients"] or [item["ordinal"] for item in clients] != list(range(1, len(clients) + 1)):
        return proof_failure("PROBE_TOPOLOGY_INVALID", "probe report does not contain the exact expected client topology", session), None
    if len({item["nonce"] for item in clients}) != len(clients):
        return proof_failure("PROBE_CLIENT_DUPLICATE", "probe report contains duplicate client nonces", session), None
    runtimes = [report["server"], *clients]
    if len({item["engineVersion"] for item in runtimes}) != 1:
        return proof_failure("PROBE_ENGINE_MISMATCH", "server and client engine versions do not match", session), None
    if len({item["buildId"] for item in clients}) != 1:
        return proof_failure("PROBE_CLIENT_BUILD_MISMATCH", "client build identifiers do not match", session), None
    resources: list[dict[str, Any]] = []
    empty = {"completeness": "partial", "functions": [], "events": [], "resources": resources, "modules": []}
    snapshot = {
        "schemaVersion": "1.0.0", "producer": "neon-runtime-probe-1",
        "sessionId": session["sessionId"], "profile": session["profile"],
        "observedAt": datetime.fromtimestamp(report["observedUnix"], timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "catalogueSha256": session["catalogue"]["sha256"], "projectSha256": session["project"]["sha256"],
        "observations": [
            {"id": "runtime:server-1", "side": "server", "engineVersion": report["server"]["engineVersion"], "buildId": report["server"]["buildId"], **empty},
            *[
                {"id": f"runtime:client-{item['ordinal']}", "side": "client", "engineVersion": item["engineVersion"], "buildId": item["buildId"], **empty}
                for item in clients
            ],
        ],
    }
    snapshot_payload = canonical_json(snapshot).encode("utf-8")
    comparison = compare_runtime_snapshot(
        None, project, session["project"]["sha256"], catalogue, session["catalogue"]["sha256"],
        project_api, session["sessionId"], schema_store, session["createdAt"], session["expiresAt"], snapshot_payload,
        True,
    )
    if comparison["status"] != "pass":
        result = proof_failure("PROBE_RUNTIME_CONTRACT_FAILED", "authenticated observations do not satisfy the pinned runtime contracts", session)
        result["diagnostics"] = comparison["diagnostics"]
        result["summary"]["errors"] = comparison["summary"]["errors"]
        result["summary"]["warnings"] = comparison["summary"]["warnings"]
        result["summary"]["observations"] = comparison["summary"]["observations"]
        return result, snapshot
    labels = ["server-checked", "client-checked", "in-game-checked"]
    if session["profile"] == "neon-multiclient":
        labels.append("multiplayer-checked")
    snapshot_sha = sha256_bytes(snapshot_payload)
    evidence = {
        "schemaVersion": "1.0.0", "runId": f"run:probe-{config['challenge'][:24]}",
        "profile": session["profile"], "observedAt": snapshot["observedAt"], "durationMs": 0,
        "labels": labels,
        "scenario": {"id": "scenario:authenticated-runtime-topology", "sha256": probe_source_sha256()},
        "assertions": [
            {"id": "assertion:probe-authentication", "status": "pass"},
            {"id": "assertion:runtime-contracts", "status": "pass"},
            {"id": "assertion:topology", "status": "pass"},
            {"id": "assertion:matching-engine-versions", "status": "pass"},
            {"id": "assertion:matching-client-builds", "status": "pass"},
        ],
        "artifacts": [{"id": "artifact:runtime-snapshot", "path": session["snapshot"]["path"], "sha256": snapshot_sha}],
    }
    result = {
        "schemaVersion": "1.0.0", "command": "runtime.prove", "status": "pass",
        "summary": {"errors": 0, "warnings": comparison["summary"]["warnings"], "observations": len(snapshot["observations"])},
        "diagnostics": comparison["diagnostics"],
        "proof": {
            "sessionId": session["sessionId"], "profile": session["profile"],
            "scope": "authenticated-runtime-proof", "grantedEvidenceLabels": labels,
            "snapshotSha256": snapshot_sha, "evidence": evidence,
        },
    }
    return result, snapshot
