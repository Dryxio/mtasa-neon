from __future__ import annotations

import os
import re
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any

from .anchored import DirectoryAnchor
from .context import (
    PROJECT_PACK_DIRECTORIES,
    PROJECT_PACK_PATHS,
    ContextGenerationError,
    generate_project_context,
    verify_project_context,
)
from .jsonio import canonical_json, load_json
from .project import check_project
from .schema import SchemaStore


CATALOGUE_DIRECTORY = ".neon-tooling"
CATALOGUE_NAME = "neon-api.json"
PROJECT_NAME = "neon.project.json"
AGENT_GUIDE_NAME = "NEON_AGENT.md"
MAX_DISCOVERY_DIRECTORIES = 4096
COMPONENT_NAME_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
SKIPPED_DIRECTORIES = {
    ".git", ".hg", ".neon", ".neon-runtime", ".neon-runs", ".neon-sessions",
    CATALOGUE_DIRECTORY, "node_modules", "vendor", "__pycache__",
}


class InitializationError(ValueError):
    def __init__(self, code: str, message: str, path: str = "."):
        super().__init__(message)
        self.code = code
        self.path = path


def _relative_path(workspace: Path, candidate: Path) -> str:
    try:
        relative = candidate.relative_to(workspace)
    except ValueError as exc:
        raise InitializationError("RESOURCE_OUTSIDE_WORKSPACE", "resource must be inside the selected workspace") from exc
    value = PurePosixPath(*relative.parts).as_posix()
    if not value or value == "." or ".." in PurePosixPath(value).parts:
        raise InitializationError(
            "RESOURCE_ROOT_UNSUPPORTED",
            "the workspace itself cannot be an MTA resource; select its parent gamemode directory",
        )
    return value


def _resource_entry(workspace: Path, directory: Path) -> dict[str, str]:
    if directory.is_symlink():
        raise InitializationError("RESOURCE_SYMLINK_REJECTED", "resource directories cannot be symbolic links", _relative_path(workspace, directory))
    try:
        resolved = directory.resolve(strict=True)
    except OSError as exc:
        raise InitializationError("RESOURCE_INVALID", str(exc), str(directory)) from exc
    relative = _relative_path(workspace, resolved)
    name = resolved.name
    if not COMPONENT_NAME_RE.fullmatch(name):
        raise InitializationError(
            "RESOURCE_NAME_INVALID",
            f"resource directory name {name!r} must contain only letters, digits, dot, underscore, or dash",
            relative,
        )
    meta = resolved / "meta.xml"
    if not meta.is_file() or meta.is_symlink():
        raise InitializationError("RESOURCE_META_MISSING", "resource has no regular meta.xml", relative)
    return {"name": name, "path": relative}


def discover_resources(workspace: Path, explicit: list[str] | None = None) -> list[dict[str, str]]:
    workspace = workspace.resolve(strict=True)
    if explicit:
        entries = []
        for value in explicit:
            raw = Path(value)
            candidate = raw if raw.is_absolute() else workspace / raw
            entries.append(_resource_entry(workspace, candidate))
        return _unique_resources(entries)

    roots = []
    for candidate in (workspace / "resources", workspace / "mods" / "deathmatch" / "resources"):
        if candidate.is_dir() and not candidate.is_symlink():
            roots.append(candidate)
    if not roots:
        roots.append(workspace)

    entries: list[dict[str, str]] = []
    visited = 0
    for root in roots:
        for current_text, directories, files in os.walk(root, topdown=True, followlinks=False):
            visited += 1
            if visited > MAX_DISCOVERY_DIRECTORIES:
                raise InitializationError(
                    "RESOURCE_DISCOVERY_LIMIT",
                    f"resource discovery exceeded {MAX_DISCOVERY_DIRECTORIES} directories; use --resource explicitly",
                )
            current = Path(current_text)
            safe_directories = []
            for name in sorted(directories):
                candidate = current / name
                if name in SKIPPED_DIRECTORIES or candidate.is_symlink():
                    continue
                safe_directories.append(name)
            directories[:] = safe_directories
            if "meta.xml" in files:
                entries.append(_resource_entry(workspace, current))
                directories[:] = []
    return _unique_resources(entries)


