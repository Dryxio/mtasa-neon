#!/usr/bin/env python3
from __future__ import annotations

import argparse
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOL_DIRECTORY = Path(__file__).resolve().parent
REPOSITORY_ROOT = TOOL_DIRECTORY.parents[1]
sys.path.insert(0, str(TOOL_DIRECTORY))

from neonlib.catalogue import (  # noqa: E402
    build_catalogue,
    catalogue_divergence,
    catalogue_event_divergence,
    catalogue_runtime_inventory_issues,
    catalogue_semantic_issues,
    catalogue_source_matches,
    filesystem_snapshot,
    git_snapshot,
    semantic_snapshot_issues,
)
from neonlib.components import manifest_semantic_issues  # noqa: E402
from neonlib.jsonio import JsonDocumentError, canonical_json, load_json, write_json  # noqa: E402
from neonlib.luals import generate_luals  # noqa: E402
from neonlib.project import check_project, resolve_project_components  # noqa: E402
from neonlib.schema import SchemaStore  # noqa: E402


SCHEMA_STORE = SchemaStore(TOOL_DIRECTORY / "schemas")
DEFAULT_CATALOGUE = TOOL_DIRECTORY / "neon-api.json"
DEFAULT_PROJECT = REPOSITORY_ROOT / "neon.project.json"
DEFAULT_SEMANTICS = TOOL_DIRECTORY / "snapshots" / "api-semantics.json"


def _emit(document: dict, as_json: bool) -> None:
    if as_json:
        sys.stdout.write(canonical_json(document))
        return
    status = document.get("status", "ok").upper()
    print(f"neon: {status}")
    for diagnostic in document.get("diagnostics", []):
        location = diagnostic.get("path", "")
        if "line" in diagnostic:
            location += f":{diagnostic['line']}"
        print(f"{diagnostic['severity']}: {diagnostic['code']}: {location}: {diagnostic['message']}")


def _failure(command: str, code: str, message: str) -> dict:
    return {
        "schemaVersion": "1.0.0",
        "command": command,
        "status": "fail",
        "summary": {"errors": 1, "warnings": 0},
        "diagnostics": [{"code": code, "severity": "error", "message": message, "path": "."}],
    }


def _load_valid_catalogue(path: str, command: str) -> tuple[dict | None, dict | None]:
    try:
        catalogue = load_json(Path(path).resolve())
    except JsonDocumentError as exc:
        return None, _failure(command, "CATALOGUE_JSON_INVALID", str(exc))
    issues = SCHEMA_STORE.validate("neon-api", catalogue)
    semantic_issues = catalogue_semantic_issues(catalogue)
    if issues or semantic_issues:
        details = [f"{issue.pointer}: {issue.message}" for issue in issues] + semantic_issues
        return None, _failure(command, "CATALOGUE_SCHEMA_INVALID", "; ".join(details))
    return catalogue, None


def _api_result(command: str, symbols: list[dict], total: int | None = None) -> dict:
    return {
        "schemaVersion": "1.0.0",
        "command": command,
        "status": "pass",
        "summary": {"errors": 0, "warnings": 0, "matches": len(symbols), "total": len(symbols) if total is None else total},
        "diagnostics": [],
        "symbols": symbols,
    }


def _emit_api(result: dict, as_json: bool) -> None:
    if as_json:
        sys.stdout.write(canonical_json(result))
        return
    if result.get("status") != "pass":
        _emit(result, False)
        return
    for symbol in result.get("symbols", []):
        sides = ", ".join(symbol.get("sides", [])) or "not side-specific"
        print(f"{symbol['name']} [{symbol['kind']}; {sides}; {symbol['state']}]")
        contracts = symbol.get("contracts", [])
        for contract in contracts:
            if contract.get("signature"):
                print(f"  {contract['provider']}/{contract['side']}: {contract['signature']}")
        description = symbol.get("description")
        if description:
            print(f"  {' '.join(description.splitlines())}")
    summary = result.get("summary", {})
    if summary.get("total", 0) > summary.get("matches", 0):
        print(f"showing {summary['matches']} of {summary['total']} matches")


