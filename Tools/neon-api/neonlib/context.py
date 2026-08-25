from __future__ import annotations

import os
import tempfile
from pathlib import Path
from typing import Any

from .components import file_sha256
from .discovery import discovery_keywords
from .jsonio import JsonDocumentError, canonical_json, load_json, sha256_bytes
from .luals import render_luals, render_project_luals
from .project import (
    CheckState,
    LUA_CALL_RE,
    LUA_EVENT_HANDLER_RE,
    LUA_INSTANCE_MEMBER_CALL_RE,
    LUA_OOP_ASSIGN_RE,
    LUA_STATIC_MEMBER_CALL_RE,
    _decode_lua_string,
    _parse_meta,
    _resolve_relative,
    check_project,
    resolve_project_components,
    strip_lua_comments,
    strip_lua_noncode,
    META_ASSET_TAGS,
)
from .schema import SchemaStore, schema_major


class ContextGenerationError(ValueError):
    def __init__(self, result: dict[str, Any]):
        super().__init__("project check failed")
        self.result = result


PROJECT_PAYLOAD_PATHS = {
    "agent-context.json", "api-index.json", "project-api.json",
    "client/.luarc.json", "client/mta-client.lua", "client/mta-shared.lua", "client/project-client.lua",
    "server/.luarc.json", "server/mta-server.lua", "server/mta-shared.lua", "server/project-server.lua",
}
PROJECT_PACK_PATHS = PROJECT_PAYLOAD_PATHS | {"artifacts.json"}
PROJECT_PACK_DIRECTORIES = {"client", "server"}


def _effective_sides(symbol: dict[str, Any], profile: str) -> list[str]:
    sides = list(symbol.get("inheritedSides", [])) if profile == "mta-upstream" else list(symbol.get("sides", []))
    if profile == "neon-server":
        sides = [side for side in sides if side != "client"]
    elif profile == "neon-client":
        sides = [side for side in sides if side != "server"]
    return sorted(set(sides))


def _compact_symbol(symbol: dict[str, Any], profile: str) -> dict[str, Any]:
    record: dict[str, Any] = {
        "id": symbol["id"],
        "kind": symbol["kind"],
        "name": symbol["name"],
        "origin": symbol["origin"],
        "state": symbol["state"],
        "sides": _effective_sides(symbol, profile),
    }
    for field in ("category", "description"):
        value = symbol.get(field)
        if value:
            record[field] = " ".join(value.replace("\r", "").splitlines())
    keywords = discovery_keywords(symbol)
    if keywords:
        record["keywords"] = keywords
    return record


def build_api_index(catalogue: dict[str, Any], profile: str, catalogue_sha256: str) -> dict[str, Any]:
    symbols = []
    for symbol in catalogue["symbols"]:
        record = _compact_symbol(symbol, profile)
        if record["sides"] or symbol["kind"] in ("element", "type"):
            symbols.append(record)
    symbols.sort(key=lambda item: item["id"])
    return {
        "schemaVersion": "1.0.0",
        "profile": profile,
        "catalogueSha256": catalogue_sha256,
        "symbols": symbols,
    }


def _oop_method(catalogue_by_key: dict[tuple[str, str], dict[str, Any]], class_symbol: dict[str, Any], name: str, profile: str) -> dict[str, Any] | None:
    parents_field = "inheritedParents" if profile == "mta-upstream" else "parents"
    pending = [class_symbol]
    visited: set[str] = set()
    while pending:
        current = pending.pop(0)
        if current["name"] in visited:
            continue
        visited.add(current["name"])
        member = next((item for item in current.get("methods", []) if item["name"] == name), None)
        if member is not None:
            return member
        for parent_name in current.get(parents_field, []):
            parent = catalogue_by_key.get(("class", parent_name))
            if parent is not None:
                pending.append(parent)
    return None


