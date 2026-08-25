from __future__ import annotations

import ast
import re
import html
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

from . import SCHEMA_VERSION
from .components import file_sha256, load_manifest, manifest_sha256, manifest_symbols
from .jsonio import JsonDocumentError, load_json
from .schema import SchemaStore, schema_major


MAX_XML_BYTES = 4 * 1024 * 1024
LUA_CALL_RE = re.compile(r"(?<![.:\w])([A-Za-z_]\w*)\s*\(")
LUA_FUNCTION_RE = re.compile(r"(?:\blocal\s+)?\bfunction\s+([A-Za-z_]\w*)\s*\(|\blocal\s+([A-Za-z_]\w*)\s*=\s*function\b")
LUA_EVENT_HANDLER_RE = re.compile(r'''(?<![.:\w])addEventHandler\s*\(\s*(["'])((?:\\.|(?!\1).)*)\1''')
LUA_ADD_EVENT_RE = re.compile(r'''(?<![.:\w])addEvent\s*\(\s*(["'])((?:\\.|(?!\1).)*)\1(?:\s*,\s*(true|false)\b)?''')
LUA_EXPORT_CALL_RE = re.compile(
    r'''\bexports\s*(?:\[\s*(["'])((?:\\.|(?!\1).)*)\1\s*\]|\.\s*([A-Za-z_][A-Za-z0-9_.-]*))\s*:\s*([A-Za-z_]\w*)\s*\('''
)
LUA_STATIC_MEMBER_CALL_RE = re.compile(r"\b([A-Z][A-Za-z0-9_]*)[.:]([A-Za-z_]\w*)\s*\(")
LUA_OOP_ASSIGN_RE = re.compile(r"\b(?:local\s+)?([A-Za-z_]\w*)\s*=\s*([A-Z][A-Za-z0-9_]*)\.create\s*\(")
LUA_INSTANCE_MEMBER_CALL_RE = re.compile(r"\b([A-Za-z_]\w*):([A-Za-z_]\w*)\s*\(")
LUA_GLOBAL_FUNCTION_RE = re.compile(r"(?m)^[ \t]*(?:function\s+([A-Za-z_]\w*)\s*\(|([A-Za-z_]\w*)\s*=\s*function\b)")
LUA_BUILTINS = {
    "assert", "collectgarbage", "dofile", "error", "getfenv", "getmetatable", "ipairs", "load", "loadfile", "loadstring",
    "module", "next", "pairs", "pcall", "print", "rawequal", "rawget", "rawset", "require", "select", "setfenv",
    "setmetatable", "tonumber", "tostring", "type", "unpack", "xpcall",
}
LUA_KEYWORDS = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local",
    "nil", "not", "or", "repeat", "return", "then", "true", "until", "while",
}
XML_NAME_RE = re.compile(r"^[A-Za-z_:][A-Za-z0-9_.:-]*$")
XML_ATTRIBUTE_RE = re.compile(r"\s*([A-Za-z_:][A-Za-z0-9_.:-]*)\s*=\s*(['\"])(.*?)\2", re.DOTALL)
XML_ENTITY_RE = re.compile(r"&(?:amp|quot|apos|lt|gt|#[0-9]+|#x[0-9A-Fa-f]+);")


@dataclass(frozen=True)
class Diagnostic:
    code: str
    severity: str
    message: str
    path: str = "neon.project.json"
    line: int | None = None
    side: str | None = None
    symbol: str | None = None

    def as_dict(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "code": self.code,
            "severity": self.severity,
            "message": self.message,
            "path": self.path,
        }
        if self.line is not None:
            result["line"] = self.line
        if self.side is not None:
            result["side"] = self.side
        if self.symbol is not None:
            result["symbol"] = self.symbol
        return result


@dataclass
class CheckState:
    diagnostics: list[Diagnostic] = field(default_factory=list)
    files: set[str] = field(default_factory=set)
    resources: set[str] = field(default_factory=set)
    api_requirements: int = 0

    def add(self, code: str, message: str, **kwargs: Any) -> None:
        self.diagnostics.append(Diagnostic(code, "error", message, **kwargs))

    def warn(self, code: str, message: str, **kwargs: Any) -> None:
        self.diagnostics.append(Diagnostic(code, "warning", message, **kwargs))


@dataclass
class MetaElement:
    tag: str
    attributes: dict[str, str]
    text: str = ""
    parent: str | None = None
    depth: int = 0

    def get(self, name: str, default: str | None = None) -> str | None:
        return self.attributes.get(name, default)


@dataclass(frozen=True)
class MetaDocument:
    tag: str
    elements: tuple[MetaElement, ...]

    def findall(self, name: str, parent: str | None = None, depth: int | None = None) -> list[MetaElement]:
        return [
            element for element in self.elements
            if element.tag == name and (parent is None or element.parent == parent) and (depth is None or element.depth == depth)
        ]


@dataclass(frozen=True)
class LoadedComponent:
    kind: str
    name: str
    root: Path | None
    display_root: str
    manifest_path: Path | None
    manifest_display: str | None
    manifest: dict[str, Any] | None


META_ASSET_TAGS = ("file", "map", "config", "html")


def _load_component(workspace: Path, entry: dict[str, Any], kind: str, schema_store: SchemaStore, state: CheckState) -> LoadedComponent:
    name = entry["name"]
    display_root = entry["path"].rstrip("/")
    root = _resolve_relative(workspace, entry["path"])
    if root is None:
        state.add("PATH_OUTSIDE_WORKSPACE", f"{kind} path is not workspace-relative: {entry['path']}", path=entry["path"])
        return LoadedComponent(kind, name, None, display_root, None, None, None)
    if not root.is_dir():
        state.add("MISSING_RESOURCE" if kind == "resource" else "MISSING_MODULE", f"{kind} directory does not exist: {entry['path']}", path=entry["path"])
        return LoadedComponent(kind, name, root, display_root, None, None, None)
    manifest_name = entry.get("manifest")
    if not manifest_name:
        return LoadedComponent(kind, name, root, display_root, None, None, None)
    manifest_path = _resolve_relative(root, manifest_name)
    manifest_display = f"{display_root}/{manifest_name}"
    if manifest_path is None:
        state.add("PATH_OUTSIDE_WORKSPACE", f"manifest path escapes {kind}: {manifest_name}", path=manifest_display)
        return LoadedComponent(kind, name, root, display_root, None, manifest_display, None)
    if not manifest_path.is_file():
        state.add("COMPONENT_MANIFEST_MISSING", f"declared component manifest does not exist: {manifest_name}", path=manifest_display)
        return LoadedComponent(kind, name, root, display_root, manifest_path, manifest_display, None)
    manifest, issues = load_manifest(manifest_path, schema_store)
    for issue in issues:
        state.add(issue.code, issue.message, path=manifest_display)
    if manifest is not None:
        if manifest["kind"] != kind:
            state.add("COMPONENT_KIND_MISMATCH", f"manifest kind {manifest['kind']} does not match project {kind}", path=manifest_display)
            manifest = None
        elif manifest["name"] != name:
            state.add("COMPONENT_NAME_MISMATCH", f"manifest name {manifest['name']} does not match project name {name}", path=manifest_display)
            manifest = None
    return LoadedComponent(kind, name, root, display_root, manifest_path, manifest_display, manifest)


