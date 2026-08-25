from __future__ import annotations

from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any

from .jsonio import JsonDocumentError, sha256_bytes
from .schema import SchemaStore, schema_major


ACTIVE_STATES = {"verified", "runtime-only", "opaque"}
MAX_RUNTIME_DIAGNOSTICS = 4096


def _diagnostic(
    code: str,
    severity: str,
    message: str,
    path: str,
    *,
    observation: str | None = None,
    symbol: str | None = None,
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "code": code, "severity": severity,
        "message": message if len(message) <= 1024 else message[:1000] + "…[truncated]",
        "path": path[:512],
    }
    if observation is not None:
        result["observation"] = observation[:128]
    if symbol is not None:
        result["symbol"] = symbol[:128]
    return result


def _version(value: str) -> tuple[int, int, int]:
    core = value.split("+", 1)[0].split("-", 1)[0]
    major, minor, patch = core.split(".")
    return int(major), int(minor), int(patch)


def _expected_sides(symbol: dict[str, Any], profile: str) -> set[str]:
    key = "inheritedSides" if profile == "mta-upstream" else "sides"
    return set(symbol.get(key, []))


def _expected_symbols(catalogue: dict[str, Any], profile: str, side: str, kind: str) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for symbol in catalogue["symbols"]:
        if symbol.get("kind") != kind or symbol.get("state") not in ACTIVE_STATES:
            continue
        if profile not in symbol.get("profiles", []) or side not in _expected_sides(symbol, profile):
            continue
        result[symbol["name"]] = symbol
    return result


def _duplicates(values: list[dict[str, Any]], field: str = "name") -> list[str]:
    seen: set[str] = set()
    duplicates: set[str] = set()
    for value in values:
        key = value[field]
        if key in seen:
            duplicates.add(key)
        seen.add(key)
    return sorted(duplicates)


def _topology_issues(snapshot: dict[str, Any]) -> list[dict[str, Any]]:
    observations = snapshot["observations"]
    sides = [item["side"] for item in observations]
    profile = snapshot["profile"]
    diagnostics: list[dict[str, Any]] = []
    servers, clients = sides.count("server"), sides.count("client")
    if profile == "mta-upstream" and not observations:
        diagnostics.append(_diagnostic("RUNTIME_TOPOLOGY_INCOMPLETE", "error", "mta-upstream requires at least one observation", "/observations"))
    exact = {
        "neon-server": (1, 0),
        "neon-client": (0, 1),
        "neon-pair": (1, 1),
    }
    if profile in exact and (servers, clients) != exact[profile]:
        expected = exact[profile]
        code = "RUNTIME_TOPOLOGY_INCOMPLETE" if servers < expected[0] or clients < expected[1] else "RUNTIME_TOPOLOGY_UNEXPECTED"
        diagnostics.append(_diagnostic(
            code, "error",
            f"{profile} requires exactly {expected[0]} server and {expected[1]} client observations; got {servers} and {clients}",
            "/observations",
        ))
    if profile == "neon-multiclient" and (servers != 1 or clients < 2):
        diagnostics.append(_diagnostic(
            "RUNTIME_TOPOLOGY_INCOMPLETE" if servers < 1 or clients < 2 else "RUNTIME_TOPOLOGY_UNEXPECTED",
            "error", f"neon-multiclient requires exactly one server and at least two clients; got {servers} and {clients}",
            "/observations",
        ))
    if profile in {"neon-pair", "neon-multiclient"} and len({item["buildId"] for item in observations}) != 1:
        diagnostics.append(_diagnostic("RUNTIME_BUILD_MISMATCH", "error", "paired Neon observations must report the same buildId", "/observations"))
    return diagnostics


