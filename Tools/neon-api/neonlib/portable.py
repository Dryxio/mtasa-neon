from __future__ import annotations

import hashlib
import sys
import tempfile
from pathlib import Path
from typing import Any

from .anchored import DirectoryAnchor
from .context import generate_project_context, verify_project_context
from .discovery import search_symbols
from .jsonio import JsonDocumentError, parse_json_bytes, write_json
from .package_contract import MINIMUM_PYTHON_VERSION, PACKAGE_VERSION, REQUIRED_PACKAGE_PATHS
from .project import check_project
from .schema import SchemaStore


MANIFEST_NAME = "NEON_CLI_MANIFEST.json"
MINIMUM_PYTHON = (3, 10)
MAX_MANIFEST_BYTES = 4 * 1024 * 1024
MAX_PACKAGE_FILE_BYTES = 32 * 1024 * 1024


def package_root(tool_directory: Path) -> Path:
    return tool_directory.parents[1]


def _diagnostic(code: str, message: str, path: str = ".") -> dict[str, str]:
    return {"code": code, "severity": "error", "message": message[:1024], "path": path}


def distribution_mode(tool_directory: Path) -> str:
    root = package_root(tool_directory)
    has_manifest = (root / MANIFEST_NAME).exists() or (root / MANIFEST_NAME).is_symlink()
    has_maintainer_tests = (tool_directory / "tests" / "test_neon_api.py").is_file()
    return "portable" if has_manifest or not has_maintainer_tests else "repository"


def _verified_package(
    tool_directory: Path, schema_store: SchemaStore, *, required: bool,
) -> tuple[list[dict[str, str]], bytes | None]:
    root = package_root(tool_directory)
    try:
        anchor = DirectoryAnchor(root)
    except (OSError, ValueError) as exc:
        return [_diagnostic("PACKAGE_ROOT_UNSAFE", str(exc), ".")], None
    with anchor:
        return _verify_anchored_package(anchor, schema_store, required=required)


def _verify_anchored_package(
    anchor: DirectoryAnchor, schema_store: SchemaStore, *, required: bool,
) -> tuple[list[dict[str, str]], bytes | None]:
    try:
        manifest_payload = anchor.read(MANIFEST_NAME, MAX_MANIFEST_BYTES)
    except FileNotFoundError:
        if required:
            return [_diagnostic("PACKAGE_MANIFEST_MISSING", "portable package manifest is missing", MANIFEST_NAME)], None
        return [], None
    except (OSError, ValueError) as exc:
        return [_diagnostic("PACKAGE_MANIFEST_UNSAFE", str(exc), MANIFEST_NAME)], None
    try:
        manifest = parse_json_bytes(manifest_payload, max_bytes=MAX_MANIFEST_BYTES)
    except JsonDocumentError as exc:
        return [_diagnostic("PACKAGE_MANIFEST_INVALID", str(exc), MANIFEST_NAME)], None
    issues = schema_store.validate("neon-package-manifest", manifest)
    if issues:
        return ([
            _diagnostic("PACKAGE_MANIFEST_INVALID", f"{issue.pointer}: {issue.message}", MANIFEST_NAME)
            for issue in issues
        ], None)
    diagnostics: list[dict[str, str]] = []
    catalogue_entries = [entry for entry in manifest["files"] if entry["path"] == "Tools/neon-api/neon-api.json"]
    if len(catalogue_entries) != 1 or catalogue_entries[0]["sha256"] != manifest["catalogueSha256"]:
        diagnostics.append(_diagnostic(
            "PACKAGE_CATALOGUE_BINDING_INVALID",
            "catalogueSha256 must match the unique packaged catalogue entry",
            MANIFEST_NAME,
        ))
    seen: set[str] = set()
    catalogue_payload: bytes | None = None
    for entry in sorted(manifest["files"], key=lambda item: item["path"]):
        relative = entry["path"]
        if relative in seen:
            diagnostics.append(_diagnostic("PACKAGE_PATH_INVALID", f"unsafe or duplicate manifest path: {relative}", MANIFEST_NAME))
            continue
        seen.add(relative)
        if entry["size"] > MAX_PACKAGE_FILE_BYTES:
            diagnostics.append(_diagnostic("PACKAGE_FILE_TOO_LARGE", f"packaged file exceeds {MAX_PACKAGE_FILE_BYTES} bytes", relative))
            continue
        try:
            payload = anchor.read(relative, MAX_PACKAGE_FILE_BYTES)
        except FileNotFoundError:
            diagnostics.append(_diagnostic("PACKAGE_FILE_MISSING", "packaged file is missing", relative))
            continue
        except (OSError, ValueError) as exc:
            diagnostics.append(_diagnostic("PACKAGE_FILE_UNSAFE", str(exc), relative))
            continue
        if len(payload) != entry["size"]:
            diagnostics.append(_diagnostic("PACKAGE_FILE_SIZE_MISMATCH", "packaged file size does not match the signed manifest", relative))
            continue
        if hashlib.sha256(payload).hexdigest() != entry["sha256"]:
            diagnostics.append(_diagnostic("PACKAGE_FILE_HASH_MISMATCH", "packaged file hash does not match the signed manifest", relative))
            continue
        if relative == "Tools/neon-api/neon-api.json":
            catalogue_payload = payload
    if seen != REQUIRED_PACKAGE_PATHS:
        missing = sorted(REQUIRED_PACKAGE_PATHS - seen)
        unexpected = sorted(seen - REQUIRED_PACKAGE_PATHS)
        details = []
        if missing:
            details.append(f"missing required entries: {', '.join(missing)}")
        if unexpected:
            details.append(f"unexpected entries: {', '.join(unexpected)}")
        diagnostics.append(_diagnostic("PACKAGE_INVENTORY_INVALID", "; ".join(details), MANIFEST_NAME))
    if catalogue_payload is not None:
        try:
            catalogue = parse_json_bytes(catalogue_payload)
        except JsonDocumentError as exc:
            diagnostics.append(_diagnostic("PACKAGE_CATALOGUE_BINDING_INVALID", str(exc), "Tools/neon-api/neon-api.json"))
        else:
            expected_metadata = {
                "packageVersion": PACKAGE_VERSION,
                "minimumPython": MINIMUM_PYTHON_VERSION,
                "catalogueVersion": catalogue.get("catalogueVersion"),
                "engineVersion": catalogue.get("engine", {}).get("version") if isinstance(catalogue.get("engine"), dict) else None,
            }
            for field, expected in expected_metadata.items():
                if manifest.get(field) != expected:
                    diagnostics.append(_diagnostic(
                        "PACKAGE_METADATA_BINDING_INVALID",
                        f"{field} does not match the packaged CLI/catalogue contract",
                        MANIFEST_NAME,
                    ))
    if not anchor.current():
        diagnostics.append(_diagnostic("PACKAGE_ROOT_CHANGED", "package root identity changed during verification", "."))
    return diagnostics, catalogue_payload


