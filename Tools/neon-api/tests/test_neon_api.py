from __future__ import annotations

import copy
import json
import os
import signal
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock
from pathlib import Path


TOOL_DIRECTORY = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = TOOL_DIRECTORY.parents[1]
CATALOGUE_PATH = TOOL_DIRECTORY / "neon-api.json"
SEMANTIC_SNAPSHOT_PATH = TOOL_DIRECTORY / "snapshots" / "api-semantics.json"
CLI_PATH = TOOL_DIRECTORY / "neon.py"
sys.path.insert(0, str(TOOL_DIRECTORY))

from neonlib.catalogue import (  # noqa: E402
    SourceSnapshot,
    build_catalogue,
    catalogue_divergence,
    catalogue_event_divergence,
    catalogue_runtime_inventory_issues,
    catalogue_semantic_issues,
    catalogue_source_matches,
    extract_enum_registrations,
    extract_event_registrations,
    extract_oop_registrations,
    extract_registrations,
    filesystem_snapshot,
    git_snapshot,
    semantic_snapshot_issues,
)
from neonlib.jsonio import JsonDocumentError, canonical_json, load_json, sha256_bytes, sha256_file, write_json  # noqa: E402
from neonlib.luals import generate_luals, render_luals  # noqa: E402
from neonlib.mutation import mutation_failure  # noqa: E402
from neonlib.components import manifest_semantic_issues  # noqa: E402
from neonlib.context import ContextGenerationError, build_api_index, generate_project_context, verify_project_context  # noqa: E402
from neonlib.discovery import discovery_keywords, search_symbols, tokenize  # noqa: E402
from neonlib.project import check_project, resolve_project_components  # noqa: E402
from neonlib.runtime import compare_runtime_snapshot  # noqa: E402
from neonlib.scenario import _run_step  # noqa: E402
from neonlib.schema import SchemaStore  # noqa: E402
from neonlib.supervisor import MAX_AUDIT_BYTES, _authorization, _load_session, request_supervisor, start_supervisor  # noqa: E402
import neonlib.supervisor as supervisor_module  # noqa: E402


SCHEMAS = SchemaStore(TOOL_DIRECTORY / "schemas")


def base_project() -> dict:
    return {
        "schemaVersion": "1.0.0",
        "name": "closed-harness",
        "profile": "neon-pair",
        "engine": {"minimumVersion": "1.7.0", "maximumVersionExclusive": "1.8.0"},
        "catalogue": "api.json",
        "resources": [],
        "externalDependencies": [],
        "requiredApis": [],
        "unknownApis": "allow",
    }


def diagnostic_codes(result: dict) -> list[str]:
    return [diagnostic["code"] for diagnostic in result["diagnostics"]]


def base_component(name: str = "demo", kind: str = "resource") -> dict:
    component = {
        "schemaVersion": "1.0.0",
        "kind": kind,
        "name": name,
        "version": "1.0.0",
        "lifecycle": {"start": "automatic", "stop": "clean", "reloadSafe": True, "persistentState": "none"},
        "dependencies": [],
        "exports": [],
        "events": [],
        "elements": [],
        "acl": [],
        "capabilities": [],
    }
    if kind == "module":
        component["module"] = {"abi": "MTA-1.7-server", "entrypoint": "InitModule", "platforms": ["windows"], "architectures": ["x64"]}
    return component


class ContractSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.catalogue = load_json(CATALOGUE_PATH)

    def test_repository_catalogue_is_global_valid_and_semantically_stable(self) -> None:
        self.assertEqual(SCHEMAS.validate("neon-api", self.catalogue), [])
        self.assertEqual(self.catalogue["schemaVersion"], "1.1.0")
        self.assertEqual(self.catalogue["catalogueVersion"], "1.2.0")
        self.assertEqual(catalogue_semantic_issues(self.catalogue), [])
        self.assertGreater(len(self.catalogue["symbols"]), 1500)
        origins = {symbol["origin"] for symbol in self.catalogue["symbols"]}
        self.assertEqual(origins, {"mta", "neon"})
        names = {symbol["name"] for symbol in self.catalogue["symbols"]}
        self.assertIn("createVehicle", names)
        self.assertIn("dxDrawText", names)
        self.assertIn("createRope", names)
        self.assertTrue(self.catalogue["sources"]["upstreamWiki"]["imported"])
        self.assertTrue(self.catalogue["sources"]["neonWiki"]["imported"])
        self.assertEqual(self.catalogue["statistics"]["functions"], 1842)
        self.assertEqual(self.catalogue["statistics"]["events"], 241)
        self.assertEqual(self.catalogue["statistics"]["elements"], 71)
        self.assertEqual(self.catalogue["statistics"]["types"], 9)
        self.assertEqual(self.catalogue["statistics"]["classes"], 75)
        self.assertEqual(self.catalogue["statistics"]["enums"], 97)

    def test_semantic_snapshot_is_strict_pinned_and_content_addressed(self) -> None:
        snapshot = load_json(SEMANTIC_SNAPSHOT_PATH)
        self.assertEqual(SCHEMAS.validate("neon-semantic-snapshot", snapshot), [])
        self.assertEqual(semantic_snapshot_issues(snapshot), [])
        self.assertEqual(snapshot["sources"]["upstreamWiki"]["revision"], "39e80f8108fef8de0dfdf61876daf702d583243e")
        self.assertRegex(snapshot["sources"]["neonWiki"]["revision"], r"^[0-9a-f]{40}$")
        self.assertEqual(snapshot["sources"]["upstreamWiki"]["license"], "GFDL-1.3-or-later")
        self.assertEqual(snapshot["sources"]["neonWiki"]["license"], "GFDL-1.3-or-later")
        self.assertEqual(
            {key: len(snapshot[key]) for key in ("functions", "events", "elements", "types")},
            {"functions": 1694, "events": 220, "elements": 71, "types": 9},
        )

    def test_semantic_snapshot_detects_tampering(self) -> None:
        snapshot = load_json(SEMANTIC_SNAPSHOT_PATH)
        snapshot["functions"][0]["name"] = "tampered"
        issues = semantic_snapshot_issues(snapshot)
        self.assertTrue(any("digest" in issue for issue in issues))
        self.assertTrue(any("deterministic name order" in issue for issue in issues))

    def test_representative_global_entities_have_contracts_and_provenance(self) -> None:
        by_key = {(symbol["kind"], symbol["name"]): symbol for symbol in self.catalogue["symbols"]}
        vehicle = by_key[("function", "createVehicle")]
        self.assertEqual(vehicle["state"], "verified")
        self.assertEqual(vehicle["parameters"][0], {
            "description": "The vehicle ID of the vehicle being created.",
            "name": "model", "optional": False, "type": "int",
        })
        self.assertEqual(vehicle["returns"][0]["type"], "vehicle")
        self.assertEqual(vehicle["contracts"][0]["oop"]["constructorClass"], "Vehicle")
        rope = by_key[("function", "createRope")]
        self.assertEqual(rope["origin"], "neon")
        self.assertEqual(rope["contracts"][0]["provider"], "neon")
        self.assertEqual(rope["contracts"][0]["testResource"], "test-resources/rope-test")
        event = by_key[("event", "onPlayerJoin")]
        self.assertEqual(event["parameters"], [])
        self.assertEqual(event["state"], "verified")
        self.assertEqual(event["sides"], ["server"])
        self.assertEqual(event["evidence"], ["documented", "source-inspected"])
        click = by_key[("event", "onClientClick")]
        self.assertEqual(click["state"], "verified")
        self.assertEqual(click["registrationDifferences"], ["parameter-names"])
        damage = by_key[("event", "onClientPlayerDamage")]
        self.assertEqual(damage["state"], "conflict")
        self.assertEqual(damage["registrationDifferences"], ["parameter-count"])
        ped = by_key[("class", "Ped")]
        kill = next(item for item in ped["methods"] if item["name"] == "kill")
        self.assertEqual(kill["globalFunctions"], ["killPed"])
        armor = next(item for item in ped["properties"] if item["name"] == "armor")
        self.assertEqual(armor["setters"], ["setPedArmor"])
        element_type = by_key[("enum", "element-type")]
        self.assertIn("vehicle", element_type["values"])
        self.assertIn("player", element_type["values"])
        vector = by_key[("element", "Vector3")]
        self.assertTrue(vector["oopOnlyMethods"])
        licenses = {item["license"] for symbol in (vehicle, rope, event) for item in symbol["provenance"]}
        self.assertEqual(licenses, {"GFDL-1.3-or-later", "GPL-3.0-or-later"})

    def test_valid_contract_samples(self) -> None:
        documents = {
            "neon-project": base_project(),
            "neon-component": base_component(),
            "neon-test": {
                "schemaVersion": "1.0.0",
                "id": "test:closed-harness",
                "profile": "neon-pair",
                "steps": [{"id": "step:check", "action": "check", "timeoutMs": 1000, "inputs": {}}],
                "assertions": ["assertion:no-errors"],
            },
            "neon-assertion": {
                "schemaVersion": "1.0.0",
                "id": "assertion:no-errors",
                "kind": "equals",
                "actual": "/summary/errors",
                "expected": 0,
                "message": "the static check must be clean",
            },
            "neon-artifact": {
                "schemaVersion": "1.0.0",
                "id": "artifact:check-result",
                "kind": "json",
                "path": "artifacts/check.json",
                "mediaType": "application/json",
                "size": 2,
                "sha256": "0" * 64,
            },
            "neon-check-result": {
                "schemaVersion": "1.0.0",
                "command": "check",
                "status": "pass",
                "summary": {"errors": 0, "warnings": 0, "resources": 0, "files": 0, "apiRequirements": 0},
                "diagnostics": [],
            },
        }
        for schema, document in documents.items():
            with self.subTest(schema=schema):
                self.assertEqual(SCHEMAS.validate(schema, document), [])

    def test_all_contracts_reject_unknown_fields(self) -> None:
        samples = {
            "neon-project": base_project(),
            "neon-component": base_component(),
            "neon-test": {
                "schemaVersion": "1.0.0", "id": "test:x", "profile": "neon-pair",
                "steps": [{"id": "step:x", "action": "check", "timeoutMs": 1, "inputs": {}}], "assertions": ["assertion:x"],
            },
            "neon-assertion": {"schemaVersion": "1.0.0", "id": "assertion:x", "kind": "truthy", "actual": "/x", "message": "x"},
            "neon-artifact": {"schemaVersion": "1.0.0", "id": "artifact:x", "kind": "json", "path": "x.json", "mediaType": "application/json", "size": 1, "sha256": "0" * 64},
        }
        for schema, document in samples.items():
            with self.subTest(schema=schema):
                document["typoField"] = True
                self.assertTrue(SCHEMAS.validate(schema, document))

    def test_component_semantics_reject_module_without_abi_duplicates_and_bad_parameter_order(self) -> None:
        component = base_component("native", "module")
        del component["module"]
        component["exports"] = [{
            "name": "call", "side": "server", "description": "Call native code.", "http": False, "restricted": False,
            "parameters": [
                {"name": "optional", "type": "string", "optional": True, "description": "Optional value."},
                {"name": "required", "type": "number", "optional": False, "description": "Required value."},
            ],
            "returns": [],
        }] * 2
        codes = {issue.code for issue in manifest_semantic_issues(component)}
        self.assertEqual(codes, {"MODULE_ABI_MISSING", "COMPONENT_DUPLICATE_IDENTITY", "COMPONENT_PARAMETER_ORDER_INVALID"})

    def test_component_schema_cli_applies_semantic_validation(self) -> None:
        component = base_component()
        component["dependencies"] = [{"kind": "resource", "name": "demo", "optional": False}]
        with tempfile.TemporaryDirectory(prefix="neon-component-schema-") as temporary:
            path = Path(temporary) / "component.yaml"
            write_json(path, component)
            completed = subprocess.run(
                [sys.executable, str(CLI_PATH), "schema", "validate", "--schema", "neon-component", str(path), "--json"],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
            )
        result = json.loads(completed.stdout)
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(diagnostic_codes(result), ["COMPONENT_SELF_DEPENDENCY"])

    def test_artifact_rejects_path_traversal_and_bad_hash(self) -> None:
        artifact = {
            "schemaVersion": "1.0.0", "id": "artifact:x", "kind": "json", "path": "../x.json",
            "mediaType": "application/json", "size": 1, "sha256": "not-a-hash",
        }
        issues = SCHEMAS.validate("neon-artifact", artifact)
        self.assertEqual({issue.pointer for issue in issues}, {"/path", "/sha256"})

    def test_catalogue_semantics_reject_duplicate_identity(self) -> None:
        corrupted = copy.deepcopy(self.catalogue)
        corrupted["symbols"].insert(1, copy.deepcopy(corrupted["symbols"][0]))
        issues = catalogue_semantic_issues(corrupted)
        self.assertTrue(any("duplicate symbol id" in issue for issue in issues))
        self.assertTrue(any("duplicate function name" in issue for issue in issues))

    def test_api_schema_rejects_unknown_symbol_field(self) -> None:
        corrupted = copy.deepcopy(self.catalogue)
        corrupted["symbols"][0]["guessedSignature"] = True
        issues = SCHEMAS.validate("neon-api", corrupted)
        self.assertIn("/symbols/0/guessedSignature", {issue.pointer for issue in issues})

    def test_api_schema_rejects_unknown_contract_field(self) -> None:
        corrupted = copy.deepcopy(self.catalogue)
        symbol = next(item for item in corrupted["symbols"] if item.get("contracts"))
        symbol["contracts"][0]["hallucinated"] = True
        issues = SCHEMAS.validate("neon-api", corrupted)
        self.assertTrue(any(issue.pointer.endswith("/hallucinated") for issue in issues))

    def test_catalogue_semantics_detect_statistics_and_provenance_tampering(self) -> None:
        corrupted = copy.deepcopy(self.catalogue)
        corrupted["statistics"]["functions"] += 1
        corrupted["symbols"][0]["provenance"].append(copy.deepcopy(corrupted["symbols"][0]["provenance"][0]))
        issues = catalogue_semantic_issues(corrupted)
        self.assertIn("catalogue statistics do not match symbols", issues)
        self.assertTrue(any("duplicate provenance" in issue for issue in issues))

    def test_catalogue_semantics_rejects_incomplete_and_duplicate_runtime_inventory(self) -> None:
        corrupted = copy.deepcopy(self.catalogue)
        ped = next(symbol for symbol in corrupted["symbols"] if symbol["kind"] == "class" and symbol["name"] == "Ped")
        del ped["definitions"]
        ped["methods"][0]["bindings"].append(copy.deepcopy(ped["methods"][0]["bindings"][0]))
        issues = catalogue_semantic_issues(corrupted)
        self.assertIn("symbol Ped kind class is missing definitions", issues)
        self.assertTrue(any("duplicate bindings" in issue for issue in issues))

    def test_check_result_rejects_unknown_severity(self) -> None:
        result = {
            "schemaVersion": "1.0.0",
            "command": "check",
            "status": "fail",
            "summary": {"errors": 1, "warnings": 0, "resources": 0, "files": 0, "apiRequirements": 0},
            "diagnostics": [{"code": "BAD", "severity": "notice", "message": "bad", "path": "."}],
        }
        self.assertTrue(SCHEMAS.validate("neon-check-result", result))


class RegistrationExtractionTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("git"), "Git executable is required for repository snapshot tests")
    def test_git_snapshot_revision_ignores_tooling_only_commits(self) -> None:
        with tempfile.TemporaryDirectory(prefix="neon-source-revision-") as temporary:
            repository = Path(temporary)
            source = repository / "Client/mods/deathmatch/logic/luadefs/Test.cpp"
            source.parent.mkdir(parents=True)
            source.write_text('AddFunction("alpha", Alpha);\n', encoding="utf-8")
            subprocess.run(["git", "init", "-q"], cwd=repository, check=True)
            subprocess.run(["git", "config", "user.name", "Neon Harness"], cwd=repository, check=True)
            subprocess.run(["git", "config", "user.email", "harness@example.invalid"], cwd=repository, check=True)
            subprocess.run(["git", "add", "."], cwd=repository, check=True)
            subprocess.run(["git", "commit", "-q", "-m", "source"], cwd=repository, check=True)
            source_revision = subprocess.run(
                ["git", "rev-parse", "HEAD"], cwd=repository, text=True, stdout=subprocess.PIPE, check=True
            ).stdout.strip()
            (repository / "README.md").write_text("tooling only\n", encoding="utf-8")
            subprocess.run(["git", "add", "README.md"], cwd=repository, check=True)
            subprocess.run(["git", "commit", "-q", "-m", "tooling"], cwd=repository, check=True)
            snapshot = git_snapshot(repository, "HEAD")
            self.assertEqual(snapshot.revision, source_revision)
            self.assertIn(source.relative_to(repository).as_posix(), snapshot.files)

    def test_extracts_supported_registration_forms_and_ignores_comments(self) -> None:
        source = r'''
            constexpr static const std::pair<const char*, lua_CFunction> functions[]{
                {"alpha", Alpha},
                // {"commented", Commented},
                {"bravo", ArgumentParser<Bravo>},
            };
            CLuaCFunctions::AddFunction("charlie", Charlie, true);
            AddFunction("delta", Delta);
            lua_register(luaVM, "echo", Echo);
            /* CLuaCFunctions::AddFunction("hidden", Hidden); */
        '''
        snapshot = SourceSnapshot("a" * 40, {"Shared/mods/deathmatch/logic/luadefs/Test.cpp": source})
        registrations = extract_registrations(snapshot)
        self.assertEqual([item.name for item in registrations], ["alpha", "bravo", "charlie", "delta", "echo"])
        self.assertTrue(next(item for item in registrations if item.name == "charlie").restricted)
        self.assertTrue(all(item.side == "shared" for item in registrations))

    def test_extracts_multiline_events_and_remote_policy_without_comments(self) -> None:
        source = r'''
            m_Events.AddEvent("onAlpha", "player, reason", nullptr, false);
            m_Events.AddEvent(
                "onBravo", "value", nullptr, true
            );
            // m_Events.AddEvent("onHidden", "", nullptr, true);
        '''
        snapshot = SourceSnapshot("a" * 40, {"Client/mods/deathmatch/logic/CClientGame.cpp": source})
        events = extract_event_registrations(snapshot)
        self.assertEqual([item.name for item in events], ["onAlpha", "onBravo"])
        self.assertEqual(events[0].arguments, ("player", "reason"))
        self.assertFalse(events[0].allow_remote_trigger)
        self.assertTrue(events[1].allow_remote_trigger)

    def test_extracts_oop_classes_methods_properties_and_native_wrappers(self) -> None:
        source = r'''
            lua_newclass(luaVM);
            lua_classfunction(luaVM, "create", "createWidget");
            lua_classfunction(luaVM, "native", ArgumentParser<CreateNative>);
            lua_classvariable(luaVM, "enabled", "setWidgetEnabled", "isWidgetEnabled");
            lua_classvariable(luaVM, "nativeValue", SetNative, GetNative);
            lua_registerclass(luaVM, "Widget", "Element");
            // lua_registerclass(luaVM, "Hidden");
        '''
        snapshot = SourceSnapshot("a" * 40, {"Client/mods/deathmatch/logic/luadefs/Test.cpp": source})
        classes, methods, properties = extract_oop_registrations(snapshot)
        self.assertEqual([(item.name, item.parent) for item in classes], [("Widget", "Element")])
        self.assertEqual([(item.name, item.global_function) for item in methods], [("create", "createWidget"), ("native", "")])
        self.assertEqual(next(item for item in methods if item.name == "native").native_function, "ArgumentParser<CreateNative>")
        self.assertEqual([(item.name, item.setter, item.getter) for item in properties], [
            ("enabled", "setWidgetEnabled", "isWidgetEnabled"), ("nativeValue", "", ""),
        ])
        native_property = next(item for item in properties if item.name == "nativeValue")
        self.assertEqual((native_property.native_setter, native_property.native_getter), ("SetNative", "GetNative"))

    def test_extracts_plain_class_and_stringified_enums_deterministically(self) -> None:
        source = r'''
            IMPLEMENT_ENUM_BEGIN(eMode)
            ADD_ENUM(MODE_A, "alpha")
            ADD_ENUM1(MODE_B)
            // ADD_ENUM(MODE_C, "hidden")
            IMPLEMENT_ENUM_END_DEFAULTS("mode", MODE_A, "alpha")
            IMPLEMENT_ENUM_CLASS_BEGIN(StrongMode)
            ADD_ENUM(StrongMode::A, "alpha")
            IMPLEMENT_ENUM_CLASS_END("strong-mode")
        '''
        snapshot = SourceSnapshot("a" * 40, {"Shared/mods/deathmatch/logic/Enums.cpp": source})
        enums = extract_enum_registrations(snapshot)
        self.assertEqual([(item.name, item.values) for item in enums], [
            ("mode", ("alpha", "MODE_B")), ("strong-mode", ("alpha",)),
        ])

    def test_divergence_reports_both_directions(self) -> None:
        upstream = SourceSnapshot("a" * 40, {"Server/mods/deathmatch/logic/luadefs/Test.cpp": 'CLuaCFunctions::AddFunction("alpha", Alpha);'})
        neon = SourceSnapshot("b" * 40, {"Server/mods/deathmatch/logic/luadefs/Test.cpp": 'CLuaCFunctions::AddFunction("alpha", Alpha);'})
        catalogue = build_catalogue(neon, upstream, engine_version="1.7.0", wiki_revision="c" * 40)
        changed = SourceSnapshot("d" * 40, {"Server/mods/deathmatch/logic/luadefs/Test.cpp": 'CLuaCFunctions::AddFunction("bravo", Bravo);'})
        uncatalogued, missing = catalogue_divergence(catalogue, changed)
        self.assertEqual(uncatalogued, [("bravo", "server")])
        self.assertEqual(missing, [("alpha", "server")])

    def test_event_divergence_reports_both_directions(self) -> None:
        path = "Server/mods/deathmatch/logic/CGame.cpp"
        upstream = SourceSnapshot("a" * 40, {path: 'm_Events.AddEvent("onAlpha", "", nullptr, false);'})
        catalogue = build_catalogue(upstream, upstream, engine_version="1.7.0", wiki_revision="c" * 40)
        changed = SourceSnapshot("d" * 40, {path: 'm_Events.AddEvent("onBravo", "", nullptr, false);'})
        uncatalogued, missing = catalogue_event_divergence(catalogue, changed)
        self.assertEqual(uncatalogued, [("onBravo", "server")])
        self.assertEqual(missing, [("onAlpha", "server")])

    def test_repository_runtime_inventory_is_complete_and_locked(self) -> None:
        snapshot = filesystem_snapshot(REPOSITORY_ROOT)
        events = extract_event_registrations(snapshot)
        classes, methods, properties = extract_oop_registrations(snapshot)
        enums = extract_enum_registrations(snapshot)
        self.assertEqual((len(events), len(classes), len(methods), len(properties), len(enums)), (240, 90, 1541, 516, 111))
        catalogue = load_json(CATALOGUE_PATH)
        self.assertEqual(catalogue_runtime_inventory_issues(catalogue, snapshot), [])

    def test_runtime_inventory_detects_missing_oop_binding(self) -> None:
        snapshot = SourceSnapshot("a" * 40, {
            "Client/mods/deathmatch/logic/luadefs/Test.cpp": '''
                lua_newclass(luaVM);
                lua_classfunction(luaVM, "create", "createWidget");
                lua_registerclass(luaVM, "Widget", "Element");
            ''',
        })
        catalogue = build_catalogue(snapshot, snapshot, engine_version="1.7.0", wiki_revision="c" * 40)
        widget = next(symbol for symbol in catalogue["symbols"] if symbol["kind"] == "class")
        widget["methods"][0]["bindings"] = []
        self.assertEqual(catalogue_runtime_inventory_issues(catalogue, snapshot), [
            "1 registered method records are absent from the catalogue",
        ])

    def test_runtime_inventory_detects_changed_event_signature(self) -> None:
        path = "Server/mods/deathmatch/logic/CGame.cpp"
        snapshot = SourceSnapshot("a" * 40, {path: 'm_Events.AddEvent("onAlpha", "player, reason", nullptr, false);'})
        catalogue = build_catalogue(snapshot, snapshot, engine_version="1.7.0", wiki_revision="c" * 40)
        event = next(symbol for symbol in catalogue["symbols"] if symbol["kind"] == "event")
        event["eventDefinitions"][0]["arguments"] = ["player"]
        self.assertEqual(catalogue_runtime_inventory_issues(catalogue, snapshot), [
            "1 registered event records are absent from the catalogue",
            "1 catalogued event records have no source registration",
        ])

    def test_catalogue_build_is_deterministic_for_identical_snapshots(self) -> None:
        snapshot = SourceSnapshot("a" * 40, {"Client/mods/deathmatch/logic/luadefs/Test.cpp": 'CLuaCFunctions::AddFunction("alpha", Alpha);'})
        first = build_catalogue(snapshot, snapshot, engine_version="1.7.0", wiki_revision="b" * 40)
        second = build_catalogue(snapshot, snapshot, engine_version="1.7.0", wiki_revision="b" * 40)
        self.assertEqual(canonical_json(first), canonical_json(second))
        self.assertTrue(catalogue_source_matches(first, snapshot))
        changed = SourceSnapshot("c" * 40, {**snapshot.files, "Shared/mods/deathmatch/logic/luadefs/Extra.cpp": "// changed"})
        self.assertFalse(catalogue_source_matches(first, changed))

    def test_upstream_symbol_removed_by_neon_remains_available_to_upstream_profile(self) -> None:
        upstream = SourceSnapshot("a" * 40, {"Server/mods/deathmatch/logic/luadefs/Test.cpp": 'CLuaCFunctions::AddFunction("alpha", Alpha);'})
        neon = SourceSnapshot("b" * 40, {})
        catalogue = build_catalogue(neon, upstream, engine_version="1.7.0", wiki_revision="c" * 40)
        symbol = catalogue["symbols"][0]
        self.assertEqual(symbol["state"], "runtime-only")
        self.assertEqual(symbol["sides"], [])
        self.assertEqual(symbol["inheritedSides"], ["server"])
        self.assertEqual(symbol["profiles"], ["mta-upstream"])
        self.assertEqual(catalogue_semantic_issues(catalogue), [])

    def test_explicit_documented_side_conflict_is_preserved(self) -> None:
        upstream = SourceSnapshot("a" * 40, {})
        neon = SourceSnapshot("b" * 40, {"Server/mods/deathmatch/logic/luadefs/Test.cpp": 'AddFunction("alpha", Alpha);'})
        semantics = {
            "schemaVersion": "1.0.0",
            "sources": {
                "upstreamWiki": {"repository": "https://example.test/mta.git", "revision": "c" * 40, "license": "GFDL-1.3-or-later"},
                "neonWiki": {"repository": "https://example.test/neon.git", "revision": "d" * 40, "license": "GFDL-1.3-or-later"},
            },
            "functions": [{
                "name": "alpha", "provider": "neon", "contracts": [{
                    "side": "client", "parameters": [], "returns": [{"type": "bool"}],
                    "requiresReview": False, "sourcePath": "docs/alpha.ts",
                }],
            }],
            "events": [], "elements": [], "types": [], "digest": "0" * 64,
        }
        catalogue = build_catalogue(neon, upstream, engine_version="1.7.0", wiki_revision="c" * 40, semantic_snapshot=semantics)
        self.assertEqual(catalogue["symbols"][0]["state"], "conflict")
        self.assertEqual(catalogue["symbols"][0]["sides"], ["server"])