def _matches_filters(symbol: dict, args: argparse.Namespace) -> bool:
    if args.kind and symbol.get("kind") != args.kind:
        return False
    if args.origin and symbol.get("origin") != args.origin:
        return False
    if args.state and symbol.get("state") != args.state:
        return False
    if args.profile and args.profile not in symbol.get("profiles", []):
        return False
    if args.side:
        sides = symbol.get("inheritedSides", []) if args.profile == "mta-upstream" else symbol.get("sides", [])
        if args.side not in sides:
            return False
    return True


def command_api_search(args: argparse.Namespace) -> int:
    catalogue, failure = _load_valid_catalogue(args.catalogue, "api.search")
    if failure:
        _emit_api(failure, args.json)
        return 1
    query = args.query.casefold().strip()
    if not query:
        result = _failure("api.search", "API_QUERY_EMPTY", "search query must contain at least one non-whitespace character")
        _emit_api(result, args.json)
        return 1
    tokens = query.split()
    ranked = []
    for symbol in catalogue["symbols"]:
        if not _matches_filters(symbol, args):
            continue
        member_terms = [
            value
            for field in ("methods", "properties")
            for member in symbol.get(field, [])
            for value in (
                member.get("name", ""), *member.get("globalFunctions", []), *member.get("inheritedGlobalFunctions", []),
                *member.get("setters", []), *member.get("getters", []),
                *member.get("inheritedSetters", []), *member.get("inheritedGetters", []),
            )
        ]
        haystack = " ".join((
            symbol.get("name", ""), symbol.get("description", ""), symbol.get("category", ""),
            *symbol.get("parents", []), *symbol.get("inheritedParents", []),
            *symbol.get("values", []), *symbol.get("inheritedValues", []), *member_terms,
        )).casefold()
        if all(token in haystack for token in tokens):
            name = symbol["name"].casefold()
            score = 0 if name == query else 1 if name.startswith(query) else 2 if all(token in name for token in tokens) else 3
            ranked.append((score, name, symbol["name"], symbol["kind"], symbol))
    matches = [item[-1] for item in sorted(ranked, key=lambda item: item[:-1])]
    total = len(matches)
    result = _api_result("api.search", matches[: args.limit], total)
    _emit_api(result, args.json)
    return 0


def command_api_get(args: argparse.Namespace) -> int:
    catalogue, failure = _load_valid_catalogue(args.catalogue, "api.get")
    if failure:
        _emit_api(failure, args.json)
        return 1
    exact = [symbol for symbol in catalogue["symbols"] if symbol["name"] == args.name and _matches_filters(symbol, args)]
    if not exact:
        folded = [symbol for symbol in catalogue["symbols"] if symbol["name"].casefold() == args.name.casefold() and _matches_filters(symbol, args)]
        exact = folded
    if not exact:
        result = _failure("api.get", "API_NOT_FOUND", f"no API entity named {args.name} matches the selected filters")
        _emit_api(result, args.json)
        return 1
    result = _api_result("api.get", exact)
    _emit_api(result, args.json)
    return 0


def command_api_stats(args: argparse.Namespace) -> int:
    catalogue, failure = _load_valid_catalogue(args.catalogue, "api.stats")
    if failure:
        _emit_api(failure, args.json)
        return 1
    result = {
        "schemaVersion": "1.0.0",
        "command": "api.stats",
        "status": "pass",
        "summary": {"errors": 0, "warnings": 0, **catalogue["statistics"]},
        "diagnostics": [],
        "sources": catalogue["sources"],
    }
    if args.json:
        sys.stdout.write(canonical_json(result))
    else:
        print(json.dumps(result["summary"], indent=2, sort_keys=True))
    return 0


def command_check(args: argparse.Namespace) -> int:
    default_project = Path.cwd() / "neon.project.json"
    project = Path(args.project).resolve() if args.project else (default_project if default_project.is_file() else DEFAULT_PROJECT)
    catalogue = Path(args.catalogue).resolve() if args.catalogue else None
    result = check_project(project, SCHEMA_STORE, catalogue)
    result_issues = SCHEMA_STORE.validate("neon-check-result", result)
    if result_issues:
        result = _failure("check", "INTERNAL_RESULT_INVALID", "; ".join(f"{issue.pointer}: {issue.message}" for issue in result_issues))
    _emit(result, args.json)
    return 0 if result["status"] == "pass" else 1