def verify_package_manifest(tool_directory: Path, schema_store: SchemaStore, *, required: bool = False) -> list[dict[str, str]]:
    diagnostics, _ = _verified_package(tool_directory, schema_store, required=required)
    return diagnostics


def run_portable_self_test(tool_directory: Path, schema_store: SchemaStore, *, command: str = "self-test") -> dict[str, Any]:
    mode = distribution_mode(tool_directory)
    diagnostics, catalogue_payload = _verified_package(tool_directory, schema_store, required=mode == "portable")
    checks = 1
    if sys.version_info < MINIMUM_PYTHON:
        diagnostics.append(_diagnostic("PYTHON_VERSION_UNSUPPORTED", "Neon requires Python 3.10 or newer"))
    checks += 1
    if catalogue_payload is None:
        try:
            with DirectoryAnchor(package_root(tool_directory)) as anchor:
                catalogue_payload = anchor.read("Tools/neon-api/neon-api.json", MAX_PACKAGE_FILE_BYTES)
        except (OSError, ValueError) as exc:
            diagnostics.append(_diagnostic("CATALOGUE_INVALID", str(exc), "Tools/neon-api/neon-api.json"))
    try:
        catalogue = parse_json_bytes(catalogue_payload) if catalogue_payload is not None else None
    except JsonDocumentError as exc:
        diagnostics.append(_diagnostic("CATALOGUE_INVALID", str(exc), "Tools/neon-api/neon-api.json"))
        catalogue = None
    checks += 1
    if catalogue is not None:
        issues = schema_store.validate("neon-api", catalogue)
        diagnostics.extend(
            _diagnostic("CATALOGUE_INVALID", f"{issue.pointer}: {issue.message}", "Tools/neon-api/neon-api.json")
            for issue in issues
        )
        matches = search_symbols(catalogue["symbols"], "create vehicle")[:5] if not issues else []
        if not any(item.get("name") == "createVehicle" for item in matches):
            diagnostics.append(_diagnostic("API_DISCOVERY_FAILED", "semantic discovery could not find createVehicle"))
    checks += 1

    if catalogue is not None and not diagnostics:
        try:
            with tempfile.TemporaryDirectory(prefix="neon-portable-self-test-") as temporary:
                root = Path(temporary)
                resource = root / "resources" / "smoke"
                resource.mkdir(parents=True)
                (resource / "meta.xml").write_text('<meta><script src="server.lua" type="server" /></meta>\n', encoding="utf-8")
                (resource / "server.lua").write_text('local vehicle = createVehicle(411, 0, 0, 3)\n', encoding="utf-8")
                (root / ".neon-tooling").mkdir()
                (root / ".neon-tooling" / "neon-api.json").write_bytes(catalogue_payload)
                project = {
                    "schemaVersion": "1.1.0",
                    "name": "portable-self-test",
                    "profile": "neon-pair",
                    "engine": {"minimumVersion": "1.7.0", "maximumVersionExclusive": "1.8.0"},
                    "catalogue": ".neon-tooling/neon-api.json",
                    "resources": [{"name": "smoke", "path": "resources/smoke"}],
                    "modules": [],
                    "externalDependencies": [],
                    "requiredApis": [],
                    "unknownApis": "allow",
                    "unknownComponents": "allow-opaque",
                }
                write_json(root / "neon.project.json", project)
                checks += 1
                checked = check_project(root / "neon.project.json", schema_store)
                if checked["status"] != "pass":
                    diagnostics.extend(checked["diagnostics"])
                else:
                    checks += 1
                    generate_project_context(root / "neon.project.json", schema_store, root / ".neon")
                    checks += 1
                    verified = verify_project_context(root / "neon.project.json", schema_store, root / ".neon")
                    if verified["status"] != "pass":
                        diagnostics.extend(verified["diagnostics"])
        except (OSError, ValueError) as exc:
            diagnostics.append(_diagnostic("PORTABLE_WORKFLOW_FAILED", str(exc)))

    errors = sum(item.get("severity") == "error" for item in diagnostics)
    return {
        "schemaVersion": "1.0.0",
        "command": command,
        "status": "pass" if not errors else "fail",
        "summary": {"errors": errors, "warnings": 0, "tests": checks, "skipped": 0},
        "diagnostics": diagnostics,
        "mode": mode,
    }
