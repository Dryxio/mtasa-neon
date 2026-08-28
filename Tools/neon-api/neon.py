#!/usr/bin/env python3
from __future__ import annotations

import argparse
import io
import json
import re
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


if sys.version_info < (3, 10):
    sys.stderr.write("Neon CLI requires Python 3.10 or newer.\n")
    raise SystemExit(2)


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
from neonlib.context import ContextGenerationError, generate_project_context, verify_project_context  # noqa: E402
from neonlib.discovery import search_symbols, tokenize  # noqa: E402
from neonlib.jsonio import JsonDocumentError, canonical_json, load_json, write_json  # noqa: E402
from neonlib.initialize import InitializationError, initialization_failure, initialize_workspace  # noqa: E402
from neonlib.luals import generate_luals  # noqa: E402
from neonlib.mutation import mutation_failure  # noqa: E402
from neonlib.proof import install_runtime_probe, probe_install_failure, proof_failure  # noqa: E402
from neonlib.project import check_project, resolve_project_components  # noqa: E402
from neonlib.portable import distribution_mode, run_portable_self_test  # noqa: E402
from neonlib.scenario import run_scenario, verify_scenario_run  # noqa: E402
from neonlib.schema import SchemaStore  # noqa: E402
from neonlib.supervisor import (  # noqa: E402
    SupervisorMutationOutcomeUnknown,
    request_supervisor,
    run_driver_guardian,
    run_supervisor_daemon,
    runtime_compare_failure,
    start_supervisor,
    supervisor_failure,
)


SCHEMA_STORE = SchemaStore(TOOL_DIRECTORY / "schemas")
DEFAULT_CATALOGUE = TOOL_DIRECTORY / "neon-api.json"
DEFAULT_PROJECT = REPOSITORY_ROOT / "neon.project.json"
DEFAULT_SEMANTICS = TOOL_DIRECTORY / "snapshots" / "api-semantics.json"
CLI_VERSION = "1.0.0"


def _absolute_without_resolving(path: str) -> Path:
    # Output paths must retain their final symlink component so the generator
    # can reject it before writing. Path.resolve() would erase that evidence.
    candidate = Path(path)
    return candidate if candidate.is_absolute() else Path.cwd() / candidate


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
    # Preserve camel-case boundaries for exact API names; tokenization performs
    # Unicode-aware case folding after it separates those boundaries.
    query = args.query.strip()
    if not query:
        result = _failure("api.search", "API_QUERY_EMPTY", "search query must contain at least one non-whitespace character")
        _emit_api(result, args.json)
        return 1
    natural_tokens = tokenize(query)
    side_phrase = re.search(r"\b(client|server)[\s-]+side\b", query, re.IGNORECASE)
    if args.side is None and side_phrase:
        args.side = side_phrase.group(1).casefold()
        query = " ".join(token for token in natural_tokens if token not in {"client", "server", "side"})
    if not tokenize(query, drop_stop_words=True):
        result = _failure("api.search", "API_QUERY_EMPTY", "search query must contain at least one meaningful term")
        _emit_api(result, args.json)
        return 1
    matches = search_symbols((symbol for symbol in catalogue["symbols"] if _matches_filters(symbol, args)), query)
    total = len(matches)
    visible = matches[: args.limit]
    if not args.full:
        visible = [_search_summary(symbol, args) for symbol in visible]
    result = _api_result("api.search", visible, total)
    _emit_api(result, args.json)
    return 0


def _search_summary(symbol: dict, args: argparse.Namespace) -> dict:
    sides = symbol.get("inheritedSides", []) if args.profile == "mta-upstream" else symbol.get("sides", [])
    if args.profile == "neon-client":
        sides = [side for side in sides if side == "client"]
    elif args.profile == "neon-server":
        sides = [side for side in sides if side == "server"]
    result = {
        "id": symbol["id"], "kind": symbol["kind"], "name": symbol["name"],
        "origin": symbol["origin"], "state": symbol["state"], "sides": sorted(set(sides)),
    }
    for field in ("category", "description"):
        if value := symbol.get(field):
            normalized = " ".join(value.replace("\r", "").splitlines())
            result[field] = normalized if len(normalized) <= 320 else normalized[:317].rstrip() + "..."
    return result


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