def _compare_inventory(
    observation: dict[str, Any], catalogue: dict[str, Any], profile: str, kind: str,
) -> list[dict[str, Any]]:
    identifier = observation["id"]
    side = observation["side"]
    plural = "functions" if kind == "function" else "events"
    actual_items = observation[plural]
    diagnostics: list[dict[str, Any]] = []
    for name in _duplicates(actual_items):
        diagnostics.append(_diagnostic(
            f"RUNTIME_{kind.upper()}_DUPLICATE", "error", f"runtime reports {name} more than once",
            f"/observations/{identifier}/{plural}", observation=identifier, symbol=name,
        ))
    actual = {item["name"]: item for item in actual_items}
    expected = _expected_symbols(catalogue, profile, side, kind)
    if observation["completeness"] == "complete":
        for name in sorted(set(expected) - set(actual)):
            diagnostics.append(_diagnostic(
                f"RUNTIME_{kind.upper()}_MISSING", "error", f"expected {side} {kind} {name} is absent from the complete inventory",
                f"/observations/{identifier}/{plural}", observation=identifier, symbol=name,
            ))
    elif kind == "function":
        diagnostics.append(_diagnostic(
            "RUNTIME_INVENTORY_PARTIAL", "warning", f"{identifier} cannot prove absence because its inventory is partial",
            f"/observations/{identifier}", observation=identifier,
        ))
    catalogue_by_name = {
        item["name"]: item for item in catalogue["symbols"] if item.get("kind") == kind
    }
    for name in sorted(set(actual) - set(expected)):
        known = catalogue_by_name.get(name)
        if known is None:
            diagnostics.append(_diagnostic(
                f"RUNTIME_{kind.upper()}_UNCATALOGUED", "error", f"runtime exposes uncatalogued {kind} {name}",
                f"/observations/{identifier}/{plural}", observation=identifier, symbol=name,
            ))
        else:
            diagnostics.append(_diagnostic(
                f"RUNTIME_{kind.upper()}_UNAVAILABLE", "error", f"runtime exposes {kind} {name} outside the selected profile or side",
                f"/observations/{identifier}/{plural}", observation=identifier, symbol=name,
            ))
    for name in sorted(set(actual).intersection(expected)):
        symbol = expected[name]
        entry = actual[name]
        if kind == "function" and entry["restricted"] != bool(symbol.get("restricted", False)):
            diagnostics.append(_diagnostic(
                "RUNTIME_RESTRICTION_MISMATCH", "error", f"runtime restriction for {name} differs from the catalogue",
                f"/observations/{identifier}/functions", observation=identifier, symbol=name,
            ))
        if kind == "event" and entry["allowRemoteTrigger"] != bool(symbol.get("allowRemoteTrigger", False)):
            diagnostics.append(_diagnostic(
                "RUNTIME_EVENT_REMOTE_POLICY_MISMATCH", "error", f"runtime remote-trigger policy for {name} differs from the catalogue",
                f"/observations/{identifier}/events", observation=identifier, symbol=name,
            ))
    return diagnostics