def command_project_resolve(args: argparse.Namespace) -> int:
    default_project = Path.cwd() / "neon.project.json"
    project = Path(args.project).resolve() if args.project else (default_project if default_project.is_file() else DEFAULT_PROJECT)
    catalogue = Path(args.catalogue).resolve() if args.catalogue else None
    result = resolve_project_components(project, SCHEMA_STORE, catalogue)
    issues = SCHEMA_STORE.validate("neon-project-api", result)
    if issues:
        result = _failure("project.resolve", "INTERNAL_RESULT_INVALID", "; ".join(f"{issue.pointer}: {issue.message}" for issue in issues))
    if args.json:
        sys.stdout.write(canonical_json(result))
    else:
        _emit(result, False)
        if result.get("components"):
            print(f"{len(result['components'])} components, {len(result['symbols'])} project-local symbols")
    return 0 if result["status"] == "pass" else 1


def command_schema_validate(args: argparse.Namespace) -> int:
    path = Path(args.document).resolve()
    try:
        document = load_json(path)
    except JsonDocumentError as exc:
        result = _failure("schema.validate", "JSON_INVALID", str(exc))
        _emit(result, args.json)
        return 1
    issues = SCHEMA_STORE.validate(args.schema, document)
    diagnostics = [
        {
            "code": "SCHEMA_INVALID",
            "severity": "error",
            "message": issue.message,
            "path": issue.pointer,
        }
        for issue in issues
    ]
    if args.schema == "neon-component" and not issues and isinstance(document, dict):
        diagnostics.extend(
            {"code": issue.code, "severity": "error", "message": issue.message, "path": "/"}
            for issue in manifest_semantic_issues(document)
        )
    result = {
        "schemaVersion": "1.0.0",
        "command": "schema.validate",
        "status": "pass" if not diagnostics else "fail",
        "summary": {"errors": len(diagnostics), "warnings": 0},
        "diagnostics": diagnostics,
    }
    _emit(result, args.json)
    return 0 if not diagnostics else 1


def command_catalogue_build(args: argparse.Namespace) -> int:
    repository = Path(args.repository).resolve()
    try:
        neon = git_snapshot(repository, args.neon_ref)
        upstream = git_snapshot(repository, args.upstream_ref)
        semantics = load_json(Path(args.semantics).resolve())
        semantic_schema_issues = SCHEMA_STORE.validate("neon-semantic-snapshot", semantics)
        semantic_issues = semantic_snapshot_issues(semantics)
        if semantic_schema_issues or semantic_issues:
            details = "; ".join(
                [f"{issue.pointer}: {issue.message}" for issue in semantic_schema_issues] + semantic_issues
            )
            raise ValueError(f"semantic snapshot is invalid: {details}")
        catalogue = build_catalogue(
            neon,
            upstream,
            engine_version=args.engine_version,
            wiki_revision=args.wiki_revision,
            semantic_snapshot=semantics,
        )
    except (JsonDocumentError, ValueError) as exc:
        result = _failure("catalogue.build", "SOURCE_SNAPSHOT_FAILED", str(exc))
        _emit(result, args.json)
        return 1
    issues = SCHEMA_STORE.validate("neon-api", catalogue)
    semantic_issues = catalogue_semantic_issues(catalogue)
    if issues or semantic_issues:
        details = [f"{issue.pointer}: {issue.message}" for issue in issues] + semantic_issues
        result = _failure("catalogue.build", "CATALOGUE_SCHEMA_INVALID", "; ".join(details))
        _emit(result, args.json)
        return 1
    output = Path(args.output).resolve()
    write_json(output, catalogue)
    result = {
        "schemaVersion": "1.0.0",
        "command": "catalogue.build",
        "status": "pass",
        "summary": {"errors": 0, "warnings": 0, "symbols": len(catalogue["symbols"])},
        "diagnostics": [],
        "output": str(output),
    }
    _emit(result, args.json)
    return 0