def command_init(args: argparse.Namespace) -> int:
    try:
        result = initialize_workspace(
            Path(args.workspace), SCHEMA_STORE, DEFAULT_CATALOGUE,
            name=args.name, profile=args.profile, explicit_resources=args.resource,
        )
    except InitializationError as exc:
        result = initialization_failure(exc.code, str(exc), exc.path)
    except (JsonDocumentError, OSError, ValueError) as exc:
        result = initialization_failure("INITIALIZATION_FAILED", str(exc))
    issues = SCHEMA_STORE.validate("neon-init-result", result)
    if issues:
        result = initialization_failure(
            "INTERNAL_RESULT_INVALID",
            "; ".join(f"{issue.pointer}: {issue.message}" for issue in issues),
        )
    _emit(result, args.json)
    return 0 if result["status"] == "pass" else 1


def command_version(args: argparse.Namespace) -> int:
    catalogue = load_json(DEFAULT_CATALOGUE)
    result = {
        "schemaVersion": "1.0.0",
        "command": "version",
        "status": "pass",
        "summary": {"errors": 0, "warnings": 0},
        "diagnostics": [],
        "cliVersion": CLI_VERSION,
        "catalogueVersion": catalogue["catalogueVersion"],
        "engineVersion": catalogue["engine"]["version"],
        "minimumPython": "3.10.0",
        "pythonVersion": ".".join(str(value) for value in sys.version_info[:3]),
        "mode": distribution_mode(TOOL_DIRECTORY),
    }
    if args.json:
        sys.stdout.write(canonical_json(result))
    else:
        print(f"Neon CLI {CLI_VERSION} ({result['mode']}), catalogue {result['catalogueVersion']}, Python {result['pythonVersion']}")
    return 0


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


def command_scenario_run(args: argparse.Namespace) -> int:
    try:
        result = run_scenario(
            Path(args.scenario), [Path(path) for path in args.assertion],
            Path(args.workspace), SCHEMA_STORE, Path(__file__).resolve(),
            Path(args.output) if args.output else None, args.observed_at,
        )
    except (JsonDocumentError, OSError, ValueError) as exc:
        result = _failure("scenario.run", "SCENARIO_INVALID", str(exc))
    _emit(result, args.json)
    return 0 if result["status"] == "pass" else 1


def command_scenario_execute(args: argparse.Namespace) -> int:
    workspace = Path(args.workspace)
    session_path = Path(args.session)
    try:
        status = request_supervisor(workspace, session_path, "status", SCHEMA_STORE)
        if "scenario.execute" not in status["session"]["capabilities"]:
            raise ValueError("scenario.execute is not enabled for this session")
        authorization = request_supervisor(workspace, session_path, "scenario.authorize", SCHEMA_STORE)
        if authorization["status"] != "pass":
            raise ValueError("supervisor rejected scenario execution")

        def execute_runtime(step: dict) -> dict:
            inputs = step["inputs"]
            if set(inputs) != {"resource"} or not isinstance(inputs.get("resource"), str):
                raise ValueError(f"{step['action']} requires exactly one string resource input")
            command = f"{step['action']}/{inputs['resource']}"
            try:
                return request_supervisor(
                    workspace, session_path, command, SCHEMA_STORE, step["timeoutMs"],
                )
            except SupervisorMutationOutcomeUnknown as exc:
                return mutation_failure(
                    step["action"], "MUTATION_OUTCOME_UNKNOWN", str(exc), inputs["resource"],
                    status["session"]["sessionId"],
                )
            except TimeoutError as exc:
                return mutation_failure(
                    step["action"], "SCENARIO_STEP_TIMEOUT", str(exc), inputs["resource"],
                    status["session"]["sessionId"],
                )

        result = run_scenario(
            Path(args.scenario), [Path(path) for path in args.assertion], workspace,
            SCHEMA_STORE, Path(__file__).resolve(), Path(args.output) if args.output else None,
            args.observed_at, execute_runtime, status["session"]["profile"], "scenario.execute",
        )
    except (JsonDocumentError, OSError, ValueError) as exc:
        result = _failure("scenario.execute", "SCENARIO_EXECUTION_FAILED", str(exc))
    _emit(result, args.json)
    return 0 if result["status"] == "pass" else 1