def _compare_components(
    snapshot: dict[str, Any], project: dict[str, Any], project_api: dict[str, Any],
) -> list[dict[str, Any]]:
    diagnostics: list[dict[str, Any]] = []
    expected_resources = {item["name"] for item in project.get("resources", [])}
    expected_modules = {item["name"] for item in project.get("modules", [])}
    expected_exports: dict[str, set[tuple[str, str]]] = {}
    expected_module_exports: dict[str, set[tuple[str, str]]] = {}
    for symbol in project_api.get("symbols", []):
        if symbol.get("kind") == "export" and symbol.get("ownerKind") == "resource":
            expected_exports.setdefault(symbol["owner"], set()).add((symbol["name"], symbol["side"]))
        if symbol.get("kind") == "function" and symbol.get("ownerKind") == "module":
            expected_module_exports.setdefault(symbol["owner"], set()).add((symbol["name"], symbol["side"]))
    expected_module_contracts = {
        item["name"]: item for item in project_api.get("components", []) if item.get("kind") == "module"
    }
    resource_sides: dict[str, set[str]] = {}
    for component in project_api.get("components", []):
        if component.get("kind") == "resource":
            resource_sides.setdefault(component["name"], set()).update(component.get("sides", []))
    for symbol in project_api.get("symbols", []):
        if symbol.get("ownerKind") == "resource" and symbol.get("side"):
            resource_sides.setdefault(symbol["owner"], set()).add(symbol["side"])
    for observation in snapshot["observations"]:
        identifier = observation["id"]
        resources = {item["name"]: item for item in observation["resources"]}
        modules = {item["name"]: item for item in observation["modules"]}
        for name in _duplicates(observation["resources"]):
            diagnostics.append(_diagnostic("RUNTIME_RESOURCE_DUPLICATE", "error", f"runtime reports resource {name} more than once", f"/observations/{identifier}/resources", observation=identifier, symbol=name))
        for name in _duplicates(observation["modules"]):
            diagnostics.append(_diagnostic("RUNTIME_MODULE_DUPLICATE", "error", f"runtime reports module {name} more than once", f"/observations/{identifier}/modules", observation=identifier, symbol=name))
        expected_resources_for_side = {
            name for name in expected_resources
            if observation["side"] == "server"
            or any(side in {observation["side"], "shared"} for side in resource_sides.get(name, set()))
        }
        if observation["completeness"] == "complete":
            for name in sorted(expected_resources_for_side - set(resources)):
                diagnostics.append(_diagnostic("RUNTIME_RESOURCE_MISSING", "error", f"project resource {name} is absent", f"/observations/{identifier}/resources", observation=identifier, symbol=name))
        for name in sorted(set(resources) - expected_resources):
            diagnostics.append(_diagnostic("RUNTIME_RESOURCE_UNDECLARED", "error", f"project inventory contains undeclared resource {name}", f"/observations/{identifier}/resources", observation=identifier, symbol=name))
        for name in sorted(expected_resources.intersection(resources)):
            resource = resources[name]
            if name not in expected_resources_for_side:
                diagnostics.append(_diagnostic(
                    "RUNTIME_RESOURCE_WRONG_SIDE", "error",
                    f"project resource {name} is not declared for the {observation['side']} side",
                    f"/observations/{identifier}/resources", observation=identifier, symbol=name,
                ))
            if resource["state"] != "running":
                diagnostics.append(_diagnostic("RUNTIME_RESOURCE_NOT_RUNNING", "error", f"project resource {name} is {resource['state']}", f"/observations/{identifier}/resources", observation=identifier, symbol=name))
            export_values = [(item["name"], item["side"]) for item in resource["exports"]]
            if len(set(export_values)) != len(export_values):
                diagnostics.append(_diagnostic(
                    "RUNTIME_EXPORT_DUPLICATE", "error", f"resource {name} reports duplicate exports",
                    f"/observations/{identifier}/resources", observation=identifier, symbol=name,
                ))
            actual_exports = set(export_values)
            expected = {
                item for item in expected_exports.get(name, set())
                if item[1] in {observation["side"], "shared"}
            }
            if observation["completeness"] == "complete":
                for export in sorted(expected - actual_exports):
                    diagnostics.append(_diagnostic("RUNTIME_EXPORT_MISSING", "error", f"resource {name} is missing export {export[0]} ({export[1]})", f"/observations/{identifier}/resources", observation=identifier, symbol=export[0]))
            for export in sorted(actual_exports - expected):
                diagnostics.append(_diagnostic("RUNTIME_EXPORT_UNDECLARED", "error", f"resource {name} exposes undeclared export {export[0]} ({export[1]})", f"/observations/{identifier}/resources", observation=identifier, symbol=export[0]))
        expected_modules_for_side = {
            name for name in expected_modules
            if observation["side"] == "server" and not expected_module_exports.get(name)
            or any(side in {observation["side"], "shared"} for _, side in expected_module_exports.get(name, set()))
        }
        if observation["completeness"] == "complete":
            for name in sorted(expected_modules_for_side - set(modules)):
                diagnostics.append(_diagnostic("RUNTIME_MODULE_MISSING", "error", f"project module {name} is absent", f"/observations/{identifier}/modules", observation=identifier, symbol=name))
        for name in sorted(set(modules) - expected_modules):
            diagnostics.append(_diagnostic("RUNTIME_MODULE_UNDECLARED", "error", f"project inventory contains undeclared module {name}", f"/observations/{identifier}/modules", observation=identifier, symbol=name))
        for name in sorted(expected_modules.intersection(modules)):
            actual = modules[name]
            if name not in expected_modules_for_side:
                diagnostics.append(_diagnostic(
                    "RUNTIME_MODULE_WRONG_SIDE", "error",
                    f"project module {name} is not declared for the {observation['side']} side",
                    f"/observations/{identifier}/modules", observation=identifier, symbol=name,
                ))
            expected = expected_module_contracts.get(name, {})
            manifest = expected.get("manifest")
            module_contract = expected.get("module")
            binary = expected.get("binary")
            expected_values = {
                "version": manifest.get("version") if manifest else None,
                "abi": module_contract.get("abi") if module_contract else None,
                "manifestSha256": manifest.get("sha256") if manifest else None,
                "binarySha256": binary.get("sha256") if binary else None,
            }
            mismatch_codes = {
                "version": "RUNTIME_MODULE_VERSION_MISMATCH",
                "abi": "RUNTIME_MODULE_ABI_MISMATCH",
                "manifestSha256": "RUNTIME_MODULE_MANIFEST_HASH_MISMATCH",
                "binarySha256": "RUNTIME_MODULE_BINARY_HASH_MISMATCH",
            }
            for field, expected_value in expected_values.items():
                if expected_value is not None and actual[field] != expected_value:
                    diagnostics.append(_diagnostic(
                        mismatch_codes[field], "error",
                        f"runtime module {name} {field} differs from the approved project contract",
                        f"/observations/{identifier}/modules", observation=identifier, symbol=name,
                    ))
            export_values = {(item["name"], item["side"]) for item in actual["exports"]}
            if len(export_values) != len(actual["exports"]):
                diagnostics.append(_diagnostic("RUNTIME_MODULE_EXPORT_DUPLICATE", "error", f"module {name} reports duplicate exports", f"/observations/{identifier}/modules", observation=identifier, symbol=name))
            expected_values = {
                item for item in expected_module_exports.get(name, set())
                if item[1] in {observation["side"], "shared"}
            }
            if observation["completeness"] == "complete":
                for export in sorted(expected_values - export_values):
                    diagnostics.append(_diagnostic("RUNTIME_MODULE_EXPORT_MISSING", "error", f"module {name} is missing export {export[0]} ({export[1]})", f"/observations/{identifier}/modules", observation=identifier, symbol=export[0]))
            for export in sorted(export_values - expected_values):
                diagnostics.append(_diagnostic("RUNTIME_MODULE_EXPORT_UNDECLARED", "error", f"module {name} exposes undeclared export {export[0]} ({export[1]})", f"/observations/{identifier}/modules", observation=identifier, symbol=export[0]))
    return diagnostics