def _unique_resources(entries: list[dict[str, str]]) -> list[dict[str, str]]:
    by_name: dict[str, dict[str, str]] = {}
    for entry in sorted(entries, key=lambda item: (item["name"], item["path"])):
        previous = by_name.get(entry["name"])
        if previous is not None and previous["path"] != entry["path"]:
            raise InitializationError(
                "DUPLICATE_RESOURCE_NAME",
                f"resource name {entry['name']!r} is used by both {previous['path']} and {entry['path']}",
            )
        by_name[entry["name"]] = entry
    return sorted(by_name.values(), key=lambda item: item["name"])


def _safe_name(value: str) -> str:
    candidate = re.sub(r"[^A-Za-z0-9_.-]+", "-", value).strip("-.")
    return candidate or "mta-gamemode"


def _install_catalogue(
    workspace: Path,
    workspace_anchor: DirectoryAnchor,
    source: Path,
    created: list[str],
    preserved: list[str],
    owned: dict[str, tuple[int, int]],
    owned_directories: set[str],
) -> Path:
    target_directory = workspace / CATALOGUE_DIRECTORY
    directory_created = False
    kind = workspace_anchor.entry_kind(CATALOGUE_DIRECTORY)
    if kind == "missing":
        directory_identity = workspace_anchor.mkdir_new(CATALOGUE_DIRECTORY, 0o700)
        directory_created = True
        created.append(CATALOGUE_DIRECTORY)
        owned[CATALOGUE_DIRECTORY] = directory_identity
        owned_directories.add(CATALOGUE_DIRECTORY)
    elif kind != "directory":
        raise InitializationError(
            "TOOLING_DIRECTORY_UNSAFE",
            f"{CATALOGUE_DIRECTORY} must be a real directory, not a link or special file",
            CATALOGUE_DIRECTORY,
        )
    target = target_directory / CATALOGUE_NAME
    source_payload = source.read_bytes()
    with DirectoryAnchor(target_directory, writable=True) as anchor:
        if directory_created and anchor.identity != owned[CATALOGUE_DIRECTORY]:
            raise InitializationError(
                "TOOLING_DIRECTORY_CHANGED_CONCURRENTLY",
                "the workspace tooling directory identity changed before catalogue installation",
                CATALOGUE_DIRECTORY,
            )
        kind = anchor.entry_kind(CATALOGUE_NAME)
        if kind != "missing":
            if kind != "file":
                raise InitializationError("CATALOGUE_TARGET_UNSAFE", "catalogue target is not a regular file", f"{CATALOGUE_DIRECTORY}/{CATALOGUE_NAME}")
            if anchor.read(CATALOGUE_NAME, 32 * 1024 * 1024) != source_payload:
                raise InitializationError(
                    "CATALOGUE_VERSION_CONFLICT",
                    "an existing workspace catalogue differs; move it aside or use its matching Neon CLI",
                    f"{CATALOGUE_DIRECTORY}/{CATALOGUE_NAME}",
                )
            preserved.append(f"{CATALOGUE_DIRECTORY}/{CATALOGUE_NAME}")
            return target
        catalogue_identity = anchor.write_new(CATALOGUE_NAME, source_payload, 0o600)
        if not anchor.current():
            anchor.unlink_if_identity(CATALOGUE_NAME, catalogue_identity)
            raise InitializationError(
                "TOOLING_DIRECTORY_CHANGED_CONCURRENTLY",
                "the workspace tooling directory identity changed while the catalogue was installed",
                CATALOGUE_DIRECTORY,
            )
    owned[f"{CATALOGUE_DIRECTORY}/{CATALOGUE_NAME}"] = catalogue_identity
    created.append(f"{CATALOGUE_DIRECTORY}/{CATALOGUE_NAME}")
    return target