def command_catalogue_import(args: argparse.Namespace) -> int:
    importer = TOOL_DIRECTORY / "importer" / "import-sources.mjs"
    dependencies = TOOL_DIRECTORY / "importer" / "node_modules"
    if not dependencies.is_dir():
        result = _failure(
            "catalogue.import",
            "IMPORTER_DEPENDENCIES_MISSING",
            f"run npm ci --ignore-scripts in {TOOL_DIRECTORY / 'importer'} before refreshing the semantic snapshot",
        )
        _emit(result, args.json)
        return 1
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_handle = tempfile.NamedTemporaryFile(prefix=f".{output.name}.", suffix=".tmp", dir=output.parent, delete=False)
    temporary_handle.close()
    temporary = Path(temporary_handle.name)
    command = [
        args.node,
        str(importer),
        "--upstream-wiki", str(Path(args.upstream_wiki).resolve()),
        "--upstream-revision", args.upstream_revision,
        "--neon-wiki", str(Path(args.neon_wiki).resolve()),
        "--neon-revision", args.neon_revision,
        "--output", str(temporary),
    ]
    try:
        completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    except OSError as exc:
        temporary.unlink(missing_ok=True)
        result = _failure("catalogue.import", "SEMANTIC_IMPORT_FAILED", str(exc))
        _emit(result, args.json)
        return 1
    if completed.returncode:
        temporary.unlink(missing_ok=True)
        result = _failure("catalogue.import", "SEMANTIC_IMPORT_FAILED", completed.stderr.strip() or "semantic importer failed")
        _emit(result, args.json)
        return 1
    try:
        snapshot = load_json(temporary)
    except JsonDocumentError as exc:
        temporary.unlink(missing_ok=True)
        result = _failure("catalogue.import", "SEMANTIC_IMPORT_INVALID", str(exc))
        _emit(result, args.json)
        return 1
    issues = SCHEMA_STORE.validate("neon-semantic-snapshot", snapshot)
    semantic_issues = semantic_snapshot_issues(snapshot)
    if issues or semantic_issues:
        temporary.unlink(missing_ok=True)
        details = [f"{issue.pointer}: {issue.message}" for issue in issues] + semantic_issues
        result = _failure("catalogue.import", "SEMANTIC_IMPORT_INVALID", "; ".join(details))
        _emit(result, args.json)
        return 1
    temporary.replace(output)
    result = {
        "schemaVersion": "1.0.0",
        "command": "catalogue.import",
        "status": "pass",
        "summary": {
            "errors": 0,
            "warnings": 0,
            "functions": len(snapshot["functions"]),
            "events": len(snapshot["events"]),
            "elements": len(snapshot["elements"]),
            "types": len(snapshot["types"]),
        },
        "diagnostics": [],
        "output": str(output),
        "digest": snapshot["digest"],
    }
    _emit(result, args.json)
    return 0