def command_scenario_verify(args: argparse.Namespace) -> int:
    result = verify_scenario_run(Path(args.workspace), Path(args.run), SCHEMA_STORE)
    issues = SCHEMA_STORE.validate("neon-scenario-verify-result", result)
    if issues:
        result = _failure("scenario.verify", "INTERNAL_RESULT_INVALID", "; ".join(f"{issue.pointer}: {issue.message}" for issue in issues))
    _emit(result, args.json)
    return 0 if result["status"] == "pass" else 1


def _emit_validated(result: dict, schema: str, as_json: bool) -> int:
    issues = SCHEMA_STORE.validate(schema, result)
    if issues:
        details = "; ".join(f"{issue.pointer}: {issue.message}" for issue in issues[:16])
        command = result.get("command", "internal")
        if schema == "neon-runtime-compare-result":
            result = runtime_compare_failure("INTERNAL_RESULT_INVALID", details)
        elif schema == "neon-proof-result":
            proof = result.get("proof", {})
            result = proof_failure("INTERNAL_RESULT_INVALID", details, {
                "sessionId": proof.get("sessionId", "session:unavailable"),
                "profile": proof.get("profile", "neon-pair"),
            })
        elif schema == "neon-supervisor-result":
            if command not in {"supervisor.start", "supervisor.status", "supervisor.stop"}:
                command = "supervisor.status"
            result = supervisor_failure(command, "INTERNAL_RESULT_INVALID", details)
        elif schema == "neon-mutation-result":
            result = mutation_failure(command, "INTERNAL_RESULT_INVALID", details)
        else:
            result = _failure(command, "INTERNAL_RESULT_INVALID", details)
    _emit(result, as_json)
    return 0 if result["status"] == "pass" else 1


def command_supervisor_start(args: argparse.Namespace) -> int:
    if not 10 <= args.ttl <= 86400:
        result = supervisor_failure("supervisor.start", "SUPERVISOR_TTL_INVALID", "ttl must be from 10 to 86400 seconds")
        return _emit_validated(result, "neon-supervisor-result", args.json)
    try:
        result = start_supervisor(
            Path(args.workspace), Path(args.project), Path(args.catalogue) if args.catalogue else None,
            Path(args.snapshot), Path(args.output), args.ttl, Path(__file__).resolve(), SCHEMA_STORE,
            tuple(args.enable), Path(args.server_root) if args.server_root else None,
            Path(args.client_root) if args.client_root else None, args.connect_port,
        )
    except (JsonDocumentError, OSError, ValueError) as exc:
        result = supervisor_failure("supervisor.start", "SUPERVISOR_START_FAILED", str(exc))
        return _emit_validated(result, "neon-supervisor-result", args.json)
    return _emit_validated(result, "neon-supervisor-result", args.json)


def command_supervisor_status(args: argparse.Namespace) -> int:
    try:
        result = request_supervisor(Path(args.workspace), Path(args.session), "status", SCHEMA_STORE)
    except (JsonDocumentError, OSError, ValueError) as exc:
        result = supervisor_failure("supervisor.status", "SUPERVISOR_REQUEST_FAILED", str(exc))
        return _emit_validated(result, "neon-supervisor-result", args.json)
    return _emit_validated(result, "neon-supervisor-result", args.json)


def command_supervisor_stop(args: argparse.Namespace) -> int:
    try:
        result = request_supervisor(Path(args.workspace), Path(args.session), "shutdown", SCHEMA_STORE)
    except (JsonDocumentError, OSError, ValueError) as exc:
        result = supervisor_failure("supervisor.stop", "SUPERVISOR_REQUEST_FAILED", str(exc))
        return _emit_validated(result, "neon-supervisor-result", args.json)
    return _emit_validated(result, "neon-supervisor-result", args.json)