def compare_runtime_snapshot(
    snapshot_path: Path | None,
    project: dict[str, Any],
    project_sha256: str,
    catalogue: dict[str, Any],
    catalogue_sha256: str,
    project_api: dict[str, Any],
    session_id: str,
    schema_store: SchemaStore,
    session_created_at: str | None = None,
    session_expires_at: str | None = None,
    snapshot_payload: bytes | None = None,
) -> dict[str, Any]:
    diagnostics: list[dict[str, Any]] = []
    snapshot_sha256 = "0" * 64
    try:
        if snapshot_payload is None:
            if snapshot_path is None:
                raise JsonDocumentError("runtime snapshot payload is unavailable")
            if snapshot_path.stat().st_size > 16 * 1024 * 1024:
                raise JsonDocumentError("runtime snapshot exceeds 16777216 bytes")
            payload = snapshot_path.read_bytes()
        else:
            payload = snapshot_payload
            if len(payload) > 16 * 1024 * 1024:
                raise JsonDocumentError("runtime snapshot exceeds 16777216 bytes")
        snapshot_sha256 = sha256_bytes(payload)
        from .scenario import load_json_text

        snapshot = load_json_text(payload.decode("utf-8"))
    except (JsonDocumentError, OSError, UnicodeError) as exc:
        diagnostics.append(_diagnostic("RUNTIME_SNAPSHOT_INVALID", "error", str(exc), "."))
        return _result(diagnostics, project, project_sha256, catalogue_sha256, snapshot_sha256, 0, {}, session_id)
    issues = schema_store.validate("neon-runtime-snapshot", snapshot)
    for issue in issues:
        diagnostics.append(_diagnostic("RUNTIME_SNAPSHOT_INVALID", "error", f"{issue.pointer}: {issue.message}", issue.pointer))
    if issues or not isinstance(snapshot, dict):
        return _result(diagnostics, project, project_sha256, catalogue_sha256, snapshot_sha256, 0, {}, session_id)
    if schema_major(snapshot["schemaVersion"]) != 1:
        diagnostics.append(_diagnostic("RUNTIME_SNAPSHOT_SCHEMA_UNSUPPORTED", "error", f"unsupported runtime snapshot schema {snapshot['schemaVersion']}", "/schemaVersion"))
    observed_at: datetime | None = None
    try:
        observed_at = datetime.strptime(snapshot["observedAt"], "%Y-%m-%dT%H:%M:%SZ")
    except ValueError:
        diagnostics.append(_diagnostic("RUNTIME_SNAPSHOT_TIME_INVALID", "error", "observedAt is not a real UTC timestamp", "/observedAt"))
    if observed_at is not None and session_created_at is not None and session_expires_at is not None:
        created_at = datetime.strptime(session_created_at, "%Y-%m-%dT%H:%M:%SZ")
        expires_at = datetime.strptime(session_expires_at, "%Y-%m-%dT%H:%M:%SZ")
        if not created_at <= observed_at <= expires_at:
            diagnostics.append(_diagnostic(
                "RUNTIME_SNAPSHOT_OUTSIDE_SESSION", "error",
                "snapshot observation time is outside the supervisor session window", "/observedAt",
            ))
        if observed_at > datetime.now(timezone.utc).replace(tzinfo=None) + timedelta(seconds=5):
            diagnostics.append(_diagnostic(
                "RUNTIME_SNAPSHOT_FROM_FUTURE", "error", "snapshot observation time is in the future", "/observedAt",
            ))
    if snapshot["sessionId"] != session_id:
        diagnostics.append(_diagnostic("RUNTIME_SESSION_MISMATCH", "error", "snapshot belongs to another supervisor session", "/sessionId"))
    if snapshot["profile"] != project["profile"]:
        diagnostics.append(_diagnostic("RUNTIME_PROFILE_MISMATCH", "error", "snapshot profile differs from the project", "/profile"))
    if snapshot["projectSha256"] != project_sha256:
        diagnostics.append(_diagnostic("RUNTIME_PROJECT_MISMATCH", "error", "snapshot project hash differs from the pinned session input", "/projectSha256"))
    if snapshot["catalogueSha256"] != catalogue_sha256:
        diagnostics.append(_diagnostic("RUNTIME_CATALOGUE_MISMATCH", "error", "snapshot catalogue hash differs from the pinned session input", "/catalogueSha256"))
    identifiers = [item["id"] for item in snapshot["observations"]]
    if len(set(identifiers)) != len(identifiers):
        diagnostics.append(_diagnostic("RUNTIME_OBSERVATION_DUPLICATE", "error", "observation ids must be unique", "/observations"))
    diagnostics.extend(_topology_issues(snapshot))
    minimum = _version(project["engine"]["minimumVersion"])
    maximum = _version(project["engine"]["maximumVersionExclusive"])
    for observation in snapshot["observations"]:
        version = _version(observation["engineVersion"])
        if not minimum <= version < maximum:
            diagnostics.append(_diagnostic(
                "RUNTIME_ENGINE_VERSION_INCOMPATIBLE", "error",
                f"runtime {observation['engineVersion']} is outside [{project['engine']['minimumVersion']}, {project['engine']['maximumVersionExclusive']})",
                f"/observations/{observation['id']}/engineVersion", observation=observation["id"],
            ))
        diagnostics.extend(_compare_inventory(observation, catalogue, project["profile"], "function"))
        diagnostics.extend(_compare_inventory(observation, catalogue, project["profile"], "event"))
    diagnostics.extend(_compare_components(snapshot, project, project_api))
    return _result(
        diagnostics, project, project_sha256, catalogue_sha256, snapshot_sha256, len(snapshot["observations"]), snapshot, session_id,
    )