def _safe_output_path(output_root: Path, relative: str) -> Path:
    # Generated paths are fixed by Neon, but a pre-existing symlink must not
    # turn a harmless context refresh into a write outside the chosen output.
    root = output_root.resolve()
    candidate = output_root / relative
    if _has_symlink_component(output_root, Path(relative).parent):
        raise ValueError(f"generated path contains a symbolic link: {relative}")
    candidate.parent.mkdir(parents=True, exist_ok=True)
    resolved_parent = candidate.parent.resolve()
    try:
        resolved_parent.relative_to(root)
    except ValueError as exc:
        raise ValueError(f"generated path escapes output directory: {relative}") from exc
    if candidate.is_symlink() or candidate.exists() and not candidate.is_file():
        raise ValueError(f"generated path is not a regular file: {relative}")
    return candidate


def _has_symlink_component(root: Path, relative: Path) -> bool:
    candidate = root
    for part in relative.parts:
        candidate /= part
        if candidate.is_symlink():
            return True
    return False


def _has_symlink_below_base(base: Path, target: Path) -> bool:
    # The project directory is the trust boundary. Common platform aliases
    # such as macOS /tmp are accepted when both project and target share them,
    # while any user-controlled symlink crossed on the way to the pack is not.
    base_absolute = base if base.is_absolute() else Path.cwd() / base
    target_absolute = target if target.is_absolute() else Path.cwd() / target
    try:
        common = Path(os.path.commonpath((str(base_absolute), str(target_absolute))))
        relative = target_absolute.relative_to(common)
    except (ValueError, OSError):
        return True
    return _has_symlink_component(common, relative)


def _write_artifact(output_root: Path, relative: str, payload: bytes, artifact_id: str, kind: str, media_type: str) -> dict[str, Any]:
    path = _safe_output_path(output_root, relative)
    _atomic_write(path, payload)
    return {
        "schemaVersion": "1.0.0",
        "id": artifact_id,
        "kind": kind,
        "path": relative,
        "mediaType": media_type,
        "size": len(payload),
        "sha256": sha256_bytes(payload),
    }


def _atomic_write(path: Path, payload: bytes) -> None:
    # Payloads become visible atomically and artifacts.json is written last, so
    # agents never need to trust a half-written file after interruption.
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def _unexpected_pack_paths(output_root: Path, expected_files: set[str]) -> list[str]:
    if not output_root.exists():
        return []
    if not output_root.is_dir():
        return ["."]
    expected_directories = {str(parent) for value in expected_files for parent in Path(value).parents if str(parent) != "."}
    unexpected: list[str] = []
    for current, directories, files in os.walk(output_root, followlinks=False):
        current_path = Path(current)
        for name in [*directories, *files]:
            path = current_path / name
            relative = path.relative_to(output_root).as_posix()
            expected = relative in expected_directories if path.is_dir() and not path.is_symlink() else relative in expected_files
            if not expected or path.is_symlink() and relative not in expected_files:
                unexpected.append(relative)
    return sorted(set(unexpected))