def command_runtime_compare(args: argparse.Namespace) -> int:
    try:
        result = request_supervisor(Path(args.workspace), Path(args.session), "runtime.compare", SCHEMA_STORE)
    except (JsonDocumentError, OSError, ValueError) as exc:
        result = runtime_compare_failure("SUPERVISOR_REQUEST_FAILED", str(exc))
        return _emit_validated(result, "neon-runtime-compare-result", args.json)
    return _emit_validated(result, "neon-runtime-compare-result", args.json)


def command_runtime_probe_install(args: argparse.Namespace) -> int:
    try:
        result = install_runtime_probe(Path(args.server_root))
    except (JsonDocumentError, OSError, ValueError) as exc:
        result = probe_install_failure("PROBE_INSTALL_FAILED", str(exc))
        return _emit_validated(result, "neon-probe-install-result", args.json)
    return _emit_validated(result, "neon-probe-install-result", args.json)


def command_runtime_prove(args: argparse.Namespace) -> int:
    if not 1 <= args.timeout_ms <= 600000 or not 10 <= args.poll_ms <= 5000:
        result = proof_failure("PROBE_TIMEOUT_INVALID", "timeout-ms must be 1..600000 and poll-ms must be 10..5000", {
            "sessionId": "session:unavailable", "profile": "neon-pair",
        })
        return _emit_validated(result, "neon-proof-result", args.json)
    deadline = time.monotonic() + args.timeout_ms / 1000
    result: dict | None = None
    try:
        while True:
            remaining_ms = max(1, int((deadline - time.monotonic()) * 1000))
            result = request_supervisor(
                Path(args.workspace), Path(args.session), "runtime.prove", SCHEMA_STORE,
                min(remaining_ms, 10000),
            )
            codes = {item.get("code") for item in result.get("diagnostics", [])}
            if result["status"] == "pass" or "PROBE_NOT_READY" not in codes:
                break
            if time.monotonic() >= deadline:
                proof = result.get("proof", {})
                result = proof_failure("PROBE_TIMEOUT", f"authenticated runtime proof was not ready within {args.timeout_ms} milliseconds", {
                    "sessionId": proof.get("sessionId", "session:unavailable"),
                    "profile": proof.get("profile", "neon-pair"),
                })
                break
            time.sleep(min(args.poll_ms / 1000, max(0, deadline - time.monotonic())))
    except TimeoutError:
        proof = result.get("proof", {}) if isinstance(result, dict) else {}
        result = proof_failure("PROBE_TIMEOUT", f"authenticated runtime proof was not ready within {args.timeout_ms} milliseconds", {
            "sessionId": proof.get("sessionId", "session:unavailable"),
            "profile": proof.get("profile", "neon-pair"),
        })
    except (JsonDocumentError, OSError, ValueError) as exc:
        result = proof_failure("SUPERVISOR_REQUEST_FAILED", str(exc), {
            "sessionId": "session:unavailable", "profile": "neon-pair",
        })
    return _emit_validated(result, "neon-proof-result", args.json)


def command_supervisor_daemon(args: argparse.Namespace) -> int:
    return run_supervisor_daemon(
        Path(args.workspace), Path(args.session_directory), args.session_id, args.project,
        args.catalogue, args.snapshot, args.ttl, SCHEMA_STORE, tuple(args.capability),
        Path(args.server_root) if args.server_root else None,
        Path(args.client_root) if args.client_root else None, args.connect_port, args.test_client_adapter,
    )


def command_driver_guardian(args: argparse.Namespace) -> int:
    return run_driver_guardian(
        Path(args.server_root), args.expected_sha256, args.host, args.port, args.token,
    )


def command_resource_lifecycle(args: argparse.Namespace) -> int:
    command = f"resource.{args.resource_command}/{args.resource}"
    try:
        result = request_supervisor(
            Path(args.workspace), Path(args.session), command, SCHEMA_STORE, args.timeout_ms,
        )
    except SupervisorMutationOutcomeUnknown as exc:
        result = mutation_failure(
            f"resource.{args.resource_command}", "MUTATION_OUTCOME_UNKNOWN", str(exc), args.resource,
            exc.session_id,
        )
    except TimeoutError as exc:
        result = mutation_failure(
            f"resource.{args.resource_command}", "SUPERVISOR_REQUEST_TIMEOUT", str(exc), args.resource,
        )
    except (JsonDocumentError, OSError, ValueError) as exc:
        result = mutation_failure(f"resource.{args.resource_command}", "SUPERVISOR_REQUEST_FAILED", str(exc), args.resource)
    return _emit_validated(result, "neon-mutation-result", args.json)


