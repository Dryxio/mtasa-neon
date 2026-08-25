#!/usr/bin/env python3
from __future__ import annotations

import argparse
import io
import sys
import unittest
from pathlib import Path


TOOL_DIRECTORY = Path(__file__).resolve().parent
REPOSITORY_ROOT = TOOL_DIRECTORY.parents[1]
sys.path.insert(0, str(TOOL_DIRECTORY))

from neonlib.catalogue import (  # noqa: E402
    build_catalogue,
    catalogue_divergence,
    catalogue_semantic_issues,
    catalogue_source_matches,
    filesystem_snapshot,
    git_snapshot,
)
from neonlib.jsonio import JsonDocumentError, canonical_json, load_json, write_json  # noqa: E402
from neonlib.luals import generate_luals  # noqa: E402
from neonlib.project import check_project  # noqa: E402
from neonlib.schema import SchemaStore  # noqa: E402


SCHEMA_STORE = SchemaStore(TOOL_DIRECTORY / "schemas")
DEFAULT_CATALOGUE = TOOL_DIRECTORY / "neon-api.json"
DEFAULT_PROJECT = REPOSITORY_ROOT / "neon.project.json"


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


def command_check(args: argparse.Namespace) -> int:
    project = Path(args.project).resolve()
    catalogue = Path(args.catalogue).resolve() if args.catalogue else None
    result = check_project(project, SCHEMA_STORE, catalogue)
    result_issues = SCHEMA_STORE.validate("neon-check-result", result)
    if result_issues:
        result = _failure("check", "INTERNAL_RESULT_INVALID", "; ".join(f"{issue.pointer}: {issue.message}" for issue in result_issues))
    _emit(result, args.json)
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
    result = {
        "schemaVersion": "1.0.0",
        "command": "schema.validate",
        "status": "pass" if not issues else "fail",
        "summary": {"errors": len(issues), "warnings": 0},
        "diagnostics": diagnostics,
    }
    _emit(result, args.json)
    return 0 if not issues else 1


def command_catalogue_build(args: argparse.Namespace) -> int:
    repository = Path(args.repository).resolve()
    try:
        neon = git_snapshot(repository, args.neon_ref)
        upstream = git_snapshot(repository, args.upstream_ref)
        catalogue = build_catalogue(neon, upstream, engine_version=args.engine_version, wiki_revision=args.wiki_revision)
    except ValueError as exc:
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


def command_catalogue_verify(args: argparse.Namespace) -> int:
    repository = Path(args.repository).resolve()
    try:
        catalogue = load_json(Path(args.catalogue).resolve())
        snapshot = git_snapshot(repository, args.source_ref) if args.source_ref else filesystem_snapshot(repository)
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
    diagnostics = []
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
            "sourceDrift": 0 if catalogue_source_matches(catalogue, snapshot) else 1,
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
    check.add_argument("--project", default=str(DEFAULT_PROJECT))
    check.add_argument("--catalogue")
    check.add_argument("--json", action="store_true")
    check.set_defaults(handler=command_check)

    harness = subcommands.add_parser("harness", help="run the dependency-free closed test harness")
    harness.add_argument("--json", action="store_true")
    harness.set_defaults(handler=command_harness)

    schema = subcommands.add_parser("schema", help="validate contract documents")
    schema_subcommands = schema.add_subparsers(dest="schema_command", required=True)
    validate = schema_subcommands.add_parser("validate")
    validate.add_argument("--schema", required=True, choices=("neon-api", "neon-project", "neon-test", "neon-assertion", "neon-artifact", "neon-check-result"))
    validate.add_argument("document")
    validate.add_argument("--json", action="store_true")
    validate.set_defaults(handler=command_schema_validate)

    catalogue = subcommands.add_parser("catalogue", help="build or verify the effective MTA API")
    catalogue_subcommands = catalogue.add_subparsers(dest="catalogue_command", required=True)
    build = catalogue_subcommands.add_parser("build")
    build.add_argument("--repository", default=str(REPOSITORY_ROOT))
    build.add_argument("--neon-ref", default="HEAD")
    build.add_argument("--upstream-ref", default="upstream/master")
    build.add_argument("--engine-version", default="1.7.0")
    build.add_argument("--wiki-revision", default="39e80f8108fef8de0dfdf61876daf702d583243e")
    build.add_argument("--output", default=str(DEFAULT_CATALOGUE))
    build.add_argument("--json", action="store_true")
    build.set_defaults(handler=command_catalogue_build)
    verify = catalogue_subcommands.add_parser("verify")
    verify.add_argument("--repository", default=str(REPOSITORY_ROOT))
    verify.add_argument("--catalogue", default=str(DEFAULT_CATALOGUE))
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