def _rollback_initialization(
    workspace: Path,
    workspace_anchor: DirectoryAnchor,
    created: list[str],
    owned: dict[str, tuple[int, int]],
    owned_directories: set[str],
    *,
    published_context: dict[str, tuple[int, int]],
    context_anchor: DirectoryAnchor | None = None,
) -> list[dict[str, str]]:
    diagnostics: list[dict[str, str]] = []
    context = workspace / ".neon"
    if ".neon" in owned:
        if context_anchor is None:
            try:
                if not workspace_anchor.unlink_if_identity(".neon", owned[".neon"], directory=True):
                    diagnostics.append({
                        "code": "INITIALIZATION_ROLLBACK_INCOMPLETE", "severity": "warning",
                        "message": "the claimed .neon directory was replaced and was preserved", "path": ".neon",
                    })
            except FileNotFoundError:
                pass
            except (OSError, ValueError):
                diagnostics.append({
                    "code": "INITIALIZATION_ROLLBACK_INCOMPLETE", "severity": "warning",
                    "message": "the claimed .neon directory could not be removed safely", "path": ".neon",
                })
        elif context_anchor.identity == owned[".neon"]:
            for relative in sorted(published_context, key=lambda value: (value.count("/"), value), reverse=True):
                try:
                    is_directory = relative in PROJECT_PACK_DIRECTORIES
                    if not context_anchor.unlink_if_identity(
                        relative, published_context[relative], directory=is_directory,
                        expected_directories=published_context,
                    ):
                        diagnostics.append({
                            "code": "INITIALIZATION_ROLLBACK_INCOMPLETE", "severity": "warning",
                            "message": "a published context path was replaced concurrently and was preserved",
                            "path": f".neon/{relative}",
                        })
                        continue
                except FileNotFoundError:
                    pass
                except (OSError, ValueError) as exc:
                    diagnostics.append({
                        "code": "INITIALIZATION_ROLLBACK_FAILED", "severity": "error",
                        "message": f"could not remove a published context path: {exc}", "path": f".neon/{relative}",
                    })
            try:
                if not workspace_anchor.unlink_if_identity(".neon", owned[".neon"], directory=True):
                    diagnostics.append({
                        "code": "INITIALIZATION_ROLLBACK_INCOMPLETE", "severity": "warning",
                        "message": "the .neon directory was replaced concurrently and was preserved", "path": ".neon",
                    })
            except FileNotFoundError:
                pass
            except (OSError, ValueError):
                diagnostics.append({
                    "code": "INITIALIZATION_ROLLBACK_INCOMPLETE", "severity": "warning",
                    "message": "generated context contains files not owned by this init and was preserved", "path": ".neon",
                })
        else:
            diagnostics.append({
                "code": "INITIALIZATION_ROLLBACK_INCOMPLETE", "severity": "warning",
                "message": "the .neon directory identity changed concurrently and was preserved", "path": ".neon",
            })
    for relative in sorted(set(created), key=lambda value: (value.count("/"), value), reverse=True):
        if relative == ".neon":
            continue
        try:
            is_directory = relative in owned_directories
            expected = owned.get(relative)
            if expected is None or not workspace_anchor.unlink_if_identity(
                relative, expected, directory=is_directory,
            ):
                diagnostics.append({
                    "code": "INITIALIZATION_ROLLBACK_INCOMPLETE", "severity": "warning",
                    "message": "a generated path was replaced concurrently and was preserved", "path": relative,
                })
                continue
        except FileNotFoundError:
            pass
        except (OSError, ValueError) as exc:
            severity = "warning" if relative in owned_directories else "error"
            diagnostics.append({
                "code": "INITIALIZATION_ROLLBACK_FAILED" if severity == "error" else "INITIALIZATION_ROLLBACK_INCOMPLETE",
                "severity": severity,
                "message": f"could not remove newly created path: {exc}", "path": relative,
            })
    return diagnostics


def _publish_context_exclusively(
    staging: Path,
    context_anchor: DirectoryAnchor,
    expected_identity: tuple[int, int],
) -> dict[str, tuple[int, int]]:
    published: dict[str, tuple[int, int]] = {}
    try:
        if context_anchor.identity != expected_identity or not context_anchor.current():
            raise InitializationError(
                "CONTEXT_CHANGED_CONCURRENTLY",
                "the .neon directory identity changed before context publication",
                ".neon",
            )
        for relative in sorted(PROJECT_PACK_DIRECTORIES):
            try:
                identity = context_anchor.mkdir_new(relative, 0o700)
            except (FileExistsError, ValueError) as exc:
                raise InitializationError(
                    "CONTEXT_CHANGED_CONCURRENTLY",
                    f"refusing to replace a concurrently created context path: {relative}",
                    f".neon/{relative}",
                ) from exc
            published[relative] = identity
        for relative in sorted(PROJECT_PACK_PATHS):
            source = staging.joinpath(*PurePosixPath(relative).parts)
            if source.is_symlink() or not source.is_file():
                raise InitializationError("GENERATED_CONTEXT_INVALID", "staged context file is missing or unsafe", relative)
            try:
                identity = context_anchor.write_new(
                    relative,
                    source.read_bytes(),
                    0o600,
                    expected_directories=published,
                )
            except (FileExistsError, OSError, ValueError) as exc:
                raise InitializationError(
                    "CONTEXT_CHANGED_CONCURRENTLY",
                    f"refusing to replace a concurrently created context path: {relative}",
                    f".neon/{relative}",
                ) from exc
            published[relative] = identity
        if not context_anchor.current() or not _published_context_current(context_anchor, published):
            raise InitializationError(
                "CONTEXT_CHANGED_CONCURRENTLY",
                "the .neon directory or one of its generated paths changed during context publication",
                ".neon",
            )
    except Exception:
        for relative in sorted(published, key=lambda value: (value.count("/"), value), reverse=True):
            try:
                is_directory = relative in PROJECT_PACK_DIRECTORIES
                context_anchor.unlink_if_identity(
                    relative,
                    published[relative],
                    directory=is_directory,
                    expected_directories=published,
                )
            except (OSError, ValueError):
                pass
        raise
    return published