class ApiDiscoveryTests(unittest.TestCase):
    def run_cli(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CLI_PATH), *arguments, "--catalogue", str(CATALOGUE_PATH), "--json"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def test_search_is_tokenized_ranked_filtered_and_stable(self) -> None:
        completed = self.run_cli("api", "search", "draw text", "--side", "client", "--kind", "function", "--limit", "5")
        self.assertEqual(completed.returncode, 0)
        self.assertEqual(completed.stderr, "")
        result = json.loads(completed.stdout)
        self.assertEqual(result["symbols"][0]["name"], "dxDrawText")
        self.assertLessEqual(len(result["symbols"]), 5)
        repeated = self.run_cli("api", "search", "draw text", "--side", "client", "--kind", "function", "--limit", "5")
        self.assertEqual(repeated.stdout, completed.stdout)

    def test_search_understands_intent_synonyms_sparse_contracts_and_french_terms(self) -> None:
        cases = (
            ("make player invisible", "setElementAlpha", ("--kind", "function")),
            ("create car", "createVehicle", ("--kind", "function")),
            ("when player joins", "onPlayerJoin", ("--kind", "event", "--side", "server")),
            ("delay callback", "setTimer", ("--kind", "function")),
            ("npc pathfinding", "setPedNavigateTo", ("--kind", "function", "--origin", "neon")),
            ("ocean floor boundary", "setWorldSeaBedOuterBoundary", ("--kind", "function", "--origin", "neon")),
            ("armure joueur", "getPlayerArmor", ("--kind", "function")),
        )
        for query, expected, filters in cases:
            with self.subTest(query=query):
                completed = self.run_cli("api", "search", query, *filters, "--limit", "10")
                self.assertEqual(completed.returncode, 0)
                names = [symbol["name"] for symbol in json.loads(completed.stdout)["symbols"]]
                self.assertIn(expected, names[:5])
                if query in ("make player invisible", "npc pathfinding"):
                    self.assertEqual(names[0], expected)

    def test_search_is_compact_by_default_and_full_is_explicit(self) -> None:
        compact = self.run_cli("api", "search", "createVehicle", "--limit", "1")
        compact_symbol = json.loads(compact.stdout)["symbols"][0]
        self.assertEqual(compact_symbol["name"], "createVehicle")
        self.assertNotIn("contracts", compact_symbol)
        full = self.run_cli("api", "search", "createVehicle", "--limit", "1", "--full")
        self.assertIn("contracts", json.loads(full.stdout)["symbols"][0])
        self.assertLess(len(compact.stdout), len(full.stdout) // 4)

    def test_search_handles_camel_case_members_plural_words_and_one_typo(self) -> None:
        completed = self.run_cli("api", "search", "kill ped", "--kind", "class", "--limit", "5")
        self.assertIn("Ped", [symbol["name"] for symbol in json.loads(completed.stdout)["symbols"]])
        completed = self.run_cli("api", "search", "list vehicles", "--kind", "function", "--limit", "10")
        self.assertIn("getElementsByType", [symbol["name"] for symbol in json.loads(completed.stdout)["symbols"][:5]])
        completed = self.run_cli("api", "search", "transparncy player", "--kind", "function", "--limit", "10")
        self.assertIn("setElementAlpha", [symbol["name"] for symbol in json.loads(completed.stdout)["symbols"][:5]])

    def test_search_closes_domain_vocabulary_and_false_positive_regressions(self) -> None:
        cases = (
            ("delay callback", (), "setTimer", 1),
            ("http request", (), "fetchRemote", 3),
            ("web request", (), "fetchRemote", 3),
            ("tire burst", ("--origin", "neon"), "setVehicleTyresCanBurst", 5),
            ("tyre burst", ("--origin", "neon"), "setVehicleTyresCanBurst", 5),
            ("replay speed", ("--origin", "neon", "--state", "runtime-only"), "setVehiclePlaybackSpeed", 5),
        )
        for query, filters, expected, maximum_rank in cases:
            with self.subTest(query=query):
                completed = self.run_cli("api", "search", query, "--kind", "function", *filters, "--limit", "10")
                self.assertEqual(completed.returncode, 0)
                names = [symbol["name"] for symbol in json.loads(completed.stdout)["symbols"]]
                self.assertIn(expected, names[:maximum_rank])

        for query in ("ambient traffic", "civilian traffic"):
            completed = self.run_cli("api", "search", query, "--kind", "function", "--origin", "neon", "--limit", "10")
            first = json.loads(completed.stdout)["symbols"][0]["name"]
            self.assertTrue(first.startswith("getAmbientVehicle") and first.endswith("Candidate"), (query, first))
        destructible = self.run_cli("api", "search", "destructible object", "--kind", "function", "--origin", "neon", "--limit", "5")
        destructible_names = [symbol["name"] for symbol in json.loads(destructible.stdout)["symbols"]]
        self.assertTrue({"setObjectBreakProfile", "createObjectBreakEffect"}.intersection(destructible_names[:3]))

        backfire = self.run_cli("api", "search", "backfire", "--limit", "20")
        backfire_names = [symbol["name"] for symbol in json.loads(backfire.stdout)["symbols"]]
        self.assertEqual(backfire_names[0], "enginePlayVehicleAudioBackfire")
        self.assertFalse(any("Browser" in name or "gui" in name for name in backfire_names))

    def test_compound_and_natural_side_queries_are_normalized(self) -> None:
        for query in ("raycast", "ray cast"):
            with self.subTest(query=query):
                completed = self.run_cli("api", "search", query, "--kind", "function", "--side", "client", "--limit", "5")
                names = [symbol["name"] for symbol in json.loads(completed.stdout)["symbols"]]
                self.assertIn("processLineOfSight", names)
                self.assertIn("processLineAgainstMesh", names)
        completed = self.run_cli("api", "search", "server side player join", "--kind", "event", "--limit", "10")
        symbols = json.loads(completed.stdout)["symbols"]
        self.assertEqual(symbols[0]["name"], "onPlayerJoin")
        self.assertTrue(all("server" in symbol["sides"] for symbol in symbols))

    def test_search_rejects_only_stop_words_and_preserves_filters(self) -> None:
        completed = self.run_cli("api", "search", "how do i use the api")
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(json.loads(completed.stdout)["diagnostics"][0]["code"], "API_QUERY_EMPTY")
        completed = self.run_cli("api", "search", "draw text", "--side", "server", "--kind", "function")
        self.assertNotIn("dxDrawText", [symbol["name"] for symbol in json.loads(completed.stdout)["symbols"]])

    def test_discovery_primitives_are_deterministic_and_bounded(self) -> None:
        catalogue = load_json(CATALOGUE_PATH)
        alpha = next(symbol for symbol in catalogue["symbols"] if symbol["name"] == "setElementAlpha")
        self.assertEqual(tokenize("setWorldSeaBedOuterBoundary"), ("set", "world", "sea", "bed", "outer", "boundary"))
        self.assertEqual(search_symbols([alpha], "make player invisible"), [alpha])
        keywords = discovery_keywords(alpha)
        self.assertEqual(keywords, discovery_keywords(alpha))
        self.assertLessEqual(len(keywords), 16)
        self.assertEqual(len(keywords), len(set(keywords)))

    def test_get_exposes_typed_neon_contract(self) -> None:
        completed = self.run_cli("api", "get", "createRope", "--kind", "function", "--profile", "neon-pair")
        self.assertEqual(completed.returncode, 0)
        symbol = json.loads(completed.stdout)["symbols"][0]
        self.assertEqual(symbol["contracts"][0]["signature"], "rope|false createRope(float x, float y, float z [, table options])")
        self.assertEqual(symbol["contracts"][0]["parameters"][0]["type"], "float")

    def test_get_exposes_runtime_oop_and_enum_contracts(self) -> None:
        completed = self.run_cli("api", "get", "Ped", "--kind", "class", "--profile", "neon-pair")
        self.assertEqual(completed.returncode, 0)
        ped = json.loads(completed.stdout)["symbols"][0]
        self.assertEqual(ped["parents"], ["Element"])
        self.assertEqual(next(item for item in ped["methods"] if item["name"] == "kill")["globalFunctions"], ["killPed"])
        completed = self.run_cli("api", "get", "element-type", "--kind", "enum", "--side", "server")
        self.assertEqual(completed.returncode, 0)
        self.assertIn("vehicle", json.loads(completed.stdout)["symbols"][0]["values"])

    def test_search_indexes_oop_bindings_and_enum_values(self) -> None:
        completed = self.run_cli("api", "search", "killPed", "--kind", "class", "--limit", "5")
        self.assertEqual(completed.returncode, 0)
        self.assertIn("Ped", [symbol["name"] for symbol in json.loads(completed.stdout)["symbols"]])
        completed = self.run_cli("api", "search", "db-connection", "--kind", "enum", "--limit", "5")
        self.assertEqual(completed.returncode, 0)
        self.assertEqual(json.loads(completed.stdout)["symbols"][0]["name"], "element-type")

    def test_get_unknown_entity_has_stable_failure(self) -> None:
        completed = self.run_cli("api", "get", "definitelyMissingEntity")
        self.assertEqual(completed.returncode, 1)
        result = json.loads(completed.stdout)
        self.assertEqual(result["diagnostics"][0]["code"], "API_NOT_FOUND")
        self.assertEqual(completed.stderr, "")

    def test_empty_search_is_rejected(self) -> None:
        completed = self.run_cli("api", "search", "   ")
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(json.loads(completed.stdout)["diagnostics"][0]["code"], "API_QUERY_EMPTY")

    def test_documented_only_function_is_not_claimed_as_runtime_available(self) -> None:
        catalogue = load_json(CATALOGUE_PATH)
        symbol = next(item for item in catalogue["symbols"] if item["kind"] == "function" and item["name"] == "inspect")
        self.assertEqual(symbol["state"], "documented-only")
        project = base_project()
        project["requiredApis"] = [{"name": "inspect", "side": "server"}]
        with tempfile.TemporaryDirectory(prefix="neon-doc-only-") as temporary:
            path = Path(temporary) / "neon.project.json"
            write_json(path, project)
            result = check_project(path, SCHEMAS, CATALOGUE_PATH)
        self.assertEqual(diagnostic_codes(result), ["API_UNAVAILABLE"])


class ProjectHarnessTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="neon-closed-harness-")
        self.root = Path(self.temporary.name)
        self.project_path = self.root / "neon.project.json"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_project(self, project: dict) -> None:
        write_json(self.project_path, project)

    def add_resource(
        self, project: dict, *, meta: str, files: dict[str, str] | None = None, name: str = "demo",
        manifest: dict | str | None = None,
    ) -> None:
        resource = self.root / "resources" / name
        resource.mkdir(parents=True)
        (resource / "meta.xml").write_text(meta, encoding="utf-8")
        for relative, content in (files or {}).items():
            path = resource / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        entry = {"name": name, "path": f"resources/{name}"}
        if manifest is not None:
            manifest_path = resource / "neon.component.yaml"
            if isinstance(manifest, str):
                manifest_path.write_text(manifest, encoding="utf-8")
            else:
                write_json(manifest_path, manifest)
            entry["manifest"] = "neon.component.yaml"
        project["resources"].append(entry)

    def add_module(self, project: dict, *, name: str = "native", manifest: dict | str | None = None, binary: bool = True) -> None:
        module = self.root / "modules" / name
        module.mkdir(parents=True)
        entry = {"name": name, "path": f"modules/{name}"}
        if manifest is not None:
            manifest_path = module / "neon.module.yaml"
            if isinstance(manifest, str):
                manifest_path.write_text(manifest, encoding="utf-8")
            else:
                write_json(manifest_path, manifest)
            entry["manifest"] = "neon.module.yaml"
        if binary:
            (module / "native.dll").write_bytes(b"closed-harness-placeholder")
            entry["binary"] = "native.dll"
        project.setdefault("modules", []).append(entry)

    def check(self, project: dict) -> dict:
        self.write_project(project)
        return check_project(self.project_path, SCHEMAS, CATALOGUE_PATH)

    def test_valid_client_server_project_passes(self) -> None:
        project = base_project()
        self.add_resource(
            project,
            meta='<meta><script src="server.lua" type="server"/><script src="client.lua" type="client"/></meta>',
            files={"server.lua": "local vehicle = createVehicle(411, 0, 0, 3)\n", "client.lua": "dxDrawText('ready', 0, 0)\n"},
        )
        result = self.check(project)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["summary"], {"errors": 0, "warnings": 0, "resources": 1, "files": 2, "apiRequirements": 0})

    def test_missing_function_has_stable_json_contract(self) -> None:
        project = base_project()
        project["requiredApis"] = [{"name": "definitelyMissingNeonFunction", "side": "client"}]
        self.write_project(project)
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "check", "--project", str(self.project_path), "--catalogue", str(CATALOGUE_PATH), "--json"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        expected = {
            "schemaVersion": "1.0.0",
            "command": "check",
            "status": "fail",
            "summary": {"errors": 1, "warnings": 0, "resources": 0, "files": 0, "apiRequirements": 1},
            "diagnostics": [{
                "code": "API_NOT_FOUND", "severity": "error", "message": "required API definitelyMissingNeonFunction does not exist",
                "path": "neon.project.json", "side": "client", "symbol": "definitelyMissingNeonFunction",
            }],
        }
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stderr, "")
        self.assertEqual(completed.stdout, canonical_json(expected))
        repeat = subprocess.run(completed.args, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        self.assertEqual(repeat.stdout, completed.stdout)

    def test_cli_uses_project_in_current_working_directory_by_default(self) -> None:
        project = base_project()
        self.write_project(project)
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "check", "--catalogue", str(CATALOGUE_PATH), "--json"],
            cwd=self.root,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(completed.returncode, 0)
        self.assertEqual(json.loads(completed.stdout)["status"], "pass")
        self.assertEqual(completed.stderr, "")

    def test_known_api_on_wrong_side_is_rejected(self) -> None:
        project = base_project()
        self.add_resource(project, meta='<meta><script src="server.lua" type="server"/></meta>', files={"server.lua": "dxDrawText('bad', 0, 0)\n"})
        result = self.check(project)
        self.assertIn("API_WRONG_SIDE", diagnostic_codes(result))
        diagnostic = next(item for item in result["diagnostics"] if item["code"] == "API_WRONG_SIDE")
        self.assertEqual((diagnostic["side"], diagnostic["symbol"], diagnostic["line"]), ("server", "dxDrawText", 1))

    def test_known_event_on_wrong_side_is_rejected_but_custom_events_are_allowed(self) -> None:
        project = base_project()
        self.add_resource(
            project,
            meta='<meta><script src="server.lua" type="server"/></meta>',
            files={"server.lua": '''
                addEventHandler("onClientRender", root, function() end)
                addEventHandler("onMyCustomEvent", root, function() end)
                -- addEventHandler("onClientClick", root, function() end)
            '''},
        )
        result = self.check(project)
        self.assertEqual(diagnostic_codes(result), ["EVENT_WRONG_SIDE"])
        diagnostic = result["diagnostics"][0]
        self.assertEqual((diagnostic["side"], diagnostic["symbol"], diagnostic["line"]), ("server", "onClientRender", 2))

    def test_conflicting_global_function_and_event_are_blocked_from_active_contracts(self) -> None:
        project = base_project()
        self.add_resource(
            project,
            meta='<meta><script src="server.lua" type="server"/><script src="client.lua" type="client"/></meta>',
            files={
                "server.lua": "getPlayerBlurLevel(source)\n",
                "client.lua": "addEventHandler('onClientBrowserNavigate', root, function() end)\n",
            },
        )
        result = self.check(project)
        self.assertEqual(diagnostic_codes(result), ["API_CONFLICT", "EVENT_CONFLICT"])
        self.assertEqual({item["symbol"] for item in result["diagnostics"]}, {"getPlayerBlurLevel", "onClientBrowserNavigate"})

    def test_conflicting_symbols_cannot_fall_through_as_unknown_on_opposite_side(self) -> None:
        project = base_project()
        self.add_resource(
            project,
            meta='<meta><script src="server.lua" type="server"/><script src="client.lua" type="client"/></meta>',
            files={
                "server.lua": "addEventHandler('onClientBrowserNavigate', root, function() end)\n",
                "client.lua": "getPlayerBlurLevel(localPlayer)\n",
            },
        )
        result = self.check(project)
        self.assertEqual(diagnostic_codes(result), ["API_CONFLICT", "EVENT_CONFLICT"])
        self.assertEqual({item["symbol"] for item in result["diagnostics"]}, {"getPlayerBlurLevel", "onClientBrowserNavigate"})

    def test_catalogued_but_profile_excluded_symbols_are_not_allowed_unknowns(self) -> None:
        project = base_project()
        project["profile"] = "neon-server"
        self.add_resource(
            project,
            meta='<meta><script src="server.lua" type="server"/></meta>',
            files={"server.lua": "dxDrawText('wrong profile', 0, 0)\naddEventHandler('onClientRender', root, function() end)\n"},
        )
        result = self.check(project)
        self.assertEqual(diagnostic_codes(result), ["API_UNAVAILABLE", "EVENT_UNAVAILABLE"])

    def test_profile_excluded_oop_static_and_inferred_instance_calls_are_rejected(self) -> None:
        project = base_project()
        project["profile"] = "mta-upstream"
        self.add_resource(
            project,
            meta='<meta><oop>true</oop><script src="client.lua" type="client"/></meta>',
            files={"client.lua": "local bird = Bird.create(0, 0, 3, {})\nbird:setColors(0xFFFFFFFF, 0xFF000000)\n"},
        )
        result = self.check(project)
        self.assertEqual(diagnostic_codes(result), ["API_UNAVAILABLE", "API_UNAVAILABLE"])
        self.assertEqual({item["symbol"] for item in result["diagnostics"]}, {"Bird.create", "Bird.setColors"})

    def test_oop_calls_are_checked_by_profile_and_side_without_blocking_valid_neon_calls(self) -> None:
        client_project = base_project()
        client_project["profile"] = "neon-client"
        self.add_resource(
            client_project,
            meta='<meta><oop>true</oop><script src="client.lua" type="client"/></meta>',
            files={"client.lua": "local bird = Bird.create(0, 0, 3, {})\nbird:setColors(0xFFFFFFFF, 0xFF000000)\n"},
        )
        self.assertEqual(self.check(client_project)["status"], "pass")

        server_project = base_project()
        self.add_resource(
            server_project,
            name="server-demo",
            meta='<meta><oop>true</oop><script src="server.lua" type="server"/></meta>',
            files={"server.lua": "local bird = Bird.create(0, 0, 3, {})\nbird:setColors(0xFFFFFFFF, 0xFF000000)\n"},
        )
        result = self.check(server_project)
        self.assertEqual(diagnostic_codes(result), ["API_WRONG_SIDE", "API_WRONG_SIDE"])

    def test_registered_oop_use_requires_resource_oop_activation(self) -> None:
        project = base_project()
        self.add_resource(
            project,
            meta='<meta><script src="server.lua" type="server"/></meta>',
            files={"server.lua": "Ped.create(7, 0, 0, 3)\n"},
        )
        result = self.check(project)
        self.assertEqual(diagnostic_codes(result), ["RESOURCE_OOP_MISSING"])

    def test_profile_excluded_required_api_is_reported_unavailable(self) -> None:
        project = base_project()
        project["profile"] = "mta-upstream"
        project["requiredApis"] = [{"name": "createBird", "side": "client"}]
        result = self.check(project)
        self.assertEqual(diagnostic_codes(result), ["API_UNAVAILABLE"])

    def test_conflicting_required_api_is_rejected_explicitly(self) -> None:
        project = base_project()
        project["requiredApis"] = [{"name": "getPlayerBlurLevel", "side": "server"}]
        self.assertEqual(diagnostic_codes(self.check(project)), ["API_CONFLICT"])

    def test_incompatible_engine_version_is_rejected(self) -> None:
        project = base_project()
        project["engine"] = {"minimumVersion": "2.0.0", "maximumVersionExclusive": "3.0.0"}
        self.assertEqual(diagnostic_codes(self.check(project)), ["ENGINE_VERSION_INCOMPATIBLE"])

    def test_invalid_engine_range_is_rejected(self) -> None:
        project = base_project()
        project["engine"] = {"minimumVersion": "1.8.0", "maximumVersionExclusive": "1.7.0"}
        self.assertEqual(diagnostic_codes(self.check(project)), ["ENGINE_VERSION_RANGE_INVALID"])

    def test_unsupported_schema_major_is_rejected(self) -> None:
        project = base_project()
        project["schemaVersion"] = "2.0.0"
        self.assertEqual(diagnostic_codes(self.check(project)), ["SCHEMA_VERSION_UNSUPPORTED"])

    def test_missing_script_file_is_rejected(self) -> None:
        project = base_project()
        self.add_resource(project, meta='<meta><script src="missing.lua" type="client"/></meta>')
        self.assertIn("MISSING_FILE", diagnostic_codes(self.check(project)))

    def test_missing_resource_dependency_is_rejected(self) -> None:
        project = base_project()
        self.add_resource(project, meta='<meta><include resource="inventory"/><script src="server.lua" type="server"/></meta>', files={"server.lua": "print('x')\n"})
        self.assertIn("MISSING_DEPENDENCY", diagnostic_codes(self.check(project)))

    def test_external_dependency_is_accepted(self) -> None:
        project = base_project()
        project["externalDependencies"] = ["inventory"]
        self.add_resource(project, meta='<meta><include resource="inventory"/><script src="server.lua" type="server"/></meta>', files={"server.lua": "print('x')\n"})
        self.assertEqual(self.check(project)["status"], "pass")

    def test_profile_rejects_opposite_side_script(self) -> None:
        project = base_project()
        project["profile"] = "neon-server"
        self.add_resource(project, meta='<meta><script src="client.lua" type="client"/></meta>', files={"client.lua": "dxDrawText('x', 0, 0)\n"})
        self.assertIn("PROFILE_SIDE_MISMATCH", diagnostic_codes(self.check(project)))

    def test_duplicate_json_keys_are_rejected(self) -> None:
        self.project_path.write_text('{"schemaVersion":"1.0.0","schemaVersion":"1.0.0"}', encoding="utf-8")
        result = check_project(self.project_path, SCHEMAS, CATALOGUE_PATH)
        self.assertEqual(diagnostic_codes(result), ["PROJECT_JSON_INVALID"])
        self.assertIn("duplicate object key", result["diagnostics"][0]["message"])

    def test_nonfinite_json_numbers_are_rejected(self) -> None:
        self.project_path.write_text('{"schemaVersion":"1.0.0","value":NaN}', encoding="utf-8")
        result = check_project(self.project_path, SCHEMAS, CATALOGUE_PATH)
        self.assertEqual(diagnostic_codes(result), ["PROJECT_JSON_INVALID"])
        self.assertIn("non-finite JSON number", result["diagnostics"][0]["message"])

    def test_schema_rejects_unknown_project_field_and_traversal(self) -> None:
        project = base_project()
        project["typo"] = True
        project["catalogue"] = "../outside.json"
        codes = diagnostic_codes(self.check(project))
        self.assertEqual(codes, ["PROJECT_SCHEMA_INVALID", "PROJECT_SCHEMA_INVALID"])

    def test_dtd_and_entities_are_rejected(self) -> None:
        project = base_project()
        self.add_resource(project, meta='<!DOCTYPE meta [<!ENTITY x "boom">]><meta><script src="x.lua"/></meta>', files={"x.lua": "print('x')\n"})
        self.assertEqual(diagnostic_codes(self.check(project)), ["UNSAFE_XML"])

    def test_malformed_xml_and_duplicate_attributes_are_rejected(self) -> None:
        for index, meta in enumerate(('<meta><script src="x.lua"></meta>', '<meta><script src="x.lua" src="y.lua"/></meta>')):
            with self.subTest(meta=meta):
                project = base_project()
                self.add_resource(project, meta=meta, files={"x.lua": "print('x')\n"}, name=f"malformed-{index}")
                self.assertEqual(diagnostic_codes(self.check(project)), ["INVALID_META"])

    def test_duplicate_resource_names_are_rejected(self) -> None:
        project = base_project()
        self.add_resource(project, meta="<meta/>", name="demo")
        project["resources"].append({"name": "demo", "path": "resources/demo"})
        self.assertIn("DUPLICATE_RESOURCE", diagnostic_codes(self.check(project)))

    def test_missing_resource_directory_is_rejected(self) -> None:
        project = base_project()
        project["resources"] = [{"name": "missing", "path": "resources/missing"}]
        self.assertEqual(diagnostic_codes(self.check(project)), ["MISSING_RESOURCE"])

    @unittest.skipIf(os.name == "nt", "creating symlinks is not reliably available to unprivileged Windows tests")
    def test_resource_symlink_cannot_escape_workspace(self) -> None:
        project = base_project()
        outside = Path(self.temporary.name + "-outside")
        outside.mkdir()
        try:
            resources = self.root / "resources"
            resources.mkdir()
            (resources / "escape").symlink_to(outside, target_is_directory=True)
            project["resources"] = [{"name": "escape", "path": "resources/escape"}]
            self.assertEqual(diagnostic_codes(self.check(project)), ["PATH_OUTSIDE_WORKSPACE"])
        finally:
            outside.rmdir()

    def test_strict_unknown_api_ignores_declared_functions_but_rejects_unknown_global(self) -> None:
        project = base_project()
        project["unknownApis"] = "error"
        source = "local function helper() end\nhelper()\nunknownAgentCall()\n"
        self.add_resource(project, meta='<meta><script src="server.lua" type="server"/></meta>', files={"server.lua": source})
        result = self.check(project)
        unknown = [item for item in result["diagnostics"] if item["code"] == "API_UNKNOWN"]
        self.assertEqual([(item["symbol"], item["line"]) for item in unknown], [("unknownAgentCall", 3)])

    def test_strict_unknown_api_ignores_anonymous_callbacks_and_parenthesized_keywords(self) -> None:
        project = base_project()
        project["unknownApis"] = "error"
        source = """addEventHandler("onClientRender", root, function()
    if (true) then
        dxDrawText("ready", 0, 0)
    end
end)
"""
        self.add_resource(project, meta='<meta><script src="client.lua" type="client"/></meta>', files={"client.lua": source})
        self.assertEqual(self.check(project)["status"], "pass")

    def test_lua_comments_and_strings_do_not_create_calls(self) -> None:
        project = base_project()
        project["unknownApis"] = "error"
        source = "-- fakeCall()\nlocal text = 'alsoFake()'\n--[[ hiddenCall() ]]\nprint(text)\n"
        self.add_resource(project, meta='<meta><script src="server.lua" type="server"/></meta>', files={"server.lua": source})
        self.assertEqual(self.check(project)["status"], "pass")

    def test_complete_resource_contract_matches_meta_lua_event_acl_and_resolves_stably(self) -> None:
        project = base_project()
        manifest = base_component("inventory")
        manifest["exports"] = [{
            "name": "takeItem", "side": "server", "parameters": [{"name": "item", "type": "string", "optional": False, "description": "Item identifier."}],
            "returns": [{"name": "removed", "type": "boolean", "description": "Whether the item was removed."}],
            "description": "Remove one item.", "http": False, "restricted": False,
        }]
        manifest["events"] = [{
            "name": "inventoryChanged", "side": "server", "directions": ["defines", "emits"], "parameters": [],
            "allowRemoteTrigger": False, "description": "Signals an inventory mutation.",
        }]
        manifest["acl"] = [{"right": "function.restartResource", "required": True, "reason": "Restart the owned inventory resource."}]
        meta = (
            '<meta><script src="server.lua" type="server"/><export function="takeItem" type="server" http="false" restricted="false"/>'
            '<aclrequest><right name="function.restartResource" access="true"/></aclrequest></meta>'
        )
        source = "function takeItem(item) return true end\naddEvent('inventoryChanged', false)\n"
        self.add_resource(project, name="inventory", meta=meta, files={"server.lua": source}, manifest=manifest)
        result = self.check(project)
        self.assertEqual(result["status"], "pass")
        resolved_a = resolve_project_components(self.project_path, SCHEMAS, CATALOGUE_PATH)
        resolved_b = resolve_project_components(self.project_path, SCHEMAS, CATALOGUE_PATH)
        self.assertEqual(canonical_json(resolved_a), canonical_json(resolved_b))
        self.assertEqual(SCHEMAS.validate("neon-project-api", resolved_a), [])
        self.assertEqual(resolved_a["components"][0]["state"], "verified")
        self.assertEqual(
            [(item["id"], item["state"]) for item in resolved_a["symbols"]],
            [("resource:inventory:event:inventoryChanged", "verified"), ("resource:inventory:server-export:takeItem", "verified")],
        )
        self.assertEqual({item["signatureKnown"] for item in resolved_a["symbols"]}, {True})

    def test_manifest_export_must_match_meta_global_implementation_and_security_flags(self) -> None:
        project = base_project()
        manifest = base_component("inventory")
        manifest["exports"] = [{
            "name": "takeItem", "side": "server", "parameters": [], "returns": [], "description": "Remove an item.",
            "http": False, "restricted": True,
        }]
        self.add_resource(
            project, name="inventory", manifest=manifest,
            meta='<meta><script src="server.lua" type="server"/><export function="takeItem" type="server" restricted="false"/></meta>',
            files={"server.lua": "local function takeItem() end\n"},
        )
        codes = diagnostic_codes(self.check(project))
        self.assertIn("RESOURCE_EXPORT_IMPLEMENTATION_MISSING", codes)
        self.assertIn("RESOURCE_EXPORT_SECURITY_MISMATCH", codes)

    def test_manifest_export_absent_from_meta_is_rejected(self) -> None:
        project = base_project()
        manifest = base_component()
        manifest["exports"] = [{"name": "publicCall", "side": "server", "parameters": [], "returns": [], "description": "Public call.", "http": False, "restricted": False}]
        self.add_resource(project, manifest=manifest, meta='<meta><script src="server.lua" type="server"/></meta>', files={"server.lua": "function publicCall() end\n"})
        self.assertIn("RESOURCE_EXPORT_NOT_IN_META", diagnostic_codes(self.check(project)))

    def test_shared_export_requires_both_meta_sides_and_both_implementations(self) -> None:
        project = base_project()
        manifest = base_component()
        manifest["exports"] = [{"name": "sharedCall", "side": "shared", "parameters": [], "returns": [], "description": "Shared call.", "http": False, "restricted": False}]
        self.add_resource(
            project, manifest=manifest,
            meta='<meta><script src="shared.lua" type="shared"/><export function="sharedCall" type="server"/></meta>',
            files={"shared.lua": "function sharedCall() end\n"},
        )
        self.assertIn("RESOURCE_EXPORT_SIDE_MISMATCH", diagnostic_codes(self.check(project)))
        resource = self.root / "resources" / "demo"
        (resource / "meta.xml").write_text('<meta><script src="shared.lua" type="shared"/><export function="sharedCall" type="server"/><export function="sharedCall" type="client"/></meta>', encoding="utf-8")
        self.assertEqual(self.check(project)["status"], "pass")

    def test_opaque_resource_contract_is_warning_by_default_and_error_in_strict_mode(self) -> None:
        project = base_project()
        self.add_resource(project, meta='<meta><script src="server.lua"/><export function="legacy" type="server"/></meta>', files={"server.lua": "function legacy() end\naddEvent('legacyChanged', true)\n"})
        allowed = self.check(project)
        self.assertEqual(allowed["status"], "pass")
        self.assertEqual(set(diagnostic_codes(allowed)), {"RESOURCE_EXPORT_OPAQUE", "RESOURCE_EVENT_OPAQUE"})
        project["unknownComponents"] = "error"
        strict = self.check(project)
        self.assertEqual(strict["status"], "fail")
        self.assertEqual(strict["summary"]["errors"], 2)
        resolved = resolve_project_components(self.project_path, SCHEMAS, CATALOGUE_PATH)
        self.assertEqual({item["state"] for item in resolved["symbols"]}, {"opaque"})

    def test_custom_event_definition_remote_flag_and_side_are_checked(self) -> None:
        project = base_project()
        manifest = base_component("events")
        manifest["events"] = [{"name": "serverSignal", "side": "server", "directions": ["defines"], "parameters": [], "allowRemoteTrigger": False, "description": "Server signal."}]
        self.add_resource(project, name="events", manifest=manifest, meta='<meta><script src="server.lua" type="server"/></meta>', files={"server.lua": "addEvent('serverSignal', true)\n"})
        self.add_resource(project, name="consumer", meta='<meta><script src="client.lua" type="client"/></meta>', files={"client.lua": "addEventHandler('serverSignal', root, function() end)\n"})
        codes = diagnostic_codes(self.check(project))
        self.assertIn("RESOURCE_EVENT_REMOTE_MISMATCH", codes)
        self.assertIn("EVENT_WRONG_SIDE", codes)

    def test_manifest_defined_event_requires_source_definition(self) -> None:
        project = base_project()
        manifest = base_component()
        manifest["events"] = [{"name": "missingSignal", "side": "server", "directions": ["defines"], "parameters": [], "allowRemoteTrigger": False, "description": "Missing signal."}]
        self.add_resource(project, manifest=manifest, meta='<meta><script src="server.lua" type="server"/></meta>', files={"server.lua": "print('no event')\n"})
        self.assertIn("RESOURCE_EVENT_DEFINITION_MISSING", diagnostic_codes(self.check(project)))

    def test_add_event_default_remote_flag_is_understood_as_false(self) -> None:
        project = base_project()
        manifest = base_component()
        manifest["events"] = [{"name": "localSignal", "side": "server", "directions": ["defines"], "parameters": [], "allowRemoteTrigger": False, "description": "Local signal."}]
        self.add_resource(project, manifest=manifest, meta='<meta><script src="server.lua"/></meta>', files={"server.lua": "addEvent('localSignal')\n"})
        self.assertEqual(self.check(project)["status"], "pass")

    def test_resource_export_calls_validate_provider_name_and_side(self) -> None:
        project = base_project()
        manifest = base_component("inventory")
        manifest["exports"] = [{"name": "takeItem", "side": "server", "parameters": [], "returns": [], "description": "Take an item.", "http": False, "restricted": False}]
        self.add_resource(project, name="inventory", manifest=manifest, meta='<meta><script src="server.lua"/><export function="takeItem" type="server"/></meta>', files={"server.lua": "function takeItem() end\n"})
        self.add_resource(project, name="client-consumer", meta='<meta><script src="client.lua" type="client"/></meta>', files={"client.lua": "exports.inventory:takeItem()\nexports.inventory:missingCall()\nexports.ghost:anyCall()\n"})
        result = self.check(project)
        self.assertIn("RESOURCE_EXPORT_WRONG_SIDE", diagnostic_codes(result))
        self.assertIn("RESOURCE_EXPORT_UNKNOWN", diagnostic_codes(result))
        self.assertIn("RESOURCE_EXPORT_PROVIDER_UNKNOWN", diagnostic_codes(result))
        self.assertEqual(result["summary"]["errors"], 1)
        self.assertEqual(result["summary"]["warnings"], 2)

    def test_acl_and_required_manifest_dependency_are_checked(self) -> None:
        project = base_project()
        manifest = base_component("gameplay")
        manifest["acl"] = [{"right": "function.kickPlayer", "required": True, "reason": "Moderate the local test session."}]
        manifest["dependencies"] = [{"kind": "resource", "name": "inventory", "optional": False}]
        self.add_resource(project, name="gameplay", manifest=manifest, meta='<meta><script src="server.lua"/></meta>', files={"server.lua": "print('x')\n"})
        codes = diagnostic_codes(self.check(project))
        self.assertIn("COMPONENT_DEPENDENCY_MISSING", codes)
        self.assertIn("COMPONENT_DEPENDENCY_UNDECLARED", codes)
        self.assertIn("RESOURCE_ACL_MISSING", codes)

    def test_acl_right_outside_aclrequest_cannot_satisfy_security_contract(self) -> None:
        project = base_project()
        manifest = base_component()
        manifest["acl"] = [{"right": "function.kickPlayer", "required": True, "reason": "Moderate the test session."}]
        self.add_resource(project, manifest=manifest, meta='<meta><unrelated><right name="function.kickPlayer" access="true"/></unrelated></meta>')
        self.assertIn("RESOURCE_ACL_MISSING", diagnostic_codes(self.check(project)))

    def test_dependency_minimum_version_is_checked_and_unknown_versions_are_not_guessed(self) -> None:
        project = base_project()
        inventory = base_component("inventory")
        inventory["version"] = "1.4.0"
        gameplay = base_component("gameplay")
        gameplay["dependencies"] = [{"kind": "resource", "name": "inventory", "optional": False, "minimumVersion": "2.0.0"}]
        self.add_resource(project, name="inventory", manifest=inventory, meta="<meta/>")
        self.add_resource(project, name="gameplay", manifest=gameplay, meta='<meta><include resource="inventory"/></meta>')
        self.assertIn("COMPONENT_DEPENDENCY_VERSION_INCOMPATIBLE", diagnostic_codes(self.check(project)))
        del project["resources"][0]["manifest"]
        self.assertIn("COMPONENT_DEPENDENCY_VERSION_UNKNOWN", diagnostic_codes(self.check(project)))

    def test_oop_requirement_checks_true_value_not_tag_presence(self) -> None:
        project = base_project()
        manifest = base_component()
        manifest["oopRequired"] = True
        self.add_resource(project, manifest=manifest, meta="<meta><oop>false</oop></meta>")
        self.assertIn("RESOURCE_OOP_MISSING", diagnostic_codes(self.check(project)))
        (self.root / "resources" / "demo" / "meta.xml").write_text("<meta><oop>true</oop></meta>", encoding="utf-8")
        self.assertEqual(self.check(project)["status"], "pass")

    def test_module_contract_exposes_declared_function_without_claiming_runtime_verification(self) -> None:
        project = base_project()
        manifest = base_component("native", "module")
        manifest["exports"] = [{"name": "nativePing", "side": "server", "parameters": [], "returns": [], "description": "Ping the native module.", "http": False, "restricted": False}]
        self.add_module(project, manifest=manifest)
        self.add_resource(project, meta='<meta><script src="server.lua"/></meta>', files={"server.lua": "nativePing()\n"})
        self.assertEqual(self.check(project)["status"], "pass")
        resolved = resolve_project_components(self.project_path, SCHEMAS, CATALOGUE_PATH)
        module = next(item for item in resolved["components"] if item["kind"] == "module")
        symbol = next(item for item in resolved["symbols"] if item["ownerKind"] == "module")
        self.assertEqual((module["state"], symbol["state"]), ("documented-only", "documented-only"))
        self.assertEqual(module["binary"]["size"], len(b"closed-harness-placeholder"))
        self.assertRegex(module["binary"]["sha256"], r"^[0-9a-f]{64}$")

    def test_module_without_manifest_is_opaque_and_binary_path_is_bounded(self) -> None:
        project = base_project()
        self.add_module(project, manifest=None)
        allowed = self.check(project)
        self.assertEqual((allowed["status"], diagnostic_codes(allowed)), ("pass", ["MODULE_OPAQUE"]))
        project["unknownComponents"] = "error"
        project["modules"][0]["binary"] = "../outside.dll"
        codes = diagnostic_codes(self.check(project))
        self.assertIn("PROJECT_SCHEMA_INVALID", codes)

    @unittest.skipIf(os.name == "nt", "creating symlinks is not reliably available to unprivileged Windows tests")
    def test_component_manifest_and_module_binary_symlinks_cannot_escape_their_roots(self) -> None:
        project = base_project()
        self.add_resource(project, meta="<meta/>")
        self.add_module(project, manifest=None, binary=False)
        outside = Path(self.temporary.name + "-component-outside")
        outside.mkdir()
        try:
            outside_manifest = outside / "manifest.json"
            write_json(outside_manifest, base_component())
            outside_binary = outside / "native.dll"
            outside_binary.write_bytes(b"outside")
            (self.root / "resources" / "demo" / "manifest.yaml").symlink_to(outside_manifest)
            (self.root / "modules" / "native" / "native.dll").symlink_to(outside_binary)
            project["resources"][0]["manifest"] = "manifest.yaml"
            project["modules"][0]["binary"] = "native.dll"
            codes = diagnostic_codes(self.check(project))
            self.assertEqual(codes.count("PATH_OUTSIDE_WORKSPACE"), 2)
        finally:
            outside_manifest.unlink()
            outside_binary.unlink()
            outside.rmdir()

    def test_component_kind_name_and_json_compatible_yaml_are_enforced(self) -> None:
        for index, manifest, expected in (
            (0, {**base_component("other"), "name": "other"}, "COMPONENT_NAME_MISMATCH"),
            (1, base_component("demo-1", "module"), "COMPONENT_KIND_MISMATCH"),
            (2, "schemaVersion: 1.0.0\nkind: resource\n", "COMPONENT_MANIFEST_INVALID"),
        ):
            with self.subTest(expected=expected):
                project = base_project()
                self.add_resource(project, name=f"demo-{index}", manifest=manifest, meta="<meta/>")
                result = self.check(project)
                self.assertIn(expected, diagnostic_codes(result))
                if index == 2:
                    self.assertIn("JSON-compatible YAML 1.2", next(item["message"] for item in result["diagnostics"] if item["code"] == expected))

    def test_manifest_does_not_hide_additional_opaque_observations_from_resolution(self) -> None:
        project = base_project()
        manifest = base_component()
        self.add_resource(
            project, manifest=manifest,
            meta='<meta><script src="server.lua"/><export function="legacy" type="server"/></meta>',
            files={"server.lua": "function legacy() end\naddEvent('legacySignal', false)\n"},
        )
        self.write_project(project)
        resolved = resolve_project_components(self.project_path, SCHEMAS, CATALOGUE_PATH)
        self.assertEqual(resolved["status"], "pass")
        self.assertEqual({item["id"] for item in resolved["symbols"]}, {"resource:demo:server-export:legacy", "resource:demo:event:legacySignal"})
        self.assertEqual({item["state"] for item in resolved["symbols"]}, {"opaque"})
        self.assertEqual({item["signatureKnown"] for item in resolved["symbols"]}, {False})
        self.assertTrue(all("parameters" not in item and "returns" not in item for item in resolved["symbols"]))

    def test_project_resolution_preserves_resource_sides_without_public_symbols(self) -> None:
        project = base_project()
        self.add_resource(
            project, manifest=base_component(),
            meta='<meta><script src="client.lua" type="client"/></meta>',
            files={"client.lua": "local ready = true\n"},
        )
        self.write_project(project)
        resolved = resolve_project_components(self.project_path, SCHEMAS, CATALOGUE_PATH)
        self.assertEqual((resolved["status"], resolved["symbols"]), ("pass", []))
        self.assertEqual(resolved["components"][0]["sides"], ["client"])
        self.assertEqual(SCHEMAS.validate("neon-project-api", resolved), [])

    def test_project_resolve_cli_emits_schema_valid_byte_stable_json(self) -> None:
        project = base_project()
        manifest = base_component()
        self.add_resource(project, manifest=manifest, meta="<meta/>")
        self.write_project(project)
        command = [sys.executable, str(CLI_PATH), "project", "resolve", "--project", str(self.project_path), "--catalogue", str(CATALOGUE_PATH), "--json"]
        first = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        second = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        self.assertEqual((first.returncode, first.stderr), (0, ""))
        self.assertEqual(first.stdout, second.stdout)
        self.assertEqual(SCHEMAS.validate("neon-project-api", json.loads(first.stdout)), [])

    def test_project_resolve_cli_failure_remains_a_complete_schema_valid_catalogue(self) -> None:
        project = base_project()
        manifest = base_component()
        manifest["exports"] = [{"name": "missingExport", "side": "server", "parameters": [], "returns": [], "description": "Missing export.", "http": False, "restricted": False}]
        self.add_resource(project, manifest=manifest, meta="<meta/>")
        self.write_project(project)
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "project", "resolve", "--project", str(self.project_path), "--catalogue", str(CATALOGUE_PATH), "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        result = json.loads(completed.stdout)
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(result["command"], "project.resolve")
        self.assertEqual(SCHEMAS.validate("neon-project-api", result), [])
        self.assertEqual(result["components"][0]["state"], "conflict")
        self.assertEqual(result["symbols"][0]["state"], "conflict")