def _component_sides(side: str) -> tuple[str, ...]:
    return ("client", "server") if side == "shared" else (side,)


def _component_policy_diagnostic(state: CheckState, strict: bool, code: str, message: str, **kwargs: Any) -> None:
    (state.add if strict else state.warn)(code, message, **kwargs)


def parse_semver(version: str) -> tuple[int, int, int] | None:
    match = re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:[-+].*)?", version)
    return tuple(int(match.group(index)) for index in range(1, 4)) if match else None


def _within(root: Path, candidate: Path) -> bool:
    try:
        candidate.relative_to(root)
        return True
    except ValueError:
        return False


def _resolve_relative(root: Path, value: str) -> Path | None:
    pure = PurePosixPath(value.replace("\\", "/"))
    if pure.is_absolute() or ".." in pure.parts or "." in pure.parts:
        return None
    candidate = (root / Path(*pure.parts)).resolve()
    return candidate if _within(root.resolve(), candidate) else None


def strip_lua_noncode(source: str) -> str:
    result = list(source)
    index = 0
    state = "code"
    quote = ""
    long_close = ""
    while index < len(source):
        current = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if current in ('"', "'"):
                state, quote = "string", current
                result[index] = " "
            elif current == "-" and following == "-":
                equals = 0
                cursor = index + 2
                if cursor < len(source) and source[cursor] == "[":
                    cursor += 1
                    while cursor < len(source) and source[cursor] == "=":
                        equals += 1
                        cursor += 1
                    if cursor < len(source) and source[cursor] == "[":
                        long_close = "]" + ("=" * equals) + "]"
                        state = "long"
                    else:
                        state = "line"
                else:
                    state = "line"
                result[index] = result[index + 1] = " "
                index += 1
            elif current == "[":
                cursor = index + 1
                equals = 0
                while cursor < len(source) and source[cursor] == "=":
                    equals += 1
                    cursor += 1
                if cursor < len(source) and source[cursor] == "[":
                    long_close = "]" + ("=" * equals) + "]"
                    state = "long"
                    result[index] = " "

        elif state == "string":
            if current == "\\":
                result[index] = " "
                if index + 1 < len(source) and source[index + 1] != "\n":
                    result[index + 1] = " "
                    index += 1
            elif current == quote:
                result[index] = " "
                state = "code"
            elif current != "\n":
                result[index] = " "
        elif state == "line":
            if current == "\n":
                state = "code"
            else:
                result[index] = " "
        elif state == "long":
            if source.startswith(long_close, index):
                for offset in range(len(long_close)):
                    result[index + offset] = " "
                index += len(long_close) - 1
                state = "code"
            elif current != "\n":
                result[index] = " "
        index += 1
    return "".join(result)


def strip_lua_comments(source: str) -> str:
    """Remove comments while retaining quoted literals and source line offsets."""
    result = list(source)
    index = 0
    quote = ""
    state = "code"
    long_close = ""
    while index < len(source):
        current = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if current in ('"', "'"):
                state, quote = "string", current
            elif current == "-" and following == "-":
                cursor = index + 2
                equals = 0
                if cursor < len(source) and source[cursor] == "[":
                    cursor += 1
                    while cursor < len(source) and source[cursor] == "=":
                        equals += 1
                        cursor += 1
                    if cursor < len(source) and source[cursor] == "[":
                        long_close = "]" + ("=" * equals) + "]"
                        state = "long-comment"
                    else:
                        state = "line-comment"
                else:
                    state = "line-comment"
                result[index] = result[index + 1] = " "
                index += 1
        elif state == "string":
            if current == "\\":
                index += 1
            elif current == quote:
                state = "code"
        elif state == "line-comment":
            if current == "\n":
                state = "code"
            else:
                result[index] = " "
        elif state == "long-comment":
            if source.startswith(long_close, index):
                for offset in range(len(long_close)):
                    result[index + offset] = " "
                index += len(long_close) - 1
                state = "code"
            elif current != "\n":
                result[index] = " "
        index += 1
    return "".join(result)


def _available_by_side(catalogue: dict, profile: str) -> dict[str, set[str]]:
    result = {"client": set(), "server": set()}
    for symbol in catalogue.get("symbols", []):
        if symbol.get("kind") != "function" or symbol.get("state") in ("unavailable", "documented-only", "conflict"):
            continue
        sides = symbol.get("inheritedSides", []) if profile == "mta-upstream" else symbol.get("sides", [])
        for side in sides:
            if side in result:
                result[side].add(symbol["name"])
    if profile == "neon-server":
        result["client"].clear()
    elif profile == "neon-client":
        result["server"].clear()
    return result


def _available_events_by_side(catalogue: dict, profile: str) -> dict[str, set[str]]:
    result = {"client": set(), "server": set()}
    for symbol in catalogue.get("symbols", []):
        if symbol.get("kind") != "event" or symbol.get("state") in ("unavailable", "documented-only", "conflict"):
            continue
        sides = symbol.get("inheritedSides", []) if profile == "mta-upstream" else symbol.get("sides", [])
        for side in sides:
            if side in result:
                result[side].add(symbol["name"])
    if profile == "neon-server":
        result["client"].clear()
    elif profile == "neon-client":
        result["server"].clear()
    return result


def _conflicts_by_side(catalogue: dict, profile: str, kind: str) -> dict[str, set[str]]:
    result = {"client": set(), "server": set()}
    for symbol in catalogue.get("symbols", []):
        if symbol.get("kind") != kind or symbol.get("state") != "conflict":
            continue
        if profile in symbol.get("profiles", []):
            # A conflict means no side has a trustworthy active contract. It
            # must not become an allowed "unknown" merely because code calls
            # it from the opposite side of the disputed registration.
            result["client"].add(symbol["name"])
            result["server"].add(symbol["name"])
    if profile == "neon-server":
        result["client"].clear()
    elif profile == "neon-client":
        result["server"].clear()
    return result


def _line(source: str, offset: int) -> int:
    return source.count("\n", 0, offset) + 1


def _effective_oop_sides(symbol: dict[str, Any], profile: str) -> set[str]:
    sides = set(symbol.get("inheritedSides", [])) if profile == "mta-upstream" else set(symbol.get("sides", []))
    if profile == "neon-server":
        sides.discard("client")
    elif profile == "neon-client":
        sides.discard("server")
    return sides


def _find_oop_member(classes: dict[str, dict[str, Any]], class_symbol: dict[str, Any], member_name: str, profile: str) -> dict[str, Any] | None:
    parents_field = "inheritedParents" if profile == "mta-upstream" else "parents"
    pending = [class_symbol]
    visited: set[str] = set()
    while pending:
        current = pending.pop(0)
        if current["name"] in visited:
            continue
        visited.add(current["name"])
        member = next((item for item in current.get("methods", []) if item["name"] == member_name), None)
        if member is not None:
            return member
        pending.extend(classes[parent] for parent in current.get(parents_field, []) if parent in classes)
    return None


