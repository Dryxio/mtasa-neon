from __future__ import annotations

import ast
import hashlib
import io
import json
import re
import subprocess
import tarfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping

SOURCE_PREFIXES = (
    "Client/mods/deathmatch/logic/luadefs",
    "Client/mods/deathmatch/logic/lua/CLuaManager.cpp",
    "Server/mods/deathmatch/logic/luadefs",
    "Shared/mods/deathmatch/logic/luadefs",
    "Client/mods/deathmatch/logic/CClientGame.cpp",
    "Client/mods/deathmatch/logic/lua/CLuaFunctionParseHelpers.cpp",
    "Server/mods/deathmatch/logic/CGame.cpp",
    "Server/mods/deathmatch/logic/lua/CLuaFunctionParseHelpers.cpp",
    "Shared/mods/deathmatch/logic/Enums.cpp",
)
NEON_REPOSITORY = "https://github.com/Dryxio/mtasa-neon.git"
UPSTREAM_REPOSITORY = "https://github.com/multitheftauto/mtasa-blue.git"
SOURCE_LICENSE = "GPL-3.0-or-later"
CATALOGUE_SCHEMA_VERSION = "1.1.0"
PAIR_DECLARATION_RE = re.compile(
    r"std::pair\s*<\s*const\s+char\s*\*\s*,\s*lua_CFunction\s*>\s+[A-Za-z_]\w*\s*\[\s*]\s*\{"
)
PAIR_ENTRY_RE = re.compile(r'\{\s*"([A-Za-z_]\w*)"\s*,')
DIRECT_PATTERNS = (
    re.compile(r'CLuaCFunctions\s*::\s*AddFunction\s*\(\s*"([A-Za-z_]\w*)"'),
    re.compile(r'(?<!:)\bAddFunction\s*\(\s*"([A-Za-z_]\w*)"'),
    re.compile(r'\blua_register\s*\(\s*[^,]+,\s*"([A-Za-z_]\w*)"'),
)


@dataclass(frozen=True, order=True)
class Registration:
    name: str
    side: str
    path: str
    line: int
    restricted: bool = False


@dataclass(frozen=True, order=True)
class EventRegistration:
    name: str
    side: str
    arguments: tuple[str, ...]
    path: str
    line: int
    allow_remote_trigger: bool = False


@dataclass(frozen=True, order=True)
class OopMethodRegistration:
    class_name: str
    name: str
    side: str
    path: str
    line: int
    global_function: str = ""
    native_function: str = ""


@dataclass(frozen=True, order=True)
class OopPropertyRegistration:
    class_name: str
    name: str
    side: str
    path: str
    line: int
    setter: str = ""
    getter: str = ""
    native_setter: str = ""
    native_getter: str = ""


@dataclass(frozen=True, order=True)
class OopClassRegistration:
    name: str
    side: str
    path: str
    line: int
    parent: str = ""
    static: bool = False


@dataclass(frozen=True, order=True)
class EnumRegistration:
    name: str
    cpp_type: str
    side: str
    values: tuple[str, ...]
    path: str
    line: int


@dataclass(frozen=True)
class SourceSnapshot:
    revision: str
    files: Mapping[str, str]

    @property
    def digest(self) -> str:
        digest = hashlib.sha256()
        for path in sorted(self.files):
            digest.update(path.encode("utf-8"))
            digest.update(b"\0")
            digest.update(self.files[path].encode("utf-8"))
            digest.update(b"\0")
        return digest.hexdigest()


def filesystem_snapshot(root: Path, revision: str = "working-tree") -> SourceSnapshot:
    files: dict[str, str] = {}
    for prefix in SOURCE_PREFIXES:
        source_path = root / prefix
        if source_path.is_file():
            files[source_path.relative_to(root).as_posix()] = source_path.read_text(encoding="utf-8", errors="strict")
            continue
        if not source_path.is_dir():
            continue
        for path in sorted(source_path.rglob("*.cpp")):
            files[path.relative_to(root).as_posix()] = path.read_text(encoding="utf-8", errors="strict")
    return SourceSnapshot(revision=revision, files=files)


