from __future__ import annotations

import re
import html
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

from . import SCHEMA_VERSION
from .jsonio import JsonDocumentError, load_json
from .schema import SchemaStore, schema_major


MAX_XML_BYTES = 4 * 1024 * 1024
LUA_CALL_RE = re.compile(r"(?<![.:\w])([A-Za-z_]\w*)\s*\(")
LUA_FUNCTION_RE = re.compile(r"(?:\blocal\s+)?\bfunction\s+([A-Za-z_]\w*)\s*\(|\blocal\s+([A-Za-z_]\w*)\s*=\s*function\b")
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


@dataclass(frozen=True)
class MetaElement:
    tag: str
    attributes: dict[str, str]

    def get(self, name: str, default: str | None = None) -> str | None:
        return self.attributes.get(name, default)


@dataclass(frozen=True)
class MetaDocument:
    tag: str
    elements: tuple[MetaElement, ...]

    def findall(self, name: str) -> list[MetaElement]:
        return [element for element in self.elements if element.tag == name]


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


def _available_by_side(catalogue: dict, profile: str) -> dict[str, set[str]]:
    result = {"client": set(), "server": set()}
    for symbol in catalogue.get("symbols", []):
        if symbol.get("kind") != "function" or symbol.get("state") in ("unavailable", "documented-only"):
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


def _line(source: str, offset: int) -> int:
    return source.count("\n", 0, offset) + 1


def _scan_lua(path: Path, display_path: str, side: str, catalogue: dict, profile: str, strict_unknown: bool, state: CheckState) -> None:
    try:
        source = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        state.add("FILE_READ_ERROR", str(exc), path=display_path, side=side)
        return
    code = strip_lua_noncode(source)
    declared = {name for match in LUA_FUNCTION_RE.finditer(code) for name in match.groups() if name}
    available = _available_by_side(catalogue, profile)
    all_known = available["client"] | available["server"]
    required_sides = ("client", "server") if side == "shared" else (side,)
    for match in LUA_CALL_RE.finditer(code):
        name = match.group(1)
        if name in declared or name in LUA_BUILTINS or name in LUA_KEYWORDS:
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
        elif strict_unknown and name not in all_known:
            state.add(
                "API_UNKNOWN",
                f"{name} is not present in the selected API catalogue",
                path=display_path,
                line=_line(code, match.start(1)),
                side=side,
                symbol=name,
            )


def _validate_requirements(requirements: Iterable[dict], catalogue: dict, profile: str, state: CheckState, path: str) -> None:
    available = _available_by_side(catalogue, profile)
    for requirement in requirements:
        state.api_requirements += 1
        name = requirement["name"]
        side = requirement["side"]
        required_sides = ("client", "server") if side == "shared" else (side,)
        present = [candidate for candidate in required_sides if name in available[candidate]]
        if not present:
            known = name in (available["client"] | available["server"])
            state.add(
                "API_WRONG_SIDE" if known else "API_NOT_FOUND",
                f"required API {name} is unavailable on {side}" if known else f"required API {name} does not exist",
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
    stack: list[str] = []
    root: str | None = None
    root_closed = False
    elements: list[MetaElement] = []
    cursor = 0
    while cursor < len(source):
        opening = source.find("<", cursor)
        if opening < 0:
            if not stack and source[cursor:].strip():
                raise ValueError("text is not allowed outside the root element")
            break
        if not stack and source[cursor:opening].strip():
            raise ValueError("text is not allowed outside the root element")
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
            if not XML_NAME_RE.fullmatch(name) or not stack or stack[-1] != name:
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
        elements.append(MetaElement(name, attributes))
        if not self_closing:
            stack.append(name)
        elif not stack:
            root_closed = True
    if root is None:
        raise ValueError("meta.xml has no root element")
    if stack:
        raise ValueError(f"unclosed XML tag {stack[-1]}")
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
    resource_names = {resource["name"] for resource in project["resources"]}
    external = set(project.get("externalDependencies", []))
    strict_unknown = project.get("unknownApis", "allow") == "error"

    for resource in sorted(project["resources"], key=lambda value: value["name"]):
        name = resource["name"]
        state.resources.add(name)
        resource_path = _resolve_relative(workspace, resource["path"])
        if resource_path is None:
            state.add("PATH_OUTSIDE_WORKSPACE", f"resource path is not workspace-relative: {resource['path']}", path=resource["path"])
            continue
        if not resource_path.is_dir():
            state.add("MISSING_RESOURCE", f"resource directory does not exist: {resource['path']}", path=resource["path"])
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
        for include in root.findall("include"):
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
        for script in root.findall("script"):
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
                _scan_lua(script_path, display_path, side, catalogue, profile, strict_unknown, state)
    return _result(state)


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