def _validate_oop_call(
    class_name: str,
    member_name: str,
    offset: int,
    code: str,
    display_path: str,
    side: str,
    profile: str,
    required_sides: tuple[str, ...],
    classes: dict[str, dict[str, Any]],
    functions: dict[str, dict[str, Any]],
    state: CheckState,
) -> None:
    class_symbol = classes.get(class_name)
    if class_symbol is None:
        return
    display_symbol = f"{class_name}.{member_name}"
    if profile not in class_symbol.get("profiles", []):
        state.add(
            "API_UNAVAILABLE", f"{display_symbol} is unavailable in profile {profile}", path=display_path,
            line=_line(code, offset), side=side, symbol=display_symbol,
        )
        return
    class_sides = _effective_oop_sides(class_symbol, profile)
    missing_class_sides = [candidate for candidate in required_sides if candidate not in class_sides]
    if missing_class_sides:
        state.add(
            "API_WRONG_SIDE" if class_sides else "API_UNAVAILABLE",
            f"{display_symbol} is unavailable on {', '.join(missing_class_sides)}", path=display_path,
            line=_line(code, offset), side=side, symbol=display_symbol,
        )
        return
    member = _find_oop_member(classes, class_symbol, member_name, profile)
    if member is None:
        state.add(
            "API_UNKNOWN", f"{display_symbol} is not a registered OOP method", path=display_path,
            line=_line(code, offset), side=side, symbol=display_symbol,
        )
        return
    member_sides = _effective_oop_sides(member, profile)
    missing_member_sides = [candidate for candidate in required_sides if candidate not in member_sides]
    if missing_member_sides:
        state.add(
            "API_WRONG_SIDE" if member_sides else "API_UNAVAILABLE",
            f"{display_symbol} is unavailable on {', '.join(missing_member_sides)}", path=display_path,
            line=_line(code, offset), side=side, symbol=display_symbol,
        )
        return
    binding_field = "inheritedBindings" if profile == "mta-upstream" else "bindings"
    bound_names = {
        binding.get("globalFunction") for binding in member.get(binding_field, [])
        if binding.get("side") in required_sides and binding.get("globalFunction")
    }
    if any(functions.get(name, {}).get("state") == "conflict" for name in bound_names):
        state.add(
            "API_CONFLICT", f"{display_symbol} is backed by a conflicting global contract", path=display_path,
            line=_line(code, offset), side=side, symbol=display_symbol,
        )


def _scan_lua(
    path: Path,
    display_path: str,
    side: str,
    catalogue: dict,
    profile: str,
    strict_unknown: bool,
    state: CheckState,
    project_functions: dict[str, set[str]] | None = None,
    project_events: dict[str, set[str]] | None = None,
    resource_exports: dict[str, dict[str, set[str]]] | None = None,
    strict_components: bool = False,
) -> bool:
    try:
        source = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        state.add("FILE_READ_ERROR", str(exc), path=display_path, side=side)
        return False
    code = strip_lua_noncode(source)
    comment_free = strip_lua_comments(source)
    declared = {name for match in LUA_FUNCTION_RE.finditer(code) for name in match.groups() if name}
    available = _available_by_side(catalogue, profile)
    for candidate_side, names in (project_functions or {}).items():
        if candidate_side in available:
            available[candidate_side].update(names)
    all_known = available["client"] | available["server"]
    catalogued = {symbol["name"] for symbol in catalogue.get("symbols", []) if symbol.get("kind") == "function"}
    conflicts = _conflicts_by_side(catalogue, profile, "function")
    required_sides = ("client", "server") if side == "shared" else (side,)
    for match in LUA_CALL_RE.finditer(code):
        name = match.group(1)
        if name in declared or name in LUA_BUILTINS or name in LUA_KEYWORDS:
            continue
        conflicting_sides = [required for required in required_sides if name in conflicts[required]]
        if conflicting_sides:
            state.add(
                "API_CONFLICT",
                f"{name} has an unresolved contract conflict on {', '.join(conflicting_sides)}",
                path=display_path,
                line=_line(code, match.start(1)),
                side=side,
                symbol=name,
            )
            continue
        missing_sides = [required for required in required_sides if name not in available[required]]
        if name in all_known and missing_sides:
            state.add(
                "API_WRONG_SIDE",
                f"{name} is unavailable on {', '.join(missing_sides)}",
                path=display_path,
                line=_line(code, match.start(1)),
                side=side,
                symbol=name,
            )
        elif name in catalogued and missing_sides:
            state.add(
                "API_UNAVAILABLE",
                f"{name} has no active registration for profile {profile} on {', '.join(missing_sides)}",
                path=display_path,
                line=_line(code, match.start(1)),
                side=side,
                symbol=name,
            )
        elif strict_unknown and name not in all_known:
            state.add(
                "API_UNKNOWN",
                f"{name} is not present in the selected API catalogue",
                path=display_path,
                line=_line(code, match.start(1)),
                side=side,
                symbol=name,
            )
    available_events = _available_events_by_side(catalogue, profile)
    for candidate_side, names in (project_events or {}).items():
        if candidate_side in available_events:
            available_events[candidate_side].update(names)
    all_known_events = available_events["client"] | available_events["server"]
    catalogued_events = {symbol["name"] for symbol in catalogue.get("symbols", []) if symbol.get("kind") == "event"}
    event_conflicts = _conflicts_by_side(catalogue, profile, "event")
    for match in LUA_EVENT_HANDLER_RE.finditer(comment_free):
        try:
            name = ast.literal_eval(match.group(1) + match.group(2) + match.group(1))
        except (SyntaxError, ValueError):
            continue
        if not isinstance(name, str):
            continue
        conflicting_sides = [required for required in required_sides if name in event_conflicts[required]]
        if conflicting_sides:
            state.add(
                "EVENT_CONFLICT",
                f"{name} has an unresolved event contract conflict on {', '.join(conflicting_sides)}",
                path=display_path,
                line=_line(comment_free, match.start(2)),
                side=side,
                symbol=name,
            )
            continue
        missing_sides = [required for required in required_sides if name not in available_events[required]]
        if name in all_known_events and missing_sides:
            state.add(
                "EVENT_WRONG_SIDE",
                f"{name} is unavailable on {', '.join(missing_sides)}",
                path=display_path,
                line=_line(comment_free, match.start(2)),
                side=side,
                symbol=name,
            )
        elif name in catalogued_events and missing_sides:
            state.add(
                "EVENT_UNAVAILABLE",
                f"{name} has no active registration for profile {profile} on {', '.join(missing_sides)}",
                path=display_path,
                line=_line(comment_free, match.start(2)),
                side=side,
                symbol=name,
            )
    classes = {symbol["name"]: symbol for symbol in catalogue.get("symbols", []) if symbol.get("kind") == "class"}
    functions = {symbol["name"]: symbol for symbol in catalogue.get("symbols", []) if symbol.get("kind") == "function"}
    oop_used = False
    for match in LUA_STATIC_MEMBER_CALL_RE.finditer(code):
        oop_used = oop_used or match.group(1) in classes
        _validate_oop_call(
            match.group(1), match.group(2), match.start(1), code, display_path, side, profile,
            required_sides, classes, functions, state,
        )
    inferred_instances = {match.group(1): match.group(2) for match in LUA_OOP_ASSIGN_RE.finditer(code)}
    for match in LUA_INSTANCE_MEMBER_CALL_RE.finditer(code):
        class_name = inferred_instances.get(match.group(1))
        if class_name is not None:
            oop_used = oop_used or class_name in classes
            _validate_oop_call(
                class_name, match.group(2), match.start(1), code, display_path, side, profile,
                required_sides, classes, functions, state,
            )
    for match in LUA_EXPORT_CALL_RE.finditer(comment_free):
        literal = match.group(1) + match.group(2) + match.group(1) if match.group(2) is not None else None
        if literal is not None:
            try:
                dependency = ast.literal_eval(literal)
            except (SyntaxError, ValueError):
                continue
        else:
            dependency = match.group(3)
        export_name = match.group(4)
        if not isinstance(dependency, str):
            continue
        contract = (resource_exports or {}).get(dependency)
        if contract is None:
            _component_policy_diagnostic(
                state,
                strict_components,
                "RESOURCE_EXPORT_PROVIDER_UNKNOWN",
                f"resource export provider {dependency} is not declared by this project",
                path=display_path,
                line=_line(comment_free, match.start(4)),
                side=side,
                symbol=export_name,
            )
            continue
        missing_sides = [required for required in required_sides if export_name not in contract.get(required, set())]
        all_exports = contract.get("client", set()) | contract.get("server", set())
        if export_name not in all_exports:
            _component_policy_diagnostic(
                state,
                strict_components,
                "RESOURCE_EXPORT_UNKNOWN",
                f"resource {dependency} does not declare export {export_name}",
                path=display_path,
                line=_line(comment_free, match.start(4)),
                side=side,
                symbol=export_name,
            )
        elif missing_sides:
            state.add(
                "RESOURCE_EXPORT_WRONG_SIDE",
                f"resource export {dependency}.{export_name} is unavailable on {', '.join(missing_sides)}",
                path=display_path,
                line=_line(comment_free, match.start(4)),
                side=side,
                symbol=export_name,
            )
    return oop_used


