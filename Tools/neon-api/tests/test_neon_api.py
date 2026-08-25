from __future__ import annotations

import copy
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
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
from neonlib.jsonio import canonical_json, load_json, sha256_bytes, write_json  # noqa: E402
from neonlib.luals import generate_luals, render_luals  # noqa: E402
from neonlib.components import manifest_semantic_issues  # noqa: E402
from neonlib.context import ContextGenerationError, build_api_index, generate_project_context, verify_project_context  # noqa: E402
from neonlib.discovery import discovery_keywords, search_symbols, tokenize  # noqa: E402
from neonlib.project import check_project, resolve_project_components  # noqa: E402
from neonlib.schema import SchemaStore  # noqa: E402


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
