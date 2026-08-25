from __future__ import annotations

import copy
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOL_DIRECTORY = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = TOOL_DIRECTORY.parents[1]
CATALOGUE_PATH = TOOL_DIRECTORY / "neon-api.json"
CLI_PATH = TOOL_DIRECTORY / "neon.py"
sys.path.insert(0, str(TOOL_DIRECTORY))

from neonlib.catalogue import (  # noqa: E402
    SourceSnapshot,
    build_catalogue,
    catalogue_divergence,
    catalogue_semantic_issues,
    catalogue_source_matches,
    extract_registrations,
)
from neonlib.jsonio import canonical_json, load_json, write_json  # noqa: E402
from neonlib.luals import generate_luals  # noqa: E402
from neonlib.project import check_project  # noqa: E402
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


class ContractSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.catalogue = load_json(CATALOGUE_PATH)

    def test_repository_catalogue_is_global_valid_and_semantically_stable(self) -> None:
        self.assertEqual(SCHEMAS.validate("neon-api", self.catalogue), [])
        self.assertEqual(catalogue_semantic_issues(self.catalogue), [])
        self.assertGreater(len(self.catalogue["symbols"]), 1500)
        origins = {symbol["origin"] for symbol in self.catalogue["symbols"]}
        self.assertEqual(origins, {"mta", "neon"})
        names = {symbol["name"] for symbol in self.catalogue["symbols"]}
        self.assertIn("createVehicle", names)
        self.assertIn("dxDrawText", names)
        self.assertIn("createRope", names)
        self.assertFalse(self.catalogue["sources"]["upstreamWiki"]["imported"])

    def test_valid_contract_samples(self) -> None:
        documents = {
            "neon-project": base_project(),
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

    def test_divergence_reports_both_directions(self) -> None:
        upstream = SourceSnapshot("a" * 40, {"Server/mods/deathmatch/logic/luadefs/Test.cpp": 'CLuaCFunctions::AddFunction("alpha", Alpha);'})
        neon = SourceSnapshot("b" * 40, {"Server/mods/deathmatch/logic/luadefs/Test.cpp": 'CLuaCFunctions::AddFunction("alpha", Alpha);'})
        catalogue = build_catalogue(neon, upstream, engine_version="1.7.0", wiki_revision="c" * 40)
        changed = SourceSnapshot("d" * 40, {"Server/mods/deathmatch/logic/luadefs/Test.cpp": 'CLuaCFunctions::AddFunction("bravo", Bravo);'})
        uncatalogued, missing = catalogue_divergence(catalogue, changed)
        self.assertEqual(uncatalogued, [("bravo", "server")])
        self.assertEqual(missing, [("alpha", "server")])

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
        self.assertEqual(symbol["state"], "opaque")
        self.assertEqual(symbol["sides"], [])
        self.assertEqual(symbol["inheritedSides"], ["server"])
        self.assertEqual(symbol["profiles"], ["mta-upstream"])
        self.assertEqual(catalogue_semantic_issues(catalogue), [])


class ProjectHarnessTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="neon-closed-harness-")
        self.root = Path(self.temporary.name)
        self.project_path = self.root / "neon.project.json"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_project(self, project: dict) -> None:
        write_json(self.project_path, project)

    def add_resource(self, project: dict, *, meta: str, files: dict[str, str] | None = None, name: str = "demo") -> None:
        resource = self.root / "resources" / name
        resource.mkdir(parents=True)
        (resource / "meta.xml").write_text(meta, encoding="utf-8")
        for relative, content in (files or {}).items():
            path = resource / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        project["resources"].append({"name": name, "path": f"resources/{name}"})

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

    def test_known_api_on_wrong_side_is_rejected(self) -> None:
        project = base_project()
        self.add_resource(project, meta='<meta><script src="server.lua" type="server"/></meta>', files={"server.lua": "dxDrawText('bad', 0, 0)\n"})
        result = self.check(project)
        self.assertIn("API_WRONG_SIDE", diagnostic_codes(result))
        diagnostic = next(item for item in result["diagnostics"] if item["code"] == "API_WRONG_SIDE")
        self.assertEqual((diagnostic["side"], diagnostic["symbol"], diagnostic["line"]), ("server", "dxDrawText", 1))

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

    def test_lua_comments_and_strings_do_not_create_calls(self) -> None:
        project = base_project()
        project["unknownApis"] = "error"
        source = "-- fakeCall()\nlocal text = 'alsoFake()'\n--[[ hiddenCall() ]]\nprint(text)\n"
        self.add_resource(project, meta='<meta><script src="server.lua" type="server"/></meta>', files={"server.lua": source})
        self.assertEqual(self.check(project)["status"], "pass")


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
            self.assertIn("---@param ... unknown", shared)


if __name__ == "__main__":
    unittest.main()