def _project_files(project_path: Path, project: dict[str, Any]) -> tuple[list[dict[str, Any]], set[tuple[str, str]], set[tuple[str, str, str]]]:
    workspace = project_path.parent.resolve()
    records: dict[str, dict[str, Any]] = {}
    used: set[tuple[str, str]] = set()
    used_members: set[tuple[str, str, str]] = set()

    def add_file(path: Path, display: str, kind: str, owner: str, side: str | None = None) -> None:
        record: dict[str, Any] = {
            "path": display,
            "kind": kind,
            "owner": owner,
            "size": path.stat().st_size,
            "sha256": file_sha256(path),
        }
        if side is not None:
            record["side"] = side
        records[display] = record

    add_file(project_path, project_path.name, "project", project["name"])
    for resource in sorted(project["resources"], key=lambda item: item["name"]):
        root = _resolve_relative(workspace, resource["path"])
        if root is None:
            continue
        meta_name = resource.get("meta", "meta.xml")
        meta_path = _resolve_relative(root, meta_name)
        meta_display = f"{resource['path'].rstrip('/')}/{meta_name}"
        if meta_path is None or not meta_path.is_file():
            continue
        add_file(meta_path, meta_display, "meta", resource["name"])
        if resource.get("manifest"):
            manifest = _resolve_relative(root, resource["manifest"])
            if manifest is not None and manifest.is_file():
                add_file(manifest, f"{resource['path'].rstrip('/')}/{resource['manifest']}", "manifest", resource["name"])
        meta_state = CheckState()
        document = _parse_meta(meta_path, meta_display, meta_state)
        if document is None:
            continue
        for tag in META_ASSET_TAGS:
            for asset in document.findall(tag, "meta", 1):
                source = asset.get("src")
                asset_path = _resolve_relative(root, source) if source else None
                if asset_path is None or not asset_path.is_file():
                    continue
                if tag in ("file", "html"):
                    asset_side = "client"
                elif tag == "map":
                    asset_side = "server"
                else:
                    configured_side = asset.get("type", "server")
                    asset_side = configured_side if configured_side in ("client", "server", "shared") else None
                add_file(
                    asset_path,
                    f"{resource['path'].rstrip('/')}/{source}",
                    "asset",
                    resource["name"],
                    asset_side,
                )
        for script in document.findall("script", "meta", 1):
            source = script.get("src")
            side = script.get("type", "server")
            script_path = _resolve_relative(root, source) if source else None
            if script_path is None or not script_path.is_file():
                continue
            display = f"{resource['path'].rstrip('/')}/{source}"
            add_file(script_path, display, "lua", resource["name"], side)
            text = script_path.read_text(encoding="utf-8")
            code = strip_lua_noncode(text)
            comments_removed = strip_lua_comments(text)
            for match in LUA_CALL_RE.finditer(code):
                used.add(("function", match.group(1)))
            for match in LUA_STATIC_MEMBER_CALL_RE.finditer(code):
                used_members.add((match.group(1), match.group(2), side))
            inferred_instances = {match.group(1): match.group(2) for match in LUA_OOP_ASSIGN_RE.finditer(code)}
            for match in LUA_INSTANCE_MEMBER_CALL_RE.finditer(code):
                class_name = inferred_instances.get(match.group(1))
                if class_name is not None:
                    used_members.add((class_name, match.group(2), side))
            for match in LUA_EVENT_HANDLER_RE.finditer(comments_removed):
                name = _decode_lua_string(match.group(1), match.group(2))
                if name is not None:
                    used.add(("event", name))

    for module in sorted(project.get("modules", []), key=lambda item: item["name"]):
        root = _resolve_relative(workspace, module["path"])
        if root is None:
            continue
        for field, kind in (("manifest", "manifest"), ("binary", "binary")):
            relative = module.get(field)
            path = _resolve_relative(root, relative) if relative else None
            if path is not None and path.is_file():
                add_file(path, f"{module['path'].rstrip('/')}/{relative}", kind, module["name"])
    return [records[key] for key in sorted(records)], used, used_members


def _luarc(side: str) -> dict[str, Any]:
    return {
        "$schema": "https://raw.githubusercontent.com/LuaLS/vscode-lua/master/setting/schema.json",
        "runtime": {"version": "Lua 5.1"},
        "workspace": {"checkThirdParty": False, "library": ["mta-shared.lua", f"mta-{side}.lua", f"project-{side}.lua"]},
        "diagnostics": {"globals": ["exports", "resource", "resourceRoot", "root", "source"]},
    }