def _published_context_current(
    context_anchor: DirectoryAnchor,
    published: dict[str, tuple[int, int]],
) -> bool:
    for relative, expected in published.items():
        try:
            current = context_anchor.entry_identity(
                relative, directory=relative in PROJECT_PACK_DIRECTORIES,
            )
        except (OSError, ValueError):
            return False
        if current != expected:
            return False
    return True


def _agent_guide() -> bytes:
    return (
        "# Neon agent workflow\n\n"
        "This is an MTA/Neon Lua project. Use the local Neon CLI as the source of "
        "truth for engine APIs and project validation.\n\n"
        "1. Read `.neon/agent-context.json` and use `.neon/api-index.json` for compact discovery.\n"
        "2. Search by intent with `neon api search \"keywords\" --json`; use `neon api get NAME --json` before relying on an exact signature or side.\n"
        "3. After meaningful Lua, meta.xml, dependency, or project changes, run `neon check --json`.\n"
        "4. Refresh context with `neon generate project --json`, then require `neon context verify --json` to pass.\n"
        "5. Run the gamemode's own tests when present. `neon harness` tests the CLI distribution; it is not a substitute for gamemode tests.\n\n"
        "These checks are explicit, not automatic. Do not claim they passed unless you ran them and inspected their JSON result.\n"
    ).encode("utf-8")