def command_client_launch(args: argparse.Namespace) -> int:
    command = f"client.launch/{args.role}"
    try:
        result = request_supervisor(
            Path(args.workspace), Path(args.session), command, SCHEMA_STORE, args.timeout_ms,
        )
    except SupervisorMutationOutcomeUnknown as exc:
        result = mutation_failure("client.launch", "MUTATION_OUTCOME_UNKNOWN", str(exc), args.role, exc.session_id)
    except TimeoutError as exc:
        result = mutation_failure("client.launch", "SUPERVISOR_REQUEST_TIMEOUT", str(exc), args.role)
    except (JsonDocumentError, OSError, ValueError) as exc:
        result = mutation_failure("client.launch", "SUPERVISOR_REQUEST_FAILED", str(exc), args.role)
    return _emit_validated(result, "neon-mutation-result", args.json)


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
    artifacts = generate_luals(catalogue, output, args.profile)
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


def command_generate_project(args: argparse.Namespace) -> int:
    default_project = Path.cwd() / "neon.project.json"
    project = _absolute_without_resolving(args.project) if args.project else (default_project if default_project.is_file() else DEFAULT_PROJECT)
    output = _absolute_without_resolving(args.output) if args.output else project.parent / ".neon"
    catalogue = Path(args.catalogue).resolve() if args.catalogue else None
    try:
        context, artifacts = generate_project_context(project, SCHEMA_STORE, output, catalogue)
    except ContextGenerationError as exc:
        result = {**exc.result, "command": "generate.project"}
        _emit(result, args.json)
        return 1
    except (OSError, ValueError) as exc:
        result = _failure("generate.project", "GENERATION_OUTPUT_UNSAFE", str(exc))
        _emit(result, args.json)
        return 1
    issues = SCHEMA_STORE.validate("neon-agent-context", context)
    issues_text = [f"agent-context{issue.pointer}: {issue.message}" for issue in issues]
    try:
        index = load_json(output / "api-index.json")
    except JsonDocumentError as exc:
        issues_text.append(f"api-index.json: {exc}")
    else:
        issues_text.extend(f"api-index{issue.pointer}: {issue.message}" for issue in SCHEMA_STORE.validate("neon-api-index", index))
    for artifact in artifacts["artifacts"]:
        issues_text.extend(f"artifact {artifact['id']}{issue.pointer}: {issue.message}" for issue in SCHEMA_STORE.validate("neon-artifact", artifact))
    if issues_text:
        result = _failure("generate.project", "GENERATED_CONTRACT_INVALID", "; ".join(issues_text))
        _emit(result, args.json)
        return 1
    result = {
        "schemaVersion": "1.0.0",
        "command": "generate.project",
        "status": "pass",
        "summary": {"errors": 0, "warnings": context["validation"]["summary"]["warnings"], "artifacts": len(artifacts["artifacts"]), "files": len(context["files"]), "usedApis": len(context["usedApiIds"])},
        "diagnostics": context["validation"]["diagnostics"],
        "output": str(output),
    }
    _emit(result, args.json)
    return 0


def command_context_verify(args: argparse.Namespace) -> int:
    default_project = Path.cwd() / "neon.project.json"
    project = _absolute_without_resolving(args.project) if args.project else (default_project if default_project.is_file() else DEFAULT_PROJECT)
    context_directory = _absolute_without_resolving(args.context) if args.context else project.parent / ".neon"
    catalogue = Path(args.catalogue).resolve() if args.catalogue else None
    result = verify_project_context(project, SCHEMA_STORE, context_directory, catalogue)
    _emit(result, args.json)
    return 0 if result["status"] == "pass" else 1