def _validate_requirements(requirements: Iterable[dict], catalogue: dict, profile: str, state: CheckState, path: str) -> None:
    available = _available_by_side(catalogue, profile)
    conflicts = _conflicts_by_side(catalogue, profile, "function")
    catalogued = {symbol["name"] for symbol in catalogue.get("symbols", []) if symbol.get("kind") == "function"}
    for requirement in requirements:
        state.api_requirements += 1
        name = requirement["name"]
        side = requirement["side"]
        required_sides = ("client", "server") if side == "shared" else (side,)
        conflicting_sides = [candidate for candidate in required_sides if name in conflicts[candidate]]
        if conflicting_sides:
            state.add(
                "API_CONFLICT",
                f"required API {name} has an unresolved contract conflict on {', '.join(conflicting_sides)}",
                path=path,
                side=side,
                symbol=name,
            )
            continue
        present = [candidate for candidate in required_sides if name in available[candidate]]
        if not present:
            known = name in (available["client"] | available["server"])
            code = "API_WRONG_SIDE" if known else "API_UNAVAILABLE" if name in catalogued else "API_NOT_FOUND"
            state.add(
                code,
                f"required API {name} is unavailable on {side}" if code != "API_NOT_FOUND" else f"required API {name} does not exist",
                path=path,
                side=side,
                symbol=name,
            )
        elif side == "shared" and len(present) != 2:
            state.add(
                "API_WRONG_SIDE",
                f"required shared API {name} is not available on both client and server",
                path=path,
                side=side,
                symbol=name,
            )


def _read_xml_token(source: str, opening: int) -> tuple[str, int]:
    if source.startswith("<!--", opening):
        closing = source.find("-->", opening + 4)
        if closing < 0:
            raise ValueError("unterminated XML comment")
        return source[opening : closing + 3], closing + 3
    if source.startswith("<![CDATA[", opening):
        closing = source.find("]]>", opening + 9)
        if closing < 0:
            raise ValueError("unterminated CDATA section")
        return source[opening : closing + 3], closing + 3
    if source.startswith("<?", opening):
        closing = source.find("?>", opening + 2)
        if closing < 0:
            raise ValueError("unterminated processing instruction")
        return source[opening : closing + 2], closing + 2
    quote = ""
    index = opening + 1
    while index < len(source):
        current = source[index]
        if quote:
            if current == quote:
                quote = ""
        elif current in ("'", '"'):
            quote = current
        elif current == ">":
            return source[opening : index + 1], index + 1
        index += 1
    raise ValueError("unterminated XML tag")


def _decode_xml_attribute(value: str) -> str:
    remainder = XML_ENTITY_RE.sub("", value)
    if "&" in remainder:
        raise ValueError("unknown or unterminated XML entity")
    return html.unescape(value)


def _parse_xml_attributes(source: str) -> dict[str, str]:
    attributes: dict[str, str] = {}
    offset = 0
    while offset < len(source):
        if source[offset:].strip() == "":
            break
        match = XML_ATTRIBUTE_RE.match(source, offset)
        if not match:
            raise ValueError("malformed XML attribute")
        name = match.group(1)
        if name in attributes:
            raise ValueError(f"duplicate XML attribute {name}")
        attributes[name] = _decode_xml_attribute(match.group(3))
        offset = match.end()
    return attributes


def _parse_meta_document(payload: bytes) -> MetaDocument:
    try:
        source = payload.decode("utf-8-sig")
    except UnicodeError as exc:
        raise ValueError("meta.xml must be UTF-8") from exc
    stack: list[tuple[str, int]] = []
    root: str | None = None
    root_closed = False
    elements: list[MetaElement] = []
    cursor = 0
    while cursor < len(source):
        opening = source.find("<", cursor)
        if opening < 0:
            if not stack and source[cursor:].strip():
                raise ValueError("text is not allowed outside the root element")
            if stack:
                elements[stack[-1][1]].text += source[cursor:]
            break
        if not stack and source[cursor:opening].strip():
            raise ValueError("text is not allowed outside the root element")
        if stack:
            elements[stack[-1][1]].text += source[cursor:opening]
        token, cursor = _read_xml_token(source, opening)
        if token.startswith("<!--") or token.startswith("<?"):
            continue
        if token.startswith("<![CDATA["):
            if not stack:
                raise ValueError("CDATA is not allowed outside the root element")
            continue
        if token.startswith("<!"):
            raise ValueError("XML declarations other than comments and CDATA are forbidden")

        content = token[1:-1].strip()
        closing = content.startswith("/")
        self_closing = content.endswith("/") and not closing
        if closing:
            name = content[1:].strip()
            if not XML_NAME_RE.fullmatch(name) or not stack or stack[-1][0] != name:
                raise ValueError(f"mismatched closing tag {name}")
            stack.pop()
            if not stack:
                root_closed = True
            continue
        if self_closing:
            content = content[:-1].rstrip()
        parts = content.split(None, 1)
        name = parts[0] if parts else ""
        if not XML_NAME_RE.fullmatch(name):
            raise ValueError(f"invalid XML tag name {name!r}")
        if root_closed:
            raise ValueError("multiple XML root elements")
        attributes = _parse_xml_attributes(parts[1] if len(parts) == 2 else "")
        if root is None:
            root = name
        elements.append(MetaElement(name, attributes, parent=stack[-1][0] if stack else None, depth=len(stack)))
        if not self_closing:
            stack.append((name, len(elements) - 1))
        elif not stack:
            root_closed = True
    if root is None:
        raise ValueError("meta.xml has no root element")
    if stack:
        raise ValueError(f"unclosed XML tag {stack[-1][0]}")
    return MetaDocument(root, tuple(elements))


