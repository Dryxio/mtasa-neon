from __future__ import annotations

import hashlib
import io
import json
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
NEON_REPOSITORY = "https://github.com/Dryxio/mtasa-neon.git"
UPSTREAM_REPOSITORY = "https://github.com/multitheftauto/mtasa-blue.git"
SOURCE_LICENSE = "GPL-3.0-or-later"
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
    if semantic_snapshot:
        for kind, collection in (("event", "events"), ("element", "elements"), ("type", "types")):
            symbols.extend(_document_symbol(kind, entry, semantic_snapshot) for entry in semantic_snapshot.get(collection, []))
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

    kind_counts = {kind: sum(symbol["kind"] == kind for symbol in symbols) for kind in ("function", "event", "element", "type")}

    return {
        "schemaVersion": SCHEMA_VERSION,
        "catalogueVersion": "1.1.0",
        "engine": {"family": "mta-neon", "version": engine_version},
        "sources": {
            "neon": {"repository": NEON_REPOSITORY, "revision": neon.revision, "registrationDigest": neon.digest},
            "upstream": {"repository": UPSTREAM_REPOSITORY, "revision": upstream.revision, "registrationDigest": upstream.digest},
            "upstreamWiki": upstream_wiki,
            "neonWiki": neon_wiki,
        },
        "statistics": {
            **{f"{kind}s" if kind != "type" else "types": count for kind, count in kind_counts.items()},
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
        if symbol.get("state") != "unavailable" and symbol.get("kind") in ("function", "event") and not sides and not inherited:
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
        if symbol.get("origin") == "mta" and symbol.get("kind") in ("function", "event") and not inherited:
            issues.append(f"MTA symbol {name} has no inherited side")
        if symbol.get("origin") == "neon" and symbol.get("kind") == "function" and inherited:
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
    statistics = catalogue.get("statistics", {})
    expected_statistics = {
        "functions": sum(symbol.get("kind") == "function" for symbol in symbols),
        "events": sum(symbol.get("kind") == "event" for symbol in symbols),
        "elements": sum(symbol.get("kind") == "element" for symbol in symbols),
        "types": sum(symbol.get("kind") == "type" for symbol in symbols),
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