def generate_project_context(
    project_path: Path,
    schema_store: SchemaStore,
    output_directory: Path,
    catalogue_override: Path | None = None,
) -> tuple[dict[str, Any], dict[str, Any]]:
    if _has_symlink_below_base(project_path.parent, output_directory):
        raise ValueError("output directory path cannot contain a symbolic link below the project trust boundary")
    unexpected = _unexpected_pack_paths(output_directory, PROJECT_PACK_PATHS)
    if unexpected:
        raise ValueError(f"output directory contains unowned path: {unexpected[0]}")
    check = check_project(project_path, schema_store, catalogue_override)
    if check["status"] != "pass":
        raise ContextGenerationError(check)
    project = load_json(project_path)
    workspace = project_path.parent.resolve()
    catalogue_path = catalogue_override or _resolve_relative(workspace, project["catalogue"])
    if catalogue_path is None:
        raise ValueError("catalogue path cannot be resolved")
    catalogue = load_json(catalogue_path)
    catalogue_hash = file_sha256(catalogue_path)
    resolved = resolve_project_components(project_path, schema_store, catalogue_path)
    if resolved["status"] != "pass":
        raise ContextGenerationError(resolved)
    files, used_names, used_members = _project_files(project_path, project)
    by_key = {(item["kind"], item["name"]): item for item in catalogue["symbols"]}
    used_ids = {by_key[key]["id"] for key in used_names if key in by_key}
    function_by_name = {item["name"]: item for item in catalogue["symbols"] if item["kind"] == "function"}
    for class_name, member_name, script_side in used_members:
        class_symbol = by_key.get(("class", class_name))
        if class_symbol is None:
            continue
        used_ids.add(class_symbol["id"])
        member = _oop_method(by_key, class_symbol, member_name, project["profile"])
        binding_field = "inheritedBindings" if project["profile"] == "mta-upstream" else "bindings"
        used_sides = ("client", "server") if script_side == "shared" else (script_side,)
        function_names = [] if member is None else [
            binding["globalFunction"] for binding in member.get(binding_field, [])
            if binding.get("side") in used_sides and binding.get("globalFunction")
        ]
        for function_name in function_names:
            if function_name in function_by_name:
                used_ids.add(function_by_name[function_name]["id"])
    used_ids = sorted(used_ids)
    index = build_api_index(catalogue, project["profile"], catalogue_hash)

    project_api_payload = canonical_json(resolved).encode("utf-8")
    api_index_payload = canonical_json(index).encode("utf-8")
    context = {
        "schemaVersion": "1.0.0",
        "project": {"name": project["name"], "profile": project["profile"], "engine": project["engine"], "sha256": file_sha256(project_path)},
        "catalogue": {
            "schemaVersion": catalogue["schemaVersion"], "catalogueVersion": catalogue["catalogueVersion"],
            "engine": catalogue["engine"], "sha256": catalogue_hash, "symbols": len(index["symbols"]),
        },
        "projectApi": {"path": "project-api.json", "sha256": sha256_bytes(project_api_payload), "symbols": len(resolved["symbols"])},
        "apiIndex": {"path": "api-index.json", "sha256": sha256_bytes(api_index_payload), "symbols": len(index["symbols"])},
        "files": files,
        "usedApiIds": used_ids,
        "validation": {"status": check["status"], "summary": check["summary"], "diagnostics": check["diagnostics"]},
        "guidance": {
            "validate": "neon check --json",
            "resolve": "neon project resolve --json",
            "discover": "neon api search <query> --json",
            "rule": "Never infer a signature when signatureKnown is false or state is opaque/conflict.",
        },
    }
    context_payload = canonical_json(context).encode("utf-8")

    output_directory.mkdir(parents=True, exist_ok=True)
    artifacts = [
        _write_artifact(output_directory, "project-api.json", project_api_payload, "neon:artifact:project-api", "json", "application/json"),
        _write_artifact(output_directory, "api-index.json", api_index_payload, "neon:artifact:api-index", "json", "application/json"),
        _write_artifact(output_directory, "agent-context.json", context_payload, "neon:artifact:agent-context", "json", "application/json"),
    ]
    local_symbols = resolved["symbols"]
    for side in ("client", "server"):
        for name, payload in (
            ("mta-shared.lua", render_luals(catalogue, "shared", project["profile"]).encode("utf-8")),
            (f"mta-{side}.lua", render_luals(catalogue, side, project["profile"]).encode("utf-8")),
            (f"project-{side}.lua", render_project_luals(local_symbols, side).encode("utf-8")),
            (".luarc.json", canonical_json(_luarc(side)).encode("utf-8")),
        ):
            kind, media = ("luals", "text/x-lua") if name.endswith(".lua") else ("json", "application/json")
            artifact_name = name.removeprefix(".").replace(".", "-")
            artifacts.append(_write_artifact(output_directory, f"{side}/{name}", payload, f"neon:artifact:{side}-{artifact_name}", kind, media))
    artifacts.sort(key=lambda item: item["id"])
    artifact_index = {"schemaVersion": "1.0.0", "artifacts": artifacts}
    _atomic_write(_safe_output_path(output_directory, "artifacts.json"), canonical_json(artifact_index).encode("utf-8"))
    return context, artifact_index