def command_harness(args: argparse.Namespace) -> int:
    if distribution_mode(TOOL_DIRECTORY) == "portable":
        result = run_portable_self_test(TOOL_DIRECTORY, SCHEMA_STORE, command="harness")
        _emit(result, args.json)
        return 0 if result["status"] == "pass" else 1
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


def command_self_test(args: argparse.Namespace) -> int:
    result = run_portable_self_test(TOOL_DIRECTORY, SCHEMA_STORE)
    _emit(result, args.json)
    return 0 if result["status"] == "pass" else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="neon", description="MTA Neon agent development contracts")
    subcommands = parser.add_subparsers(dest="command", required=True)

    version = subcommands.add_parser("version", help="show CLI, catalogue, engine, and Python versions")
    version.add_argument("--json", action="store_true")
    version.set_defaults(handler=command_version)

    init = subcommands.add_parser("init", help="discover resources and create a ready agent workspace without overwriting files")
    init.add_argument("--workspace", default=".")
    init.add_argument("--name")
    init.add_argument("--profile", default="neon-pair", choices=("mta-upstream", "neon-client", "neon-server", "neon-pair", "neon-multiclient"))
    init.add_argument("--resource", action="append", help="workspace-relative resource directory; repeat to disable automatic discovery")
    init.add_argument("--json", action="store_true")
    init.set_defaults(handler=command_init)

    check = subcommands.add_parser("check", help="validate a project without starting MTA")
    check.add_argument("--project", help="project file; defaults to ./neon.project.json, then the repository project")
    check.add_argument("--catalogue")
    check.add_argument("--json", action="store_true")
    check.set_defaults(handler=command_check)

    harness = subcommands.add_parser("harness", help="run the dependency-free closed test harness")
    harness.add_argument("--json", action="store_true")
    harness.set_defaults(handler=command_harness)

    self_test = subcommands.add_parser("self-test", help="verify a portable package and exercise its isolated project workflow")
    self_test.add_argument("--json", action="store_true")
    self_test.set_defaults(handler=command_self_test)

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

    api_search = api_subcommands.add_parser("search", help="find API entities by name, intent, signature, OOP binding, or description")
    api_search.add_argument("query")
    api_search.add_argument("--limit", type=int, default=20, choices=range(1, 101), metavar="1..100")
    api_search.add_argument("--full", action="store_true", help="return complete contracts; prefer api get after compact discovery")
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
    validate.add_argument("--schema", required=True, choices=("neon-api", "neon-api-index", "neon-agent-context", "neon-semantic-snapshot", "neon-project", "neon-component", "neon-project-api", "neon-test", "neon-assertion", "neon-artifact", "neon-artifact-index", "neon-evidence", "neon-test-result", "neon-scenario-verify-result", "neon-runtime-snapshot", "neon-runtime-compare-result", "neon-supervisor-session", "neon-supervisor-result", "neon-mutation-result", "neon-probe-config", "neon-probe-report", "neon-proof-result", "neon-probe-install-result", "neon-init-result", "neon-package-manifest", "neon-check-result"))
    validate.add_argument("document")
    validate.add_argument("--json", action="store_true")
    validate.set_defaults(handler=command_schema_validate)

    scenario = subcommands.add_parser("scenario", help="run bounded local development scenarios and assertions")
    scenario_subcommands = scenario.add_subparsers(dest="scenario_command", required=True)
    scenario_run = scenario_subcommands.add_parser("run", help="execute allowlisted static steps and emit evidence")
    scenario_run.add_argument("scenario")
    scenario_run.add_argument("--assertion", action="append", required=True, help="assertion document; repeat for every scenario assertion")
    scenario_run.add_argument("--workspace", default=".", help="approved workspace boundary")
    scenario_run.add_argument("--output", help="new or empty output directory inside the workspace")
    scenario_run.add_argument("--observed-at", help="UTC evidence time in YYYY-MM-DDTHH:MM:SSZ form")
    scenario_run.add_argument("--json", action="store_true")
    scenario_run.set_defaults(handler=command_scenario_run)
    scenario_execute = scenario_subcommands.add_parser("execute", help="execute explicitly authorized bounded runtime steps")
    scenario_execute.add_argument("session", help="workspace-relative supervisor session.json path")
    scenario_execute.add_argument("scenario")
    scenario_execute.add_argument("--assertion", action="append", required=True)
    scenario_execute.add_argument("--workspace", default=".")
    scenario_execute.add_argument("--output")
    scenario_execute.add_argument("--observed-at")
    scenario_execute.add_argument("--json", action="store_true")
    scenario_execute.set_defaults(handler=command_scenario_execute)
    scenario_verify = scenario_subcommands.add_parser("verify", help="verify a saved run's contracts, identity, and artifact integrity")
    scenario_verify.add_argument("run", help="run directory inside the approved workspace")
    scenario_verify.add_argument("--workspace", default=".", help="approved workspace boundary")
    scenario_verify.add_argument("--json", action="store_true")
    scenario_verify.set_defaults(handler=command_scenario_verify)

    supervisor = subcommands.add_parser("supervisor", help="manage an expiring local read-only runtime observation session")
    supervisor_subcommands = supervisor.add_subparsers(dest="supervisor_command", required=True)
    supervisor_start = supervisor_subcommands.add_parser("start", help="start a loopback-only read supervisor")
    supervisor_start.add_argument("--workspace", default=".")
    supervisor_start.add_argument("--project", default="neon.project.json")
    supervisor_start.add_argument("--catalogue")
    supervisor_start.add_argument("--snapshot", default=".neon-runtime/runtime-snapshot.json")
    supervisor_start.add_argument("--output", default=".neon-sessions", help="parent directory for a new unique session")
    supervisor_start.add_argument("--ttl", type=int, default=900, help="session lifetime in seconds (10..86400)")
    supervisor_start.add_argument("--enable", action="append", default=[], choices=("resource.lifecycle", "scenario.execute", "client.launch"), help="explicitly grant one bounded mutation capability")
    supervisor_start.add_argument("--server-root", help="explicit local MTA server directory required by resource.lifecycle")
    supervisor_start.add_argument("--client-root", help="explicit local MTA client directory required by client.launch")
    supervisor_start.add_argument("--connect-port", type=int, default=22003, help="loopback MTA server port used by launched clients")
    supervisor_start.add_argument("--json", action="store_true")
    supervisor_start.set_defaults(handler=command_supervisor_start)
    for name, handler in (("status", command_supervisor_status), ("stop", command_supervisor_stop)):
        supervisor_command = supervisor_subcommands.add_parser(name)
        supervisor_command.add_argument("session", help="workspace-relative session.json path")
        supervisor_command.add_argument("--workspace", default=".")
        supervisor_command.add_argument("--json", action="store_true")
        supervisor_command.set_defaults(handler=handler)

    runtime = subcommands.add_parser("runtime", help="compare read-only runtime observations with pinned contracts")
    runtime_subcommands = runtime.add_subparsers(dest="runtime_command", required=True)
    runtime_compare = runtime_subcommands.add_parser("compare")
    runtime_compare.add_argument("session", help="workspace-relative supervisor session.json path")
    runtime_compare.add_argument("--workspace", default=".")
    runtime_compare.add_argument("--json", action="store_true")
    runtime_compare.set_defaults(handler=command_runtime_compare)
    runtime_prove = runtime_subcommands.add_parser("prove", help="wait for an authenticated real-client runtime proof")
    runtime_prove.add_argument("session", help="workspace-relative supervisor session.json path")
    runtime_prove.add_argument("--workspace", default=".")
    runtime_prove.add_argument("--timeout-ms", type=int, default=120000)
    runtime_prove.add_argument("--poll-ms", type=int, default=500)
    runtime_prove.add_argument("--json", action="store_true")
    runtime_prove.set_defaults(handler=command_runtime_prove)
    runtime_probe = runtime_subcommands.add_parser("probe", help="manage the trusted bundled runtime probe")
    runtime_probe_subcommands = runtime_probe.add_subparsers(dest="runtime_probe_command", required=True)
    runtime_probe_install = runtime_probe_subcommands.add_parser("install", help="install exact trusted probe assets into a local MTA server")
    runtime_probe_install.add_argument("--server-root", required=True)
    runtime_probe_install.add_argument("--json", action="store_true")
    runtime_probe_install.set_defaults(handler=command_runtime_probe_install)

    resource = subcommands.add_parser("resource", help="submit allowlisted resource lifecycle commands")
    resource_subcommands = resource.add_subparsers(dest="resource_command", required=True)
    for name in ("start", "stop", "restart"):
        lifecycle = resource_subcommands.add_parser(name)
        lifecycle.add_argument("session", help="workspace-relative supervisor session.json path")
        lifecycle.add_argument("resource")
        lifecycle.add_argument("--workspace", default=".")
        lifecycle.add_argument("--timeout-ms", type=int, default=10000)
        lifecycle.add_argument("--json", action="store_true")
        lifecycle.set_defaults(handler=command_resource_lifecycle)

    client = subcommands.add_parser("client", help="launch exact approved local MTA clients")
    client_subcommands = client.add_subparsers(dest="client_command", required=True)
    client_launch = client_subcommands.add_parser("launch")
    client_launch.add_argument("session", help="workspace-relative supervisor session.json path")
    client_launch.add_argument("role", choices=tuple(f"client-{index}" for index in range(1, 9)))
    client_launch.add_argument("--workspace", default=".")
    client_launch.add_argument("--timeout-ms", type=int, default=10000)
    client_launch.add_argument("--json", action="store_true")
    client_launch.set_defaults(handler=command_client_launch)

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
    luals.add_argument("--profile", default="neon-pair", choices=("mta-upstream", "neon-client", "neon-server", "neon-pair", "neon-multiclient"))
    luals.add_argument("--json", action="store_true")
    luals.set_defaults(handler=command_generate_luals)
    project_generate = generate_subcommands.add_parser("project", help="generate deterministic agent context and project-aware LuaLS libraries")
    project_generate.add_argument("--project", help="project file; defaults to ./neon.project.json, then the repository project")
    project_generate.add_argument("--catalogue")
    project_generate.add_argument("--output", help="output directory; defaults to ./.neon beside the project")
    project_generate.add_argument("--json", action="store_true")
    project_generate.set_defaults(handler=command_generate_project)

    context = subcommands.add_parser("context", help="verify generated agent context freshness and integrity")
    context_subcommands = context.add_subparsers(dest="context_command", required=True)
    context_verify = context_subcommands.add_parser("verify")
    context_verify.add_argument("--project", help="project file; defaults to ./neon.project.json, then the repository project")
    context_verify.add_argument("--catalogue")
    context_verify.add_argument("--context", help="context directory; defaults to ./.neon beside the project")
    context_verify.add_argument("--json", action="store_true")
    context_verify.set_defaults(handler=command_context_verify)
    return parser


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "_driver-guardian":
        guardian = argparse.ArgumentParser(prog="neon internal-driver-guardian", add_help=False)
        guardian.add_argument("--server-root", required=True)
        guardian.add_argument("--expected-sha256", required=True)
        guardian.add_argument("--host", required=True)
        guardian.add_argument("--port", type=int, required=True)
        guardian.add_argument("--token", required=True)
        args = guardian.parse_args(sys.argv[2:])
        return command_driver_guardian(args)
    if len(sys.argv) > 1 and sys.argv[1] == "_supervisor-daemon":
        daemon = argparse.ArgumentParser(prog="neon internal-supervisor", add_help=False)
        daemon.add_argument("--workspace", required=True)
        daemon.add_argument("--session-directory", required=True)
        daemon.add_argument("--session-id", required=True)
        daemon.add_argument("--project", required=True)
        daemon.add_argument("--catalogue", required=True)
        daemon.add_argument("--snapshot", required=True)
        daemon.add_argument("--ttl", type=int, required=True)
        daemon.add_argument("--capability", action="append", default=[])
        daemon.add_argument("--server-root")
        daemon.add_argument("--client-root")
        daemon.add_argument("--connect-port", type=int, default=22003)
        daemon.add_argument("--test-client-adapter", action="store_true")
        args = daemon.parse_args(sys.argv[2:])
        return command_supervisor_daemon(args)
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