def initialize_workspace(
    workspace: Path,
    schema_store: SchemaStore,
    source_catalogue: Path,
    *,
    name: str | None = None,
    profile: str = "neon-pair",
    explicit_resources: list[str] | None = None,
) -> dict[str, Any]:
    try:
        workspace = workspace.resolve(strict=True)
    except OSError as exc:
        raise InitializationError("WORKSPACE_INVALID", str(exc)) from exc
    if not workspace.is_dir():
        raise InitializationError("WORKSPACE_INVALID", "workspace must be a directory")
    project_path = workspace / PROJECT_NAME
    if project_path.exists() or project_path.is_symlink():
        raise InitializationError("PROJECT_ALREADY_EXISTS", "refusing to overwrite an existing neon.project.json", PROJECT_NAME)
    context_path = workspace / ".neon"
    if context_path.exists() or context_path.is_symlink():
        raise InitializationError("CONTEXT_ALREADY_EXISTS", "refusing to overwrite an existing .neon context directory", ".neon")

    resources = discover_resources(workspace, explicit_resources)
    if not resources:
        raise InitializationError(
            "NO_RESOURCES_FOUND",
            "no resource containing meta.xml was found; place resources below the workspace or pass --resource",
        )
    source_document = load_json(source_catalogue)
    project = {
        "schemaVersion": "1.1.0",
        "name": _safe_name(name or workspace.name),
        "profile": profile,
        "engine": {"minimumVersion": "1.7.0", "maximumVersionExclusive": "1.8.0"},
        "catalogue": f"{CATALOGUE_DIRECTORY}/{CATALOGUE_NAME}",
        "resources": resources,
        "modules": [],
        "externalDependencies": [],
        "requiredApis": [],
        "unknownApis": "allow",
        "unknownComponents": "allow-opaque",
    }
    project_issues = schema_store.validate("neon-project", project)
    if project_issues:
        raise InitializationError("GENERATED_PROJECT_INVALID", "; ".join(f"{issue.pointer}: {issue.message}" for issue in project_issues))
    catalogue_issues = schema_store.validate("neon-api", source_document)
    if catalogue_issues:
        raise InitializationError("BUNDLED_CATALOGUE_INVALID", "; ".join(f"{issue.pointer}: {issue.message}" for issue in catalogue_issues))

    workspace_anchor = DirectoryAnchor(workspace, writable=True)
    if workspace_anchor.entry_kind(PROJECT_NAME) != "missing":
        workspace_anchor.close()
        raise InitializationError("PROJECT_ALREADY_EXISTS", "refusing to overwrite an existing neon.project.json", PROJECT_NAME)
    if workspace_anchor.entry_kind(".neon") != "missing":
        workspace_anchor.close()
        raise InitializationError("CONTEXT_ALREADY_EXISTS", "refusing to overwrite an existing .neon context directory", ".neon")

    created: list[str] = []
    owned: dict[str, tuple[int, int]] = {}
    owned_directories: set[str] = set()
    preserved: list[str] = []
    initialization_diagnostics: list[dict[str, str]] = []
    context_identity: tuple[int, int] | None = None
    context_anchor: DirectoryAnchor | None = None
    published_context: dict[str, tuple[int, int]] = {}
    try:
        def rollback() -> list[dict[str, str]]:
            return _rollback_initialization(
                workspace,
                workspace_anchor,
                created,
                owned,
                owned_directories,
                published_context=published_context,
                context_anchor=context_anchor,
            )

        try:
            context_identity = workspace_anchor.mkdir_new(".neon", 0o700)
        except FileExistsError as exc:
            raise InitializationError("CONTEXT_ALREADY_EXISTS", "refusing to use a concurrently created .neon directory", ".neon") from exc
        owned[".neon"] = context_identity
        owned_directories.add(".neon")
        created.append(".neon")
        try:
            context_anchor = DirectoryAnchor(context_path, writable=True)
            if context_anchor.identity != context_identity:
                raise InitializationError(
                    "CONTEXT_CHANGED_CONCURRENTLY",
                    "the .neon directory identity changed immediately after creation",
                    ".neon",
                )
        except Exception:
            rollback()
            raise
        try:
            _install_catalogue(
                workspace, workspace_anchor, source_catalogue,
                created, preserved, owned, owned_directories,
            )
            try:
                owned[PROJECT_NAME] = workspace_anchor.write_new(
                    PROJECT_NAME, canonical_json(project).encode("utf-8"), 0o600,
                )
            except FileExistsError as exc:
                raise InitializationError(
                    "PROJECT_ALREADY_EXISTS",
                    "refusing to overwrite a concurrently created neon.project.json",
                    PROJECT_NAME,
                ) from exc
            created.append(PROJECT_NAME)
            guide_kind = workspace_anchor.entry_kind(AGENT_GUIDE_NAME)
            if guide_kind == "file":
                preserved.append(AGENT_GUIDE_NAME)
                initialization_diagnostics.append({
                    "code": "AGENT_GUIDE_PRESERVED", "severity": "warning",
                    "message": "an existing NEON_AGENT.md was preserved; merge the documented Neon workflow manually if needed",
                    "path": AGENT_GUIDE_NAME,
                })
            elif guide_kind == "missing":
                try:
                    owned[AGENT_GUIDE_NAME] = workspace_anchor.write_new(
                        AGENT_GUIDE_NAME, _agent_guide(), 0o600,
                    )
                except FileExistsError as exc:
                    raise InitializationError(
                        "AGENT_GUIDE_CHANGED_CONCURRENTLY",
                        "refusing to overwrite a concurrently created agent guide",
                        AGENT_GUIDE_NAME,
                    ) from exc
                created.append(AGENT_GUIDE_NAME)
            else:
                raise InitializationError("AGENT_GUIDE_UNSAFE", "existing agent guide must be a regular file", AGENT_GUIDE_NAME)
        except Exception:
            rollback_diagnostics = rollback()
            rollback_errors = [item for item in rollback_diagnostics if item["severity"] == "error"]
            if rollback_errors:
                raise InitializationError(
                    "INITIALIZATION_ROLLBACK_FAILED",
                    "; ".join(item["message"] for item in rollback_errors),
                )
            raise

        check = check_project(project_path, schema_store)
        if check["status"] != "pass" or not workspace_anchor.current():
            diagnostics = [*check["diagnostics"], *initialization_diagnostics]
            if not workspace_anchor.current():
                diagnostics.insert(0, {
                    "code": "WORKSPACE_CHANGED_CONCURRENTLY", "severity": "error",
                    "message": "the workspace directory identity changed during project validation", "path": ".",
                })
            diagnostics.extend(rollback())
            return _init_result("fail", resources, [], preserved, diagnostics, context_ready=False)
        try:
            # Staging belongs to the CLI, not to the untrusted workspace
            # namespace. A concurrent workspace rename can therefore affect
            # reads only; every init mutation remains handle-anchored.
            system_temporary = Path(tempfile.gettempdir()).resolve(strict=True)
            with tempfile.TemporaryDirectory(prefix="neon-init-stage-", dir=system_temporary) as temporary:
                staging = Path(temporary)
                generate_project_context(project_path, schema_store, staging)
                staged_verification = verify_project_context(project_path, schema_store, staging)
                if staged_verification["status"] != "pass":
                    diagnostics = [*staged_verification["diagnostics"], *initialization_diagnostics]
                    diagnostics.extend(rollback())
                    return _init_result("fail", resources, [], preserved, diagnostics, context_ready=False)
                if not workspace_anchor.current():
                    raise InitializationError(
                        "WORKSPACE_CHANGED_CONCURRENTLY",
                        "the workspace directory identity changed during context staging",
                        ".",
                    )
                published_context = _publish_context_exclusively(staging, context_anchor, context_identity)
        except ContextGenerationError as exc:
            diagnostics = [*exc.result["diagnostics"], *initialization_diagnostics, *rollback()]
            return _init_result("fail", resources, [], preserved, diagnostics, context_ready=False)
        except (InitializationError, OSError, ValueError) as exc:
            code = exc.code if isinstance(exc, InitializationError) else "CONTEXT_GENERATION_FAILED"
            path = exc.path if isinstance(exc, InitializationError) else ".neon"
            diagnostic = {"code": code, "severity": "error", "message": str(exc), "path": path}
            diagnostics = [diagnostic, *initialization_diagnostics, *rollback()]
            return _init_result("fail", resources, [], preserved, diagnostics, context_ready=False)
        verified = verify_project_context(project_path, schema_store, workspace / ".neon")
        context_current = context_anchor.current() and _published_context_current(
            context_anchor, published_context,
        )
        if verified["status"] != "pass" or not context_current or not workspace_anchor.current():
            diagnostics = [*verified["diagnostics"], *initialization_diagnostics]
            if not workspace_anchor.current():
                diagnostics.insert(0, {
                    "code": "WORKSPACE_CHANGED_CONCURRENTLY", "severity": "error",
                    "message": "the workspace directory identity changed during final verification", "path": ".",
                })
            if not context_current:
                diagnostics.insert(0, {
                    "code": "CONTEXT_CHANGED_CONCURRENTLY", "severity": "error",
                    "message": "the .neon directory identity changed during final verification", "path": ".neon",
                })
            diagnostics.extend(rollback())
            return _init_result("fail", resources, [], preserved, diagnostics, context_ready=False)
        return _init_result("pass", resources, created, preserved, [*check["diagnostics"], *initialization_diagnostics], context_ready=True)
    finally:
        if context_anchor is not None:
            context_anchor.close()
        workspace_anchor.close()