class LuaLsGenerationTests(unittest.TestCase):
    def test_generation_is_byte_stable_and_separates_sides(self) -> None:
        catalogue = load_json(CATALOGUE_PATH)
        with tempfile.TemporaryDirectory(prefix="neon-luals-a-") as first, tempfile.TemporaryDirectory(prefix="neon-luals-b-") as second:
            first_index = generate_luals(catalogue, Path(first))
            second_index = generate_luals(catalogue, Path(second))
            self.assertEqual(first_index, second_index)
            for artifact in first_index["artifacts"]:
                self.assertEqual(SCHEMAS.validate("neon-artifact", artifact), [])
            for filename in ("mta-shared.lua", "mta-client.lua", "mta-server.lua", "artifacts.json"):
                self.assertEqual((Path(first) / filename).read_bytes(), (Path(second) / filename).read_bytes())
            client = (Path(first) / "mta-client.lua").read_text(encoding="utf-8")
            server = (Path(first) / "mta-server.lua").read_text(encoding="utf-8")
            shared = (Path(first) / "mta-shared.lua").read_text(encoding="utf-8")
            self.assertIn("function dxDrawText(...)", client)
            self.assertNotIn("function dxDrawText(...)", server)
            self.assertIn("function createVehicle(...)", shared)
            self.assertIn("---@param model integer", shared)
            self.assertIn("---@return vehicle value", shared)
            self.assertIn("---@class Vector3: element", shared)
            self.assertIn("---@param ... unknown", shared)

    def test_runtime_only_signatures_are_explicitly_unknown_and_checked_in_output_matches(self) -> None:
        catalogue = load_json(CATALOGUE_PATH)
        shared = render_luals(catalogue, "shared")
        opaque = shared[shared.index("--- `mta:function:base64Encode`"):]
        opaque = opaque[:opaque.index("\n\n", 1)]
        self.assertIn("---@param ... unknown", opaque)
        self.assertIn("---@return unknown", opaque)
        self.assertNotIn("---@return nil", opaque)
        self.assertNotIn("function getPlayerBlurLevel(...)", shared)
        with tempfile.TemporaryDirectory(prefix="neon-luals-checked-") as temporary:
            generate_luals(catalogue, Path(temporary))
            for filename in ("mta-shared.lua", "mta-client.lua", "mta-server.lua", "artifacts.json"):
                self.assertEqual((Path(temporary) / filename).read_bytes(), (TOOL_DIRECTORY / "generated" / filename).read_bytes())

    def test_upstream_profile_excludes_neon_only_function(self) -> None:
        catalogue = load_json(CATALOGUE_PATH)
        current = "".join(render_luals(catalogue, side, "neon-pair") for side in ("shared", "client", "server"))
        upstream = "".join(render_luals(catalogue, side, "mta-upstream") for side in ("shared", "client", "server"))
        self.assertIn("function createRope(...)", current)
        self.assertNotIn("function createRope(...)", upstream)
        index = build_api_index(catalogue, "mta-upstream", "0" * 64)
        ids = {item["id"] for item in index["symbols"]}
        self.assertNotIn("neon:function:createRope", ids)
        self.assertNotIn("neon:class:Bird", ids)

    def test_oop_luals_uses_exact_side_bindings_and_safe_semantics(self) -> None:
        catalogue = load_json(CATALOGUE_PATH)
        client = render_luals(catalogue, "client")
        server = render_luals(catalogue, "server")
        self.assertIn("---@class Ped: Element", server)
        self.assertIn("---@field armor number", server)
        self.assertIn("function Ped.create(...)", server)
        kill = server[server.rindex("--- Global binding: `mta:function:killPed`"):]
        kill = kill[:kill.index("\n\n")]
        self.assertNotIn("thePed", kill)
        self.assertIn("---@param theKiller? ped", kill)
        self.assertIn("function Ped:kill(...)", kill)
        self.assertIn("---@class Bird: Element", client)
        self.assertNotIn("---@class Bird: Element", server)
        create_bird = client[client.index("--- Global binding: `neon:function:createBird`"):]
        create_bird = create_bird[:create_bird.index("\n\n")]
        self.assertIn("---@param x number", create_bird)
        self.assertIn("---@param options? table", create_bird)
        self.assertIn("---@return bird|false", create_bird)
        self.assertIn("function Bird.create(...)", create_bird)
        self.assertNotIn("function Bird:create(...)", client)
        bird_colors = client[client.index("--- Global binding: `neon:function:getBirdColors`"):]
        bird_colors = bird_colors[:bird_colors.index("\n\n")]
        self.assertIn("function Bird:getColors(...)", bird_colors)
        self.assertNotIn("unknown", bird_colors)


class RuntimeComparisonTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="neon-runtime-compare-")
        self.root = Path(self.temporary.name)
        self.project = base_project()
        self.project["resources"] = [{"name": "inventory", "path": "resources/inventory"}]
        self.project["modules"] = [{"name": "native", "path": "modules/native"}]
        self.catalogue = {
            "symbols": [
                {
                    "id": "mta:function:createThing", "kind": "function", "name": "createThing", "state": "verified",
                    "profiles": ["neon-pair"], "sides": ["client", "server"], "restricted": False,
                },
                {
                    "id": "mta:event:onThing", "kind": "event", "name": "onThing", "state": "verified",
                    "profiles": ["neon-pair"], "sides": ["server"], "allowRemoteTrigger": False,
                },
            ],
        }
        self.project_api = {
            "components": [
                {"kind": "resource", "name": "inventory", "sides": ["server"]},
                {
                    "kind": "module", "name": "native",
                    "manifest": {"version": "1.0.0", "sha256": "3" * 64},
                    "module": {"abi": "MTA-1.7-server"}, "binary": {"sha256": "4" * 64},
                },
            ],
            "symbols": [
                {
                    "id": "resource:inventory:server-export:takeItem", "kind": "export", "name": "takeItem",
                    "owner": "inventory", "ownerKind": "resource", "side": "server",
                },
                {
                    "id": "module:native:function:nativePing", "kind": "function", "name": "nativePing",
                    "owner": "native", "ownerKind": "module", "side": "server",
                },
            ],
        }
        self.project_sha = "1" * 64
        self.catalogue_sha = "2" * 64
        self.session_id = "session:test-runtime"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def snapshot(self, completeness: str = "complete") -> dict:
        return {
            "schemaVersion": "1.0.0", "producer": "neon-runtime-probe-1", "sessionId": self.session_id,
            "profile": "neon-pair", "observedAt": "2026-08-25T12:00:00Z",
            "catalogueSha256": self.catalogue_sha, "projectSha256": self.project_sha,
            "observations": [
                {
                    "id": "runtime:server-1", "side": "server", "engineVersion": "1.7.0", "buildId": "neon:test",
                    "completeness": completeness, "functions": [{"name": "createThing", "restricted": False}],
                    "events": [{"name": "onThing", "allowRemoteTrigger": False}],
                    "resources": [{"name": "inventory", "state": "running", "exports": [{"name": "takeItem", "side": "server"}]}],
                    "modules": [{
                        "name": "native", "version": "1.0.0", "abi": "MTA-1.7-server",
                        "manifestSha256": "3" * 64, "binarySha256": "4" * 64,
                        "exports": [{"name": "nativePing", "side": "server"}],
                    }],
                },
                {
                    "id": "runtime:client-1", "side": "client", "engineVersion": "1.7.0", "buildId": "neon:test",
                    "completeness": completeness, "functions": [{"name": "createThing", "restricted": False}],
                    "events": [], "resources": [], "modules": [],
                },
            ],
        }

    def compare(self, snapshot: dict) -> dict:
        path = self.root / "runtime.json"
        write_json(path, snapshot)
        return compare_runtime_snapshot(
            path, self.project, self.project_sha, self.catalogue, self.catalogue_sha,
            self.project_api, self.session_id, SCHEMAS,
        )

    def test_complete_matching_pair_passes_without_evidence_overclaim(self) -> None:
        result = self.compare(self.snapshot())
        self.assertEqual((result["status"], result["diagnostics"]), ("pass", []))
        self.assertEqual(result["comparison"]["scope"], "observation-only")
        self.assertEqual(result["comparison"]["grantedEvidenceLabels"], [])
        self.assertEqual(SCHEMAS.validate("neon-runtime-compare-result", result), [])

    def test_partial_inventory_is_visible_uncertainty_not_false_absence(self) -> None:
        snapshot = self.snapshot("partial")
        snapshot["observations"][0]["functions"] = []
        snapshot["observations"][1]["functions"] = []
        result = self.compare(snapshot)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(diagnostic_codes(result), ["RUNTIME_INVENTORY_PARTIAL", "RUNTIME_INVENTORY_PARTIAL"])
        self.assertTrue(all(item["severity"] == "warning" for item in result["diagnostics"]))

    def test_complete_component_identity_and_extras_cannot_false_pass(self) -> None:
        snapshot = self.snapshot()
        module = snapshot["observations"][0]["modules"][0]
        module["version"] = "0.0.0"
        module["abi"] = "wrong-abi"
        module["manifestSha256"] = "5" * 64
        module["binarySha256"] = "6" * 64
        module["exports"] = []
        snapshot["observations"][0]["modules"].append({
            "name": "unexpected-module", "version": "999.0.0", "abi": "unknown",
            "manifestSha256": None, "binarySha256": None, "exports": [],
        })
        snapshot["observations"][0]["resources"].append({
            "name": "unexpected-resource", "state": "failed",
            "exports": [{"name": "undeclaredExport", "side": "server"}],
        })
        result = self.compare(snapshot)
        self.assertEqual(result["status"], "fail")
        self.assertTrue({
            "RUNTIME_MODULE_VERSION_MISMATCH", "RUNTIME_MODULE_ABI_MISMATCH",
            "RUNTIME_MODULE_MANIFEST_HASH_MISMATCH", "RUNTIME_MODULE_BINARY_HASH_MISMATCH",
            "RUNTIME_MODULE_EXPORT_MISSING", "RUNTIME_MODULE_UNDECLARED", "RUNTIME_RESOURCE_UNDECLARED",
        }.issubset(set(diagnostic_codes(result))))

    def test_partial_component_inventory_never_proves_absence(self) -> None:
        snapshot = self.snapshot("partial")
        snapshot["observations"][0]["modules"] = []
        snapshot["observations"][0]["resources"] = []
        result = self.compare(snapshot)
        self.assertEqual(result["status"], "pass")
        self.assertNotIn("RUNTIME_MODULE_MISSING", diagnostic_codes(result))
        self.assertNotIn("RUNTIME_RESOURCE_MISSING", diagnostic_codes(result))

    def test_partial_inventory_rejects_positively_observed_rogue_components(self) -> None:
        snapshot = self.snapshot("partial")
        snapshot["observations"][0]["resources"].append({"name": "rogue-resource", "state": "running", "exports": []})
        snapshot["observations"][0]["modules"].append({
            "name": "rogue-module", "version": None, "abi": None,
            "manifestSha256": None, "binarySha256": None, "exports": [],
        })
        snapshot["observations"][1]["resources"].append({
            "name": "rogue-client-resource", "state": "running",
            "exports": [{"name": "clientBackdoor", "side": "client"}],
        })
        result = self.compare(snapshot)
        self.assertEqual(result["status"], "fail")
        self.assertTrue({"RUNTIME_RESOURCE_UNDECLARED", "RUNTIME_MODULE_UNDECLARED"}.issubset(diagnostic_codes(result)))

    def test_partial_inventory_rejects_positively_observed_rogue_apis(self) -> None:
        snapshot = self.snapshot("partial")
        snapshot["observations"][0]["functions"].append({"name": "rogueBackdoor", "restricted": False})
        snapshot["observations"][0]["events"].append({"name": "onRogueBackdoor", "allowRemoteTrigger": True})
        result = self.compare(snapshot)
        self.assertEqual(result["status"], "fail")
        self.assertTrue({
            "RUNTIME_FUNCTION_UNCATALOGUED", "RUNTIME_EVENT_UNCATALOGUED",
        }.issubset(diagnostic_codes(result)))

    def test_complete_resource_exports_are_compared_per_observation_side(self) -> None:
        self.project_api["symbols"].append({
            "id": "resource:inventory:client-export:openInventory", "kind": "export",
            "name": "openInventory", "owner": "inventory", "ownerKind": "resource", "side": "client",
        })
        snapshot = self.snapshot()
        snapshot["observations"][1]["resources"] = [{
            "name": "inventory", "state": "running",
            "exports": [{"name": "openInventory", "side": "client"}],
        }]
        result = self.compare(snapshot)
        self.assertEqual((result["status"], result["diagnostics"]), ("pass", []))

    def test_complete_client_inventory_cannot_omit_a_declared_client_resource(self) -> None:
        self.project_api["components"][0]["sides"].append("client")
        result = self.compare(self.snapshot())
        self.assertEqual(result["status"], "fail")
        self.assertIn("RUNTIME_RESOURCE_MISSING", diagnostic_codes(result))

    def test_observed_resource_on_an_undeclared_side_cannot_pass(self) -> None:
        snapshot = self.snapshot()
        snapshot["observations"][1]["resources"] = [{
            "name": "inventory", "state": "running", "exports": [],
        }]
        result = self.compare(snapshot)
        self.assertEqual(result["status"], "fail")
        self.assertIn("RUNTIME_RESOURCE_WRONG_SIDE", diagnostic_codes(result))

    def test_conflicted_global_api_is_never_an_active_runtime_expectation(self) -> None:
        self.catalogue["symbols"].append({
            "id": "mta:function:unsafeConflict", "kind": "function", "name": "unsafeConflict",
            "state": "conflict", "profiles": ["neon-pair"], "sides": ["client"], "restricted": False,
        })
        snapshot = self.snapshot()
        snapshot["observations"][1]["functions"].append({"name": "unsafeConflict", "restricted": False})
        result = self.compare(snapshot)
        self.assertEqual(result["status"], "fail")
        self.assertIn("RUNTIME_FUNCTION_UNAVAILABLE", diagnostic_codes(result))

    def test_partial_inventory_rejects_wrong_side_known_module_and_all_identity_mismatches(self) -> None:
        snapshot = self.snapshot("partial")
        snapshot["observations"][0]["modules"] = []
        snapshot["observations"][1]["modules"] = [{
            "name": "native", "version": "9.9.9", "abi": "attacker-abi",
            "manifestSha256": "5" * 64, "binarySha256": "6" * 64,
            "exports": [{"name": "clientBackdoor", "side": "client"}],
        }]
        result = self.compare(snapshot)
        self.assertEqual(result["status"], "fail")
        self.assertTrue({
            "RUNTIME_MODULE_WRONG_SIDE", "RUNTIME_MODULE_VERSION_MISMATCH", "RUNTIME_MODULE_ABI_MISMATCH",
            "RUNTIME_MODULE_MANIFEST_HASH_MISMATCH", "RUNTIME_MODULE_BINARY_HASH_MISMATCH",
            "RUNTIME_MODULE_EXPORT_UNDECLARED",
        }.issubset(diagnostic_codes(result)))

    def test_pair_rejects_extra_client_and_names_are_bounded(self) -> None:
        snapshot = self.snapshot()
        extra = copy.deepcopy(snapshot["observations"][1])
        extra["id"] = "runtime:client-2"
        snapshot["observations"].append(extra)
        result = self.compare(snapshot)
        self.assertIn("RUNTIME_TOPOLOGY_UNEXPECTED", diagnostic_codes(result))
        snapshot = self.snapshot()
        snapshot["observations"][0]["functions"][0]["name"] = "f" * 129
        result = self.compare(snapshot)
        self.assertEqual(diagnostic_codes(result), ["RUNTIME_SNAPSHOT_INVALID"])

    def test_large_valid_inventory_produces_a_bounded_schema_valid_result(self) -> None:
        snapshot = self.snapshot("partial")
        snapshot["observations"][0]["functions"] = [
            {"name": f"unknownFunction{index:05d}", "restricted": False}
            for index in range(5000)
        ]
        result = self.compare(snapshot)
        self.assertEqual(result["status"], "fail")
        self.assertIn("RUNTIME_DIAGNOSTICS_TRUNCATED", diagnostic_codes(result))
        self.assertLess(len(canonical_json(result).encode("utf-8")), 16 * 1024 * 1024)
        self.assertEqual(SCHEMAS.validate("neon-runtime-compare-result", result), [])

    def test_observation_ids_and_semvers_cannot_amplify_or_crash_results(self) -> None:
        snapshot = self.snapshot("partial")
        snapshot["observations"][0]["id"] = "runtime:" + ("a" * 600)
        snapshot["observations"][0]["functions"] = [
            {"name": f"unknownFunction{index:05d}", "restricted": False} for index in range(5000)
        ]
        result = self.compare(snapshot)
        self.assertEqual(diagnostic_codes(result), ["RUNTIME_SNAPSHOT_INVALID"])
        self.assertEqual(SCHEMAS.validate("neon-runtime-compare-result", result), [])
        self.assertLess(len(canonical_json(result)), 4096)

        snapshot = self.snapshot()
        snapshot["observations"][0]["engineVersion"] = ("9" * 5000) + ".0.0"
        result = self.compare(snapshot)
        self.assertEqual(diagnostic_codes(result), ["RUNTIME_SNAPSHOT_INVALID"])
        self.assertEqual(SCHEMAS.validate("neon-runtime-compare-result", result), [])

    def test_runtime_divergence_is_precise_and_blocking(self) -> None:
        snapshot = self.snapshot()
        snapshot["observations"][0]["functions"][0]["restricted"] = True
        snapshot["observations"][0]["events"][0]["allowRemoteTrigger"] = True
        snapshot["observations"][0]["resources"][0]["state"] = "stopped"
        snapshot["observations"][0]["resources"][0]["exports"] = []
        snapshot["observations"][0]["modules"] = []
        snapshot["observations"][1]["functions"] = []
        snapshot["observations"][1]["events"] = [{"name": "onThing", "allowRemoteTrigger": False}]
        snapshot["observations"][1]["engineVersion"] = "1.8.0"
        snapshot["observations"][1]["buildId"] = "neon:other"
        result = self.compare(snapshot)
        self.assertEqual(result["status"], "fail")
        self.assertTrue({
            "RUNTIME_RESTRICTION_MISMATCH", "RUNTIME_EVENT_REMOTE_POLICY_MISMATCH", "RUNTIME_RESOURCE_NOT_RUNNING",
            "RUNTIME_EXPORT_MISSING", "RUNTIME_MODULE_MISSING", "RUNTIME_FUNCTION_MISSING",
            "RUNTIME_EVENT_UNAVAILABLE", "RUNTIME_ENGINE_VERSION_INCOMPATIBLE", "RUNTIME_BUILD_MISMATCH",
        }.issubset(set(diagnostic_codes(result))))

    def test_identity_topology_duplicates_and_unknowns_are_bounded(self) -> None:
        snapshot = self.snapshot()
        snapshot["sessionId"] = "session:other"
        snapshot["catalogueSha256"] = "3" * 64
        snapshot["projectSha256"] = "4" * 64
        snapshot["observations"].pop()
        snapshot["observations"][0]["functions"].append({"name": "createThing", "restricted": False})
        snapshot["observations"][0]["functions"].append({"name": "futureFunction", "restricted": False})
        result = self.compare(snapshot)
        self.assertEqual(result["status"], "fail")
        self.assertTrue({
            "RUNTIME_SESSION_MISMATCH", "RUNTIME_CATALOGUE_MISMATCH", "RUNTIME_PROJECT_MISMATCH",
            "RUNTIME_TOPOLOGY_INCOMPLETE", "RUNTIME_FUNCTION_DUPLICATE", "RUNTIME_FUNCTION_UNCATALOGUED",
        }.issubset(set(diagnostic_codes(result))))

    def test_invalid_snapshot_schema_and_timestamp_fail_closed(self) -> None:
        snapshot = self.snapshot()
        snapshot["observedAt"] = "2026-02-31T12:00:00Z"
        result = self.compare(snapshot)
        self.assertIn("RUNTIME_SNAPSHOT_TIME_INVALID", diagnostic_codes(result))
        snapshot["typo"] = True
        result = self.compare(snapshot)
        self.assertEqual(diagnostic_codes(result), ["RUNTIME_SNAPSHOT_INVALID"])


class SupervisorIntegrationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="neon-supervisor-")
        self.root = Path(self.temporary.name)
        try:
            os.link(CATALOGUE_PATH, self.root / "api.json")
        except OSError:
            shutil.copyfile(CATALOGUE_PATH, self.root / "api.json")
        write_json(self.root / "neon.project.json", base_project())
        self.sessions: list[Path] = []

    def tearDown(self) -> None:
        for session in self.sessions:
            try:
                document = load_json(session)
                if document.get("state") == "active":
                    request_supervisor(self.root, session, "shutdown", SCHEMAS)
            except (OSError, ValueError):
                pass
        self.temporary.cleanup()

    def start(self, ttl: int = 30, snapshot: str = "runtime.json") -> tuple[dict, Path]:
        result = start_supervisor(
            self.root, Path("neon.project.json"), None, Path(snapshot), Path("sessions"),
            ttl, CLI_PATH, SCHEMAS,
        )
        session = self.root / result["session"]["sessionPath"]
        self.sessions.append(session)
        return result, session

    def partial_snapshot(self, session: dict) -> dict:
        return {
            "schemaVersion": "1.0.0", "producer": "neon-runtime-probe-1", "sessionId": session["sessionId"],
            "profile": session["profile"], "observedAt": session["createdAt"],
            "catalogueSha256": session["catalogue"]["sha256"], "projectSha256": session["project"]["sha256"],
            "observations": [
                {"id": "runtime:server-1", "side": "server", "engineVersion": "1.7.0", "buildId": "neon:test", "completeness": "partial", "functions": [{"name": "createVehicle", "restricted": False}], "events": [{"name": "onPlayerJoin", "allowRemoteTrigger": False}], "resources": [], "modules": []},
                {"id": "runtime:client-1", "side": "client", "engineVersion": "1.7.0", "buildId": "neon:test", "completeness": "partial", "functions": [{"name": "createVehicle", "restricted": False}], "events": [], "resources": [], "modules": []},
            ],
        }

    def raw_request(self, session: dict, request: dict) -> dict:
        request = dict(request)
        token = request.pop("token", None)
        with socket.create_connection(("127.0.0.1", session["transport"]["port"]), timeout=3) as connection:
            with connection.makefile("rb") as stream:
                challenge = json.loads(stream.readline())
            if token is not None and isinstance(request.get("schemaVersion"), str):
                request.setdefault("challenge", challenge["challenge"])
                request.setdefault("nonce", "a" * 64)
                signed = {
                    key: request[key] for key in ("schemaVersion", "sessionId", "challenge", "nonce", "command")
                }
                request["authorization"] = _authorization(token, signed)
            payload = canonical_json(request).encode("utf-8")
            connection.sendall(payload)
            connection.shutdown(socket.SHUT_WR)
            response = bytearray()
            while chunk := connection.recv(65536):
                response.extend(chunk)
        document = json.loads(response)
        return document.get("result", document)

    def test_real_loopback_session_missing_snapshot_compare_and_revocation(self) -> None:
        started, session_path = self.start()
        self.assertEqual(started["status"], "pass")
        self.assertEqual(SCHEMAS.validate("neon-supervisor-result", started), [])
        self.assertNotIn("token", canonical_json(started))
        session = load_json(session_path)
        self.assertEqual(SCHEMAS.validate("neon-supervisor-session", session), [])
        self.assertEqual((session["transport"]["host"], session["state"]), ("127.0.0.1", "active"))
        if os.name != "nt":
            self.assertEqual(session_path.stat().st_mode & 0o777, 0o600)
        status = request_supervisor(self.root, session_path, "status", SCHEMAS)
        self.assertFalse(status["session"]["snapshotAvailable"])
        missing = request_supervisor(self.root, session_path, "runtime.compare", SCHEMAS)
        self.assertEqual((missing["status"], diagnostic_codes(missing)), ("fail", ["RUNTIME_SNAPSHOT_UNAVAILABLE"]))

        write_json(self.root / "runtime.json", self.partial_snapshot(session))
        compared = request_supervisor(self.root, session_path, "runtime.compare", SCHEMAS)
        self.assertEqual(compared["status"], "pass")
        self.assertEqual(SCHEMAS.validate("neon-runtime-compare-result", compared), [])
        self.assertEqual(compared["comparison"]["grantedEvidenceLabels"], [])
        stopped = request_supervisor(self.root, session_path, "shutdown", SCHEMAS)
        self.assertEqual(stopped["session"]["state"], "closed")
        closed = load_json(session_path)
        self.assertNotIn("token", closed)
        with self.assertRaisesRegex(ValueError, "closed"):
            request_supervisor(self.root, session_path, "status", SCHEMAS)
        audit = (session_path.parent / "audit.jsonl").read_text(encoding="utf-8")
        self.assertNotIn(session["token"], audit)
        audit_records = [json.loads(line) for line in audit.splitlines()]
        self.assertEqual(audit_records[0]["command"], "supervisor.start")
        self.assertEqual(audit_records[-1]["command"], "shutdown")
        self.assertTrue(all(item["sessionId"] == session["sessionId"] for item in audit_records))

    @unittest.skipIf(os.name == "nt", "portable fixture executable is POSIX-only; Windows uses the real server binary")
    def test_explicit_resource_lifecycle_capability_is_bounded_and_honest(self) -> None:
        resource = self.root / "resources" / "inventory"
        resource.mkdir(parents=True)
        (resource / "meta.xml").write_text('<meta><script src="server.lua" type="server"/></meta>', encoding="utf-8")
        (resource / "server.lua").write_text("local ready = true\n", encoding="utf-8")
        project = base_project()
        project["resources"] = [{"name": "inventory", "path": "resources/inventory"}]
        write_json(self.root / "neon.project.json", project)
        with tempfile.TemporaryDirectory(prefix="neon-mta-driver-") as driver_temporary:
            driver_root = Path(driver_temporary)
            executable = driver_root / "mta-server64"
            executable.write_text(
                "#!/usr/bin/env python3\n"
                "import pathlib, sys\n"
                "for line in sys.stdin:\n"
                "    with pathlib.Path('commands.log').open('a', encoding='utf-8') as stream:\n"
                "        stream.write(line)\n"
                "        stream.flush()\n",
                encoding="utf-8",
            )
            executable.chmod(0o700)
            started = start_supervisor(
                self.root, Path("neon.project.json"), None, Path("runtime.json"), Path("sessions"),
                30, CLI_PATH, SCHEMAS, ("resource.lifecycle", "scenario.execute"), driver_root,
            )
            session_path = self.root / started["session"]["sessionPath"]
            self.sessions.append(session_path)
            session = load_json(session_path)
            self.assertEqual(session["driver"]["kind"], "mta-server-stdio")
            submitted = request_supervisor(self.root, session_path, "resource.restart/inventory", SCHEMAS)
            self.assertEqual((submitted["status"], submitted["operation"]["scope"]), ("pass", "command-submitted"))
            self.assertEqual(submitted["operation"]["grantedEvidenceLabels"], [])
            self.assertEqual(SCHEMAS.validate("neon-mutation-result", submitted), [])
            inconsistent = copy.deepcopy(submitted)
            inconsistent["operation"]["capability"] = "scenario.execute"
            inconsistent["operation"]["scope"] = "authorization-only"
            self.assertTrue(SCHEMAS.validate("neon-mutation-result", inconsistent))
            undeclared = request_supervisor(self.root, session_path, "resource.start/not-declared", SCHEMAS)
            self.assertEqual((undeclared["status"], diagnostic_codes(undeclared)), ("fail", ["RESOURCE_TARGET_UNDECLARED"]))
            authorized = request_supervisor(self.root, session_path, "scenario.authorize", SCHEMAS)
            self.assertEqual(authorized["status"], "pass")
            self.assertEqual(request_supervisor(self.root, session_path, "status", SCHEMAS)["status"], "pass")
            scenario = {
                "schemaVersion": "1.0.0", "id": "test:bounded-lifecycle", "profile": "neon-pair",
                "steps": [{
                    "id": "step:restart", "action": "resource.restart", "timeoutMs": 5000,
                    "inputs": {"resource": "inventory"},
                }],
                "assertions": ["assertion:bounded-target"],
            }
            assertion = {
                "schemaVersion": "1.0.0", "id": "assertion:bounded-target", "kind": "equals",
                "actual": "step:restart#/operation/target", "expected": "inventory",
                "message": "only the declared resource target is submitted",
            }
            write_json(self.root / "runtime-scenario.json", scenario)
            write_json(self.root / "runtime-assertion.json", assertion)
            completed = subprocess.run(
                [
                    sys.executable, str(CLI_PATH), "scenario", "execute",
                    session_path.relative_to(self.root).as_posix(), "runtime-scenario.json",
                    "--assertion", "runtime-assertion.json", "--workspace", str(self.root),
                    "--output", "runtime-run", "--json",
                ],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
            )
            scenario_result = json.loads(completed.stdout)
            self.assertEqual((completed.returncode, scenario_result["status"]), (0, "pass"))
            self.assertEqual(scenario_result["command"], "scenario.execute")
            self.assertEqual(scenario_result["evidence"]["labels"], [])
            self.assertEqual(SCHEMAS.validate("neon-test-result", scenario_result), [])
            verified = subprocess.run(
                [
                    sys.executable, str(CLI_PATH), "scenario", "verify", "runtime-run",
                    "--workspace", str(self.root), "--json",
                ],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
            )
            self.assertEqual((verified.returncode, json.loads(verified.stdout)["status"]), (0, "pass"))
            forged_result = load_json(self.root / "runtime-run" / "result.json")
            forged_result["command"] = "scenario.run"
            forged_evidence = forged_result["evidence"]
            forged_identity = canonical_json({
                "command": "scenario.run", "scenario": scenario, "assertions": [assertion],
                "steps": [
                    {"id": item["id"], "status": item["status"], "result": item["result"]}
                    for item in forged_result["steps"]
                ],
            }).encode("utf-8")
            forged_evidence["runId"] = (
                f"run:{scenario['id']}:{sha256_bytes(forged_identity)[:20]}"
            )
            forged_result["evidence"] = forged_evidence
            write_json(self.root / "runtime-run" / "evidence.json", forged_evidence)
            write_json(self.root / "runtime-run" / "result.json", forged_result)
            forged_verification = subprocess.run(
                [
                    sys.executable, str(CLI_PATH), "scenario", "verify", "runtime-run",
                    "--workspace", str(self.root), "--json",
                ],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
            )
            self.assertEqual(forged_verification.returncode, 1)
            self.assertIn(
                "SCENARIO_RUNTIME_SCOPE_INVALID",
                diagnostic_codes(json.loads(forged_verification.stdout)),
            )
            deadline = time.monotonic() + 2
            command_log = driver_root / "commands.log"
            while time.monotonic() < deadline and not command_log.exists():
                time.sleep(0.02)
            while time.monotonic() < deadline and command_log.read_text(encoding="utf-8").count("restart inventory\n") < 2:
                time.sleep(0.02)
            self.assertEqual(command_log.read_text(encoding="utf-8"), "restart inventory\nrestart inventory\n")
            scenario["steps"][0]["inputs"]["unexpected"] = "forbidden"
            write_json(self.root / "runtime-scenario.json", scenario)
            rejected_scenario = subprocess.run(
                [
                    sys.executable, str(CLI_PATH), "scenario", "execute",
                    session_path.relative_to(self.root).as_posix(), "runtime-scenario.json",
                    "--assertion", "runtime-assertion.json", "--workspace", str(self.root), "--json",
                ],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
            )
            rejected_result = json.loads(rejected_scenario.stdout)
            self.assertEqual((rejected_scenario.returncode, rejected_result["status"]), (1, "fail"))
            self.assertIn("SCENARIO_RUNTIME_REQUEST_FAILED", diagnostic_codes(rejected_result))
            self.assertEqual(command_log.read_text(encoding="utf-8"), "restart inventory\nrestart inventory\n")
            with self.assertRaisesRegex(ValueError, "not allowlisted"):
                request_supervisor(self.root, session_path, "resource.start/inventory\nstop inventory", SCHEMAS)
            os.kill(session["driver"]["pid"], 15)
            time.sleep(0.05)
            unavailable = request_supervisor(self.root, session_path, "resource.stop/inventory", SCHEMAS)
            self.assertEqual((unavailable["status"], diagnostic_codes(unavailable)), ("fail", ["MTA_SERVER_UNAVAILABLE"]))
            self.assertEqual(request_supervisor(self.root, session_path, "status", SCHEMAS)["status"], "pass")
            request_supervisor(self.root, session_path, "shutdown", SCHEMAS)

    @unittest.skipIf(os.name == "nt", "portable EOF-surviving fixture is POSIX-only")
    def test_driver_is_not_orphaned_when_supervisor_or_guardian_dies(self) -> None:
        resource = self.root / "resources" / "inventory"
        resource.mkdir(parents=True)
        (resource / "meta.xml").write_text(
            '<meta><script src="server.lua" type="server"/></meta>', encoding="utf-8",
        )
        (resource / "server.lua").write_text("local ready = true\n", encoding="utf-8")
        project = base_project()
        project["resources"] = [{"name": "inventory", "path": "resources/inventory"}]
        write_json(self.root / "neon.project.json", project)

        def process_exists(pid: int) -> bool:
            try:
                os.kill(pid, 0)
                return True
            except ProcessLookupError:
                return False

        for killed_owner in ("supervisor", "guardian"):
            with self.subTest(killed_owner=killed_owner), tempfile.TemporaryDirectory(
                prefix=f"neon-eof-driver-{killed_owner}-",
            ) as driver_temporary:
                driver_root = Path(driver_temporary)
                executable = driver_root / "mta-server64"
                executable.write_text(
                    "#!/usr/bin/env python3\n"
                    "import sys, time\n"
                    "while True:\n"
                    "    line = sys.stdin.readline()\n"
                    "    if not line:\n"
                    "        time.sleep(0.05)\n",
                    encoding="utf-8",
                )
                executable.chmod(0o700)
                started = start_supervisor(
                    self.root, Path("neon.project.json"), None, Path("runtime.json"),
                    Path("sessions"), 30, CLI_PATH, SCHEMAS, ("resource.lifecycle",), driver_root,
                )
                session_path = self.root / started["session"]["sessionPath"]
                self.sessions.append(session_path)
                session = load_json(session_path)
                driver_pid = session["driver"]["pid"]
                guardian_pid = session["driver"]["guardianPid"]
                os.kill(session["pid"] if killed_owner == "supervisor" else guardian_pid, signal.SIGKILL)
                if killed_owner == "guardian":
                    stopped = request_supervisor(self.root, session_path, "shutdown", SCHEMAS)
                    self.assertEqual(stopped["session"]["state"], "closed")
                deadline = time.monotonic() + 6
                while time.monotonic() < deadline and (
                    process_exists(driver_pid)
                    or (killed_owner == "supervisor" and process_exists(guardian_pid))
                ):
                    time.sleep(0.05)
                self.assertFalse(process_exists(driver_pid))
                if killed_owner == "supervisor":
                    self.assertFalse(process_exists(guardian_pid))
                    with self.assertRaises((OSError, ValueError)):
                        request_supervisor(self.root, session_path, "status", SCHEMAS)

    def test_read_only_session_rejects_mutation_without_revoking_reads(self) -> None:
        _, session_path = self.start()
        denied = request_supervisor(self.root, session_path, "resource.start/inventory", SCHEMAS)
        self.assertEqual((denied["status"], diagnostic_codes(denied)), ("fail", ["SUPERVISOR_CAPABILITY_DENIED"]))
        self.assertEqual(SCHEMAS.validate("neon-mutation-result", denied), [])
        self.assertEqual(request_supervisor(self.root, session_path, "status", SCHEMAS)["status"], "pass")

    @unittest.skipUnless(
        os.name == "nt" and os.environ.get("NEON_TEST_WINDOWS_DRIVER"),
        "set NEON_TEST_WINDOWS_DRIVER to the compiled Windows stdio fixture directory",
    )
    def test_windows_fixture_executes_bounded_resource_scenario(self) -> None:
        driver_root = Path(os.environ["NEON_TEST_WINDOWS_DRIVER"])
        command_log = driver_root / "commands.log"
        previous = command_log.read_text(encoding="utf-8") if command_log.exists() else ""
        resource = self.root / "resources" / "inventory"
        resource.mkdir(parents=True)
        (resource / "meta.xml").write_text('<meta><script src="server.lua" type="server"/></meta>', encoding="utf-8")
        (resource / "server.lua").write_text("local ready = true\n", encoding="utf-8")
        project = base_project()
        project["resources"] = [{"name": "inventory", "path": "resources/inventory"}]
        write_json(self.root / "neon.project.json", project)
        started = start_supervisor(
            self.root, Path("neon.project.json"), None, Path("runtime.json"), Path("sessions"),
            30, CLI_PATH, SCHEMAS, ("resource.lifecycle", "scenario.execute"), driver_root,
        )
        session_path = self.root / started["session"]["sessionPath"]
        self.sessions.append(session_path)
        submitted = request_supervisor(self.root, session_path, "resource.restart/inventory", SCHEMAS)
        self.assertEqual((submitted["status"], submitted["operation"]["scope"]), ("pass", "command-submitted"))
        scenario = {
            "schemaVersion": "1.0.0", "id": "test:windows-runtime", "profile": "neon-pair",
            "steps": [{
                "id": "step:restart", "action": "resource.restart", "timeoutMs": 5000,
                "inputs": {"resource": "inventory"},
            }],
            "assertions": ["assertion:windows-target"],
        }
        assertion = {
            "schemaVersion": "1.0.0", "id": "assertion:windows-target", "kind": "equals",
            "actual": "step:restart#/operation/target", "expected": "inventory",
            "message": "the Windows adapter submits only the declared target",
        }
        write_json(self.root / "scenario.json", scenario)
        write_json(self.root / "assertion.json", assertion)
        completed = subprocess.run(
            [
                sys.executable, str(CLI_PATH), "scenario", "execute",
                session_path.relative_to(self.root).as_posix(), "scenario.json",
                "--assertion", "assertion.json", "--workspace", str(self.root), "--json",
            ],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        result = json.loads(completed.stdout)
        self.assertEqual((completed.returncode, result["status"], result["evidence"]["labels"]), (0, "pass", []))
        self.assertEqual(result["command"], "scenario.execute")
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            current = command_log.read_text(encoding="utf-8") if command_log.exists() else ""
            if current.removeprefix(previous) == "restart inventory\nrestart inventory\n":
                break
            time.sleep(0.02)
        self.assertEqual(command_log.read_text(encoding="utf-8").removeprefix(previous), "restart inventory\nrestart inventory\n")
        request_supervisor(self.root, session_path, "shutdown", SCHEMAS)

    @unittest.skipIf(os.name == "nt", "portable executable rejection fixtures are POSIX-only")
    def test_mutation_capability_and_driver_boundaries_fail_closed(self) -> None:
        with self.assertRaisesRegex(ValueError, "not allowlisted"):
            start_supervisor(
                self.root, Path("neon.project.json"), None, Path("runtime.json"), Path("sessions"),
                30, CLI_PATH, SCHEMAS, ("shell.execute",),
            )
        with self.assertRaisesRegex(ValueError, "requires an explicitly approved"):
            start_supervisor(
                self.root, Path("neon.project.json"), None, Path("runtime.json"), Path("sessions"),
                30, CLI_PATH, SCHEMAS, ("resource.lifecycle",),
            )
        with tempfile.TemporaryDirectory(prefix="neon-driver-invalid-") as temporary:
            driver_root = Path(temporary)
            with self.assertRaisesRegex(ValueError, "only be supplied"):
                start_supervisor(
                    self.root, Path("neon.project.json"), None, Path("runtime.json"), Path("sessions"),
                    30, CLI_PATH, SCHEMAS, (), driver_root,
                )
            with self.assertRaisesRegex(ValueError, "no approved"):
                start_supervisor(
                    self.root, Path("neon.project.json"), None, Path("runtime.json"), Path("sessions"),
                    30, CLI_PATH, SCHEMAS, ("resource.lifecycle",), driver_root,
                )
            outside = driver_root / "outside"
            outside.write_text("#!/bin/sh\nwhile read line; do :; done\n", encoding="utf-8")
            outside.chmod(0o700)
            (driver_root / "mta-server64").symlink_to(outside)
            with self.assertRaisesRegex(ValueError, "no approved"):
                start_supervisor(
                    self.root, Path("neon.project.json"), None, Path("runtime.json"), Path("sessions"),
                    30, CLI_PATH, SCHEMAS, ("resource.lifecycle",), driver_root,
                )
            (driver_root / "mta-server64").unlink()
            (driver_root / "mta-server64").write_text("#!/bin/sh\nexit 17\n", encoding="utf-8")
            (driver_root / "mta-server64").chmod(0o700)
            with self.assertRaisesRegex(ValueError, "exited during startup"):
                start_supervisor(
                    self.root, Path("neon.project.json"), None, Path("runtime.json"), Path("sessions"),
                    30, CLI_PATH, SCHEMAS, ("resource.lifecycle",), driver_root,
                )

    def test_mutation_transport_timeout_is_ambiguous_revokes_and_never_retries(self) -> None:
        _, session_path = self.start()
        original = load_json(session_path)
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        accepted: list[bytes] = []

        def consume_without_reply() -> None:
            connection, _ = listener.accept()
            with connection:
                connection.sendall(canonical_json({
                    "schemaVersion": "1.0.0", "sessionId": original["sessionId"], "challenge": "8" * 64,
                }).encode("utf-8"))
                payload = bytearray()
                while chunk := connection.recv(65536):
                    payload.extend(chunk)
                accepted.append(bytes(payload))
                time.sleep(0.2)

        peer = threading.Thread(target=consume_without_reply, daemon=True)
        peer.start()
        redirected = copy.deepcopy(original)
        redirected["transport"]["port"] = listener.getsockname()[1]
        write_json(session_path, redirected)
        try:
            with self.assertRaisesRegex(supervisor_module.SupervisorMutationOutcomeUnknown, "must not be retried") as caught:
                request_supervisor(self.root, session_path, "resource.restart/inventory", SCHEMAS, 50)
            self.assertEqual(caught.exception.session_id, original["sessionId"])
            peer.join(timeout=2)
            self.assertEqual(len(accepted), 1)
            request = json.loads(accepted[0])
            self.assertEqual(request["command"], "resource.restart/inventory")
            revoked = load_json(session_path)
            self.assertEqual(revoked["state"], "closed")
            self.assertNotIn("token", revoked)
        finally:
            listener.close()

        for invalid_timeout in (0, 600001, True):
            with self.assertRaisesRegex(ValueError, "timeout"):
                request_supervisor(self.root, session_path, "status", SCHEMAS, invalid_timeout)

    def test_mutation_timeout_before_first_send_is_definite(self) -> None:
        _, session_path = self.start()
        session = load_json(session_path)

        class SlowChallengeConnection:
            def __init__(self) -> None:
                self.send_calls = 0
                self.challenge_sent = False

            def __enter__(self):
                return self

            def __exit__(self, *_args):
                return False

            def settimeout(self, _timeout: float) -> None:
                pass

            def recv(self, _maximum: int) -> bytes:
                if self.challenge_sent:
                    return b""
                self.challenge_sent = True
                time.sleep(0.08)
                return canonical_json({
                    "schemaVersion": "1.0.0", "sessionId": session["sessionId"],
                    "challenge": "6" * 64,
                }).encode("utf-8")

            def sendall(self, _payload: bytes) -> None:
                self.send_calls += 1

        peer = SlowChallengeConnection()
        with mock.patch.object(supervisor_module.socket, "create_connection", return_value=peer):
            with self.assertRaises(TimeoutError) as caught:
                request_supervisor(
                    self.root, session_path, "resource.restart/inventory", SCHEMAS, 50,
                )
        self.assertNotIsInstance(caught.exception, supervisor_module.SupervisorMutationOutcomeUnknown)
        self.assertEqual(peer.send_calls, 0)
        self.assertEqual(load_json(session_path)["state"], "closed")

    def test_invalid_post_send_mutation_response_is_outcome_unknown(self) -> None:
        _, session_path = self.start()
        original = load_json(session_path)
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        accepted: list[bytes] = []

        def forge_invalid_response() -> None:
            connection, _ = listener.accept()
            with connection:
                connection.sendall(canonical_json({
                    "schemaVersion": "1.0.0", "sessionId": original["sessionId"], "challenge": "7" * 64,
                }).encode("utf-8"))
                payload = bytearray()
                while chunk := connection.recv(65536):
                    payload.extend(chunk)
                accepted.append(bytes(payload))
                connection.sendall(canonical_json({"result": {}, "authorization": "0" * 64}).encode("utf-8"))

        peer = threading.Thread(target=forge_invalid_response, daemon=True)
        peer.start()
        redirected = copy.deepcopy(original)
        redirected["transport"]["port"] = listener.getsockname()[1]
        write_json(session_path, redirected)
        try:
            with self.assertRaisesRegex(supervisor_module.SupervisorMutationOutcomeUnknown, "invalid authenticated response") as caught:
                request_supervisor(self.root, session_path, "resource.restart/inventory", SCHEMAS)
            self.assertEqual(caught.exception.session_id, original["sessionId"])
            peer.join(timeout=2)
            self.assertEqual(len(accepted), 1)
            self.assertEqual(json.loads(accepted[0])["command"], "resource.restart/inventory")
            revoked = load_json(session_path)
            self.assertEqual(revoked["state"], "closed")
            self.assertNotIn("token", revoked)
        finally:
            listener.close()

    def test_server_pipe_submission_is_one_nonblocking_write_without_retry(self) -> None:
        stream = mock.Mock()
        stream.fileno.return_value = 42
        line = b"restart inventory\n"
        with mock.patch.object(supervisor_module.os, "write", side_effect=BlockingIOError):
            self.assertEqual(supervisor_module._write_server_input(stream, line), 0)
            supervisor_module.os.write.assert_called_once_with(42, line)
        with mock.patch.object(supervisor_module.os, "write", return_value=len(line) - 1):
            self.assertEqual(supervisor_module._write_server_input(stream, line), len(line) - 1)
            supervisor_module.os.write.assert_called_once_with(42, line)

    def test_wrong_token_input_drift_and_expiry_fail_closed(self) -> None:
        _, session_path = self.start()
        original = load_json(session_path)
        tampered = copy.deepcopy(original)
        tampered["token"] = "0" * 64
        write_json(session_path, tampered)
        with self.assertRaisesRegex(ValueError, "authorization|incomplete"):
            request_supervisor(self.root, session_path, "status", SCHEMAS)
        self.assertEqual(load_json(session_path)["state"], "closed")
        time.sleep(0.6)

        _, session_path = self.start()
        project = load_json(self.root / "neon.project.json")
        project["name"] = "changed-after-start"
        write_json(self.root / "neon.project.json", project)
        drift = request_supervisor(self.root, session_path, "runtime.compare", SCHEMAS)
        self.assertEqual(diagnostic_codes(drift), ["SUPERVISOR_INPUT_DRIFT"])
        request_supervisor(self.root, session_path, "shutdown", SCHEMAS)

        write_json(self.root / "neon.project.json", base_project())
        _, expiring = self.start(ttl=1, snapshot="later.json")
        time.sleep(1.5)
        with self.assertRaisesRegex(ValueError, "expired"):
            request_supervisor(self.root, expiring, "status", SCHEMAS)
        self.assertEqual(load_json(expiring)["state"], "expired")

    def test_active_session_record_is_an_exact_immutable_contract(self) -> None:
        _, session_path = self.start()
        session = load_json(session_path)
        tampered = copy.deepcopy(session)
        tampered["project"]["sha256"] = "f" * 64
        write_json(session_path, tampered)
        with self.assertRaises((OSError, ValueError)):
            request_supervisor(self.root, session_path, "runtime.compare", SCHEMAS)
        revoked = load_json(session_path)
        self.assertEqual(revoked["state"], "closed")
        self.assertNotIn("token", revoked)
        time.sleep(0.6)
        with self.assertRaises((OSError, ValueError)):
            self.raw_request(session, {
                "schemaVersion": "1.0.0", "sessionId": session["sessionId"],
                "token": session["token"], "command": "status",
            })

    def test_symlink_traversal_and_ttl_rejections_do_not_touch_outside(self) -> None:
        with tempfile.TemporaryDirectory(prefix="neon-supervisor-outside-") as external:
            outside = Path(external)
            (self.root / "linked-sessions").symlink_to(outside, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "symbolic link"):
                start_supervisor(
                    self.root, Path("neon.project.json"), None, Path("runtime.json"), Path("linked-sessions"),
                    30, CLI_PATH, SCHEMAS,
                )
            self.assertEqual(list(outside.iterdir()), [])
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "supervisor", "start", "--workspace", str(self.root), "--project", "../outside.json", "--ttl", "1", "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(json.loads(completed.stdout)["diagnostics"][0]["code"], "SUPERVISOR_TTL_INVALID")

        traversal = subprocess.run(
            [sys.executable, str(CLI_PATH), "supervisor", "start", "--workspace", str(self.root), "--project", "../outside.json", "--ttl", "30", "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        self.assertEqual(traversal.returncode, 1)
        self.assertEqual(json.loads(traversal.stdout)["diagnostics"][0]["code"], "SUPERVISOR_START_FAILED")

    @unittest.skipIf(os.name == "nt", "Windows uses the native handle backend")
    def test_unknown_non_dirfd_platform_fails_closed_instead_of_reopening_paths(self) -> None:
        with mock.patch.object(supervisor_module, "_HAS_DIRECTORY_FD", False):
            with self.assertRaisesRegex(OSError, "no secure handle-relative"):
                supervisor_module._open_directory(self.root)

    def test_snapshot_parent_swap_session_symlink_and_unknown_command_fail_closed(self) -> None:
        _, session_path = self.start(snapshot="observations/runtime.json")
        session = load_json(session_path)
        with tempfile.TemporaryDirectory(prefix="neon-supervisor-snapshot-outside-") as external:
            outside = Path(external)
            write_json(outside / "runtime.json", self.partial_snapshot(session))
            (self.root / "observations").symlink_to(outside, target_is_directory=True)
            unsafe = request_supervisor(self.root, session_path, "runtime.compare", SCHEMAS)
            self.assertEqual(diagnostic_codes(unsafe), ["SUPERVISOR_INPUT_UNSAFE"])

        linked_session = self.root / "linked-session.json"
        linked_session.symlink_to(session_path)
        with self.assertRaisesRegex(ValueError, "symbolic link"):
            request_supervisor(self.root, linked_session, "status", SCHEMAS)
        with self.assertRaisesRegex(ValueError, "not allowlisted"):
            request_supervisor(self.root, session_path, "runtime.compare;shutdown", SCHEMAS)
        alive = request_supervisor(self.root, session_path, "status", SCHEMAS)
        self.assertEqual(alive["session"]["state"], "active")

    def test_sessions_are_unique_and_never_adopt_existing_children(self) -> None:
        first, first_path = self.start()
        second, second_path = self.start(snapshot="second.json")
        self.assertNotEqual(first["session"]["sessionId"], second["session"]["sessionId"])
        self.assertNotEqual(first_path, second_path)
        self.assertTrue(first_path.is_file() and second_path.is_file())

    @unittest.skipIf(os.name == "nt", "deterministic directory-fd race test is POSIX-specific")
    def test_session_child_creation_stays_on_anchored_root_during_parent_swap(self) -> None:
        (self.root / "sessions").mkdir()
        original_root = self.root / "sessions-original"
        with tempfile.TemporaryDirectory(prefix="neon-start-root-outside-") as external:
            outside = Path(external)
            real_create = supervisor_module._create_directory_at

            def swap_then_create(parent_fd: int, name: str, mode: int = 0o700) -> int:
                (self.root / "sessions").rename(original_root)
                (self.root / "sessions").symlink_to(outside, target_is_directory=True)
                return real_create(parent_fd, name, mode)

            with mock.patch.object(supervisor_module, "_create_directory_at", side_effect=swap_then_create):
                with self.assertRaises(ValueError):
                    start_supervisor(
                        self.root, Path("neon.project.json"), None, Path("runtime.json"), Path("sessions"),
                        30, CLI_PATH, SCHEMAS,
                    )
            self.assertEqual(list(outside.iterdir()), [])
            self.assertEqual(len([item for item in original_root.iterdir() if item.name.startswith("session-")]), 1)

    @unittest.skipIf(os.name == "nt", "deterministic project symlink injection requires POSIX symlinks")
    def test_startup_catalogue_selection_uses_anchored_project_payload(self) -> None:
        shutil.copyfile(self.root / "api.json", self.root / "alternate.json")
        outside = self.root.parent / f"{self.root.name}-forged-project.json"
        forged = base_project()
        forged["catalogue"] = "alternate.json"
        forged["requiredApis"] = [{"name": "createVehicle", "side": "server"}]
        write_json(outside, forged)
        project_path = self.root / "neon.project.json"
        backup = self.root / "neon.project.original"
        real_resolve = supervisor_module.resolve_project_components
        observed_resolutions: list[dict] = []

        def injected_resolve(*args: object, **kwargs: object) -> dict:
            project_path.rename(backup)
            project_path.symlink_to(outside)
            try:
                result = real_resolve(*args, **kwargs)
                observed_resolutions.append(result)
                self.assertNotEqual(Path(args[0]).resolve(), project_path.resolve())
                return result
            finally:
                project_path.unlink()
                backup.rename(project_path)

        try:
            with mock.patch.object(supervisor_module, "resolve_project_components", side_effect=injected_resolve):
                started, session_path = self.start()
            session = load_json(session_path)
            self.assertEqual((started["status"], session["catalogue"]["path"]), ("pass", "api.json"))
            self.assertEqual(len(observed_resolutions), 1)
            self.assertEqual(observed_resolutions[0]["summary"]["apiRequirements"], 0)
        finally:
            outside.unlink(missing_ok=True)

    def test_malformed_unauthorized_requests_cannot_terminate_daemon(self) -> None:
        _, session_path = self.start()
        session = load_json(session_path)
        malformed = {
            "schemaVersion": 1, "sessionId": session["sessionId"],
            "token": "0" * 64, "command": "status",
        }
        rejected = self.raw_request(session, malformed)
        self.assertEqual(diagnostic_codes(rejected), ["SUPERVISOR_REQUEST_REJECTED"])

        reset = socket.create_connection(("127.0.0.1", session["transport"]["port"]), timeout=3)
        with reset.makefile("rb") as stream:
            stream.readline()
        reset.sendall(canonical_json({
            "schemaVersion": "1.0.0", "sessionId": session["sessionId"],
            "token": "0" * 64, "command": "status",
        }).encode("utf-8"))
        reset.close()
        time.sleep(0.1)
        alive = request_supervisor(self.root, session_path, "status", SCHEMAS)
        self.assertEqual(alive["session"]["state"], "active")

    def test_authenticated_request_cannot_be_replayed_on_a_new_challenge(self) -> None:
        _, session_path = self.start()
        session = load_json(session_path)
        with socket.create_connection(("127.0.0.1", session["transport"]["port"]), timeout=3) as first:
            with first.makefile("rb") as stream:
                challenge = json.loads(stream.readline())["challenge"]
            signed = {
                "schemaVersion": "1.0.0", "sessionId": session["sessionId"], "challenge": challenge,
                "nonce": "b" * 64, "command": "status",
            }
            request = {**signed, "authorization": _authorization(session["token"], signed)}
            replay = canonical_json(request).encode("utf-8")
            first.sendall(replay)
            first.shutdown(socket.SHUT_WR)
            while first.recv(65536):
                pass
        with socket.create_connection(("127.0.0.1", session["transport"]["port"]), timeout=3) as second:
            with second.makefile("rb") as stream:
                second_challenge = json.loads(stream.readline())["challenge"]
            self.assertNotEqual(challenge, second_challenge)
            second.sendall(replay)
            second.shutdown(socket.SHUT_WR)
            response = bytearray()
            while chunk := second.recv(65536):
                response.extend(chunk)
        rejected = json.loads(response)["result"]
        self.assertEqual(diagnostic_codes(rejected), ["SUPERVISOR_REQUEST_REJECTED"])
        self.assertEqual(request_supervisor(self.root, session_path, "status", SCHEMAS)["status"], "pass")

    @unittest.skipIf(os.name == "nt", "directory-descriptor swap test is POSIX-specific")
    def test_session_parent_swap_cannot_escape_or_overwrite(self) -> None:
        _, session_path = self.start()
        session = load_json(session_path)
        session_directory_name = session_path.parent.name
        original_root = self.root / "sessions-original"
        (self.root / "sessions").rename(original_root)
        with tempfile.TemporaryDirectory(prefix="neon-supervisor-external-") as external:
            outside = Path(external)
            outside_session = outside / session_directory_name
            outside_session.mkdir()
            sentinel = outside_session / "session.json"
            sentinel.write_bytes(b"external-sentinel")
            (self.root / "sessions").symlink_to(outside, target_is_directory=True)
            status = self.raw_request(session, {
                "schemaVersion": "1.0.0", "sessionId": session["sessionId"],
                "token": session["token"], "command": "status",
            })
            self.assertEqual(status["status"], "pass")
            stopped = self.raw_request(session, {
                "schemaVersion": "1.0.0", "sessionId": session["sessionId"],
                "token": session["token"], "command": "shutdown",
            })
            self.assertEqual(stopped["session"]["state"], "closed")
            self.assertEqual(sentinel.read_bytes(), b"external-sentinel")
            self.assertFalse((outside_session / "audit.jsonl").exists())
        original_session = original_root / session_directory_name / "session.json"
        self.assertEqual(load_json(original_session)["state"], "closed")

    @unittest.skipUnless(os.name == "nt", "Windows handle/reparse test")
    def test_windows_handles_reject_junctions_and_survive_parent_replacement(self) -> None:
        outside = self.root.parent / f"{self.root.name}-outside"
        outside.mkdir()
        linked = self.root / "linked"
        junction = subprocess.run(
            ["cmd.exe", "/c", "mklink", "/J", str(linked), str(outside)],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        self.assertEqual(junction.returncode, 0, junction.stdout + junction.stderr)
        root_fd = supervisor_module._open_directory(self.root)
        child_fd = None
        try:
            with self.assertRaisesRegex(ValueError, "reparse point"):
                supervisor_module._open_directory_at(root_fd, "linked")
            original = self.root.parent / f"{self.root.name}-original"
            self.root.rename(original)
            replacement = subprocess.run(
                ["cmd.exe", "/c", "mklink", "/J", str(self.root), str(outside)],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
            )
            self.assertEqual(replacement.returncode, 0, replacement.stdout + replacement.stderr)
            child_fd = supervisor_module._create_directory_at(root_fd, "anchored-session")
            supervisor_module._atomic_write_at(child_fd, "session.json", b"anchored")
            self.assertEqual(list(outside.iterdir()), [])
            self.assertEqual((original / "anchored-session" / "session.json").read_bytes(), b"anchored")
        finally:
            if child_fd is not None:
                supervisor_module._close_directory(child_fd)
            supervisor_module._close_directory(root_fd)
            if self.root.is_junction():
                self.root.rmdir()
            original = self.root.parent / f"{self.root.name}-original"
            if original.exists():
                original.rename(self.root)
            if linked.is_junction():
                linked.rmdir()
            outside.rmdir()

    def test_transitive_component_contract_drift_fails_closed(self) -> None:
        resource = self.root / "resources" / "inventory"
        resource.mkdir(parents=True)
        manifest = base_component("inventory", "resource")
        manifest["exports"] = [{
            "name": "oldExport", "side": "server", "parameters": [], "returns": [],
            "description": "Old export.", "http": False, "restricted": False,
        }]
        write_json(resource / "component.yaml", manifest)
        (resource / "meta.xml").write_text('<meta><script src="server.lua"/><export function="oldExport" type="server"/></meta>', encoding="utf-8")
        (resource / "server.lua").write_text("function oldExport() return true end\n", encoding="utf-8")
        project = base_project()
        project["resources"] = [{
            "name": "inventory", "path": "resources/inventory", "manifest": "component.yaml",
        }]
        write_json(self.root / "neon.project.json", project)
        _, session_path = self.start()
        session = load_json(session_path)
        write_json(self.root / "runtime.json", self.partial_snapshot(session))

        manifest["exports"][0]["name"] = "newExport"
        write_json(resource / "component.yaml", manifest)
        (resource / "meta.xml").write_text('<meta><script src="server.lua"/><export function="newExport" type="server"/></meta>', encoding="utf-8")
        (resource / "server.lua").write_text("function newExport() return true end\n", encoding="utf-8")
        self.assertEqual(check_project(self.root / "neon.project.json", SCHEMAS)["status"], "pass")
        drift = request_supervisor(self.root, session_path, "runtime.compare", SCHEMAS)
        self.assertEqual(diagnostic_codes(drift), ["SUPERVISOR_INPUT_DRIFT"])

    @unittest.skipIf(os.name == "nt", "high-frequency symlink race requires POSIX symlinks")
    def test_snapshot_symlink_race_can_never_read_outside_workspace(self) -> None:
        _, session_path = self.start(snapshot="race.json")
        session = load_json(session_path)
        inside = self.partial_snapshot(session)
        inside_payload = (canonical_json(inside) + "\n").encode("utf-8")
        (self.root / "race.json").write_bytes(inside_payload)
        with tempfile.TemporaryDirectory(prefix="neon-runtime-race-outside-") as external:
            outside_path = Path(external) / "outside.json"
            outside = self.partial_snapshot(session)
            outside["observations"][0]["functions"].append({"name": "outsideOnly", "restricted": False})
            outside_payload = (canonical_json(outside) + "\n").encode("utf-8")
            outside_path.write_bytes(outside_payload)
            outside_sha256 = sha256_bytes(outside_payload)
            stop = threading.Event()

            def swap() -> None:
                index = 0
                target = self.root / "race.json"
                while not stop.is_set():
                    temporary = self.root / f".race-{index % 2}.tmp"
                    temporary.write_bytes(inside_payload)
                    os.replace(temporary, target)
                    try:
                        target.unlink()
                        target.symlink_to(outside_path)
                    except FileExistsError:
                        pass
                    index += 1

            writer = threading.Thread(target=swap, daemon=True)
            writer.start()
            try:
                for _ in range(20):
                    result = request_supervisor(self.root, session_path, "runtime.compare", SCHEMAS)
                    self.assertNotEqual(result["comparison"]["snapshotSha256"], outside_sha256)
            finally:
                stop.set()
                writer.join(timeout=2)
                target = self.root / "race.json"
                target.unlink(missing_ok=True)
                target.write_bytes(inside_payload)

    def test_cli_failure_results_always_validate_advertised_contracts(self) -> None:
        invalid_ttl = subprocess.run(
            [sys.executable, str(CLI_PATH), "supervisor", "start", "--workspace", str(self.root), "--ttl", "1", "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        supervisor_result = json.loads(invalid_ttl.stdout)
        self.assertEqual((invalid_ttl.returncode, SCHEMAS.validate("neon-supervisor-result", supervisor_result)), (1, []))
        missing = subprocess.run(
            [sys.executable, str(CLI_PATH), "runtime", "compare", "missing/session.json", "--workspace", str(self.root), "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        runtime_result = json.loads(missing.stdout)
        self.assertEqual((missing.returncode, SCHEMAS.validate("neon-runtime-compare-result", runtime_result)), (1, []))
        self.assertEqual((runtime_result["comparison"]["scope"], runtime_result["comparison"]["grantedEvidenceLabels"]), ("observation-only", []))

    @unittest.skipIf(os.name == "nt", "symlink race requires POSIX symlinks")
    def test_session_file_symlink_race_never_reads_outside(self) -> None:
        _, session_path = self.start()
        legitimate = session_path.read_bytes()
        outside = self.root.parent / f"{self.root.name}-outside-session.json"
        forged_session = json.loads(legitimate)
        forged_session["sessionId"] = "session:forged"
        write_json(outside, forged_session)
        stop = threading.Event()

        def swap() -> None:
            target = session_path
            index = 0
            while not stop.is_set():
                temporary = target.parent / f".session-race-{index % 2}.tmp"
                temporary.write_bytes(legitimate)
                os.replace(temporary, target)
                try:
                    target.unlink()
                    target.symlink_to(outside)
                except FileExistsError:
                    pass
                index += 1

        writer = threading.Thread(target=swap, daemon=True)
        writer.start()
        try:
            for _ in range(2000):
                try:
                    observed, _, _ = _load_session(self.root, session_path, SCHEMAS)
                except (OSError, ValueError):
                    continue
                self.assertEqual(observed["sessionId"], json.loads(legitimate)["sessionId"])
        finally:
            stop.set()
            writer.join(timeout=2)
            session_path.unlink(missing_ok=True)
            session_path.write_bytes(legitimate)
            outside.unlink(missing_ok=True)

    def test_forged_loopback_response_is_rejected_and_token_is_not_transmitted(self) -> None:
        _, session_path = self.start()
        original = load_json(session_path)
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        captured: list[bytes] = []

        def forge() -> None:
            connection, _ = listener.accept()
            with connection:
                connection.sendall(canonical_json({
                    "schemaVersion": "1.0.0", "sessionId": original["sessionId"], "challenge": "f" * 64,
                }).encode("utf-8"))
                request = bytearray()
                while chunk := connection.recv(65536):
                    request.extend(chunk)
                captured.append(bytes(request))
                forged = {
                    "schemaVersion": "1.0.0", "command": "supervisor.status", "status": "pass",
                    "summary": {"errors": 0, "warnings": 0}, "diagnostics": [],
                    "session": {
                        "sessionId": original["sessionId"], "state": "active", "profile": original["profile"],
                        "createdAt": original["createdAt"], "expiresAt": original["expiresAt"],
                        "capabilities": original["capabilities"], "sessionPath": session_path.relative_to(self.root).as_posix(),
                        "snapshotAvailable": False,
                    },
                }
                connection.sendall(canonical_json({"result": forged, "authorization": "0" * 64}).encode("utf-8"))

        thread = threading.Thread(target=forge, daemon=True)
        thread.start()
        tampered = copy.deepcopy(original)
        tampered["transport"]["port"] = listener.getsockname()[1]
        write_json(session_path, tampered)
        try:
            with self.assertRaisesRegex(ValueError, "authorization"):
                request_supervisor(self.root, session_path, "status", SCHEMAS)
            thread.join(timeout=2)
            self.assertTrue(captured)
            self.assertNotIn(original["token"].encode("ascii"), captured[0])
            self.assertEqual(load_json(session_path)["state"], "closed")
            stale_bearer_accepted = False
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                try:
                    stale = self.raw_request(original, {
                        "schemaVersion": "1.0.0", "sessionId": original["sessionId"],
                        "token": original["token"], "command": "status",
                    })
                except (ConnectionError, OSError, ValueError, json.JSONDecodeError):
                    break
                stale_bearer_accepted = stale.get("status") == "pass"
                if stale_bearer_accepted:
                    break
                time.sleep(0.05)
            self.assertFalse(stale_bearer_accepted)
        finally:
            listener.close()

    def test_graceful_malicious_eof_revokes_record_and_original_daemon(self) -> None:
        _, session_path = self.start()
        original = load_json(session_path)
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)

        def consume_and_close() -> None:
            connection, _ = listener.accept()
            with connection:
                connection.sendall(canonical_json({
                    "schemaVersion": "1.0.0", "sessionId": original["sessionId"], "challenge": "e" * 64,
                }).encode("utf-8"))
                while connection.recv(65536):
                    pass

        thread = threading.Thread(target=consume_and_close, daemon=True)
        thread.start()
        tampered = copy.deepcopy(original)
        tampered["transport"]["port"] = listener.getsockname()[1]
        write_json(session_path, tampered)
        try:
            with self.assertRaises((JsonDocumentError, ValueError)):
                request_supervisor(self.root, session_path, "status", SCHEMAS)
            thread.join(timeout=2)
            revoked = load_json(session_path)
            self.assertEqual(revoked["state"], "closed")
            self.assertNotIn("token", revoked)
            with self.assertRaises((ConnectionError, OSError, ValueError, json.JSONDecodeError)):
                self.raw_request(original, {
                    "schemaVersion": "1.0.0", "sessionId": original["sessionId"],
                    "token": original["token"], "command": "status",
                })
        finally:
            listener.close()

    @unittest.skipIf(os.name == "nt", "parent replacement race requires POSIX symlinks")
    def test_revocation_uses_preopened_session_anchor_after_parent_replacement(self) -> None:
        _, session_path = self.start()
        original = load_json(session_path)
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        consumed = threading.Event()
        release = threading.Event()

        def consume_then_close() -> None:
            connection, _ = listener.accept()
            with connection:
                connection.sendall(canonical_json({
                    "schemaVersion": "1.0.0", "sessionId": original["sessionId"], "challenge": "9" * 64,
                }).encode("utf-8"))
                while connection.recv(65536):
                    pass
                consumed.set()
                release.wait(timeout=3)

        fake = threading.Thread(target=consume_then_close, daemon=True)
        fake.start()
        tampered = copy.deepcopy(original)
        tampered["transport"]["port"] = listener.getsockname()[1]
        write_json(session_path, tampered)
        errors: list[BaseException] = []

        def request() -> None:
            try:
                request_supervisor(self.root, session_path, "status", SCHEMAS)
            except BaseException as exc:
                errors.append(exc)

        requester = threading.Thread(target=request)
        requester.start()
        self.assertTrue(consumed.wait(timeout=3))
        original_root = self.root / "sessions-original"
        (self.root / "sessions").rename(original_root)
        with tempfile.TemporaryDirectory(prefix="neon-revoke-parent-outside-") as external:
            outside = Path(external)
            (self.root / "sessions").symlink_to(outside, target_is_directory=True)
            release.set()
            requester.join(timeout=3)
            fake.join(timeout=3)
            self.assertTrue(errors)
            anchored_session = original_root / session_path.parent.name / "session.json"
            revoked = load_json(anchored_session)
            self.assertEqual(revoked["state"], "closed")
            self.assertNotIn("token", revoked)
            self.assertEqual(list(outside.iterdir()), [])
            with self.assertRaises((ConnectionError, OSError, ValueError, json.JSONDecodeError)):
                self.raw_request(original, {
                    "schemaVersion": "1.0.0", "sessionId": original["sessionId"],
                    "token": original["token"], "command": "status",
                })
        listener.close()

    def test_preaccepted_request_cannot_cross_revocation_or_expiry(self) -> None:
        _, session_path = self.start()
        original = load_json(session_path)
        held = socket.create_connection(("127.0.0.1", original["transport"]["port"]), timeout=3)
        with held.makefile("rb") as stream:
            challenge = json.loads(stream.readline())["challenge"]
        signed = {
            "schemaVersion": "1.0.0", "sessionId": original["sessionId"], "challenge": challenge,
            "nonce": "c" * 64, "command": "status",
        }
        payload = canonical_json({**signed, "authorization": _authorization(original["token"], signed)}).encode("utf-8")

        unavailable = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        unavailable.bind(("127.0.0.1", 0))
        unavailable_port = unavailable.getsockname()[1]
        unavailable.close()
        tampered = copy.deepcopy(original)
        tampered["transport"]["port"] = unavailable_port
        write_json(session_path, tampered)
        with self.assertRaises(OSError):
            request_supervisor(self.root, session_path, "status", SCHEMAS)
        held.sendall(payload)
        held.shutdown(socket.SHUT_WR)
        try:
            response = held.recv(65536)
        except ConnectionError:
            response = b""
        held.close()
        self.assertEqual(response, b"")
        self.assertEqual(load_json(session_path)["state"], "closed")

        _, expiring_path = self.start(ttl=2, snapshot="expiring-race.json")
        expiring = load_json(expiring_path)
        held = socket.create_connection(("127.0.0.1", expiring["transport"]["port"]), timeout=3)
        with held.makefile("rb") as stream:
            challenge = json.loads(stream.readline())["challenge"]
        signed = {
            "schemaVersion": "1.0.0", "sessionId": expiring["sessionId"], "challenge": challenge,
            "nonce": "d" * 64, "command": "status",
        }
        time.sleep(2.2)
        held.sendall(canonical_json({**signed, "authorization": _authorization(expiring["token"], signed)}).encode("utf-8"))
        held.shutdown(socket.SHUT_WR)
        try:
            response = held.recv(65536)
        except ConnectionError:
            response = b""
        self.assertEqual(response, b"")
        held.close()
        expired = load_json(expiring_path)
        self.assertEqual(expired["state"], "expired")
        self.assertNotIn("token", expired)
        supervisor_module._wait_for_session_process(expiring)

    def test_rejected_request_flood_keeps_audit_bounded(self) -> None:
        _, session_path = self.start()
        session = load_json(session_path)
        for _ in range(1500):
            result = self.raw_request(session, {
                "schemaVersion": 1, "sessionId": session["sessionId"],
                "token": "0" * 64, "command": "status",
            })
            self.assertEqual(result["status"], "fail")
        audit = session_path.parent / "audit.jsonl"
        self.assertLessEqual(audit.stat().st_size, MAX_AUDIT_BYTES)
        marker = load_json(session_path.parent / "audit-truncated.json")
        self.assertEqual((marker["status"], marker["maximumBytes"]), ("truncated", MAX_AUDIT_BYTES))
        self.assertEqual(request_supervisor(self.root, session_path, "status", SCHEMAS)["status"], "pass")

    def test_oversized_invalid_session_is_bounded_and_daemon_death_revokes_bearer(self) -> None:
        oversized = self.root / "oversized-session.json"
        oversized.write_text('{"padding":"' + ('x' * 300000) + '"}', encoding="utf-8")
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "supervisor", "status", str(oversized), "--workspace", str(self.root), "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        result = json.loads(completed.stdout)
        self.assertEqual((completed.returncode, SCHEMAS.validate("neon-supervisor-result", result)), (1, []))
        self.assertLess(len(completed.stdout.encode("utf-8")), 4096)

        _, session_path = self.start(ttl=10)
        session = load_json(session_path)
        os.kill(session["pid"], 9)
        time.sleep(0.2)
        with self.assertRaises(OSError):
            request_supervisor(self.root, session_path, "status", SCHEMAS)
        revoked = load_json(session_path)
        self.assertEqual(revoked["state"], "closed")
        self.assertNotIn("token", revoked)


class ScenarioRunnerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="neon-scenario-runner-")
        self.root = Path(self.temporary.name)
        self.catalogue = self.root / "api.json"
        try:
            os.link(CATALOGUE_PATH, self.catalogue)
        except OSError:
            shutil.copyfile(CATALOGUE_PATH, self.catalogue)
        self.project_path = self.root / "neon.project.json"
        project = base_project()
        write_json(self.project_path, project)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_runtime_timeout_keeps_exit_124_and_cannot_be_expected_away(self) -> None:
        step = {
            "id": "step:runtime-timeout", "action": "resource.restart", "expectedStatus": "fail",
            "timeoutMs": 1, "inputs": {"resource": "inventory"},
        }

        def timed_out(_: dict) -> dict:
            return mutation_failure(
                "resource.restart", "SCENARIO_STEP_TIMEOUT", "request exceeded 1 millisecond",
                "inventory", "session:timeout-test",
            )

        record, diagnostics = _run_step(step, self.root, CLI_PATH, "neon-pair", timed_out)
        self.assertEqual((record["exitCode"], record["actualStatus"], record["status"]), (124, "fail", "pass"))
        self.assertIn("SCENARIO_STEP_TIMEOUT", diagnostic_codes({"diagnostics": diagnostics}))

    def assertion(self, identifier: str, kind: str, actual: str, message: str, expected: object = ...) -> Path:
        document = {"schemaVersion": "1.0.0", "id": identifier, "kind": kind, "actual": actual, "message": message}
        if expected is not ...:
            document["expected"] = expected
        path = self.root / f"{identifier.replace(':', '-')}.json"
        write_json(path, document)
        return path

    def scenario(self, steps: list[dict], assertions: list[Path], identifier: str = "test:scenario") -> Path:
        document = {
            "schemaVersion": "1.0.0", "id": identifier, "profile": "neon-pair", "steps": steps,
            "assertions": [load_json(path)["id"] for path in assertions],
        }
        path = self.root / "scenario.json"
        write_json(path, document)
        return path

    def run_cli(self, scenario: Path, assertions: list[Path], *extra: str) -> subprocess.CompletedProcess[str]:
        command = [
            sys.executable, str(CLI_PATH), "scenario", "run", str(scenario), "--workspace", str(self.root),
            "--observed-at", "2026-08-25T12:00:00Z", *extra,
        ]
        for assertion in assertions:
            command.extend(("--assertion", str(assertion)))
        command.append("--json")
        return subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)

    def verify_cli(self, run: str = "run") -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CLI_PATH), "scenario", "verify", run, "--workspace", str(self.root), "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )

    def test_static_scenario_emits_valid_evidence_artifacts_and_jsonl(self) -> None:
        assertions = [
            self.assertion("assertion:search", "equals", "step:search#/symbols/0/name", "find sparse Neon API", "setPedNavigateTo"),
            self.assertion("assertion:clean", "equals", "step:check#/summary/errors", "project must pass", 0),
        ]
        scenario = self.scenario([
            {
                "id": "step:search", "action": "api.search", "timeoutMs": 5000,
                "inputs": {"query": "npc pathfinding", "catalogue": "api.json", "origin": "neon", "state": "runtime-only", "side": "client", "limit": 5},
            },
            {"id": "step:check", "action": "check", "timeoutMs": 5000, "inputs": {"project": "neon.project.json"}},
        ], assertions)
        completed = self.run_cli(scenario, assertions, "--output", "run")
        self.assertEqual((completed.returncode, completed.stderr), (0, ""))
        result = json.loads(completed.stdout)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(SCHEMAS.validate("neon-test-result", result), [])
        self.assertEqual(SCHEMAS.validate("neon-evidence", result["evidence"]), [])
        self.assertEqual(result["evidence"]["labels"], ["static-checked"])
        artifact_index = load_json(self.root / "run" / "artifacts.json")
        for artifact in artifact_index["artifacts"]:
            self.assertEqual(SCHEMAS.validate("neon-artifact", artifact), [])
            payload = (self.root / "run" / artifact["path"]).read_bytes()
            self.assertEqual((len(payload), sha256_bytes(payload)), (artifact["size"], artifact["sha256"]))
        events = [json.loads(line) for line in (self.root / "run" / "events.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertEqual([event["sequence"] for event in events], list(range(1, len(events) + 1)))
        verified = self.verify_cli()
        self.assertEqual((verified.returncode, verified.stderr), (0, ""))
        verification = json.loads(verified.stdout)
        self.assertEqual(verification["status"], "pass")
        self.assertEqual(SCHEMAS.validate("neon-scenario-verify-result", verification), [])

    def test_scenario_verify_rejects_tampering_and_unindexed_files(self) -> None:
        assertion = self.assertion("assertion:clean", "equals", "/summary/errors", "clean project", 0)
        scenario = self.scenario([{"id": "step:check", "action": "check", "timeoutMs": 5000, "inputs": {}}], [assertion])
        completed = self.run_cli(scenario, [assertion], "--output", "run")
        self.assertEqual(completed.returncode, 0)
        (self.root / "run" / "steps" / "001.json").write_text("{}\n", encoding="utf-8")
        tampered = json.loads(self.verify_cli().stdout)
        self.assertIn("SCENARIO_ARTIFACT_TAMPERED", diagnostic_codes(tampered))

        shutil.rmtree(self.root / "run")
        self.assertEqual(self.run_cli(scenario, [assertion], "--output", "run").returncode, 0)
        (self.root / "run" / "unindexed.txt").write_text("not evidence\n", encoding="utf-8")
        unindexed = json.loads(self.verify_cli().stdout)
        self.assertIn("SCENARIO_FILE_UNINDEXED", diagnostic_codes(unindexed))

    def test_scenario_verify_rejects_control_mismatch_and_symlinks(self) -> None:
        assertion = self.assertion("assertion:clean", "equals", "/summary/errors", "clean project", 0)
        scenario = self.scenario([{"id": "step:check", "action": "check", "timeoutMs": 5000, "inputs": {}}], [assertion])
        self.assertEqual(self.run_cli(scenario, [assertion], "--output", "run").returncode, 0)
        result = load_json(self.root / "run" / "result.json")
        result["evidence"]["observedAt"] = "2026-08-25T12:00:01Z"
        write_json(self.root / "run" / "result.json", result)
        mismatch = json.loads(self.verify_cli().stdout)
        self.assertIn("SCENARIO_EVIDENCE_MISMATCH", diagnostic_codes(mismatch))

        evidence = load_json(self.root / "run" / "evidence.json")
        evidence["labels"] = ["built"]
        result["evidence"] = evidence
        write_json(self.root / "run" / "evidence.json", evidence)
        write_json(self.root / "run" / "result.json", result)
        scope = json.loads(self.verify_cli().stdout)
        self.assertIn("SCENARIO_EVIDENCE_SCOPE_INVALID", diagnostic_codes(scope))

        evidence["labels"] = ["static-checked"]
        result["evidence"] = evidence
        write_json(self.root / "run" / "evidence.json", evidence)
        write_json(self.root / "run" / "result.json", result)
        (self.root / "run" / "linked").symlink_to(self.root / "api.json")
        linked = json.loads(self.verify_cli().stdout)
        self.assertIn("SCENARIO_RUN_SYMLINK", diagnostic_codes(linked))

    def test_scenario_verify_recomputes_assertions_and_step_artifacts(self) -> None:
        assertion = self.assertion(
            "assertion:wrong", "equals", "/symbols/0/name", "deliberately wrong result", "definitelyWrong",
        )
        scenario = self.scenario([{
            "id": "step:search", "action": "api.search", "timeoutMs": 5000,
            "inputs": {"query": "draw text", "catalogue": "api.json", "side": "client", "limit": 1},
        }], [assertion])
        self.assertEqual(self.run_cli(scenario, [assertion], "--output", "forged").returncode, 1)
        original_result = load_json(self.root / "forged" / "result.json")
        result = copy.deepcopy(original_result)
        result["diagnostics"] = []
        result["summary"]["errors"] = 0
        result["status"] = "pass"
        write_json(self.root / "forged" / "result.json", result)
        rootless = json.loads(self.verify_cli("forged").stdout)
        self.assertIn("SCENARIO_IDENTITY_INVALID", diagnostic_codes(rootless))

        result = copy.deepcopy(original_result)
        result["assertions"][0]["status"] = "pass"
        result["diagnostics"] = []
        result["summary"]["errors"] = 0
        result["summary"]["passedAssertions"] = 1
        result["status"] = "pass"
        result["evidence"]["assertions"][0]["status"] = "pass"
        write_json(self.root / "forged" / "evidence.json", result["evidence"])
        write_json(self.root / "forged" / "result.json", result)
        forged = json.loads(self.verify_cli("forged").stdout)
        self.assertIn("SCENARIO_IDENTITY_INVALID", diagnostic_codes(forged))

        valid_assertion = self.assertion("assertion:valid", "equals", "/symbols/0/name", "valid result", "dxDrawText")
        valid_scenario = self.scenario([{
            "id": "step:search", "action": "api.search", "timeoutMs": 5000,
            "inputs": {"query": "draw text", "catalogue": "api.json", "side": "client", "limit": 1},
        }], [valid_assertion], identifier="test:artifact-contradiction")
        self.assertEqual(self.run_cli(valid_scenario, [valid_assertion], "--output", "contradictory").returncode, 0)
        step_path = self.root / "contradictory" / "steps" / "001.json"
        saved_step = load_json(step_path)
        saved_step["status"] = "fail"
        write_json(step_path, saved_step)
        payload = step_path.read_bytes()
        index = load_json(self.root / "contradictory" / "artifacts.json")
        artifact = next(item for item in index["artifacts"] if item["id"] == "artifact:scenario-step-001")
        artifact["size"] = len(payload)
        artifact["sha256"] = sha256_bytes(payload)
        write_json(self.root / "contradictory" / "artifacts.json", index)
        evidence = load_json(self.root / "contradictory" / "evidence.json")
        reference = next(item for item in evidence["artifacts"] if item["id"] == artifact["id"])
        reference["sha256"] = artifact["sha256"]
        write_json(self.root / "contradictory" / "evidence.json", evidence)
        result = load_json(self.root / "contradictory" / "result.json")
        result["evidence"] = evidence
        write_json(self.root / "contradictory" / "result.json", result)
        contradictory = json.loads(self.verify_cli("contradictory").stdout)
        self.assertIn("SCENARIO_IDENTITY_INVALID", diagnostic_codes(contradictory))

    def test_scenario_verify_rechecks_file_exists_against_the_workspace(self) -> None:
        assertion = self.assertion("assertion:missing", "file-exists", "never-created.txt", "file must exist")
        scenario = self.scenario([{"id": "step:check", "action": "check", "timeoutMs": 5000, "inputs": {}}], [assertion])
        self.assertEqual(self.run_cli(scenario, [assertion], "--output", "file-forged").returncode, 1)
        result = load_json(self.root / "file-forged" / "result.json")
        result["assertions"][0]["actual"] = True
        result["assertions"][0]["status"] = "pass"
        result["diagnostics"] = []
        result["summary"]["errors"] = 0
        result["summary"]["passedAssertions"] = 1
        result["status"] = "pass"
        result["evidence"]["assertions"][0]["status"] = "pass"

        events_path = self.root / "file-forged" / "events.jsonl"
        events = [json.loads(line) for line in events_path.read_text(encoding="utf-8").splitlines()]
        events[-1]["status"] = "pass"
        events_path.write_bytes(b"".join(canonical_json(event).encode("utf-8") for event in events))
        index = load_json(self.root / "file-forged" / "artifacts.json")
        event_artifact = next(item for item in index["artifacts"] if item["id"] == "artifact:scenario-events")
        event_payload = events_path.read_bytes()
        event_artifact["size"] = len(event_payload)
        event_artifact["sha256"] = sha256_bytes(event_payload)
        write_json(self.root / "file-forged" / "artifacts.json", index)
        event_reference = next(item for item in result["evidence"]["artifacts"] if item["id"] == event_artifact["id"])
        event_reference["sha256"] = event_artifact["sha256"]
        write_json(self.root / "file-forged" / "evidence.json", result["evidence"])
        write_json(self.root / "file-forged" / "result.json", result)
        forged = json.loads(self.verify_cli("file-forged").stdout)
        self.assertIn("SCENARIO_IDENTITY_INVALID", diagnostic_codes(forged))

    def test_scenario_verify_rejects_removed_infrastructure_diagnostics(self) -> None:
        assertion = self.assertion("assertion:failed", "equals", "step:build#/status", "build unavailable", "fail")
        scenario = self.scenario([{
            "id": "step:build", "action": "build", "expectedStatus": "fail", "timeoutMs": 1000, "inputs": {},
        }], [assertion])
        self.assertEqual(self.run_cli(scenario, [assertion], "--output", "infrastructure").returncode, 1)
        self.assertEqual(json.loads(self.verify_cli("infrastructure").stdout)["status"], "pass")
        result = load_json(self.root / "infrastructure" / "result.json")
        result["diagnostics"] = []
        result["summary"]["errors"] = 0
        result["status"] = "pass"
        write_json(self.root / "infrastructure" / "result.json", result)
        forged = json.loads(self.verify_cli("infrastructure").stdout)
        self.assertIn("SCENARIO_STEP_DIAGNOSTIC_MISSING", diagnostic_codes(forged))

        result["steps"][0]["result"]["diagnostics"] = []
        result["steps"][0]["result"]["summary"]["errors"] = 0
        step_path = self.root / "infrastructure" / "steps" / "001.json"
        write_json(step_path, result["steps"][0]["result"])
        step_payload = step_path.read_bytes()
        index = load_json(self.root / "infrastructure" / "artifacts.json")
        step_artifact = next(item for item in index["artifacts"] if item["id"] == "artifact:scenario-step-001")
        step_artifact["size"] = len(step_payload)
        step_artifact["sha256"] = sha256_bytes(step_payload)
        write_json(self.root / "infrastructure" / "artifacts.json", index)
        evidence = result["evidence"]
        step_reference = next(item for item in evidence["artifacts"] if item["id"] == step_artifact["id"])
        step_reference["sha256"] = step_artifact["sha256"]
        identity_payload = canonical_json({
            "command": result["command"],
            "scenario": load_json(scenario), "assertions": [load_json(assertion)],
            "steps": [{
                "id": result["steps"][0]["id"], "status": result["steps"][0]["status"],
                "result": result["steps"][0]["result"],
            }],
        }).encode("utf-8")
        evidence["runId"] = f"run:{load_json(scenario)['id']}:{sha256_bytes(identity_payload)[:20]}"
        result["evidence"] = evidence
        write_json(self.root / "infrastructure" / "evidence.json", evidence)
        write_json(self.root / "infrastructure" / "result.json", result)
        coherent_forge = json.loads(self.verify_cli("infrastructure").stdout)
        self.assertTrue({"SCENARIO_STEP_DIAGNOSTIC_MISSING", "SCENARIO_STEP_RESULT_STATUS_MISMATCH"}.intersection(diagnostic_codes(coherent_forge)))

    def test_genuine_infrastructure_failures_remain_integrity_verifiable(self) -> None:
        assertion = self.assertion("assertion:failed", "equals", "/status", "action failed", "fail")
        cases = [
            ("runtime-failure", {"id": "step:build", "action": "build", "timeoutMs": 1000, "inputs": {}}),
            ("input-failure", {"id": "step:check", "action": "check", "timeoutMs": 5000, "inputs": {"project": "../outside.json"}}),
            ("timeout-failure", {"id": "step:slow", "action": "api.search", "timeoutMs": 1, "inputs": {"query": "vehicle", "catalogue": "api.json"}}),
        ]
        for output, step in cases:
            with self.subTest(output=output):
                scenario = self.scenario([step], [assertion], identifier=f"test:{output}")
                self.assertEqual(self.run_cli(scenario, [assertion], "--output", output).returncode, 1)
                verification = json.loads(self.verify_cli(output).stdout)
                self.assertEqual((verification["status"], verification["diagnostics"]), ("pass", []))

    def test_scenario_verify_rejects_unsafe_or_missing_run(self) -> None:
        missing = json.loads(self.verify_cli("missing").stdout)
        self.assertIn("SCENARIO_RUN_UNSAFE", diagnostic_codes(missing))
        outside = json.loads(self.verify_cli("../outside").stdout)
        self.assertIn("SCENARIO_RUN_UNSAFE", diagnostic_codes(outside))

    def test_unknown_scenario_and_assertion_schema_majors_are_rejected(self) -> None:
        assertion = self.assertion("assertion:clean", "equals", "/summary/errors", "clean project", 0)
        scenario = self.scenario([{"id": "step:check", "action": "check", "timeoutMs": 5000, "inputs": {}}], [assertion])
        document = load_json(scenario)
        document["schemaVersion"] = "2.0.0"
        write_json(scenario, document)
        invalid_scenario = json.loads(self.run_cli(scenario, [assertion]).stdout)
        self.assertIn("unsupported scenario schema", invalid_scenario["diagnostics"][0]["message"])

        document["schemaVersion"] = "1.0.0"
        write_json(scenario, document)
        assertion_document = load_json(assertion)
        assertion_document["schemaVersion"] = "2.0.0"
        write_json(assertion, assertion_document)
        invalid_assertion = json.loads(self.run_cli(scenario, [assertion]).stdout)
        self.assertIn("unsupported assertion schema", invalid_assertion["diagnostics"][0]["message"])

    def test_repeated_runs_have_identical_content_artifacts_and_run_identity(self) -> None:
        assertions = [self.assertion("assertion:search", "equals", "/symbols/0/name", "find vehicle creation", "createVehicle")]
        scenario = self.scenario([{
            "id": "step:search", "action": "api.search", "timeoutMs": 5000,
            "inputs": {"query": "create car", "catalogue": "api.json", "kind": "function", "limit": 3},
        }], assertions)
        first = json.loads(self.run_cli(scenario, assertions, "--output", "run-a").stdout)
        second = json.loads(self.run_cli(scenario, assertions, "--output", "run-b").stdout)
        self.assertEqual(first["evidence"]["runId"], second["evidence"]["runId"])
        first_artifacts = load_json(self.root / "run-a" / "artifacts.json")
        second_artifacts = load_json(self.root / "run-b" / "artifacts.json")
        self.assertEqual(first_artifacts, second_artifacts)
        for artifact in first_artifacts["artifacts"]:
            self.assertEqual(
                (self.root / "run-a" / artifact["path"]).read_bytes(),
                (self.root / "run-b" / artifact["path"]).read_bytes(),
            )

    def test_saved_results_normalize_workspace_paths(self) -> None:
        assertion = self.assertion("assertion:context", "file-exists", "generated/agent-context.json", "context exists")
        scenario = self.scenario([{
            "id": "step:generate", "action": "generate.project", "timeoutMs": 10000,
            "inputs": {"project": "neon.project.json", "catalogue": "api.json", "output": "generated"},
        }], [assertion])
        completed = self.run_cli(scenario, [assertion], "--output", "run")
        result = json.loads(completed.stdout)
        self.assertEqual((completed.returncode, result["status"]), (0, "pass"))
        self.assertEqual(result["steps"][0]["result"]["output"], "workspace:/generated")
        self.assertNotIn(str(self.root), canonical_json(result))
        self.assertEqual(json.loads(self.verify_cli().stdout)["status"], "pass")

    def test_expected_negative_step_can_pass_with_precise_assertion(self) -> None:
        project = base_project()
        project["resources"] = [{"name": "missing", "path": "resources/missing"}]
        write_json(self.project_path, project)
        assertions = [self.assertion(
            "assertion:missing", "equals", "step:negative#/diagnostics/0/code", "missing resource must be detected", "MISSING_RESOURCE",
        )]
        scenario = self.scenario([{
            "id": "step:negative", "action": "check", "expectedStatus": "fail", "timeoutMs": 5000,
            "inputs": {"project": "neon.project.json"},
        }], assertions)
        completed = self.run_cli(scenario, assertions)
        result = json.loads(completed.stdout)
        self.assertEqual((completed.returncode, result["status"], result["steps"][0]["status"]), (0, "pass", "pass"))

    def test_runtime_action_is_fail_closed_even_when_failure_is_expected(self) -> None:
        assertions = [self.assertion("assertion:failed", "equals", "step:build#/status", "build is unavailable", "fail")]
        scenario = self.scenario([{
            "id": "step:build", "action": "build", "expectedStatus": "fail", "timeoutMs": 1000, "inputs": {},
        }], assertions)
        completed = self.run_cli(scenario, assertions)
        result = json.loads(completed.stdout)
        self.assertEqual(completed.returncode, 1)
        self.assertIn("SCENARIO_ACTION_UNAVAILABLE", diagnostic_codes(result))

    def test_timeout_is_an_infrastructure_failure_not_an_expected_negative(self) -> None:
        assertions = [self.assertion("assertion:failed", "equals", "step:slow#/status", "search times out", "fail")]
        scenario = self.scenario([{
            "id": "step:slow", "action": "api.search", "expectedStatus": "fail", "timeoutMs": 1,
            "inputs": {"query": "vehicle", "catalogue": "api.json"},
        }], assertions)
        result = json.loads(self.run_cli(scenario, assertions).stdout)
        self.assertIn("SCENARIO_STEP_TIMEOUT", diagnostic_codes(result))
        self.assertEqual(result["steps"][0]["exitCode"], 124)

    def test_path_traversal_and_output_symlink_are_rejected_without_writes(self) -> None:
        assertions = [self.assertion("assertion:x", "truthy", "step:x#/status", "unreachable")]
        scenario = self.scenario([{
            "id": "step:x", "action": "check", "timeoutMs": 5000, "inputs": {"project": "../outside.json"},
        }], assertions)
        result = json.loads(self.run_cli(scenario, assertions).stdout)
        self.assertIn("SCENARIO_INPUT_INVALID", diagnostic_codes(result))
        with tempfile.TemporaryDirectory(prefix="neon-scenario-outside-") as external:
            outside = Path(external)
            link = self.root / "linked-output"
            link.symlink_to(outside, target_is_directory=True)
            completed = self.run_cli(scenario, assertions, "--output", "linked-output")
            self.assertEqual(completed.returncode, 1)
            self.assertFalse(any(outside.iterdir()))

    def test_symlinked_control_documents_are_rejected(self) -> None:
        assertion = self.assertion("assertion:clean", "equals", "/summary/errors", "clean project", 0)
        scenario = self.scenario([{"id": "step:check", "action": "check", "timeoutMs": 5000, "inputs": {}}], [assertion])
        scenario_link = self.root / "scenario-link.json"
        assertion_link = self.root / "assertion-link.json"
        scenario_link.symlink_to(scenario)
        assertion_link.symlink_to(assertion)
        linked_scenario = self.run_cli(scenario_link, [assertion])
        linked_assertion = self.run_cli(scenario, [assertion_link])
        self.assertEqual((linked_scenario.returncode, linked_assertion.returncode), (1, 1))
        self.assertEqual(json.loads(linked_scenario.stdout)["diagnostics"][0]["code"], "SCENARIO_INVALID")
        self.assertEqual(json.loads(linked_assertion.stdout)["diagnostics"][0]["code"], "SCENARIO_INVALID")

    def test_macos_workspace_aliases_preserve_valid_paths_and_symlink_rejection(self) -> None:
        if not str(self.root).startswith(("/var/", "/tmp/")):
            self.skipTest("macOS /var or /tmp alias is not active")
        assertion = self.assertion("assertion:clean", "equals", "/summary/errors", "clean", 0)
        scenario = self.scenario([{"id": "step:check", "action": "check", "timeoutMs": 5000, "inputs": {}}], [assertion])
        command = [
            sys.executable, str(CLI_PATH), "scenario", "run", str(scenario), "--assertion", str(assertion),
            "--workspace", str(self.root.resolve()), "--output", str(self.root / "alias-run"), "--json",
        ]
        completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        self.assertEqual(completed.returncode, 0)
        verify = subprocess.run(
            [sys.executable, str(CLI_PATH), "scenario", "verify", str(self.root / "alias-run"), "--workspace", str(self.root.resolve()), "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        self.assertEqual((verify.returncode, json.loads(verify.stdout)["status"]), (0, "pass"))

        scenario_link = self.root / "alias-scenario-link.json"
        scenario_link.symlink_to(scenario)
        command[4] = str(scenario_link)
        command[command.index(str(self.root / "alias-run"))] = str(self.root / "alias-link-output")
        rejected = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        self.assertEqual(rejected.returncode, 1)

    def test_profile_mismatch_duplicate_steps_and_assertion_set_mismatch_fail(self) -> None:
        assertion = self.assertion("assertion:x", "truthy", "step:x#/status", "x")
        scenario = self.scenario([{"id": "step:x", "action": "check", "timeoutMs": 5000, "inputs": {}}], [assertion])
        project = base_project()
        project["profile"] = "mta-upstream"
        write_json(self.project_path, project)
        result = json.loads(self.run_cli(scenario, [assertion]).stdout)
        self.assertIn("SCENARIO_PROFILE_MISMATCH", diagnostic_codes(result))

        document = load_json(scenario)
        document["steps"].append(copy.deepcopy(document["steps"][0]))
        write_json(scenario, document)
        duplicate = json.loads(self.run_cli(scenario, [assertion]).stdout)
        self.assertEqual(duplicate["diagnostics"][0]["code"], "SCENARIO_INVALID")
        document["steps"].pop()
        write_json(scenario, document)
        other = self.assertion("assertion:other", "truthy", "/status", "other")
        mismatch = json.loads(self.run_cli(scenario, [other]).stdout)
        self.assertEqual(mismatch["diagnostics"][0]["code"], "SCENARIO_INVALID")

    def test_output_directory_is_never_adopted_or_overwritten(self) -> None:
        assertion = self.assertion("assertion:clean", "equals", "/summary/errors", "clean", 0)
        scenario = self.scenario([{"id": "step:check", "action": "check", "timeoutMs": 5000, "inputs": {}}], [assertion])
        output = self.root / "occupied"
        output.mkdir()
        sentinel = output / "user.txt"
        sentinel.write_text("owned", encoding="utf-8")
        completed = self.run_cli(scenario, [assertion], "--output", "occupied")
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "owned")

    def test_output_overlap_and_invalid_clock_fail_before_step_mutation(self) -> None:
        assertion = self.assertion("assertion:file", "file-exists", "generated/agent-context.json", "context exists")
        scenario = self.scenario([{
            "id": "step:generate", "action": "generate.project", "timeoutMs": 10000,
            "inputs": {"project": "neon.project.json", "output": "run/generated"},
        }], [assertion])
        overlap = self.run_cli(scenario, [assertion], "--output", "run")
        self.assertEqual(overlap.returncode, 1)
        self.assertFalse((self.root / "run").exists())

        nested = self.root / "nested"
        nested.mkdir()
        write_json(nested / "neon.project.json", base_project())
        nested_scenario = self.scenario([{
            "id": "step:generate", "action": "generate.project", "timeoutMs": 10000,
            "inputs": {"project": "nested/neon.project.json", "catalogue": "api.json"},
        }], [assertion], identifier="test:nested-default-overlap")
        default_overlap = self.run_cli(nested_scenario, [assertion], "--output", "nested/.neon")
        self.assertEqual(default_overlap.returncode, 1)
        self.assertFalse((nested / ".neon").exists())

        clean_scenario = self.scenario([{"id": "step:check", "action": "check", "timeoutMs": 5000, "inputs": {}}], [assertion])
        command = [
            sys.executable, str(CLI_PATH), "scenario", "run", str(clean_scenario), "--workspace", str(self.root),
            "--observed-at", "not-a-time", "--assertion", str(assertion), "--json",
        ]
        invalid = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        self.assertEqual(invalid.returncode, 1)
        self.assertEqual(json.loads(invalid.stdout)["diagnostics"][0]["code"], "SCENARIO_INVALID")

    def test_scenario_profile_pins_search_and_timeout_budget_is_bounded(self) -> None:
        assertion = self.assertion("assertion:profile", "equals", "/symbols/0/name", "profile", "createVehicle")
        scenario = self.scenario([{
            "id": "step:search", "action": "api.search", "timeoutMs": 5000,
            "inputs": {"query": "create car", "catalogue": "api.json", "profile": "mta-upstream"},
        }], [assertion])
        mismatch = json.loads(self.run_cli(scenario, [assertion]).stdout)
        self.assertIn("SCENARIO_INPUT_INVALID", diagnostic_codes(mismatch))

        document = load_json(scenario)
        document["steps"] = [
            {"id": "step:a", "action": "check", "timeoutMs": 300001, "inputs": {}},
            {"id": "step:b", "action": "check", "timeoutMs": 300000, "inputs": {}},
        ]
        write_json(scenario, document)
        budget = json.loads(self.run_cli(scenario, [assertion]).stdout)
        self.assertEqual(budget["diagnostics"][0]["code"], "SCENARIO_INVALID")

    def test_all_assertion_kinds_and_invalid_pointer_are_bounded(self) -> None:
        assertions = [
            self.assertion("assertion:eq", "equals", "/summary/errors", "equal", 0),
            self.assertion("assertion:neq", "not-equals", "/status", "not equal", "fail"),
            self.assertion("assertion:true", "truthy", "/summary", "truthy"),
            self.assertion("assertion:false", "falsy", "/diagnostics", "falsy"),
            self.assertion("assertion:contains", "contains", "/summary", "contains", "errors"),
            self.assertion("assertion:absent", "diagnostic-absent", "/diagnostics", "no internal error", "INTERNAL_ERROR"),
            self.assertion("assertion:file", "file-exists", "neon.project.json", "project exists"),
        ]
        scenario = self.scenario([{"id": "step:check", "action": "check", "timeoutMs": 5000, "inputs": {}}], assertions)
        result = json.loads(self.run_cli(scenario, assertions).stdout)
        self.assertEqual((result["status"], result["summary"]["passedAssertions"]), ("pass", 7))

        bad = self.assertion("assertion:bad", "equals", "step:missing#/x", "bad pointer", 1)
        scenario = self.scenario([{"id": "step:check", "action": "check", "timeoutMs": 5000, "inputs": {}}], [bad])
        failed = json.loads(self.run_cli(scenario, [bad]).stdout)
        self.assertIn("ASSERTION_EVALUATION_FAILED", diagnostic_codes(failed))

    def test_json_equality_keeps_booleans_distinct_from_numbers(self) -> None:
        assertion = self.assertion("assertion:typed", "equals", "/summary/errors", "zero is not false", False)
        scenario = self.scenario([{"id": "step:check", "action": "check", "timeoutMs": 5000, "inputs": {}}], [assertion])
        result = json.loads(self.run_cli(scenario, [assertion]).stdout)
        self.assertEqual((result["status"], result["assertions"][0]["status"]), ("fail", "fail"))

    def test_repository_static_smoke_scenario_passes_without_writing(self) -> None:
        scenario = TOOL_DIRECTORY / "scenarios" / "static-smoke.json"
        assertions = [
            TOOL_DIRECTORY / "scenarios" / "assertions" / "static-search.json",
            TOOL_DIRECTORY / "scenarios" / "assertions" / "static-check.json",
        ]
        command = [
            sys.executable, str(CLI_PATH), "scenario", "run", str(scenario), "--workspace", str(REPOSITORY_ROOT),
            "--observed-at", "2026-08-25T12:00:00Z",
        ]
        for assertion in assertions:
            command.extend(("--assertion", str(assertion)))
        command.append("--json")
        completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        self.assertEqual((completed.returncode, completed.stderr), (0, ""))
        self.assertEqual(json.loads(completed.stdout)["status"], "pass")


class AgentContextGenerationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="neon-agent-context-")
        self.root = Path(self.temporary.name)
        self.project_path = self.root / "neon.project.json"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def create_project(self, profile: str = "neon-pair") -> dict:
        project = base_project()
        project["schemaVersion"] = "1.1.0"
        project["profile"] = profile
        project["unknownComponents"] = "error"

        inventory = self.root / "resources" / "inventory"
        inventory.mkdir(parents=True)
        inventory_manifest = base_component("inventory")
        inventory_manifest["exports"] = [{
            "name": "takeItem", "side": "server", "parameters": [{"name": "item", "type": "string", "optional": False, "description": "Item identifier."}],
            "returns": [{"name": "removed", "type": "boolean", "description": "Whether removal succeeded."}],
            "description": "Remove an item.", "http": False, "restricted": False,
        }]
        inventory_manifest["events"] = [{"name": "inventoryChanged", "side": "server", "directions": ["defines", "emits"], "parameters": [], "allowRemoteTrigger": False, "description": "Inventory mutation."}]
        write_json(inventory / "component.yaml", inventory_manifest)
        (inventory / "meta.xml").write_text('<meta><script src="server.lua" type="server"/><export function="takeItem" type="server"/></meta>', encoding="utf-8")
        (inventory / "server.lua").write_text("function takeItem(item) return true end\naddEvent('inventoryChanged', false)\ncreateVehicle(411, 0, 0, 3)\n", encoding="utf-8")
        project["resources"].append({"name": "inventory", "path": "resources/inventory", "manifest": "component.yaml"})

        consumer = self.root / "resources" / "consumer"
        consumer.mkdir(parents=True)
        consumer_manifest = base_component("consumer")
        consumer_manifest["dependencies"] = [{"kind": "resource", "name": "inventory", "optional": False, "minimumVersion": "1.0.0"}]
        write_json(consumer / "component.yaml", consumer_manifest)
        (consumer / "meta.xml").write_text('<meta><oop>true</oop><include resource="inventory"/><script src="server.lua" type="server"/><script src="client.lua" type="client"/></meta>', encoding="utf-8")
        (consumer / "server.lua").write_text("exports.inventory:takeItem('medkit')\nnativePing()\nlocal inventoryPed = Ped.create(7, 0, 0, 3)\ninventoryPed:setData('inventory', true)\n", encoding="utf-8")
        (consumer / "client.lua").write_text("dxDrawText('inventory ready', 0, 0)\n", encoding="utf-8")
        project["resources"].append({"name": "consumer", "path": "resources/consumer", "manifest": "component.yaml"})

        module = self.root / "modules" / "native"
        module.mkdir(parents=True)
        module_manifest = base_component("native", "module")
        module_manifest["exports"] = [{"name": "nativePing", "side": "server", "parameters": [], "returns": [{"type": "boolean", "description": "Whether the module responded."}], "description": "Ping the native module.", "http": False, "restricted": False}]
        write_json(module / "component.yaml", module_manifest)
        (module / "native.dll").write_bytes(b"agent-context-placeholder")
        project["modules"] = [{"name": "native", "path": "modules/native", "manifest": "component.yaml", "binary": "native.dll"}]
        write_json(self.project_path, project)
        return project

    def generate(self, output: str = "generated") -> tuple[dict, dict]:
        return generate_project_context(self.project_path, SCHEMAS, self.root / output, CATALOGUE_PATH)

    def test_context_pack_is_schema_valid_content_addressed_and_byte_deterministic(self) -> None:
        self.create_project()
        first_context, first_artifacts = self.generate("first")
        second_context, second_artifacts = self.generate("second")
        self.assertEqual(first_context, second_context)
        self.assertEqual(first_artifacts, second_artifacts)
        self.assertEqual(SCHEMAS.validate("neon-agent-context", first_context), [])
        self.assertEqual(SCHEMAS.validate("neon-api-index", load_json(self.root / "first" / "api-index.json")), [])
        for artifact in first_artifacts["artifacts"]:
            self.assertEqual(SCHEMAS.validate("neon-artifact", artifact), [])
            self.assertEqual((self.root / "first" / artifact["path"]).read_bytes(), (self.root / "second" / artifact["path"]).read_bytes())
            self.assertEqual(sha256_bytes((self.root / "first" / artifact["path"]).read_bytes()), artifact["sha256"])
        self.assertEqual([item["path"] for item in first_context["files"]], sorted(item["path"] for item in first_context["files"]))
        self.assertTrue(all(not Path(item["path"]).is_absolute() for item in first_context["files"]))
        self.assertNotIn(str(self.root), canonical_json(first_context))

    def test_context_identifies_used_global_apis_and_full_profile_index(self) -> None:
        self.create_project()
        context, _ = self.generate()
        self.assertTrue({"mta:function:addEvent", "mta:function:createVehicle", "mta:function:dxDrawText", "mta:function:createPed", "mta:function:setElementData", "mta:class:Ped"}.issubset(context["usedApiIds"]))
        index = load_json(self.root / "generated" / "api-index.json")
        self.assertGreater(len(index["symbols"]), 2000)
        self.assertEqual([item["id"] for item in index["symbols"]], sorted(item["id"] for item in index["symbols"]))
        self.assertEqual(len({item["id"] for item in index["symbols"]}), len(index["symbols"]))
        alpha = next(item for item in index["symbols"] if item["id"] == "mta:function:setElementAlpha")
        self.assertIn("alpha", alpha["keywords"])
        self.assertIn("transparency", alpha["keywords"])

    def test_context_and_index_schemas_reject_unknown_fields(self) -> None:
        self.create_project()
        context, _ = self.generate()
        index = load_json(self.root / "generated" / "api-index.json")
        context["typo"] = True
        index["symbols"][0]["typo"] = True
        self.assertIn("/typo", {issue.pointer for issue in SCHEMAS.validate("neon-agent-context", context)})
        self.assertIn("/symbols/0/typo", {issue.pointer for issue in SCHEMAS.validate("neon-api-index", index)})

    def test_project_luals_separates_sides_and_keeps_module_evidence_visible(self) -> None:
        self.create_project()
        self.generate()
        server = (self.root / "generated" / "server" / "project-server.lua").read_text(encoding="utf-8")
        client = (self.root / "generated" / "client" / "project-client.lua").read_text(encoding="utf-8")
        self.assertIn("function nativePing(...)", server)
        self.assertIn("`module:native:function:nativePing` (documented-only)", server)
        self.assertIn("function neon_exports_inventory_server:takeItem(...)", server)
        self.assertNotIn("nativePing", client)
        self.assertNotIn("takeItem", client)
        config = load_json(self.root / "generated" / "server" / ".luarc.json")
        self.assertEqual(config["runtime"]["version"], "Lua 5.1")
        self.assertEqual(config["workspace"]["library"], ["mta-shared.lua", "mta-server.lua", "project-server.lua"])

    @unittest.skipUnless(shutil.which("luac"), "luac is optional; syntax is also exercised by the final agent harness")
    def test_generated_luals_files_are_valid_lua_syntax(self) -> None:
        self.create_project()
        self.generate()
        files = sorted((self.root / "generated").glob("*/*.lua"))
        completed = subprocess.run([shutil.which("luac"), "-p", *map(str, files)], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        self.assertEqual((completed.returncode, completed.stdout, completed.stderr), (0, "", ""))

    def test_opaque_project_luals_never_invents_zero_arity(self) -> None:
        project = base_project()
        project["unknownComponents"] = "allow-opaque"
        resource = self.root / "resources" / "legacy"
        resource.mkdir(parents=True)
        (resource / "meta.xml").write_text('<meta><script src="server.lua"/><export function="dynamicLookup" type="server"/></meta>', encoding="utf-8")
        (resource / "server.lua").write_text("function dynamicLookup(key, fallback) return fallback end\n", encoding="utf-8")
        project["resources"] = [{"name": "legacy", "path": "resources/legacy"}]
        write_json(self.project_path, project)
        self.generate()
        local = (self.root / "generated" / "server" / "project-server.lua").read_text(encoding="utf-8")
        project_api = load_json(self.root / "generated" / "project-api.json")
        symbol = project_api["symbols"][0]
        context = load_json(self.root / "generated" / "agent-context.json")
        self.assertFalse(symbol["signatureKnown"])
        self.assertNotIn("parameters", symbol)
        self.assertNotIn("returns", symbol)
        self.assertIn("---@param ... unknown", local)
        self.assertIn("---@return unknown", local)
        self.assertEqual(context["validation"]["summary"]["warnings"], 1)
        self.assertEqual(diagnostic_codes(context["validation"]), ["RESOURCE_EXPORT_OPAQUE"])
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "generate", "project", "--project", str(self.project_path), "--catalogue", str(CATALOGUE_PATH), "--output", str(self.root / "cli-opaque"), "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        cli_result = json.loads(completed.stdout)
        self.assertEqual((completed.returncode, cli_result["summary"]["warnings"]), (0, 1))
        self.assertEqual(diagnostic_codes(cli_result), ["RESOURCE_EXPORT_OPAQUE"])

    def test_failed_static_check_creates_no_output(self) -> None:
        self.create_project()
        inventory_manifest = load_json(self.root / "resources" / "inventory" / "component.yaml")
        inventory_manifest["events"][0]["allowRemoteTrigger"] = True
        write_json(self.root / "resources" / "inventory" / "component.yaml", inventory_manifest)
        output = self.root / "blocked"
        with self.assertRaises(ContextGenerationError) as raised:
            generate_project_context(self.project_path, SCHEMAS, output, CATALOGUE_PATH)
        self.assertIn("RESOURCE_EVENT_REMOTE_MISMATCH", diagnostic_codes(raised.exception.result))
        self.assertFalse(output.exists())
        cli_output = self.root / "blocked-cli"
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "generate", "project", "--project", str(self.project_path), "--catalogue", str(CATALOGUE_PATH), "--output", str(cli_output), "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        result = json.loads(completed.stdout)
        self.assertEqual((completed.returncode, result["command"]), (1, "generate.project"))
        self.assertIn("RESOURCE_EVENT_REMOTE_MISMATCH", diagnostic_codes(result))
        self.assertFalse(cli_output.exists())

    def test_server_profile_excludes_client_only_api_from_index_and_luals(self) -> None:
        self.create_project("neon-server")
        consumer_meta = self.root / "resources" / "consumer" / "meta.xml"
        consumer_meta.write_text('<meta><oop>true</oop><include resource="inventory"/><script src="server.lua" type="server"/></meta>', encoding="utf-8")
        self.generate()
        index = load_json(self.root / "generated" / "api-index.json")
        self.assertNotIn("mta:function:dxDrawText", {item["id"] for item in index["symbols"]})
        client = (self.root / "generated" / "client" / "mta-client.lua").read_text(encoding="utf-8")
        self.assertNotIn("function dxDrawText", client)

    @unittest.skipIf(os.name == "nt", "creating symlinks is not reliably available to unprivileged Windows tests")
    def test_output_symlink_is_rejected_before_writing(self) -> None:
        self.create_project()
        target = self.root / "real-output"
        target.mkdir()
        link = self.root / "linked-output"
        link.symlink_to(target, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symbolic link"):
            generate_project_context(self.project_path, SCHEMAS, link, CATALOGUE_PATH)
        self.assertEqual(list(target.iterdir()), [])

    @unittest.skipIf(os.name == "nt", "creating symlinks is not reliably available to unprivileged Windows tests")
    def test_generated_file_symlink_cannot_overwrite_outside_output(self) -> None:
        self.create_project()
        output = self.root / "output"
        (output / "server").mkdir(parents=True)
        outside = self.root / "outside.lua"
        outside.write_text("sentinel\n", encoding="utf-8")
        (output / "server" / "project-server.lua").symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "regular file"):
            generate_project_context(self.project_path, SCHEMAS, output, CATALOGUE_PATH)
        self.assertEqual(outside.read_text(encoding="utf-8"), "sentinel\n")

    def test_generate_project_cli_uses_cwd_and_default_dot_neon(self) -> None:
        self.create_project()
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "generate", "project", "--catalogue", str(CATALOGUE_PATH), "--json"],
            cwd=self.root, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        result = json.loads(completed.stdout)
        self.assertEqual((completed.returncode, completed.stderr, result["status"]), (0, "", "pass"))
        self.assertEqual(result["summary"]["artifacts"], 11)
        self.assertTrue((self.root / ".neon" / "agent-context.json").is_file())

    def test_context_verify_passes_then_detects_source_staleness(self) -> None:
        self.create_project()
        self.generate()
        valid = verify_project_context(self.project_path, SCHEMAS, self.root / "generated", CATALOGUE_PATH)
        self.assertEqual((valid["status"], valid["summary"]["artifacts"]), ("pass", 11))
        server = self.root / "resources" / "consumer" / "server.lua"
        server.write_text(server.read_text(encoding="utf-8") + "-- source changed\n", encoding="utf-8")
        stale = verify_project_context(self.project_path, SCHEMAS, self.root / "generated", CATALOGUE_PATH)
        self.assertEqual(stale["status"], "fail")
        self.assertIn("CONTEXT_STALE", diagnostic_codes(stale))

    def test_declared_resource_asset_is_hashed_and_participates_in_freshness(self) -> None:
        self.create_project()
        consumer = self.root / "resources" / "consumer"
        asset = consumer / "ui" / "status.json"
        asset.parent.mkdir()
        asset.write_text('{"ready":true}\n', encoding="utf-8")
        meta = consumer / "meta.xml"
        meta.write_text(
            '<meta><oop>true</oop><include resource="inventory"/><script src="server.lua" type="server"/>'
            '<script src="client.lua" type="client"/><file src="ui/status.json"/></meta>',
            encoding="utf-8",
        )
        context, _ = self.generate()
        record = next(item for item in context["files"] if item["path"].endswith("ui/status.json"))
        self.assertEqual((record["kind"], record["side"], record["size"]), ("asset", "client", asset.stat().st_size))
        self.assertEqual(record["sha256"], sha256_bytes(asset.read_bytes()))
        self.assertEqual(verify_project_context(self.project_path, SCHEMAS, self.root / "generated", CATALOGUE_PATH)["status"], "pass")
        asset.write_text('{"ready":false}\n', encoding="utf-8")
        stale = verify_project_context(self.project_path, SCHEMAS, self.root / "generated", CATALOGUE_PATH)
        self.assertEqual((stale["status"], diagnostic_codes(stale)), ("fail", ["CONTEXT_STALE"]))

    def test_missing_declared_resource_asset_blocks_generation(self) -> None:
        self.create_project()
        meta = self.root / "resources" / "consumer" / "meta.xml"
        meta.write_text(
            '<meta><oop>true</oop><include resource="inventory"/><script src="server.lua" type="server"/>'
            '<script src="client.lua" type="client"/><file src="ui/missing.json"/></meta>',
            encoding="utf-8",
        )
        with self.assertRaises(ContextGenerationError) as raised:
            self.generate()
        self.assertEqual(diagnostic_codes(raised.exception.result), ["MISSING_FILE"])
        self.assertFalse((self.root / "generated").exists())

    def test_context_verify_detects_payload_tampering_before_regeneration(self) -> None:
        self.create_project()
        self.generate()
        payload = self.root / "generated" / "server" / "project-server.lua"
        payload.write_text(payload.read_text(encoding="utf-8") + "-- tampered\n", encoding="utf-8")
        result = verify_project_context(self.project_path, SCHEMAS, self.root / "generated", CATALOGUE_PATH)
        self.assertEqual(result["status"], "fail")
        self.assertEqual(diagnostic_codes(result), ["CONTEXT_ARTIFACT_HASH_MISMATCH"])

    def test_unindexed_pack_file_is_rejected_without_deletion(self) -> None:
        self.create_project()
        self.generate()
        unexpected = self.root / "generated" / "server" / "stale.lua"
        unexpected.write_text("return 'user-owned sentinel'\n", encoding="utf-8")
        result = verify_project_context(self.project_path, SCHEMAS, self.root / "generated", CATALOGUE_PATH)
        self.assertEqual((result["status"], diagnostic_codes(result)), ("fail", ["CONTEXT_UNINDEXED_PATH"]))
        with self.assertRaisesRegex(ValueError, "unowned path"):
            self.generate()
        self.assertEqual(unexpected.read_text(encoding="utf-8"), "return 'user-owned sentinel'\n")

    def test_context_verify_schema_validates_referenced_api_documents(self) -> None:
        self.create_project()
        self.generate()
        output = self.root / "generated"
        api_index_path = output / "api-index.json"
        api_index = load_json(api_index_path)
        api_index["unexpected"] = True
        api_index_path.write_text(canonical_json(api_index), encoding="utf-8")
        context_path = output / "agent-context.json"
        context = load_json(context_path)
        context["apiIndex"]["sha256"] = sha256_bytes(api_index_path.read_bytes())
        context_path.write_text(canonical_json(context), encoding="utf-8")
        artifacts_path = output / "artifacts.json"
        artifacts = load_json(artifacts_path)
        for artifact in artifacts["artifacts"]:
            payload = output / artifact["path"]
            artifact["size"] = payload.stat().st_size
            artifact["sha256"] = sha256_bytes(payload.read_bytes())
        artifacts_path.write_text(canonical_json(artifacts), encoding="utf-8")
        result = verify_project_context(self.project_path, SCHEMAS, output, CATALOGUE_PATH)
        self.assertEqual((result["status"], diagnostic_codes(result)), ("fail", ["CONTEXT_SCHEMA_INVALID"]))

    def test_malformed_control_document_shapes_return_contract_errors_not_internal_errors(self) -> None:
        self.create_project()
        self.generate()
        output = self.root / "generated"
        context_path = output / "agent-context.json"
        context = load_json(context_path)
        context["files"] = 7
        context_path.write_text(canonical_json(context), encoding="utf-8")
        command = [
            sys.executable, str(CLI_PATH), "context", "verify", "--project", str(self.project_path),
            "--catalogue", str(CATALOGUE_PATH), "--context", str(output), "--json",
        ]
        completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        result = json.loads(completed.stdout)
        self.assertEqual((completed.returncode, completed.stderr, result["status"]), (1, "", "fail"))
        self.assertEqual(diagnostic_codes(result), ["CONTEXT_SCHEMA_INVALID"])

        self.generate()
        artifacts_path = output / "artifacts.json"
        artifacts = load_json(artifacts_path)
        artifacts["schemaVersion"] = 7
        artifacts_path.write_text(canonical_json(artifacts), encoding="utf-8")
        completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        result = json.loads(completed.stdout)
        self.assertEqual((completed.returncode, completed.stderr, result["status"]), (1, "", "fail"))
        self.assertEqual(diagnostic_codes(result), ["CONTEXT_ARTIFACT_INDEX_INVALID"])

    @unittest.skipIf(os.name == "nt", "creating symlinks is not reliably available to unprivileged Windows tests")
    def test_context_verify_rejects_external_control_file_symlink(self) -> None:
        self.create_project()
        self.generate()
        index = self.root / "generated" / "artifacts.json"
        outside = self.root / "outside-artifacts.json"
        index.replace(outside)
        index.symlink_to(outside)
        result = verify_project_context(self.project_path, SCHEMAS, self.root / "generated", CATALOGUE_PATH)
        self.assertEqual((result["status"], diagnostic_codes(result)), ("fail", ["CONTEXT_PATH_OUTSIDE"]))

    @unittest.skipIf(os.name == "nt", "creating symlinks is not reliably available to unprivileged Windows tests")
    def test_context_verify_rejects_internal_payload_symlink(self) -> None:
        self.create_project()
        self.generate()
        payload = self.root / "generated" / "server" / "project-server.lua"
        unindexed = payload.with_name("project-server.real.lua")
        payload.replace(unindexed)
        payload.symlink_to(unindexed.name)
        result = verify_project_context(self.project_path, SCHEMAS, self.root / "generated", CATALOGUE_PATH)
        self.assertEqual((result["status"], diagnostic_codes(result)), ("fail", ["CONTEXT_PATH_OUTSIDE"]))

    @unittest.skipIf(os.name == "nt", "creating symlinks is not reliably available to unprivileged Windows tests")
    def test_cli_preserves_and_rejects_output_directory_symlink(self) -> None:
        self.create_project()
        target = self.root / "real-output"
        target.mkdir()
        link = self.root / "linked-output"
        link.symlink_to(target, target_is_directory=True)
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "generate", "project", "--project", str(self.project_path), "--catalogue", str(CATALOGUE_PATH), "--output", str(link), "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        result = json.loads(completed.stdout)
        self.assertEqual((completed.returncode, result["status"], diagnostic_codes(result)), (1, "fail", ["GENERATION_OUTPUT_UNSAFE"]))
        self.assertEqual(list(target.iterdir()), [])
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "generate", "project", "--project", str(self.project_path), "--catalogue", str(CATALOGUE_PATH), "--output", str(target), "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        self.assertEqual((completed.returncode, json.loads(completed.stdout)["status"]), (0, "pass"))
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "context", "verify", "--project", str(self.project_path), "--catalogue", str(CATALOGUE_PATH), "--context", str(link), "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        result = json.loads(completed.stdout)
        self.assertEqual((completed.returncode, result["status"], diagnostic_codes(result)), (1, "fail", ["CONTEXT_PATH_OUTSIDE"]))

        ancestor_target = self.root / "ancestor-target"
        ancestor_target.mkdir()
        marker = ancestor_target / "sentinel.txt"
        marker.write_text("unchanged\n", encoding="utf-8")
        ancestor_link = self.root / "ancestor-link"
        ancestor_link.symlink_to(ancestor_target, target_is_directory=True)
        nested_link = ancestor_link / "nested-pack"
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "generate", "project", "--project", str(self.project_path), "--catalogue", str(CATALOGUE_PATH), "--output", str(nested_link), "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        result = json.loads(completed.stdout)
        self.assertEqual((completed.returncode, result["status"], diagnostic_codes(result)), (1, "fail", ["GENERATION_OUTPUT_UNSAFE"]))
        self.assertEqual([item.name for item in ancestor_target.iterdir()], ["sentinel.txt"])
        nested_real = ancestor_target / "nested-pack"
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "generate", "project", "--project", str(self.project_path), "--catalogue", str(CATALOGUE_PATH), "--output", str(nested_real), "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        self.assertEqual((completed.returncode, json.loads(completed.stdout)["status"]), (0, "pass"))
        completed = subprocess.run(
            [sys.executable, str(CLI_PATH), "context", "verify", "--project", str(self.project_path), "--catalogue", str(CATALOGUE_PATH), "--context", str(nested_link), "--json"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        result = json.loads(completed.stdout)
        self.assertEqual((completed.returncode, result["status"], diagnostic_codes(result)), (1, "fail", ["CONTEXT_PATH_OUTSIDE"]))

    def test_context_verify_cli_is_byte_stable(self) -> None:
        self.create_project()
        self.generate()
        command = [sys.executable, str(CLI_PATH), "context", "verify", "--project", str(self.project_path), "--catalogue", str(CATALOGUE_PATH), "--context", str(self.root / "generated"), "--json"]
        first = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        second = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        self.assertEqual((first.returncode, first.stderr), (0, ""))
        self.assertEqual(first.stdout, second.stdout)
        self.assertEqual(json.loads(first.stdout)["status"], "pass")


if __name__ == "__main__":
    unittest.main()