def command_catalogue_verify(args: argparse.Namespace) -> int:
    repository = Path(args.repository).resolve()
    try:
        catalogue = load_json(Path(args.catalogue).resolve())
        snapshot = git_snapshot(repository, args.source_ref) if args.source_ref else filesystem_snapshot(repository)
        semantics = load_json(Path(args.semantics).resolve())
    except (JsonDocumentError, ValueError) as exc:
        result = _failure("catalogue.verify", "CATALOGUE_VERIFY_FAILED", str(exc))
        _emit(result, args.json)
        return 1
    issues = SCHEMA_STORE.validate("neon-api", catalogue)
    semantic_issues = catalogue_semantic_issues(catalogue)
    if issues or semantic_issues:
        details = [f"{issue.pointer}: {issue.message}" for issue in issues] + semantic_issues
        result = _failure("catalogue.verify", "CATALOGUE_SCHEMA_INVALID", "; ".join(details))
        _emit(result, args.json)
        return 1
    unregistered, missing = catalogue_divergence(catalogue, snapshot)
    unregistered_events, missing_events = catalogue_event_divergence(catalogue, snapshot)
    runtime_inventory_issues = catalogue_runtime_inventory_issues(catalogue, snapshot)
    diagnostics = []
    semantic_schema_issues = SCHEMA_STORE.validate("neon-semantic-snapshot", semantics)
    for issue in semantic_schema_issues:
        diagnostics.append({"code": "SEMANTIC_SNAPSHOT_INVALID", "severity": "error", "message": f"{issue.pointer}: {issue.message}", "path": str(args.semantics)})
    for issue in semantic_snapshot_issues(semantics):
        diagnostics.append({"code": "SEMANTIC_SNAPSHOT_INVALID", "severity": "error", "message": issue, "path": str(args.semantics)})
    for key in ("upstreamWiki", "neonWiki"):
        catalogue_source = catalogue.get("sources", {}).get(key, {})
        snapshot_source = semantics.get("sources", {}).get(key, {})
        if catalogue_source.get("revision") != snapshot_source.get("revision") or catalogue_source.get("snapshotDigest") != semantics.get("digest"):
            diagnostics.append({
                "code": "SEMANTIC_SNAPSHOT_DRIFT",
                "severity": "error",
                "message": f"catalogue {key} provenance does not match the semantic snapshot",
                "path": str(args.semantics),
            })
    if not catalogue_source_matches(catalogue, snapshot):
        diagnostics.append(
            {
                "code": "SOURCE_SNAPSHOT_DRIFT",
                "severity": "error",
                "message": "catalogue source digest does not match the selected source snapshot",
                "path": ".",
            }
        )
    for name, side in unregistered:
        diagnostics.append(
            {"code": "REGISTRATION_UNCATALOGUED", "severity": "error", "message": f"registered {side} function is absent from catalogue", "path": ".", "side": side, "symbol": name}
        )
    for name, side in missing:
        diagnostics.append(
            {"code": "API_REGISTRATION_MISSING", "severity": "error", "message": f"catalogued {side} function has no source registration", "path": ".", "side": side, "symbol": name}
        )
    for name, side in unregistered_events:
        diagnostics.append(
            {"code": "EVENT_REGISTRATION_UNCATALOGUED", "severity": "error", "message": f"registered {side} event is absent from catalogue", "path": ".", "side": side, "symbol": name}
        )
    for name, side in missing_events:
        diagnostics.append(
            {"code": "EVENT_REGISTRATION_MISSING", "severity": "error", "message": f"catalogued {side} event has no source registration", "path": ".", "side": side, "symbol": name}
        )
    for issue in runtime_inventory_issues:
        diagnostics.append(
            {"code": "RUNTIME_INVENTORY_DIVERGENCE", "severity": "error", "message": issue, "path": "."}
        )
    diagnostics.sort(key=lambda value: (value["code"], value.get("side", ""), value.get("symbol", "")))
    result = {
        "schemaVersion": "1.0.0",
        "command": "catalogue.verify",
        "status": "pass" if not diagnostics else "fail",
        "summary": {
            "errors": len(diagnostics),
            "warnings": 0,
            "uncatalogued": len(unregistered),
            "missingRegistrations": len(missing),
            "uncataloguedEvents": len(unregistered_events),
            "missingEventRegistrations": len(missing_events),
            "runtimeInventoryDivergence": len(runtime_inventory_issues),
            "sourceDrift": 0 if catalogue_source_matches(catalogue, snapshot) else 1,
            "semanticDrift": sum(item["code"] in ("SEMANTIC_SNAPSHOT_INVALID", "SEMANTIC_SNAPSHOT_DRIFT") for item in diagnostics),
        },
        "diagnostics": diagnostics,
    }
    _emit(result, args.json)
    return 0 if not diagnostics else 1