def _init_result(
    status: str,
    resources: list[dict[str, str]],
    created: list[str],
    preserved: list[str],
    diagnostics: list[dict[str, Any]],
    *,
    context_ready: bool,
) -> dict[str, Any]:
    errors = sum(item.get("severity") == "error" for item in diagnostics)
    warnings = sum(item.get("severity") == "warning" for item in diagnostics)
    return {
        "schemaVersion": "1.0.0",
        "command": "init",
        "status": status,
        "summary": {"errors": errors, "warnings": warnings, "resources": len(resources), "contextReady": context_ready},
        "diagnostics": diagnostics,
        "project": PROJECT_NAME,
        "catalogue": f"{CATALOGUE_DIRECTORY}/{CATALOGUE_NAME}",
        "agentGuide": AGENT_GUIDE_NAME,
        "resources": resources,
        "created": sorted(set(created)),
        "preserved": sorted(set(preserved)),
    }


def initialization_failure(code: str, message: str, path: str = ".") -> dict[str, Any]:
    return {
        "schemaVersion": "1.0.0",
        "command": "init",
        "status": "fail",
        "summary": {"errors": 1, "warnings": 0, "resources": 0, "contextReady": False},
        "diagnostics": [{"code": code, "severity": "error", "message": message[:1024], "path": path}],
        "project": PROJECT_NAME,
        "catalogue": f"{CATALOGUE_DIRECTORY}/{CATALOGUE_NAME}",
        "agentGuide": AGENT_GUIDE_NAME,
        "resources": [],
        "created": [],
        "preserved": [],
    }