def git_snapshot(repository: Path, reference: str) -> SourceSnapshot:
    try:
        requested_revision = _git(repository, "rev-parse", "--verify", f"{reference}^{{commit}}").strip()
        revision = _git(repository, "log", "-1", "--format=%H", requested_revision, "--", *SOURCE_PREFIXES).strip()
        if not revision:
            revision = requested_revision
        source_paths = [
            path
            for path in _git(repository, "ls-tree", "-r", "--name-only", requested_revision, "--", *SOURCE_PREFIXES).splitlines()
            if path.endswith(".cpp")
        ]
        if not source_paths:
            return SourceSnapshot(revision=revision, files={})
        archive = subprocess.run(
            ["git", "archive", "--format=tar", requested_revision, *source_paths],
            cwd=repository,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = exc.stderr.decode("utf-8", errors="replace").strip() if isinstance(exc, subprocess.CalledProcessError) else str(exc)
        raise ValueError(f"cannot read Git source snapshot {reference}: {detail}") from exc

    files: dict[str, str] = {}
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as stream:
        for member in stream.getmembers():
            if not member.isfile() or not member.name.endswith(".cpp"):
                continue
            extracted = stream.extractfile(member)
            if extracted is None:
                continue
            files[member.name] = extracted.read().decode("utf-8", errors="strict")
    return SourceSnapshot(revision=revision, files=files)


def _git(repository: Path, *arguments: str) -> str:
    completed = subprocess.run(
        ["git", *arguments],
        cwd=repository,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    return completed.stdout


def strip_cpp_comments(source: str) -> str:
    result = list(source)
    index = 0
    state = "code"
    quote = ""
    while index < len(source):
        current = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if current in ('"', "'"):
                state = "string"
                quote = current
            elif current == "/" and following == "/":
                result[index] = result[index + 1] = " "
                state = "line-comment"
                index += 1
            elif current == "/" and following == "*":
                result[index] = result[index + 1] = " "
                state = "block-comment"
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
        elif state == "block-comment":
            if current == "*" and following == "/":
                result[index] = result[index + 1] = " "
                state = "code"
                index += 1
            elif current != "\n":
                result[index] = " "
        index += 1
    return "".join(result)


def _matching_brace(source: str, opening: int) -> int | None:
    depth = 0
    state = "code"
    quote = ""
    index = opening
    while index < len(source):
        current = source[index]
        if state == "code":
            if current in ('"', "'"):
                state = "string"
                quote = current
            elif current == "{":
                depth += 1
            elif current == "}":
                depth -= 1
                if depth == 0:
                    return index
        elif current == "\\":
            index += 1
        elif current == quote:
            state = "code"
        index += 1
    return None


def _side_for_path(path: str) -> str:
    if path.startswith("Client/"):
        return "client"
    if path.startswith("Server/"):
        return "server"
    return "shared"


def _split_cpp_arguments(source: str) -> list[str]:
    """Split one C++ call without pretending to parse the whole language."""
    arguments: list[str] = []
    start = 0
    depth = 0
    quote = ""
    index = 0
    while index < len(source):
        current = source[index]
        if quote:
            if current == "\\":
                index += 1
            elif current == quote:
                quote = ""
        elif current in ('"', "'"):
            quote = current
        elif current in "([{<":
            depth += 1
        elif current in ")]}>" and depth:
            depth -= 1
        elif current == "," and depth == 0:
            arguments.append(source[start:index].strip())
            start = index + 1
        index += 1
    arguments.append(source[start:].strip())
    return arguments


def _iter_cpp_calls(source: str, names: Iterable[str]) -> Iterable[tuple[str, list[str], int]]:
    pattern = re.compile(r"\b(" + "|".join(re.escape(name) for name in names) + r")\s*\(")
    for match in pattern.finditer(source):
        opening = source.find("(", match.start())
        depth = 0
        quote = ""
        index = opening
        while index < len(source):
            current = source[index]
            if quote:
                if current == "\\":
                    index += 1
                elif current == quote:
                    quote = ""
            elif current in ('"', "'"):
                quote = current
            elif current == "(":
                depth += 1
            elif current == ")":
                depth -= 1
                if depth == 0:
                    yield match.group(1), _split_cpp_arguments(source[opening + 1 : index]), match.start()
                    break
            index += 1


def _cpp_string(argument: str) -> str:
    match = re.fullmatch(r'(?:u8|L)?("(?:\\.|[^"\\])*")', argument.strip(), re.DOTALL)
    if not match:
        return ""
    try:
        value = ast.literal_eval(match.group(1))
    except (SyntaxError, ValueError):
        return ""
    return value if isinstance(value, str) else ""


def _native_binding(argument: str) -> str:
    value = " ".join(argument.strip().split())
    return "" if value in ("", "NULL", "nullptr", "\"\"") else value


def extract_event_registrations(snapshot: SourceSnapshot) -> list[EventRegistration]:
    registrations: set[EventRegistration] = set()
    for path in sorted(snapshot.files):
        if not path.endswith(("CClientGame.cpp", "/CGame.cpp")):
            continue
        source = strip_cpp_comments(snapshot.files[path])
        side = _side_for_path(path)
        for _, arguments, offset in _iter_cpp_calls(source, ("AddEvent",)):
            if len(arguments) < 2:
                continue
            name = _cpp_string(arguments[0])
            signature = _cpp_string(arguments[1])
            if not name:
                continue
            parameters = tuple(value.strip() for value in signature.split(",") if value.strip())
            allow_remote = any(value.strip() == "true" for value in arguments[3:])
            registrations.add(EventRegistration(name, side, parameters, path, source.count("\n", 0, offset) + 1, allow_remote))
    return sorted(registrations)


def extract_oop_registrations(
    snapshot: SourceSnapshot,
) -> tuple[list[OopClassRegistration], list[OopMethodRegistration], list[OopPropertyRegistration]]:
    classes: set[OopClassRegistration] = set()
    methods: set[OopMethodRegistration] = set()
    properties: set[OopPropertyRegistration] = set()
    call_names = ("lua_newclass", "lua_registerclass", "lua_registerstaticclass", "lua_classfunction", "lua_classvariable")
    for path in sorted(snapshot.files):
        if "/luadefs/" not in path:
            continue
        source = strip_cpp_comments(snapshot.files[path])
        side = _side_for_path(path)
        pending: list[tuple[str, list[str], int]] | None = None
        for call_name, arguments, offset in _iter_cpp_calls(source, call_names):
            if call_name == "lua_newclass":
                pending = []
                continue
            if call_name not in ("lua_registerclass", "lua_registerstaticclass"):
                if pending is not None:
                    pending.append((call_name, arguments, offset))
                continue
            if len(arguments) < 2:
                pending = None
                continue
            class_name = _cpp_string(arguments[1])
            if not class_name:
                pending = None
                continue
            parent = _cpp_string(arguments[2]) if len(arguments) >= 3 else ""
            line = source.count("\n", 0, offset) + 1
            classes.add(OopClassRegistration(class_name, side, path, line, parent, call_name == "lua_registerstaticclass"))
            for member_name, member_arguments, member_offset in pending or []:
                if len(member_arguments) < 2:
                    continue
                script_name = _cpp_string(member_arguments[1])
                if not script_name:
                    continue
                member_line = source.count("\n", 0, member_offset) + 1
                if member_name == "lua_classfunction":
                    global_function = _cpp_string(member_arguments[2]) if len(member_arguments) >= 3 else ""
                    native_argument = ""
                    if not global_function and len(member_arguments) >= 4:
                        native_argument = member_arguments[3]
                    elif not global_function and len(member_arguments) >= 3:
                        native_argument = member_arguments[2]
                    methods.add(
                        OopMethodRegistration(
                            class_name, script_name, side, path, member_line, global_function, _native_binding(native_argument)
                        )
                    )
                else:
                    setter = _cpp_string(member_arguments[2]) if len(member_arguments) >= 3 else ""
                    getter = _cpp_string(member_arguments[3]) if len(member_arguments) >= 4 else ""
                    properties.add(
                        OopPropertyRegistration(
                            class_name,
                            script_name,
                            side,
                            path,
                            member_line,
                            setter,
                            getter,
                            _native_binding(member_arguments[2]) if len(member_arguments) >= 3 and not setter else "",
                            _native_binding(member_arguments[3]) if len(member_arguments) >= 4 and not getter else "",
                        )
                    )
            pending = None
    return sorted(classes), sorted(methods), sorted(properties)


def extract_enum_registrations(snapshot: SourceSnapshot) -> list[EnumRegistration]:
    registrations: set[EnumRegistration] = set()
    begin = re.compile(r"IMPLEMENT_ENUM(?:_CLASS)?_BEGIN\s*\(\s*([^)]+?)\s*\)")
    end = re.compile(r'IMPLEMENT_ENUM(?:_CLASS)?_END(?:_DEFAULTS)?\s*\(\s*"([^"]+)"')
    value = re.compile(r'ADD_ENUM\s*\([^,]+,\s*"([^"]+)"\s*\)|ADD_ENUM1\s*\(\s*([A-Za-z_]\w*)\s*\)')
    for path in sorted(snapshot.files):
        if not path.endswith(("Enums.cpp", "CLuaFunctionParseHelpers.cpp")):
            continue
        source = strip_cpp_comments(snapshot.files[path])
        side = _side_for_path(path)
        cursor = 0
        while match := begin.search(source, cursor):
            closing = end.search(source, match.end())
            if closing is None:
                break
            values = tuple(dict.fromkeys(item.group(1) or item.group(2) for item in value.finditer(source, match.end(), closing.start())))
            registrations.add(
                EnumRegistration(
                    closing.group(1), match.group(1).strip(), side, values, path, source.count("\n", 0, match.start()) + 1
                )
            )
            cursor = closing.end()
    return sorted(registrations)


def extract_registrations(snapshot: SourceSnapshot) -> list[Registration]:
    registrations: set[Registration] = set()
    for path in sorted(snapshot.files):
        side = _side_for_path(path)
        source = strip_cpp_comments(snapshot.files[path])
        covered_spans: list[tuple[int, int]] = []
        for declaration in PAIR_DECLARATION_RE.finditer(source):
            opening = declaration.end() - 1
            closing = _matching_brace(source, opening)
            if closing is None:
                continue
            covered_spans.append((opening, closing))
            body = source[opening : closing + 1]
            for entry in PAIR_ENTRY_RE.finditer(body):
                offset = opening + entry.start(1)
                registrations.add(Registration(entry.group(1), side, path, source.count("\n", 0, offset) + 1))

        for pattern in DIRECT_PATTERNS:
            for match in pattern.finditer(source):
                if any(start <= match.start() <= end for start, end in covered_spans):
                    continue
                line_end = source.find("\n", match.end())
                if line_end < 0:
                    line_end = len(source)
                restricted = re.search(r",\s*true\s*\)\s*;", source[match.end() : line_end]) is not None
                registrations.add(
                    Registration(match.group(1), side, path, source.count("\n", 0, match.start(1)) + 1, restricted)
                )
    return sorted(registrations)


def _effective_sides(registrations: Iterable[Registration]) -> dict[str, set[str]]:
    result: dict[str, set[str]] = {}
    for registration in registrations:
        sides = result.setdefault(registration.name, set())
        if registration.side == "shared":
            sides.update(("client", "server"))
        else:
            sides.add(registration.side)
    return result


def _expanded_side(side: str) -> set[str]:
    return {"client", "server"} if side == "shared" else {side}


def _documented_sides(entries: Iterable[dict]) -> set[str]:
    return {
        effective
        for entry in entries
        for contract in entry.get("contracts", [])
        for effective in _expanded_side(contract.get("side", "shared"))
    }


def _explicit_documented_sides(entries: Iterable[dict]) -> set[str]:
    return {
        contract["side"]
        for entry in entries
        for contract in entry.get("contracts", [])
        if contract.get("side") in ("client", "server")
    }


def _profiles(current_sides: Iterable[str], inherited_sides: Iterable[str]) -> list[str]:
    current = set(current_sides)
    inherited = set(inherited_sides)
    profiles: set[str] = set()
    if inherited:
        profiles.add("mta-upstream")
    if "client" in current:
        profiles.update(("neon-client", "neon-pair", "neon-multiclient"))
    if "server" in current:
        profiles.update(("neon-server", "neon-pair", "neon-multiclient"))
    return sorted(profiles)


def _provenance(repository: str, revision: str, path: str, license_name: str) -> dict:
    return {"repository": repository, "revision": revision, "path": path, "license": license_name}


def _documentation_provenance(entry: dict, semantic_snapshot: dict) -> dict:
    source_key = "neonWiki" if entry["provider"] == "neon" else "upstreamWiki"
    source = semantic_snapshot["sources"][source_key]
    path = entry.get("contracts", [{}])[0].get("sourcePath", entry.get("sourcePath", "unknown"))
    return _provenance(source["repository"], source["revision"], path, source["license"])


def _primary_contract(entries: list[dict]) -> dict | None:
    contracts = [
        {**contract, "provider": entry["provider"]}
        for entry in entries
        for contract in entry.get("contracts", [])
    ]
    if not contracts:
        return None
    return sorted(
        contracts,
        key=lambda contract: (
            0 if contract["provider"] == "neon" else 1,
            0 if contract.get("side") == "shared" else 1,
            contract.get("side", ""),
        ),
    )[0]


def _function_symbol(
    name: str,
    neon_sides: dict[str, set[str]],
    upstream_sides: dict[str, set[str]],
    neon_sources: dict[str, list[Registration]],
    upstream_sources: dict[str, list[Registration]],
    documentation: list[dict],
    semantic_snapshot: dict | None,
    neon: SourceSnapshot,
    upstream: SourceSnapshot,
) -> dict:
    mta_docs = [entry for entry in documentation if entry["provider"] == "mta"]
    neon_docs = [entry for entry in documentation if entry["provider"] == "neon"]
    active_registration_sides = set(neon_sides.get(name, set()))
    inherited_registration_sides = set(upstream_sides.get(name, set()))
    current_doc_sides = _documented_sides(neon_docs or mta_docs)
    upstream_doc_sides = _documented_sides(mta_docs)
    has_registration = bool(active_registration_sides or inherited_registration_sides)
    has_documentation = bool(documentation)
    active_sides = sorted(active_registration_sides or (current_doc_sides if not has_registration else set()))
    inherited_sides = sorted(inherited_registration_sides or (upstream_doc_sides if not has_registration else set()))

    if has_registration and has_documentation:
        compared_entries = neon_docs or mta_docs if active_registration_sides else mta_docs
        compared_docs = _explicit_documented_sides(compared_entries)
        compared_registration = active_registration_sides if active_registration_sides else inherited_registration_sides
        # The upstream converter often stores a side-neutral contract under
        # `shared`, including for server-only functions. Explicit client/server
        # claims can prove a contradiction; absence of a claim cannot.
        state = "conflict" if compared_docs - compared_registration else "verified"
    elif has_documentation:
        state = "documented-only"
    else:
        state = "runtime-only"

    origin = "mta" if inherited_registration_sides or mta_docs else "neon"
    source_records = neon_sources.get(name) or upstream_sources.get(name, [])
    sources = [
        {"path": item.path, "line": item.line, "side": item.side}
        for item in sorted(source_records, key=lambda item: (item.path, item.line, item.side))
    ]
    contracts = [
        {**contract, "provider": entry["provider"]}
        for entry in documentation
        for contract in entry.get("contracts", [])
    ]
    contracts.sort(key=lambda contract: (contract["provider"], contract.get("side", ""), contract.get("sourcePath", "")))
    primary = _primary_contract(documentation)
    provenance = []
    if source_records:
        repository = NEON_REPOSITORY if neon_sources.get(name) else UPSTREAM_REPOSITORY
        revision = neon.revision if neon_sources.get(name) else upstream.revision
        provenance.extend(_provenance(repository, revision, item.path, SOURCE_LICENSE) for item in source_records)
    if semantic_snapshot:
        provenance.extend(_documentation_provenance(entry, semantic_snapshot) for entry in documentation)
    provenance = sorted(
        {tuple(item.values()): item for item in provenance}.values(),
        key=lambda item: (item["repository"], item["revision"], item["path"], item["license"]),
    )

    symbol = {
        "id": f"{origin}:function:{name}",
        "kind": "function",
        "name": name,
        "origin": origin,
        "state": state,
        "sides": active_sides,
        "inheritedSides": inherited_sides,
        "profiles": _profiles(active_sides, inherited_sides),
        "restricted": any(item.restricted for item in neon_sources.get(name, [])),
        "parameters": primary.get("parameters", []) if primary else [{"name": "...", "type": "unknown", "optional": True}],
        "returns": primary.get("returns", []) if primary else [{"type": "unknown"}],
        "evidence": sorted(({"source-inspected"} if has_registration else set()) | ({"documented"} if has_documentation else set())),
        "sources": sources,
        "provenance": provenance,
    }
    if contracts:
        symbol["contracts"] = contracts
    if primary and primary.get("description"):
        symbol["description"] = primary["description"]
    category = next((entry.get("category") for entry in neon_docs if entry.get("category")), None)
    if category:
        symbol["category"] = category
    return symbol


def _document_symbol(kind: str, entry: dict, semantic_snapshot: dict) -> dict:
    side = entry.get("side")
    sides = sorted(_expanded_side(side)) if side else []
    profiles = _profiles(sides, sides) if sides else ["mta-upstream", "neon-client", "neon-multiclient", "neon-pair", "neon-server"]
    source = semantic_snapshot["sources"]["upstreamWiki"]
    symbol = {
        "id": f"mta:{kind}:{entry['name']}",
        "kind": kind,
        "name": entry["name"],
        "origin": "mta",
        "state": "documented-only",
        "sides": sides,
        "inheritedSides": sides,
        "profiles": profiles,
        "restricted": False,
        "parameters": entry.get("parameters", []),
        "returns": [],
        "evidence": ["documented"],
        "sources": [],
        "provenance": [_provenance(source["repository"], source["revision"], entry["sourcePath"], source["license"])],
    }
    for key in ("description", "redirect", "version", "sourceElement", "canceling", "oopOnlyMethods"):
        if entry.get(key):
            symbol[key] = entry[key]
    return symbol


def _group_by_name(items: Iterable) -> dict[str, list]:
    result: dict[str, list] = {}
    for item in items:
        result.setdefault(item.name, []).append(item)
    return result


def _registration_sides(items: Iterable) -> set[str]:
    return {effective for item in items for effective in _expanded_side(item.side)}


def _registration_sources(items: Iterable) -> list[dict]:
    return [
        {"path": item.path, "line": item.line, "side": item.side}
        for item in sorted(items, key=lambda value: (value.path, value.line, value.side))
    ]


def _source_location(item) -> dict:
    return {"path": item.path, "line": item.line, "side": item.side}


def _registration_provenance(items: Iterable, snapshot: SourceSnapshot, repository: str) -> list[dict]:
    records = [
        _provenance(repository, snapshot.revision, item.path, SOURCE_LICENSE)
        for item in sorted(items, key=lambda value: (value.path, value.line, value.side))
    ]
    return sorted(
        {tuple(item.values()): item for item in records}.values(),
        key=lambda item: (item["repository"], item["revision"], item["path"], item["license"]),
    )


def _event_symbol(
    name: str,
    neon_items: list[EventRegistration],
    upstream_items: list[EventRegistration],
    documentation: dict | None,
    semantic_snapshot: dict | None,
    neon: SourceSnapshot,
    upstream: SourceSnapshot,
) -> dict:
    active_sides = _registration_sides(neon_items)
    inherited_sides = _registration_sides(upstream_items)
    documented_sides = _expanded_side(documentation["side"]) if documentation and documentation.get("side") else set()
    has_registration = bool(neon_items or upstream_items)
    if not has_registration:
        active_sides = set(documented_sides)
        inherited_sides = set(documented_sides)
    selected = neon_items or upstream_items
    arguments = next((item.arguments for item in selected if item.arguments), ())
    documented_arguments = [parameter["name"] for parameter in documentation.get("parameters", [])] if documentation else []
    registration_differences: list[str] = []
    if documentation and has_registration:
        if len(documented_arguments) != len(arguments):
            registration_differences.append("parameter-count")
        elif documented_arguments != list(arguments):
            registration_differences.append("parameter-names")
    if documentation and has_registration:
        compared = active_sides or inherited_sides
        state = "conflict" if documented_sides - compared or "parameter-count" in registration_differences else "verified"
    elif documentation:
        state = "documented-only"
    else:
        state = "runtime-only"
    origin = "mta" if upstream_items or documentation else "neon"
    provenance = _registration_provenance(neon_items, neon, NEON_REPOSITORY)
    provenance.extend(_registration_provenance(upstream_items, upstream, UPSTREAM_REPOSITORY))
    if documentation and semantic_snapshot:
        provenance.append(_documentation_provenance(documentation, semantic_snapshot))
    provenance = sorted(
        {tuple(item.values()): item for item in provenance}.values(),
        key=lambda item: (item["repository"], item["revision"], item["path"], item["license"]),
    )
    symbol = {
        "id": f"{origin}:event:{name}",
        "kind": "event",
        "name": name,
        "origin": origin,
        "state": state,
        "sides": sorted(active_sides),
        "inheritedSides": sorted(inherited_sides),
        "profiles": _profiles(active_sides, inherited_sides),
        "restricted": False,
        "parameters": documentation.get("parameters", []) if documentation else [
            {"name": argument, "type": "unknown", "optional": False} for argument in arguments
        ],
        "returns": [],
        "evidence": sorted(({"source-inspected"} if has_registration else set()) | ({"documented"} if documentation else set())),
        "sources": _registration_sources(selected),
        "provenance": provenance,
        "registrationArguments": list(arguments),
        "allowRemoteTrigger": any(item.allow_remote_trigger for item in selected),
        "registrationDifferences": registration_differences,
        "eventDefinitions": [
            {
                "side": item.side,
                "arguments": list(item.arguments),
                "allowRemoteTrigger": item.allow_remote_trigger,
                "source": _source_location(item),
            }
            for item in sorted(neon_items, key=lambda value: (value.side, value.path, value.line, value.arguments, value.allow_remote_trigger))
        ],
        "inheritedEventDefinitions": [
            {
                "side": item.side,
                "arguments": list(item.arguments),
                "allowRemoteTrigger": item.allow_remote_trigger,
                "source": _source_location(item),
            }
            for item in sorted(upstream_items, key=lambda value: (value.side, value.path, value.line, value.arguments, value.allow_remote_trigger))
        ],
    }
    if documentation:
        for key in ("description", "sourceElement", "canceling", "version"):
            if documentation.get(key):
                symbol[key] = documentation[key]
    return symbol


def _oop_member_records(current: list, inherited: list, *, property_member: bool = False) -> list[dict]:
    keys = {(item.class_name, item.name) for item in current + inherited}
    result = []
    for class_name, name in sorted(keys, key=lambda item: (item[1].casefold(), item[1])):
        active = [item for item in current if item.class_name == class_name and item.name == name]
        baseline = [item for item in inherited if item.class_name == class_name and item.name == name]
        selected = active or baseline
        record = {
            "name": name,
            "sides": sorted(_registration_sides(active)),
            "inheritedSides": sorted(_registration_sides(baseline)),
            "sources": _registration_sources(selected),
        }
        if property_member:
            setters = sorted({item.setter for item in active if item.setter})
            getters = sorted({item.getter for item in active if item.getter})
            inherited_setters = sorted({item.setter for item in baseline if item.setter})
            inherited_getters = sorted({item.getter for item in baseline if item.getter})
            if setters:
                record["setters"] = setters
            if getters:
                record["getters"] = getters
            if inherited_setters:
                record["inheritedSetters"] = inherited_setters
            if inherited_getters:
                record["inheritedGetters"] = inherited_getters
            record["bindings"] = [
                {
                    "side": item.side,
                    **({"setter": item.setter} if item.setter else {}),
                    **({"getter": item.getter} if item.getter else {}),
                    **({"nativeSetter": item.native_setter} if item.native_setter else {}),
                    **({"nativeGetter": item.native_getter} if item.native_getter else {}),
                    "source": _source_location(item),
                }
                for item in sorted(
                    active,
                    key=lambda value: (
                        value.side, value.path, value.line, value.setter, value.getter, value.native_setter, value.native_getter
                    ),
                )
            ]
            record["inheritedBindings"] = [
                {
                    "side": item.side,
                    **({"setter": item.setter} if item.setter else {}),
                    **({"getter": item.getter} if item.getter else {}),
                    **({"nativeSetter": item.native_setter} if item.native_setter else {}),
                    **({"nativeGetter": item.native_getter} if item.native_getter else {}),
                    "source": _source_location(item),
                }
                for item in sorted(
                    baseline,
                    key=lambda value: (
                        value.side, value.path, value.line, value.setter, value.getter, value.native_setter, value.native_getter
                    ),
                )
            ]
        else:
            functions = sorted({item.global_function for item in active if item.global_function})
            inherited_functions = sorted({item.global_function for item in baseline if item.global_function})
            if functions:
                record["globalFunctions"] = functions
            if inherited_functions:
                record["inheritedGlobalFunctions"] = inherited_functions
            record["bindings"] = [
                {
                    "side": item.side,
                    **({"globalFunction": item.global_function} if item.global_function else {}),
                    **({"nativeFunction": item.native_function} if item.native_function else {}),
                    "source": _source_location(item),
                }
                for item in sorted(
                    active, key=lambda value: (value.side, value.path, value.line, value.global_function, value.native_function)
                )
            ]
            record["inheritedBindings"] = [
                {
                    "side": item.side,
                    **({"globalFunction": item.global_function} if item.global_function else {}),
                    **({"nativeFunction": item.native_function} if item.native_function else {}),
                    "source": _source_location(item),
                }
                for item in sorted(
                    baseline, key=lambda value: (value.side, value.path, value.line, value.global_function, value.native_function)
                )
            ]
        result.append(record)
    return result


def _oop_symbols(
    neon_data: tuple[list[OopClassRegistration], list[OopMethodRegistration], list[OopPropertyRegistration]],
    upstream_data: tuple[list[OopClassRegistration], list[OopMethodRegistration], list[OopPropertyRegistration]],
    neon: SourceSnapshot,
    upstream: SourceSnapshot,
) -> list[dict]:
    neon_classes, neon_methods, neon_properties = neon_data
    upstream_classes, upstream_methods, upstream_properties = upstream_data
    current_by_name = _group_by_name(neon_classes)
    inherited_by_name = _group_by_name(upstream_classes)
    result = []
    for name in set(current_by_name) | set(inherited_by_name):
        active = current_by_name.get(name, [])
        baseline = inherited_by_name.get(name, [])
        selected = active or baseline
        origin = "mta" if baseline else "neon"
        active_sides = _registration_sides(active)
        inherited_sides = _registration_sides(baseline)
        provenance = _registration_provenance(active, neon, NEON_REPOSITORY)
        provenance.extend(_registration_provenance(baseline, upstream, UPSTREAM_REPOSITORY))
        provenance = sorted(
            {tuple(item.values()): item for item in provenance}.values(),
            key=lambda item: (item["repository"], item["revision"], item["path"], item["license"]),
        )
        result.append({
            "id": f"{origin}:class:{name}",
            "kind": "class",
            "name": name,
            "origin": origin,
            "state": "runtime-only",
            "sides": sorted(active_sides),
            "inheritedSides": sorted(inherited_sides),
            "profiles": _profiles(active_sides, inherited_sides),
            "restricted": False,
            "parameters": [],
            "returns": [],
            "evidence": ["source-inspected"],
            "sources": _registration_sources(selected),
            "provenance": provenance,
            "parents": sorted({item.parent for item in active if item.parent}),
            "inheritedParents": sorted({item.parent for item in baseline if item.parent}),
            "static": bool(active) and all(item.static for item in active),
            "inheritedStatic": bool(baseline) and all(item.static for item in baseline),
            "definitions": [
                {
                    "side": item.side,
                    **({"parent": item.parent} if item.parent else {}),
                    "static": item.static,
                    "source": _source_location(item),
                }
                for item in sorted(active, key=lambda value: (value.side, value.path, value.line, value.parent, value.static))
            ],
            "inheritedDefinitions": [
                {
                    "side": item.side,
                    **({"parent": item.parent} if item.parent else {}),
                    "static": item.static,
                    "source": _source_location(item),
                }
                for item in sorted(baseline, key=lambda value: (value.side, value.path, value.line, value.parent, value.static))
            ],
            "methods": _oop_member_records(
                [item for item in neon_methods if item.class_name == name],
                [item for item in upstream_methods if item.class_name == name],
            ),
            "properties": _oop_member_records(
                [item for item in neon_properties if item.class_name == name],
                [item for item in upstream_properties if item.class_name == name],
                property_member=True,
            ),
        })
    return result


def _enum_symbols(
    neon_items: list[EnumRegistration],
    upstream_items: list[EnumRegistration],
    neon: SourceSnapshot,
    upstream: SourceSnapshot,
) -> list[dict]:
    current_by_name = _group_by_name(neon_items)
    inherited_by_name = _group_by_name(upstream_items)
    result = []
    for name in set(current_by_name) | set(inherited_by_name):
        active = current_by_name.get(name, [])
        baseline = inherited_by_name.get(name, [])
        selected = active or baseline
        origin = "mta" if baseline else "neon"
        active_sides = _registration_sides(active)
        inherited_sides = _registration_sides(baseline)
        provenance = _registration_provenance(active, neon, NEON_REPOSITORY)
        provenance.extend(_registration_provenance(baseline, upstream, UPSTREAM_REPOSITORY))
        provenance = sorted(
            {tuple(item.values()): item for item in provenance}.values(),
            key=lambda item: (item["repository"], item["revision"], item["path"], item["license"]),
        )
        result.append({
            "id": f"{origin}:enum:{name}",
            "kind": "enum",
            "name": name,
            "origin": origin,
            "state": "runtime-only",
            "sides": sorted(active_sides),
            "inheritedSides": sorted(inherited_sides),
            "profiles": _profiles(active_sides, inherited_sides),
            "restricted": False,
            "parameters": [],
            "returns": [],
            "evidence": ["source-inspected"],
            "sources": _registration_sources(selected),
            "provenance": provenance,
            "values": sorted({value for item in active for value in item.values}),
            "inheritedValues": sorted({value for item in baseline for value in item.values}),
            "cppTypes": sorted({item.cpp_type for item in selected}),
            "definitions": [
                {"side": item.side, "cppType": item.cpp_type, "values": list(item.values), "source": _source_location(item)}
                for item in sorted(active, key=lambda value: (value.side, value.path, value.line, value.cpp_type, value.values))
            ],
            "inheritedDefinitions": [
                {"side": item.side, "cppType": item.cpp_type, "values": list(item.values), "source": _source_location(item)}
                for item in sorted(baseline, key=lambda value: (value.side, value.path, value.line, value.cpp_type, value.values))
            ],
        })
    return result


def build_catalogue(
    neon: SourceSnapshot,
    upstream: SourceSnapshot,
    *,
    engine_version: str,
    wiki_revision: str,
    semantic_snapshot: dict | None = None,
) -> dict:
    neon_registrations = extract_registrations(neon)
    upstream_registrations = extract_registrations(upstream)
    neon_events = extract_event_registrations(neon)
    upstream_events = extract_event_registrations(upstream)
    neon_oop = extract_oop_registrations(neon)
    upstream_oop = extract_oop_registrations(upstream)
    neon_enums = extract_enum_registrations(neon)
    upstream_enums = extract_enum_registrations(upstream)
    neon_sides = _effective_sides(neon_registrations)
    upstream_sides = _effective_sides(upstream_registrations)
    neon_sources: dict[str, list[Registration]] = {}
    upstream_sources: dict[str, list[Registration]] = {}
    for registration in neon_registrations:
        neon_sources.setdefault(registration.name, []).append(registration)
    for registration in upstream_registrations:
        upstream_sources.setdefault(registration.name, []).append(registration)

    documentation_by_name: dict[str, list[dict]] = {}
    if semantic_snapshot:
        for entry in semantic_snapshot.get("functions", []):
            documentation_by_name.setdefault(entry["name"], []).append(entry)
    function_names = set(neon_sides) | set(upstream_sides) | set(documentation_by_name)
    symbols = [
        _function_symbol(
            name,
            neon_sides,
            upstream_sides,
            neon_sources,
            upstream_sources,
            documentation_by_name.get(name, []),
            semantic_snapshot,
            neon,
            upstream,
        )
        for name in function_names
    ]
    event_docs: dict[str, dict] = {}
    if semantic_snapshot:
        event_docs = {entry["name"]: entry for entry in semantic_snapshot.get("events", [])}
    neon_events_by_name = _group_by_name(neon_events)
    upstream_events_by_name = _group_by_name(upstream_events)
    for name in set(neon_events_by_name) | set(upstream_events_by_name) | set(event_docs):
        symbols.append(
            _event_symbol(
                name,
                neon_events_by_name.get(name, []),
                upstream_events_by_name.get(name, []),
                event_docs.get(name),
                semantic_snapshot,
                neon,
                upstream,
            )
        )
    symbols.extend(_oop_symbols(neon_oop, upstream_oop, neon, upstream))
    symbols.extend(_enum_symbols(neon_enums, upstream_enums, neon, upstream))
    if semantic_snapshot:
        elements = [_document_symbol("element", entry, semantic_snapshot) for entry in semantic_snapshot.get("elements", [])]
        element_enum = next((item for item in neon_enums if item.name == "element-type"), None)
        element_sources = [element_enum] if element_enum else []
        element_values = set(element_enum.values) if element_enum else set()
        for symbol in elements:
            runtime_name = symbol["name"].casefold()
            if runtime_name in {value.casefold() for value in element_values}:
                symbol["state"] = "verified"
                symbol["evidence"] = ["documented", "source-inspected"]
                symbol["runtimeType"] = next(value for value in element_values if value.casefold() == runtime_name)
                symbol["sources"] = _registration_sources(element_sources)
                symbol["provenance"] = sorted(
                    symbol["provenance"] + _registration_provenance(element_sources, neon, NEON_REPOSITORY),
                    key=lambda item: (item["repository"], item["revision"], item["path"], item["license"]),
                )
            symbols.append(symbol)
        symbols.extend(_document_symbol("type", entry, semantic_snapshot) for entry in semantic_snapshot.get("types", []))
    symbols.sort(key=lambda symbol: (symbol["name"].casefold(), symbol["name"], symbol["kind"]))

    empty_digest = "0" * 64
    if semantic_snapshot:
        upstream_wiki = {
            **semantic_snapshot["sources"]["upstreamWiki"],
            "snapshotDigest": semantic_snapshot["digest"],
            "imported": True,
        }
        neon_wiki = {
            **semantic_snapshot["sources"]["neonWiki"],
            "snapshotDigest": semantic_snapshot["digest"],
            "imported": True,
        }
    else:
        upstream_wiki = {
            "repository": "https://github.com/multitheftauto/wiki.multitheftauto.com.git",
            "revision": wiki_revision,
            "license": "GFDL-1.3-or-later",
            "snapshotDigest": empty_digest,
            "imported": False,
        }
        neon_wiki = {
            "repository": "https://github.com/Dryxio/wiki.mtasa-neon.com.git",
            "revision": neon.revision,
            "license": "GFDL-1.3-or-later",
            "snapshotDigest": empty_digest,
            "imported": False,
        }

    kind_counts = {
        kind: sum(symbol["kind"] == kind for symbol in symbols)
        for kind in ("function", "event", "element", "type", "class", "enum")
    }

    return {
        "schemaVersion": CATALOGUE_SCHEMA_VERSION,
        "catalogueVersion": "1.2.0",
        "engine": {"family": "mta-neon", "version": engine_version},
        "sources": {
            "neon": {"repository": NEON_REPOSITORY, "revision": neon.revision, "registrationDigest": neon.digest},
            "upstream": {"repository": UPSTREAM_REPOSITORY, "revision": upstream.revision, "registrationDigest": upstream.digest},
            "upstreamWiki": upstream_wiki,
            "neonWiki": neon_wiki,
        },
        "statistics": {
            **{
                {"class": "classes", "type": "types"}.get(kind, f"{kind}s"): count
                for kind, count in kind_counts.items()
            },
            "documented": sum("documented" in symbol["evidence"] for symbol in symbols),
            "documentedOnly": sum(symbol["state"] == "documented-only" for symbol in symbols),
            "runtimeOnly": sum(symbol["state"] == "runtime-only" for symbol in symbols),
        },
        "symbols": symbols,
    }


def registration_pairs(registrations: Iterable[Registration]) -> set[tuple[str, str]]:
    sides = _effective_sides(registrations)
    return {(name, side) for name, values in sides.items() for side in values}


def catalogue_pairs(catalogue: dict) -> set[tuple[str, str]]:
    return {
        (symbol["name"], side)
        for symbol in catalogue.get("symbols", [])
        if symbol.get("kind") == "function" and symbol.get("sources") and symbol.get("state") != "unavailable"
        for side in symbol.get("sides", [])
    }


def catalogue_divergence(catalogue: dict, snapshot: SourceSnapshot) -> tuple[list[tuple[str, str]], list[tuple[str, str]]]:
    registered = registration_pairs(extract_registrations(snapshot))
    catalogued = catalogue_pairs(catalogue)
    return sorted(registered - catalogued), sorted(catalogued - registered)


def event_pairs(registrations: Iterable[EventRegistration]) -> set[tuple[str, str]]:
    return {
        (registration.name, side)
        for registration in registrations
        for side in _expanded_side(registration.side)
    }


def catalogue_event_pairs(catalogue: dict) -> set[tuple[str, str]]:
    return {
        (symbol["name"], side)
        for symbol in catalogue.get("symbols", [])
        if symbol.get("kind") == "event" and symbol.get("sources") and symbol.get("state") != "unavailable"
        for side in symbol.get("sides", [])
    }


def catalogue_event_divergence(catalogue: dict, snapshot: SourceSnapshot) -> tuple[list[tuple[str, str]], list[tuple[str, str]]]:
    registered = event_pairs(extract_event_registrations(snapshot))
    catalogued = catalogue_event_pairs(catalogue)
    return sorted(registered - catalogued), sorted(catalogued - registered)


def catalogue_runtime_inventory_issues(catalogue: dict, snapshot: SourceSnapshot) -> list[str]:
    classes, methods, properties = extract_oop_registrations(snapshot)
    enums = extract_enum_registrations(snapshot)
    events = extract_event_registrations(snapshot)
    expected = {
        "event": {
            (item.name, item.side, item.arguments, item.allow_remote_trigger, item.path, item.line) for item in events
        },
        "class": {(item.name, item.side, item.parent, item.static, item.path, item.line) for item in classes},
        "method": {
            (item.class_name, item.name, item.side, item.global_function, item.native_function, item.path, item.line)
            for item in methods
        },
        "property": {
            (
                item.class_name, item.name, item.side, item.setter, item.getter, item.native_setter, item.native_getter,
                item.path, item.line,
            )
            for item in properties
        },
        "enum": {(item.name, item.side, item.cpp_type, item.values, item.path, item.line) for item in enums},
    }
    actual: dict[str, set[tuple]] = {key: set() for key in expected}
    for symbol in catalogue.get("symbols", []):
        if symbol.get("kind") == "event":
            for definition in symbol.get("eventDefinitions", []):
                source = definition["source"]
                actual["event"].add((
                    symbol["name"], definition["side"], tuple(definition["arguments"]), definition["allowRemoteTrigger"],
                    source["path"], source["line"],
                ))
        elif symbol.get("kind") == "class":
            for definition in symbol.get("definitions", []):
                source = definition["source"]
                actual["class"].add((
                    symbol["name"], definition["side"], definition.get("parent", ""), definition.get("static", False),
                    source["path"], source["line"],
                ))
            for field, inventory_kind in (("methods", "method"), ("properties", "property")):
                for member in symbol.get(field, []):
                    for binding in member.get("bindings", []):
                        source = binding["source"]
                        if inventory_kind == "method":
                            actual[inventory_kind].add((
                                symbol["name"], member["name"], binding["side"], binding.get("globalFunction", ""),
                                binding.get("nativeFunction", ""), source["path"], source["line"],
                            ))
                        else:
                            actual[inventory_kind].add((
                                symbol["name"], member["name"], binding["side"], binding.get("setter", ""),
                                binding.get("getter", ""), binding.get("nativeSetter", ""), binding.get("nativeGetter", ""),
                                source["path"], source["line"],
                            ))
        elif symbol.get("kind") == "enum":
            for definition in symbol.get("definitions", []):
                source = definition["source"]
                actual["enum"].add((
                    symbol["name"], definition["side"], definition["cppType"], tuple(definition["values"]),
                    source["path"], source["line"],
                ))
    issues = []
    for kind in ("event", "class", "method", "property", "enum"):
        missing = expected[kind] - actual[kind]
        extra = actual[kind] - expected[kind]
        if missing:
            issues.append(f"{len(missing)} registered {kind} records are absent from the catalogue")
        if extra:
            issues.append(f"{len(extra)} catalogued {kind} records have no source registration")
    return issues


def catalogue_source_matches(catalogue: dict, snapshot: SourceSnapshot) -> bool:
    return catalogue.get("sources", {}).get("neon", {}).get("registrationDigest") == snapshot.digest


def catalogue_semantic_issues(catalogue: dict) -> list[str]:
    issues: list[str] = []
    symbols = catalogue.get("symbols", [])
    expected_order = sorted(symbols, key=lambda symbol: (symbol.get("name", "").casefold(), symbol.get("name", ""), symbol.get("kind", "")))
    if symbols != expected_order:
        issues.append("symbols are not in deterministic name order")
    seen_ids: set[str] = set()
    seen_names: set[tuple[str, str]] = set()
    for symbol in symbols:
        identifier = symbol.get("id", "")
        name = symbol.get("name", "")
        if identifier in seen_ids:
            issues.append(f"duplicate symbol id: {identifier}")
        seen_ids.add(identifier)
        name_key = (symbol.get("kind", ""), name)
        if name_key in seen_names:
            issues.append(f"duplicate {name_key[0]} name: {name}")
        seen_names.add(name_key)
        expected_id = f"{symbol.get('origin')}:{symbol.get('kind')}:{name}"
        if identifier != expected_id:
            issues.append(f"symbol id {identifier} does not match {expected_id}")
        sides = symbol.get("sides", [])
        inherited = symbol.get("inheritedSides", [])
        profiles = symbol.get("profiles", [])
        if sides != sorted(set(sides)):
            issues.append(f"symbol {name} sides are not sorted and unique")
        if inherited != sorted(set(inherited)):
            issues.append(f"symbol {name} inheritedSides are not sorted and unique")
        if profiles != sorted(set(profiles)):
            issues.append(f"symbol {name} profiles are not sorted and unique")
        if symbol.get("state") == "unavailable" and sides:
            issues.append(f"unavailable symbol {name} has active sides")
        if symbol.get("state") != "unavailable" and symbol.get("kind") in ("function", "event", "class", "enum") and not sides and not inherited:
            issues.append(f"active symbol {name} has no profile sides")
        expected_profiles: set[str] = set()
        if symbol.get("kind") in ("element", "type"):
            expected_profiles.update(("mta-upstream", "neon-client", "neon-server", "neon-pair", "neon-multiclient"))
        else:
            if inherited:
                expected_profiles.add("mta-upstream")
            if "client" in sides:
                expected_profiles.update(("neon-client", "neon-pair", "neon-multiclient"))
            if "server" in sides:
                expected_profiles.update(("neon-server", "neon-pair", "neon-multiclient"))
        if profiles != sorted(expected_profiles):
            issues.append(f"symbol {name} profiles do not match its side availability")
        if symbol.get("origin") == "mta" and symbol.get("kind") in ("function", "event", "class", "enum") and not inherited:
            issues.append(f"MTA symbol {name} has no inherited side")
        if symbol.get("origin") == "neon" and symbol.get("kind") in ("function", "event", "class", "enum") and inherited:
            issues.append(f"Neon symbol {name} unexpectedly has inherited sides")
        sources = symbol.get("sources", [])
        expected_sources = sorted(sources, key=lambda source: (source.get("path", ""), source.get("line", 0), source.get("side", "")))
        if sources != expected_sources:
            issues.append(f"symbol {name} source locations are not deterministic")
        source_markers = {(source.get("path"), source.get("line"), source.get("side")) for source in sources}
        if len(source_markers) != len(sources):
            issues.append(f"symbol {name} has duplicate source locations")
        provenance = symbol.get("provenance", [])
        expected_provenance = sorted(
            provenance,
            key=lambda item: (item.get("repository", ""), item.get("revision", ""), item.get("path", ""), item.get("license", "")),
        )
        if provenance != expected_provenance:
            issues.append(f"symbol {name} provenance is not deterministic")
        markers = {(item.get("repository"), item.get("revision"), item.get("path"), item.get("license")) for item in provenance}
        if len(markers) != len(provenance):
            issues.append(f"symbol {name} has duplicate provenance")
        contracts = symbol.get("contracts", [])
        expected_contracts = sorted(contracts, key=lambda item: (item.get("provider", ""), item.get("side", ""), item.get("sourcePath", "")))
        if contracts != expected_contracts:
            issues.append(f"symbol {name} contracts are not deterministic")
        if symbol.get("kind") == "function" and symbol.get("state") == "documented-only" and symbol.get("sources"):
            issues.append(f"documented-only function {name} unexpectedly has a source registration")
        for field in ("methods", "properties"):
            members = symbol.get(field, [])
            expected_members = sorted(members, key=lambda member: (member.get("name", "").casefold(), member.get("name", "")))
            if members != expected_members:
                issues.append(f"symbol {name} {field} are not deterministic")
            member_names = [member.get("name", "") for member in members]
            if len(member_names) != len(set(member_names)):
                issues.append(f"symbol {name} has duplicate {field}")
            for member in members:
                for field_name in (
                    "sides", "inheritedSides", "globalFunctions", "inheritedGlobalFunctions",
                    "setters", "getters", "inheritedSetters", "inheritedGetters",
                ):
                    values = member.get(field_name, [])
                    if values != sorted(set(values)):
                        issues.append(f"symbol {name} member {member.get('name')} {field_name} are not sorted and unique")
                member_sources = member.get("sources", [])
                expected_member_sources = sorted(
                    member_sources, key=lambda source: (source.get("path", ""), source.get("line", 0), source.get("side", ""))
                )
                if member_sources != expected_member_sources:
                    issues.append(f"symbol {name} member {member.get('name')} sources are not deterministic")
                member_markers = {(item.get("path"), item.get("line"), item.get("side")) for item in member_sources}
                if len(member_markers) != len(member_sources):
                    issues.append(f"symbol {name} member {member.get('name')} has duplicate sources")
                for binding_field in ("bindings", "inheritedBindings"):
                    bindings = member.get(binding_field, [])
                    expected_bindings = sorted(
                        bindings,
                        key=lambda binding: (
                            binding.get("side", ""), binding.get("source", {}).get("path", ""),
                            binding.get("source", {}).get("line", 0), binding.get("globalFunction", ""),
                            binding.get("nativeFunction", ""), binding.get("setter", ""), binding.get("getter", ""),
                            binding.get("nativeSetter", ""), binding.get("nativeGetter", ""),
                        ),
                    )
                    if bindings != expected_bindings:
                        issues.append(f"symbol {name} member {member.get('name')} {binding_field} are not deterministic")
                    binding_markers = {
                        (
                            binding.get("side"), binding.get("globalFunction"), binding.get("setter"), binding.get("getter"),
                            binding.get("nativeFunction"), binding.get("nativeSetter"), binding.get("nativeGetter"),
                            binding.get("source", {}).get("path"), binding.get("source", {}).get("line"),
                            binding.get("source", {}).get("side"),
                        )
                        for binding in bindings
                    }
                    if len(binding_markers) != len(bindings):
                        issues.append(f"symbol {name} member {member.get('name')} has duplicate {binding_field}")
        for field in (
            "parents", "inheritedParents", "values", "inheritedValues", "cppTypes",
            "registrationArguments", "registrationDifferences",
        ):
            values = symbol.get(field)
            if values is not None and field != "registrationArguments" and values != sorted(set(values)):
                issues.append(f"symbol {name} {field} are not sorted and unique")
        required_by_kind = {
            "event": (
                "registrationArguments", "registrationDifferences", "allowRemoteTrigger",
                "eventDefinitions", "inheritedEventDefinitions",
            ),
            "class": ("parents", "inheritedParents", "static", "inheritedStatic", "definitions", "inheritedDefinitions", "methods", "properties"),
            "enum": ("values", "inheritedValues", "cppTypes", "definitions", "inheritedDefinitions"),
        }
        for field in required_by_kind.get(symbol.get("kind"), ()):
            if field not in symbol:
                issues.append(f"symbol {name} kind {symbol.get('kind')} is missing {field}")
        for definitions_field in ("definitions", "inheritedDefinitions"):
            definitions = symbol.get(definitions_field, [])
            expected_definitions = sorted(
                definitions,
                key=lambda definition: (
                    definition.get("side", ""), definition.get("source", {}).get("path", ""),
                    definition.get("source", {}).get("line", 0), definition.get("cppType", ""),
                    definition.get("parent", ""), definition.get("static", False), tuple(definition.get("values", [])),
                ),
            )
            if definitions != expected_definitions:
                issues.append(f"symbol {name} {definitions_field} are not deterministic")
            definition_markers = {
                (
                    definition.get("side"), definition.get("cppType"), definition.get("parent"), definition.get("static"),
                    tuple(definition.get("values", [])), definition.get("source", {}).get("path"),
                    definition.get("source", {}).get("line"), definition.get("source", {}).get("side"),
                )
                for definition in definitions
            }
            if len(definition_markers) != len(definitions):
                issues.append(f"symbol {name} has duplicate {definitions_field}")
        for definitions_field in ("eventDefinitions", "inheritedEventDefinitions"):
            definitions = symbol.get(definitions_field, [])
            expected_definitions = sorted(
                definitions,
                key=lambda definition: (
                    definition.get("side", ""), definition.get("source", {}).get("path", ""),
                    definition.get("source", {}).get("line", 0), tuple(definition.get("arguments", [])),
                    definition.get("allowRemoteTrigger", False),
                ),
            )
            if definitions != expected_definitions:
                issues.append(f"symbol {name} {definitions_field} are not deterministic")
    statistics = catalogue.get("statistics", {})
    expected_statistics = {
        "functions": sum(symbol.get("kind") == "function" for symbol in symbols),
        "events": sum(symbol.get("kind") == "event" for symbol in symbols),
        "elements": sum(symbol.get("kind") == "element" for symbol in symbols),
        "types": sum(symbol.get("kind") == "type" for symbol in symbols),
        "classes": sum(symbol.get("kind") == "class" for symbol in symbols),
        "enums": sum(symbol.get("kind") == "enum" for symbol in symbols),
        "documented": sum("documented" in symbol.get("evidence", []) for symbol in symbols),
        "documentedOnly": sum(symbol.get("state") == "documented-only" for symbol in symbols),
        "runtimeOnly": sum(symbol.get("state") == "runtime-only" for symbol in symbols),
    }
    if statistics != expected_statistics:
        issues.append("catalogue statistics do not match symbols")
    return sorted(set(issues))


def semantic_snapshot_issues(snapshot: dict) -> list[str]:
    issues: list[str] = []
    for collection in ("functions", "events", "elements", "types"):
        entries = snapshot.get(collection, [])
        expected = sorted(entries, key=lambda entry: (entry.get("name", "").casefold(), entry.get("name", "")))
        if entries != expected:
            issues.append(f"semantic snapshot {collection} are not in deterministic name order")
        seen: set[tuple[str, str]] = set()
        for entry in entries:
            marker = (entry.get("provider", ""), entry.get("name", ""))
            if marker in seen:
                issues.append(f"duplicate semantic {collection} entry: {marker[0]}:{marker[1]}")
            seen.add(marker)
    entities = {key: snapshot.get(key, []) for key in ("elements", "events", "functions", "types")}
    payload = json.dumps(entities, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    digest = hashlib.sha256(payload).hexdigest()
    if snapshot.get("digest") != digest:
        issues.append("semantic snapshot digest does not match its normalized entities")
    return sorted(set(issues))
