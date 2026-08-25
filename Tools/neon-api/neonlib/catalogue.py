from __future__ import annotations

import hashlib
import io
import re
import subprocess
import tarfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping

from . import SCHEMA_VERSION


SOURCE_PREFIXES = (
    "Client/mods/deathmatch/logic/luadefs",
    "Client/mods/deathmatch/logic/lua/CLuaManager.cpp",
    "Server/mods/deathmatch/logic/luadefs",
    "Shared/mods/deathmatch/logic/luadefs",
)
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
        revision = _git(repository, "rev-parse", "--verify", f"{reference}^{{commit}}").strip()
        archive = subprocess.run(
            ["git", "archive", "--format=tar", revision, *SOURCE_PREFIXES],
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


def build_catalogue(neon: SourceSnapshot, upstream: SourceSnapshot, *, engine_version: str, wiki_revision: str) -> dict:
    neon_registrations = extract_registrations(neon)
    upstream_registrations = extract_registrations(upstream)
    neon_sides = _effective_sides(neon_registrations)
    upstream_sides = _effective_sides(upstream_registrations)
    neon_sources: dict[str, list[Registration]] = {}
    upstream_sources: dict[str, list[Registration]] = {}
    for registration in neon_registrations:
        neon_sources.setdefault(registration.name, []).append(registration)
    for registration in upstream_registrations:
        upstream_sources.setdefault(registration.name, []).append(registration)

    symbols: list[dict] = []
    for name in sorted(set(neon_sides) | set(upstream_sides), key=lambda value: (value.casefold(), value)):
        active_sides = sorted(neon_sides.get(name, set()))
        inherited_sides = sorted(upstream_sides.get(name, set()))
        profiles: list[str] = []
        if inherited_sides:
            profiles.append("mta-upstream")
        if "client" in active_sides:
            profiles.extend(("neon-client", "neon-pair", "neon-multiclient"))
        if "server" in active_sides:
            profiles.extend(("neon-server", "neon-pair", "neon-multiclient"))
        profiles = sorted(set(profiles))
        origin = "mta" if inherited_sides else "neon"
        # A symbol removed by Neon can still be active in the pinned upstream
        # profile. Availability therefore comes from the per-profile side sets,
        # not from collapsing a removal into one global unavailable state.
        state = "opaque"
        source_records = neon_sources.get(name) or upstream_sources.get(name, [])
        sources = [
            {"path": item.path, "line": item.line, "side": item.side}
            for item in sorted(source_records, key=lambda item: (item.path, item.line, item.side))
        ]
        symbols.append(
            {
                "id": f"{origin}:function:{name}",
                "kind": "function",
                "name": name,
                "origin": origin,
                "state": state,
                "sides": active_sides,
                "inheritedSides": inherited_sides,
                "profiles": profiles,
                "restricted": any(item.restricted for item in neon_sources.get(name, [])),
                "parameters": [{"name": "...", "type": "unknown", "optional": True}],
                "returns": [{"type": "unknown"}],
                "evidence": ["source-inspected"],
                "sources": sources,
            }
        )

    return {
        "schemaVersion": SCHEMA_VERSION,
        "catalogueVersion": "1.0.0",
        "engine": {"family": "mta-neon", "version": engine_version},
        "sources": {
            "neon": {"revision": neon.revision, "registrationDigest": neon.digest},
            "upstream": {"revision": upstream.revision, "registrationDigest": upstream.digest},
            "upstreamWiki": {"revision": wiki_revision, "imported": False},
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
        if symbol.get("kind") == "function" and symbol.get("state") != "unavailable"
        for side in symbol.get("sides", [])
    }


def catalogue_divergence(catalogue: dict, snapshot: SourceSnapshot) -> tuple[list[tuple[str, str]], list[tuple[str, str]]]:
    registered = registration_pairs(extract_registrations(snapshot))
    catalogued = catalogue_pairs(catalogue)
    return sorted(registered - catalogued), sorted(catalogued - registered)


def catalogue_source_matches(catalogue: dict, snapshot: SourceSnapshot) -> bool:
    return catalogue.get("sources", {}).get("neon", {}).get("registrationDigest") == snapshot.digest


def catalogue_semantic_issues(catalogue: dict) -> list[str]:
    issues: list[str] = []
    symbols = catalogue.get("symbols", [])
    expected_order = sorted(symbols, key=lambda symbol: (symbol.get("name", "").casefold(), symbol.get("name", "")))
    if symbols != expected_order:
        issues.append("symbols are not in deterministic name order")
    seen_ids: set[str] = set()
    seen_names: set[str] = set()
    for symbol in symbols:
        identifier = symbol.get("id", "")
        name = symbol.get("name", "")
        if identifier in seen_ids:
            issues.append(f"duplicate symbol id: {identifier}")
        seen_ids.add(identifier)
        if name in seen_names:
            issues.append(f"duplicate function name: {name}")
        seen_names.add(name)
        expected_id = f"{symbol.get('origin')}:function:{name}"
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
        if symbol.get("state") != "unavailable" and not sides and not inherited:
            issues.append(f"active symbol {name} has no profile sides")
        expected_profiles: set[str] = set()
        if inherited:
            expected_profiles.add("mta-upstream")
        if "client" in sides:
            expected_profiles.update(("neon-client", "neon-pair", "neon-multiclient"))
        if "server" in sides:
            expected_profiles.update(("neon-server", "neon-pair", "neon-multiclient"))
        if profiles != sorted(expected_profiles):
            issues.append(f"symbol {name} profiles do not match its side availability")
        if symbol.get("origin") == "mta" and not inherited:
            issues.append(f"MTA symbol {name} has no inherited side")
        if symbol.get("origin") == "neon" and inherited:
            issues.append(f"Neon symbol {name} unexpectedly has inherited sides")
        sources = symbol.get("sources", [])
        expected_sources = sorted(sources, key=lambda source: (source.get("path", ""), source.get("line", 0), source.get("side", "")))
        if sources != expected_sources:
            issues.append(f"symbol {name} source locations are not deterministic")
        source_markers = {(source.get("path"), source.get("line"), source.get("side")) for source in sources}
        if len(source_markers) != len(sources):
            issues.append(f"symbol {name} has duplicate source locations")
    return sorted(set(issues))