def _parse_meta(meta_path: Path, display_path: str, state: CheckState) -> MetaDocument | None:
    try:
        payload = meta_path.read_bytes()
    except OSError as exc:
        state.add("MISSING_META", str(exc), path=display_path)
        return None
    if len(payload) > MAX_XML_BYTES:
        state.add("META_TOO_LARGE", f"meta.xml exceeds {MAX_XML_BYTES} bytes", path=display_path)
        return None
    upper = payload.upper()
    if b"<!DOCTYPE" in upper or b"<!ENTITY" in upper:
        state.add("UNSAFE_XML", "DTD and entity declarations are forbidden", path=display_path)
        return None
    try:
        return _parse_meta_document(payload)
    except ValueError as exc:
        state.add("INVALID_META", str(exc), path=display_path)
        return None


def _check_engine_version(project: dict, catalogue: dict, state: CheckState) -> None:
    actual_text = catalogue.get("engine", {}).get("version", "")
    actual = parse_semver(actual_text)
    if actual is None:
        state.add("CATALOGUE_VERSION_INVALID", f"catalogue engine version {actual_text!r} is invalid")
        return
    requirement = project["engine"]
    minimum = parse_semver(requirement["minimumVersion"])
    maximum = parse_semver(requirement["maximumVersionExclusive"])
    if minimum is None or maximum is None or minimum >= maximum:
        state.add("ENGINE_VERSION_RANGE_INVALID", "minimumVersion must be lower than maximumVersionExclusive")
    elif not (minimum <= actual < maximum):
        state.add(
            "ENGINE_VERSION_INCOMPATIBLE",
            f"catalogue engine {actual_text} is outside [{requirement['minimumVersion']}, {requirement['maximumVersionExclusive']})",
        )


def _decode_lua_string(quote: str, value: str) -> str | None:
    try:
        decoded = ast.literal_eval(quote + value + quote)
    except (SyntaxError, ValueError):
        return None
    return decoded if isinstance(decoded, str) else None


def _lua_contract_facts(path: Path, side: str) -> tuple[set[str], dict[tuple[str, str], bool]]:
    try:
        source = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        return set(), {}
    code = strip_lua_noncode(source)
    comment_free = strip_lua_comments(source)
    functions = {name for match in LUA_GLOBAL_FUNCTION_RE.finditer(code) for name in match.groups() if name}
    events: dict[tuple[str, str], bool] = {}
    for match in LUA_ADD_EVENT_RE.finditer(comment_free):
        name = _decode_lua_string(match.group(1), match.group(2))
        if name is not None:
            events[(name, side)] = match.group(3) == "true"
    return functions, events


def _validate_resource_contract(
    component: LoadedComponent,
    root: MetaDocument,
    meta_path: str,
    global_functions: dict[str, set[str]],
    defined_events: dict[tuple[str, str], bool],
    project_dependencies: set[str],
    strict_components: bool,
    state: CheckState,
) -> None:
    meta_exports: dict[tuple[str, str], MetaElement] = {}
    for item in root.findall("export", "meta", 1):
        name = item.get("function")
        side = item.get("type", "server")
        if not name or side not in ("client", "server"):
            state.add("INVALID_META", "export requires a function and a valid type", path=meta_path)
            continue
        key = (name, side)
        if key in meta_exports:
            state.add("DUPLICATE_META_EXPORT", f"duplicate meta export {name} on {side}", path=meta_path, side=side, symbol=name)
        meta_exports[key] = item

    manifest = component.manifest
    if manifest is None:
        for name, side in sorted(meta_exports):
            _component_policy_diagnostic(
                state, strict_components, "RESOURCE_EXPORT_OPAQUE",
                f"resource {component.name} export {name} has no approved component manifest",
                path=meta_path, side=side, symbol=name,
            )
        for name, side in sorted(defined_events):
            _component_policy_diagnostic(
                state, strict_components, "RESOURCE_EVENT_OPAQUE",
                f"resource {component.name} event {name} has no approved component manifest",
                path=meta_path, side=side, symbol=name,
            )
        return

    manifest_path = component.manifest_display or meta_path
    declared_exports = {(item["name"], item["side"]): item for item in manifest["exports"]}
    for (name, side), export in sorted(declared_exports.items()):
        expected_meta_sides = _component_sides(side)
        matching_meta = [(name, candidate) for candidate in expected_meta_sides if (name, candidate) in meta_exports]
        if not matching_meta:
            if any(candidate[0] == name for candidate in meta_exports):
                state.add("RESOURCE_EXPORT_SIDE_MISMATCH", f"meta.xml side disagrees with manifest export {name} on {side}", path=meta_path, side=side, symbol=name)
            else:
                state.add("RESOURCE_EXPORT_NOT_IN_META", f"manifest export {name} on {side} is absent from meta.xml", path=manifest_path, side=side, symbol=name)
        elif len(matching_meta) != len(expected_meta_sides):
            missing_meta_sides = sorted(set(expected_meta_sides) - {candidate[1] for candidate in matching_meta})
            state.add("RESOURCE_EXPORT_SIDE_MISMATCH", f"meta.xml is missing manifest export {name} on {', '.join(missing_meta_sides)}", path=meta_path, side=side, symbol=name)
        expected_sides = _component_sides(side)
        missing_implementations = [candidate for candidate in expected_sides if name not in global_functions[candidate]]
        if missing_implementations:
            state.add("RESOURCE_EXPORT_IMPLEMENTATION_MISSING", f"manifest export {name} has no global Lua implementation on {', '.join(missing_implementations)}", path=manifest_path, side=side, symbol=name)
        for candidate in matching_meta:
            meta = meta_exports[candidate]
            if (meta.get("http", "false") == "true") != export["http"] or (meta.get("restricted", "false") == "true") != export["restricted"]:
                state.add("RESOURCE_EXPORT_SECURITY_MISMATCH", f"meta.xml security flags disagree with manifest export {name}", path=meta_path, side=candidate[1], symbol=name)
    for name, side in sorted(set(meta_exports) - set(declared_exports)):
        if not any(candidate["name"] == name and side in _component_sides(candidate["side"]) for candidate in manifest["exports"]):
            _component_policy_diagnostic(state, strict_components, "RESOURCE_EXPORT_OPAQUE", f"meta export {name} on {side} is absent from the component manifest", path=meta_path, side=side, symbol=name)

    declared_definitions: dict[tuple[str, str], dict[str, Any]] = {}
    for event in manifest["events"]:
        if "defines" in event["directions"]:
            for candidate_side in _component_sides(event["side"]):
                declared_definitions[(event["name"], candidate_side)] = event
    for (name, side), event in sorted(declared_definitions.items()):
        actual = defined_events.get((name, side))
        if actual is None:
            state.add("RESOURCE_EVENT_DEFINITION_MISSING", f"manifest event {name} is not defined on {side}", path=manifest_path, side=side, symbol=name)
        elif actual != event["allowRemoteTrigger"]:
            state.add("RESOURCE_EVENT_REMOTE_MISMATCH", f"addEvent remote flag disagrees with manifest event {name}", path=manifest_path, side=side, symbol=name)
    for name, side in sorted(set(defined_events) - set(declared_definitions)):
        _component_policy_diagnostic(state, strict_components, "RESOURCE_EVENT_OPAQUE", f"defined event {name} on {side} is absent from the component manifest", path=meta_path, side=side, symbol=name)

    meta_rights = {item.get("name"): item.get("access", "false") == "true" for item in root.findall("right", "aclrequest", 2) if item.get("name")}
    manifest_rights = {item["right"]: item for item in manifest["acl"]}
    for right, contract in sorted(manifest_rights.items()):
        if contract["required"] and not meta_rights.get(right, False):
            state.add("RESOURCE_ACL_MISSING", f"required ACL right {right} is not requested with access=true", path=meta_path)
    for right in sorted(set(meta_rights) - set(manifest_rights)):
        _component_policy_diagnostic(state, strict_components, "RESOURCE_ACL_OPAQUE", f"ACL request {right} is absent from the component manifest", path=meta_path)

    if manifest.get("oopRequired") and not any(item.tag == "oop" and item.parent == "meta" and item.depth == 1 and item.text.strip().casefold() == "true" for item in root.elements):
        state.add("RESOURCE_OOP_MISSING", "component manifest requires OOP but meta.xml has no <oop>true</oop>", path=meta_path)
    for dependency in manifest["dependencies"]:
        if dependency["kind"] == "resource" and not dependency["optional"] and dependency["name"] not in project_dependencies:
            state.add("COMPONENT_DEPENDENCY_UNDECLARED", f"required resource dependency {dependency['name']} is absent from project/meta declarations", path=manifest_path)