def verify_project_context(
    project_path: Path,
    schema_store: SchemaStore,
    context_directory: Path,
    catalogue_override: Path | None = None,
) -> dict[str, Any]:
    diagnostics: list[dict[str, Any]] = []

    def error(code: str, message: str, path: str) -> None:
        diagnostics.append({"code": code, "severity": "error", "message": message, "path": path})

    if _has_symlink_below_base(project_path.parent, context_directory):
        error("CONTEXT_PATH_OUTSIDE", "context directory path cannot contain a symbolic link below the project trust boundary", "artifacts.json")
        return _context_verify_result(diagnostics, 0, 0)
    if not context_directory.is_dir():
        error("CONTEXT_MISSING", "context directory does not exist", str(context_directory))
        return _context_verify_result(diagnostics, 0, 0)
    for required in ("agent-context.json", "artifacts.json"):
        candidate = context_directory / required
        if candidate.is_symlink():
            error("CONTEXT_PATH_OUTSIDE", f"context control file cannot be a symbolic link: {required}", required)
        elif not candidate.is_file():
            error("CONTEXT_MISSING", f"context control file is missing: {required}", required)
    if diagnostics:
        return _context_verify_result(diagnostics, 0, 0)
    try:
        context = load_json(context_directory / "agent-context.json")
    except JsonDocumentError as exc:
        error("CONTEXT_JSON_INVALID", str(exc), "agent-context.json")
        return _context_verify_result(diagnostics, 0, 0)
    try:
        artifact_index = load_json(context_directory / "artifacts.json")
    except JsonDocumentError as exc:
        error("CONTEXT_JSON_INVALID", str(exc), "artifacts.json")
        return _context_verify_result(diagnostics, 0, 0)
    context_issues = schema_store.validate("neon-agent-context", context)
    for issue in context_issues:
        error("CONTEXT_SCHEMA_INVALID", f"{issue.pointer}: {issue.message}", "agent-context.json")
    if context_issues or not isinstance(context, dict):
        return _context_verify_result(diagnostics, 0, 0)
    if (
        not isinstance(artifact_index, dict)
        or set(artifact_index) != {"schemaVersion", "artifacts"}
        or not isinstance(artifact_index.get("schemaVersion"), str)
        or schema_major(artifact_index["schemaVersion"]) != 1
        or not isinstance(artifact_index.get("artifacts"), list)
    ):
        error("CONTEXT_ARTIFACT_INDEX_INVALID", "artifacts.json must contain only schemaVersion and an artifacts array", "artifacts.json")
        return _context_verify_result(diagnostics, 0, len(context.get("files", [])))

    artifacts = artifact_index["artifacts"]
    ids: set[str] = set()
    paths: set[str] = set()
    for index, artifact in enumerate(artifacts):
        issues = schema_store.validate("neon-artifact", artifact)
        for issue in issues:
            error("CONTEXT_ARTIFACT_INDEX_INVALID", f"/artifacts/{index}{issue.pointer}: {issue.message}", "artifacts.json")
        if issues:
            continue
        if artifact["id"] in ids or artifact["path"] in paths:
            error("CONTEXT_ARTIFACT_DUPLICATE", f"duplicate artifact identity or path {artifact['id']}", "artifacts.json")
            continue
        ids.add(artifact["id"])
        paths.add(artifact["path"])
        if _has_symlink_component(context_directory, Path(artifact["path"])):
            error("CONTEXT_PATH_OUTSIDE", f"artifact path contains a symbolic link: {artifact['path']}", artifact["path"])
            continue
        path = _resolve_relative(context_directory, artifact["path"])
        if path is None:
            error("CONTEXT_PATH_OUTSIDE", f"artifact path escapes context directory: {artifact['path']}", "artifacts.json")
        elif not path.is_file():
            error("CONTEXT_ARTIFACT_MISSING", f"artifact is missing: {artifact['path']}", artifact["path"])
        elif path.stat().st_size != artifact["size"] or file_sha256(path) != artifact["sha256"]:
            error("CONTEXT_ARTIFACT_HASH_MISMATCH", f"artifact content does not match its recorded size/hash: {artifact['path']}", artifact["path"])

    if diagnostics:
        return _context_verify_result(diagnostics, len(artifacts), len(context.get("files", [])))

    expected_pack_files = paths | {"artifacts.json"}
    for unexpected in _unexpected_pack_paths(context_directory, expected_pack_files):
        error("CONTEXT_UNINDEXED_PATH", f"context pack contains an unindexed path: {unexpected}", unexpected)
    if diagnostics:
        return _context_verify_result(diagnostics, len(artifacts), len(context.get("files", [])))

    references = (("apiIndex", "neon-api-index"), ("projectApi", "neon-project-api"))
    artifacts_by_path = {artifact["path"]: artifact for artifact in artifacts}
    for reference_name, schema_name in references:
        reference = context[reference_name]
        artifact = artifacts_by_path.get(reference["path"])
        if artifact is None or artifact["sha256"] != reference["sha256"]:
            error("CONTEXT_REFERENCE_MISMATCH", f"{reference_name} does not match its artifact record", reference["path"])
            continue
        try:
            document = load_json(context_directory / reference["path"])
        except JsonDocumentError as exc:
            error("CONTEXT_JSON_INVALID", str(exc), reference["path"])
            continue
        for issue in schema_store.validate(schema_name, document):
            error("CONTEXT_SCHEMA_INVALID", f"{issue.pointer}: {issue.message}", reference["path"])

    if diagnostics:
        return _context_verify_result(diagnostics, len(artifacts), len(context.get("files", [])))

    # Regeneration checks both source-file freshness and every derived payload;
    # it is stronger than trusting the hashes recorded by the same old pack.
    try:
        with tempfile.TemporaryDirectory(prefix="neon-context-verify-") as temporary:
            _, expected_index = generate_project_context(project_path, schema_store, Path(temporary).resolve(), catalogue_override)
    except ContextGenerationError as exc:
        error("CONTEXT_PROJECT_INVALID", "the current project no longer passes generation checks", project_path.name)
        diagnostics.extend(exc.result["diagnostics"])
    except (OSError, ValueError) as exc:
        error("CONTEXT_REGENERATION_FAILED", str(exc), project_path.name)
    else:
        if canonical_json(expected_index) != canonical_json(artifact_index):
            error("CONTEXT_STALE", "generated context does not match the current project, catalogue, or generator", "artifacts.json")
    if not diagnostics:
        diagnostics.extend(context["validation"]["diagnostics"])
    return _context_verify_result(diagnostics, len(artifacts), len(context.get("files", [])))


def _context_verify_result(diagnostics: list[dict[str, Any]], artifacts: int, files: int) -> dict[str, Any]:
    diagnostics.sort(key=lambda item: (item["severity"], item["code"], item["path"], item["message"]))
    errors = sum(item["severity"] == "error" for item in diagnostics)
    warnings = sum(item["severity"] == "warning" for item in diagnostics)
    return {
        "schemaVersion": "1.0.0",
        "command": "context.verify",
        "status": "pass" if errors == 0 else "fail",
        "summary": {"errors": errors, "warnings": warnings, "artifacts": artifacts, "files": files},
        "diagnostics": diagnostics,
    }