def command_generate_luals(args: argparse.Namespace) -> int:
    try:
        catalogue = load_json(Path(args.catalogue).resolve())
    except JsonDocumentError as exc:
        result = _failure("generate.luals", "CATALOGUE_JSON_INVALID", str(exc))
        _emit(result, args.json)
        return 1
    issues = SCHEMA_STORE.validate("neon-api", catalogue)
    semantic_issues = catalogue_semantic_issues(catalogue)
    if issues or semantic_issues:
        details = [f"{issue.pointer}: {issue.message}" for issue in issues] + semantic_issues
        result = _failure("generate.luals", "CATALOGUE_SCHEMA_INVALID", "; ".join(details))
        _emit(result, args.json)
        return 1
    output = Path(args.output).resolve()
    artifacts = generate_luals(catalogue, output)
    result = {
        "schemaVersion": "1.0.0",
        "command": "generate.luals",
        "status": "pass",
        "summary": {"errors": 0, "warnings": 0, "artifacts": len(artifacts["artifacts"])},
        "diagnostics": [],
        "output": str(output),
    }
    _emit(result, args.json)
    return 0


def command_harness(args: argparse.Namespace) -> int:
    suite = unittest.defaultTestLoader.discover(str(TOOL_DIRECTORY / "tests"))
    captured = io.StringIO()
    outcome = unittest.TextTestRunner(stream=captured, verbosity=2).run(suite)
    diagnostics = []
    for test, traceback in sorted(outcome.failures + outcome.errors, key=lambda item: item[0].id()):
        final_line = next((line.strip() for line in reversed(traceback.splitlines()) if line.strip()), "test failed")
        diagnostics.append(
            {
                "code": "HARNESS_TEST_FAILED",
                "severity": "error",
                "message": final_line,
                "path": test.id(),
            }
        )
    result = {
        "schemaVersion": "1.0.0",
        "command": "harness",
        "status": "pass" if outcome.wasSuccessful() else "fail",
        "summary": {
            "errors": len(outcome.failures) + len(outcome.errors),
            "warnings": len(outcome.skipped),
            "tests": outcome.testsRun,
            "skipped": len(outcome.skipped),
        },
        "diagnostics": diagnostics,
    }
    _emit(result, args.json)
    if not args.json and captured.getvalue():
        print(captured.getvalue(), end="")
    return 0 if outcome.wasSuccessful() else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="neon", description="MTA Neon agent development contracts")
    subcommands = parser.add_subparsers(dest="command", required=True)

    check = subcommands.add_parser("check", help="validate a project without starting MTA")
    check.add_argument("--project", help="project file; defaults to ./neon.project.json, then the repository project")
    check.add_argument("--catalogue")
    check.add_argument("--json", action="store_true")
    check.set_defaults(handler=command_check)

    harness = subcommands.add_parser("harness", help="run the dependency-free closed test harness")
    harness.add_argument("--json", action="store_true")
    harness.set_defaults(handler=command_harness)

    project = subcommands.add_parser("project", help="resolve project-local resource and module contracts")
    project_subcommands = project.add_subparsers(dest="project_command", required=True)
    resolve = project_subcommands.add_parser("resolve", help="build the deterministic project-local API catalogue")
    resolve.add_argument("--project", help="project file; defaults to ./neon.project.json, then the repository project")
    resolve.add_argument("--catalogue")
    resolve.add_argument("--json", action="store_true")
    resolve.set_defaults(handler=command_project_resolve)

    api = subcommands.add_parser("api", help="search the semantic MTA and Neon API catalogue")
    api_subcommands = api.add_subparsers(dest="api_command", required=True)

    def add_api_filters(command: argparse.ArgumentParser) -> None:
        command.add_argument("--catalogue", default=str(DEFAULT_CATALOGUE))
        command.add_argument("--kind", choices=("function", "event", "element", "type", "class", "enum"))
        command.add_argument("--origin", choices=("mta", "neon"))
        command.add_argument("--state", choices=("verified", "documented-only", "runtime-only", "opaque", "conflict", "unavailable"))
        command.add_argument("--side", choices=("client", "server"))
        command.add_argument("--profile", choices=("mta-upstream", "neon-client", "neon-server", "neon-pair", "neon-multiclient"))
        command.add_argument("--json", action="store_true")

    api_search = api_subcommands.add_parser("search", help="find API entities by name or description")
    api_search.add_argument("query")
    api_search.add_argument("--limit", type=int, default=20, choices=range(1, 101), metavar="1..100")
    add_api_filters(api_search)
    api_search.set_defaults(handler=command_api_search)

    api_get = api_subcommands.add_parser("get", help="show the exact semantic contract for an API entity")
    api_get.add_argument("name")
    add_api_filters(api_get)
    api_get.set_defaults(handler=command_api_get)

    api_stats = api_subcommands.add_parser("stats", help="show catalogue coverage and provenance")
    api_stats.add_argument("--catalogue", default=str(DEFAULT_CATALOGUE))
    api_stats.add_argument("--json", action="store_true")
    api_stats.set_defaults(handler=command_api_stats)

    schema = subcommands.add_parser("schema", help="validate contract documents")
    schema_subcommands = schema.add_subparsers(dest="schema_command", required=True)
    validate = schema_subcommands.add_parser("validate")
    validate.add_argument("--schema", required=True, choices=("neon-api", "neon-semantic-snapshot", "neon-project", "neon-component", "neon-project-api", "neon-test", "neon-assertion", "neon-artifact", "neon-check-result"))
    validate.add_argument("document")
    validate.add_argument("--json", action="store_true")
    validate.set_defaults(handler=command_schema_validate)

    catalogue = subcommands.add_parser("catalogue", help="build or verify the effective MTA API")
    catalogue_subcommands = catalogue.add_subparsers(dest="catalogue_command", required=True)
    import_catalogue = catalogue_subcommands.add_parser("import", help="refresh the pinned MTA and Neon semantic snapshot")
    import_catalogue.add_argument("--upstream-wiki", required=True)
    import_catalogue.add_argument("--upstream-revision", default="39e80f8108fef8de0dfdf61876daf702d583243e")
    import_catalogue.add_argument("--neon-wiki", required=True)
    import_catalogue.add_argument("--neon-revision", required=True)
    import_catalogue.add_argument("--node", default="node")
    import_catalogue.add_argument("--output", default=str(DEFAULT_SEMANTICS))
    import_catalogue.add_argument("--json", action="store_true")
    import_catalogue.set_defaults(handler=command_catalogue_import)
    build = catalogue_subcommands.add_parser("build")
    build.add_argument("--repository", default=str(REPOSITORY_ROOT))
    build.add_argument("--neon-ref", default="HEAD")
    build.add_argument("--upstream-ref", default="upstream/master")
    build.add_argument("--engine-version", default="1.7.0")
    build.add_argument("--wiki-revision", default="39e80f8108fef8de0dfdf61876daf702d583243e")
    build.add_argument("--semantics", default=str(DEFAULT_SEMANTICS))
    build.add_argument("--output", default=str(DEFAULT_CATALOGUE))
    build.add_argument("--json", action="store_true")
    build.set_defaults(handler=command_catalogue_build)
    verify = catalogue_subcommands.add_parser("verify")
    verify.add_argument("--repository", default=str(REPOSITORY_ROOT))
    verify.add_argument("--catalogue", default=str(DEFAULT_CATALOGUE))
    verify.add_argument("--semantics", default=str(DEFAULT_SEMANTICS))
    verify.add_argument("--source-ref", help="Git ref to verify; omit to inspect the working tree")
    verify.add_argument("--json", action="store_true")
    verify.set_defaults(handler=command_catalogue_verify)

    generate = subcommands.add_parser("generate", help="generate deterministic development artefacts")
    generate_subcommands = generate.add_subparsers(dest="generate_command", required=True)
    luals = generate_subcommands.add_parser("luals")
    luals.add_argument("--catalogue", default=str(DEFAULT_CATALOGUE))
    luals.add_argument("--output", default=str(TOOL_DIRECTORY / "generated"))
    luals.add_argument("--json", action="store_true")
    luals.set_defaults(handler=command_generate_luals)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return args.handler(args)
    except KeyboardInterrupt:
        return 130
    except Exception as exc:  # The JSON contract must remain valid even for unexpected failures.
        as_json = bool(getattr(args, "json", False))
        _emit(_failure(args.command, "INTERNAL_ERROR", f"{type(exc).__name__}: {exc}"), as_json)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