def check_project(project_path: Path, schema_store: SchemaStore, catalogue_override: Path | None = None) -> dict[str, Any]:
    state = CheckState()
    workspace = project_path.parent.resolve()
    try:
        project = load_json(project_path)
    except JsonDocumentError as exc:
        state.add("PROJECT_JSON_INVALID", str(exc))
        return _result(state)
    issues = schema_store.validate("neon-project", project)
    if issues:
        for issue in issues:
            state.add("PROJECT_SCHEMA_INVALID", f"{issue.pointer}: {issue.message}")
        return _result(state)
    if schema_major(project["schemaVersion"]) != 1:
        state.add("SCHEMA_VERSION_UNSUPPORTED", f"unsupported project schema {project['schemaVersion']}")
        return _result(state)

    catalogue_path = catalogue_override
    if catalogue_path is None:
        catalogue_path = _resolve_relative(workspace, project["catalogue"])
    if catalogue_path is None or not catalogue_path.is_file():
        state.add("CATALOGUE_MISSING", "API catalogue does not exist")
        return _result(state)
    try:
        catalogue = load_json(catalogue_path)
    except JsonDocumentError as exc:
        state.add("CATALOGUE_JSON_INVALID", str(exc))
        return _result(state)
    catalogue_issues = schema_store.validate("neon-api", catalogue)
    if catalogue_issues:
        for issue in catalogue_issues:
            state.add("CATALOGUE_SCHEMA_INVALID", f"{issue.pointer}: {issue.message}")
        return _result(state)
    if schema_major(catalogue["schemaVersion"]) != 1:
        state.add("SCHEMA_VERSION_UNSUPPORTED", f"unsupported catalogue schema {catalogue['schemaVersion']}")
        return _result(state)

    from .catalogue import catalogue_semantic_issues

    semantic_issues = catalogue_semantic_issues(catalogue)
    if semantic_issues:
        for issue in semantic_issues:
            state.add("CATALOGUE_SEMANTIC_INVALID", issue)
        return _result(state)

    _check_engine_version(project, catalogue, state)
    profile = project["profile"]
    _validate_requirements(project.get("requiredApis", []), catalogue, profile, state, "neon.project.json")
    resource_sequence = [resource["name"] for resource in project["resources"]]
    if len(resource_sequence) != len(set(resource_sequence)):
        state.add("DUPLICATE_RESOURCE", "resource names must be unique")
    module_sequence = [module["name"] for module in project.get("modules", [])]
    if len(module_sequence) != len(set(module_sequence)):
        state.add("DUPLICATE_MODULE", "module names must be unique")
    resource_names = {resource["name"] for resource in project["resources"]}
    module_names = {module["name"] for module in project.get("modules", [])}
    external = set(project.get("externalDependencies", []))
    strict_unknown = project.get("unknownApis", "allow") == "error"
    strict_components = project.get("unknownComponents", "allow-opaque") == "error"

    loaded_resources: dict[str, LoadedComponent] = {}
    for resource in sorted(project["resources"], key=lambda value: value["name"]):
        state.resources.add(resource["name"])
        loaded_resources[resource["name"]] = _load_component(workspace, resource, "resource", schema_store, state)
    loaded_modules: dict[str, LoadedComponent] = {}
    for module in sorted(project.get("modules", []), key=lambda value: value["name"]):
        component = _load_component(workspace, module, "module", schema_store, state)
        loaded_modules[module["name"]] = component
        if component.manifest is None:
            _component_policy_diagnostic(state, strict_components, "MODULE_OPAQUE", f"module {module['name']} has no approved valid manifest; no API signatures are assumed", path=module["path"])
        binary = module.get("binary")
        if binary and component.root is not None:
            binary_path = _resolve_relative(component.root, binary)
            binary_display = f"{component.display_root}/{binary}"
            if binary_path is None:
                state.add("PATH_OUTSIDE_WORKSPACE", f"module binary path escapes module: {binary}", path=binary_display)
            elif not binary_path.is_file():
                state.add("MODULE_BINARY_MISSING", f"declared module binary does not exist: {binary}", path=binary_display)

    project_functions = {"client": set(), "server": set()}
    project_events = {"client": set(), "server": set()}
    resource_exports: dict[str, dict[str, set[str]]] = {
        name: {"client": set(), "server": set()} for name in resource_names
    }
    component_versions = {
        (component.kind, component.name): component.manifest["version"]
        for component in [*loaded_resources.values(), *loaded_modules.values()]
        if component.manifest is not None
    }
    for component in [*loaded_resources.values(), *loaded_modules.values()]:
        manifest = component.manifest
        if manifest is None:
            continue
        manifest_path = component.manifest_display or component.display_root
        for dependency in manifest["dependencies"]:
            available_names = resource_names if dependency["kind"] == "resource" else module_names if dependency["kind"] == "module" else external
            if not dependency["optional"] and dependency["name"] not in available_names:
                state.add("COMPONENT_DEPENDENCY_MISSING", f"required {dependency['kind']} dependency {dependency['name']} is not in the project", path=manifest_path)
            minimum_version = dependency.get("minimumVersion")
            if minimum_version and dependency["name"] in available_names:
                actual_version = component_versions.get((dependency["kind"], dependency["name"]))
                if actual_version is None:
                    state.add("COMPONENT_DEPENDENCY_VERSION_UNKNOWN", f"cannot verify {dependency['kind']} dependency {dependency['name']} against minimum version {minimum_version}", path=manifest_path)
                elif parse_semver(actual_version) < parse_semver(minimum_version):
                    state.add("COMPONENT_DEPENDENCY_VERSION_INCOMPATIBLE", f"{dependency['kind']} dependency {dependency['name']} version {actual_version} is below {minimum_version}", path=manifest_path)
        for export in manifest["exports"]:
            for side in _component_sides(export["side"]):
                if component.kind == "module":
                    project_functions[side].add(export["name"])
                else:
                    resource_exports[component.name][side].add(export["name"])
        for event in manifest["events"]:
            for side in _component_sides(event["side"]):
                project_events[side].add(event["name"])

    for resource in sorted(project["resources"], key=lambda value: value["name"]):
        name = resource["name"]
        component = loaded_resources[name]
        resource_path = component.root
        if resource_path is None or not resource_path.is_dir():
            continue
        meta_name = resource.get("meta", "meta.xml")
        meta_path = _resolve_relative(resource_path, meta_name)
        meta_display = f"{resource['path'].rstrip('/')}/{meta_name}"
        if meta_path is None:
            state.add("PATH_OUTSIDE_WORKSPACE", f"meta path escapes resource: {meta_name}", path=meta_display)
            continue
        root = _parse_meta(meta_path, meta_display, state)
        _validate_requirements(resource.get("requiredApis", []), catalogue, profile, state, resource["path"])
        declared_dependencies = set(resource.get("dependencies", []))
        if root is None:
            continue
        if root.tag != "meta":
            state.add("INVALID_META", "root element must be <meta>", path=meta_display)
            continue
        meta_dependencies: set[str] = set()
        for include in root.findall("include", "meta", 1):
            dependency = include.get("resource")
            if not dependency:
                state.add("INVALID_META", "include is missing resource", path=meta_display)
            else:
                meta_dependencies.add(dependency)
        for dependency in sorted((declared_dependencies | meta_dependencies) - resource_names - external):
            state.add(
                "MISSING_DEPENDENCY",
                f"resource {name} requires undeclared project dependency {dependency}",
                path=meta_display,
            )
        global_functions = {"client": set(), "server": set()}
        defined_events: dict[tuple[str, str], bool] = {}
        oop_enabled = any(
            item.tag == "oop" and item.parent == "meta" and item.depth == 1 and item.text.strip().casefold() == "true"
            for item in root.elements
        )
        oop_used = False
        for tag in META_ASSET_TAGS:
            for asset in root.findall(tag, "meta", 1):
                source = asset.get("src")
                if not source:
                    state.add("INVALID_META", f"{tag} is missing src", path=meta_display)
                    continue
                asset_path = _resolve_relative(resource_path, source)
                display_path = f"{resource['path'].rstrip('/')}/{source}"
                state.files.add(display_path)
                if asset_path is None:
                    state.add("PATH_OUTSIDE_WORKSPACE", f"{tag} path escapes resource: {source}", path=display_path)
                elif not asset_path.is_file():
                    state.add("MISSING_FILE", f"{tag} does not exist: {source}", path=display_path)
        for script in root.findall("script", "meta", 1):
            source = script.get("src")
            side = script.get("type", "server")
            if not source:
                state.add("INVALID_META", "script is missing src", path=meta_display)
                continue
            if side not in ("client", "server", "shared"):
                state.add("INVALID_SCRIPT_SIDE", f"unsupported script side {side}", path=meta_display)
                continue
            if (profile == "neon-server" and side == "client") or (profile == "neon-client" and side == "server"):
                state.add("PROFILE_SIDE_MISMATCH", f"{side} script is outside profile {profile}", path=meta_display, side=side)
            script_path = _resolve_relative(resource_path, source)
            display_path = f"{resource['path'].rstrip('/')}/{source}"
            state.files.add(display_path)
            if script_path is None:
                state.add("PATH_OUTSIDE_WORKSPACE", f"script path escapes resource: {source}", path=display_path, side=side)
            elif not script_path.is_file():
                state.add("MISSING_FILE", f"script does not exist: {source}", path=display_path, side=side)
            else:
                oop_used = _scan_lua(
                    script_path, display_path, side, catalogue, profile, strict_unknown, state,
                    project_functions, project_events, resource_exports, strict_components,
                ) or oop_used
                functions, events = _lua_contract_facts(script_path, side)
                for candidate_side in _component_sides(side):
                    global_functions[candidate_side].update(functions)
                    for (event_name, _event_side), remote in events.items():
                        key = (event_name, candidate_side)
                        if key in defined_events and defined_events[key] != remote:
                            state.add("RESOURCE_EVENT_DEFINITION_CONFLICT", f"event {event_name} is defined with conflicting remote flags", path=display_path, side=candidate_side, symbol=event_name)
                        defined_events[key] = remote
        manifest_requires_oop = component.manifest is not None and component.manifest.get("oopRequired", False)
        if oop_used and not oop_enabled and not manifest_requires_oop:
            state.add("RESOURCE_OOP_MISSING", "Lua source uses a registered OOP class but meta.xml has no <oop>true</oop>", path=meta_display)
        _validate_resource_contract(
            component, root, meta_display, global_functions, defined_events,
            declared_dependencies | meta_dependencies, strict_components, state,
        )
    return _result(state)