def _result(
    diagnostics: list[dict[str, Any]],
    project: dict[str, Any],
    project_sha256: str,
    catalogue_sha256: str,
    snapshot_sha256: str,
    observations: int,
    snapshot: dict[str, Any],
    session_id: str,
) -> dict[str, Any]:
    diagnostics.sort(key=lambda item: (item["severity"], item["code"], item["path"], item.get("observation", ""), item.get("symbol", "")))
    if len(diagnostics) > MAX_RUNTIME_DIAGNOSTICS:
        omitted = len(diagnostics) - MAX_RUNTIME_DIAGNOSTICS
        diagnostics = diagnostics[:MAX_RUNTIME_DIAGNOSTICS]
        diagnostics.append(_diagnostic(
            "RUNTIME_DIAGNOSTICS_TRUNCATED", "error",
            f"{omitted} additional diagnostics were omitted to keep the result bounded", "/diagnostics",
        ))
    errors = sum(item["severity"] == "error" for item in diagnostics)
    warnings = sum(item["severity"] == "warning" for item in diagnostics)
    return {
        "schemaVersion": "1.0.0", "command": "runtime.compare", "status": "pass" if errors == 0 else "fail",
        "summary": {
            "errors": errors, "warnings": warnings, "observations": observations,
            "functions": sum(len(item.get("functions", [])) for item in snapshot.get("observations", [])),
            "events": sum(len(item.get("events", [])) for item in snapshot.get("observations", [])),
            "resources": sum(len(item.get("resources", [])) for item in snapshot.get("observations", [])),
            "modules": sum(len(item.get("modules", [])) for item in snapshot.get("observations", [])),
        },
        "diagnostics": diagnostics,
        "comparison": {
            "sessionId": session_id, "profile": project.get("profile", "unknown"),
            "scope": "observation-only", "grantedEvidenceLabels": [],
            "observations": observations, "catalogueSha256": catalogue_sha256, "projectSha256": project_sha256,
            "snapshotSha256": snapshot_sha256,
            "runtimes": [
                {key: item[key] for key in ("id", "side", "engineVersion", "buildId", "completeness")}
                for item in snapshot.get("observations", [])
            ],
        },
    }