def _opaque_resource_symbols(component: LoadedComponent, root: MetaDocument, meta_display: str | None = None) -> list[dict[str, Any]]:
    symbols: list[dict[str, Any]] = []
    provenance = {"meta": meta_display or f"{component.display_root}/meta.xml"}
    seen: set[str] = set()
    for item in root.findall("export", "meta", 1):
        name = item.get("function")
        side = item.get("type", "server")
        if not name or side not in ("client", "server"):
            continue
        symbol_id = f"resource:{component.name}:{side}-export:{name}"
        if symbol_id not in seen:
            symbols.append({"id": symbol_id, "kind": "export", "name": name, "owner": component.name, "ownerKind": "resource",
                            "side": side, "state": "opaque", "signatureKnown": False, "description": "Opaque meta.xml export; signature is intentionally unknown.",
                            "http": item.get("http", "false") == "true", "restricted": item.get("restricted", "false") == "true", "provenance": provenance})
            seen.add(symbol_id)
    if component.root is not None:
        for script in root.findall("script", "meta", 1):
            source = script.get("src")
            side = script.get("type", "server")
            script_path = _resolve_relative(component.root, source) if source else None
            if script_path is None or not script_path.is_file() or side not in ("client", "server", "shared"):
                continue
            _, events = _lua_contract_facts(script_path, side)
            for (name, _), remote in sorted(events.items()):
                symbol_id = f"resource:{component.name}:event:{name}"
                if symbol_id not in seen:
                    symbols.append({"id": symbol_id, "kind": "event", "name": name, "owner": component.name, "ownerKind": "resource",
                                    "side": side, "state": "opaque", "directions": ["defines"], "signatureKnown": False,
                                    "allowRemoteTrigger": remote, "description": "Opaque addEvent definition; parameter contract is intentionally unknown.",
                                    "provenance": {"source": f"{component.display_root}/{source}"}})
                    seen.add(symbol_id)
                else:
                    existing = next(item for item in symbols if item["id"] == symbol_id)
                    if existing["kind"] == "event" and existing["side"] != side:
                        existing["side"] = "shared"
                        if existing["allowRemoteTrigger"] != remote:
                            existing["state"] = "conflict"
                            existing["description"] = "Opaque addEvent definitions disagree on remote triggering; parameter contract is unknown."
    return sorted(symbols, key=lambda item: item["id"])


def resolve_project_components(project_path: Path, schema_store: SchemaStore, catalogue_override: Path | None = None) -> dict[str, Any]:
    check = check_project(project_path, schema_store, catalogue_override)
    try:
        project = load_json(project_path)
    except JsonDocumentError:
        return {**check, "command": "project.resolve", "summary": {**check["summary"], "components": 0, "symbols": 0, "opaque": 0}, "components": [], "symbols": []}
    if schema_store.validate("neon-project", project) or schema_major(project.get("schemaVersion", "")) != 1:
        return {**check, "command": "project.resolve", "summary": {**check["summary"], "components": 0, "symbols": 0, "opaque": 0}, "components": [], "symbols": []}

    workspace = project_path.parent.resolve()
    entries = [("resource", item) for item in project["resources"]] + [("module", item) for item in project.get("modules", [])]
    diagnostic_errors = [item for item in check["diagnostics"] if item["severity"] == "error"]
    components: list[dict[str, Any]] = []
    symbols: list[dict[str, Any]] = []
    for kind, entry in sorted(entries, key=lambda item: (item[0], item[1]["name"])):
        scratch = CheckState()
        component = _load_component(workspace, entry, kind, schema_store, scratch)
        related_errors = [item for item in diagnostic_errors if item["path"] == entry["path"] or item["path"].startswith(entry["path"].rstrip("/") + "/")]
        manifest = component.manifest
        binary_record: dict[str, Any] | None = None
        if kind == "module" and entry.get("binary") and component.root is not None:
            binary_path = _resolve_relative(component.root, entry["binary"])
            if binary_path is not None and binary_path.is_file():
                binary_record = {"path": f"{component.display_root}/{entry['binary']}", "sha256": file_sha256(binary_path), "size": binary_path.stat().st_size}
        if manifest is None:
            component_state = "conflict" if related_errors else "opaque"
            component_symbols: list[dict[str, Any]] = []
            if kind == "resource" and component.root is not None:
                meta_name = entry.get("meta", "meta.xml")
                meta_path = _resolve_relative(component.root, meta_name)
                meta_state = CheckState()
                root = _parse_meta(meta_path, f"{component.display_root}/{meta_name}", meta_state) if meta_path else None
                if root is not None:
                    component_symbols = _opaque_resource_symbols(component, root, f"{component.display_root}/{meta_name}")
            component_record = {"kind": kind, "name": entry["name"], "path": entry["path"], "state": component_state,
                                "manifest": None, "lifecycle": None, "dependencies": [], "acl": [], "capabilities": []}
        else:
            component_state = "conflict" if related_errors else "verified" if kind == "resource" else "documented-only"
            component_symbols = manifest_symbols(manifest, component.manifest_display or entry["path"], component_state)
            if kind == "resource" and component.root is not None:
                meta_name = entry.get("meta", "meta.xml")
                meta_path = _resolve_relative(component.root, meta_name)
                meta_state = CheckState()
                root = _parse_meta(meta_path, f"{component.display_root}/{meta_name}", meta_state) if meta_path else None
                if root is not None:
                    declared_ids = {item["id"] for item in component_symbols}
                    component_symbols.extend(
                        item for item in _opaque_resource_symbols(component, root, f"{component.display_root}/{meta_name}")
                        if item["id"] not in declared_ids
                    )
                    component_symbols.sort(key=lambda item: item["id"])
            component_record = {
                "kind": kind, "name": entry["name"], "path": entry["path"], "state": component_state,
                "manifest": {"path": component.manifest_display, "sha256": manifest_sha256(manifest), "schemaVersion": manifest["schemaVersion"], "version": manifest["version"]},
                "lifecycle": manifest["lifecycle"], "dependencies": manifest["dependencies"], "acl": manifest["acl"], "capabilities": manifest["capabilities"],
            }
            if kind == "module":
                component_record["module"] = manifest["module"]
        if kind == "module":
            component_record["binary"] = binary_record
        if kind == "resource" and component.root is not None:
            meta_name = entry.get("meta", "meta.xml")
            meta_path = _resolve_relative(component.root, meta_name)
            side_state = CheckState()
            meta = _parse_meta(meta_path, f"{component.display_root}/{meta_name}", side_state) if meta_path else None
            sides: set[str] = set()
            if meta is not None:
                for script in meta.findall("script", "meta", 1):
                    side = script.get("type", "server")
                    if side in ("client", "server"):
                        sides.add(side)
                    elif side == "shared":
                        sides.update(("client", "server"))
            component_record["sides"] = sorted(sides)
        components.append(component_record)
        symbols.extend(component_symbols)
    symbols.sort(key=lambda item: item["id"])
    opaque = sum(item["state"] == "opaque" for item in symbols)
    return {
        "schemaVersion": SCHEMA_VERSION,
        "command": "project.resolve",
        "status": check["status"],
        "summary": {**check["summary"], "components": len(components), "symbols": len(symbols), "opaque": opaque},
        "diagnostics": check["diagnostics"],
        "components": components,
        "symbols": symbols,
    }


def _result(state: CheckState) -> dict[str, Any]:
    diagnostics = sorted(
        (diagnostic.as_dict() for diagnostic in state.diagnostics),
        key=lambda value: (
            value["severity"], value["code"], value["path"], value.get("line", 0), value.get("side", ""), value.get("symbol", ""), value["message"]
        ),
    )
    errors = sum(item["severity"] == "error" for item in diagnostics)
    warnings = sum(item["severity"] == "warning" for item in diagnostics)
    return {
        "schemaVersion": SCHEMA_VERSION,
        "command": "check",
        "status": "pass" if errors == 0 else "fail",
        "summary": {
            "errors": errors,
            "warnings": warnings,
            "resources": len(state.resources),
            "files": len(state.files),
            "apiRequirements": state.api_requirements,
        },
        "diagnostics": diagnostics,
    }
