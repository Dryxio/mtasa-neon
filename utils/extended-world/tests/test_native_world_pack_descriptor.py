#!/usr/bin/env python3
"""Static contracts for trusted policy/runtime manifest separation."""

from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
GAME_SA = REPOSITORY / "Client/game_sa"
TOOLS = REPOSITORY / "utils/extended-world"
RUNTIME_MANIFEST = (
    REPOSITORY
    / "Shared/data/MTA San Andreas/MTA/data/extended-world/bullworth/native-world.json"
)
sys.path.insert(0, str(TOOLS))

from native_world_manifest import parse_runtime_manifest, validate_runtime_manifest  # noqa: E402


class NativeWorldPackDescriptorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manager = (GAME_SA / "CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        cls.policy = (GAME_SA / "CNativeBullworthPackSA.cpp").read_text(encoding="utf-8")
        cls.header = (GAME_SA / "CNativeWorldPackSA.h").read_text(encoding="utf-8")
        cls.cache = (GAME_SA / "CNativeWorldCacheSA.cpp").read_text(encoding="utf-8")
        cls.manifest = json.loads(RUNTIME_MANIFEST.read_text(encoding="ascii"))

    def test_manager_is_pack_neutral_beyond_policy_selection(self) -> None:
        self.assertNotIn("[NativeBW]", self.manager)
        self.assertNotIn("MTA_NATIVE_BW_MODEL_STORES", self.manager)
        self.assertIn("GetNativeBullworthPackPolicy()", self.manager)
        self.assertIn("FindNativeWorldPackPolicy(offer.format)", self.manager)

    def test_compiled_policy_owns_only_trusted_runtime_constraints(self) -> None:
        for value in (
            '"[NativeBW]"',
            '"MTA_NATIVE_BW_MODEL_STORES"',
            '"MTA\\\\data\\\\extended-world\\\\bullworth"',
            '"native-world.json"',
        ):
            self.assertIn(value, self.policy)
        for payload_value in ("bw.ide", "bw.img", "bw.col", "18631", "19582", "4007", "1126"):
            self.assertNotIn(payload_value, self.policy)
        for trusted_value in ("15000", "160", "200", "5000", "252", "255", "191", "256"):
            self.assertIn(trusted_value, self.policy)

    def test_checked_in_manifest_preserves_bullworth_payload_contract(self) -> None:
        self.assertIs(self.manifest, validate_runtime_manifest(self.manifest))
        self.assertEqual(
            {"name": "bw.ide", "bytes": 31760, "sha256": "0bdf5aeb17eaefe6e2f42e47d38f82d65526c580f3eecc223b7b65f8b905eeb4"},
            self.manifest["files"]["ide"],
        )
        self.assertEqual(169545728, self.manifest["files"]["img"]["bytes"])
        self.assertEqual({"format", "pack_id", "files"}, set(self.manifest))

    def test_format_two_separates_compiled_policy_from_pack_identity(self) -> None:
        generic = copy.deepcopy(self.manifest)
        generic["format"] = 2
        generic["policy"] = "static-world-v1"
        generic["pack_id"] = "another_city"
        self.assertIs(generic, validate_runtime_manifest(generic))
        for token in ('"static-world-v1"', '"closed-static-world-v1"', "STATIC_WORLD_V1_POLICY"):
            self.assertIn(token, self.policy)
        self.assertIn("manifest.policyKey = g_policy->key", self.manager)
        self.assertIn("IsSafePackId(packId->string)", self.manager)
        self.assertIn("CONTENT_ID_DOMAIN_V2", self.cache)
        self.assertIn('request.policyKey == "static-world-v1"', self.cache)
        self.assertIn('"format-2 static-world publication cannot acquire an activation lease"', self.cache)

    def test_malformed_manifests_are_deterministically_rejected(self) -> None:
        mutations = []
        extra = copy.deepcopy(self.manifest)
        extra["trusted_pool_capacity"] = 999999
        mutations.append(extra)
        traversal = copy.deepcopy(self.manifest)
        traversal["files"]["img"]["name"] = "../bw.img"
        mutations.append(traversal)
        for reserved in (".", ".."):
            reserved_name = copy.deepcopy(self.manifest)
            reserved_name["files"]["ide"]["name"] = reserved
            mutations.append(reserved_name)
        uppercase_hash = copy.deepcopy(self.manifest)
        uppercase_hash["files"]["ide"]["sha256"] = uppercase_hash["files"]["ide"]["sha256"].upper()
        mutations.append(uppercase_hash)
        bad_size = copy.deepcopy(self.manifest)
        bad_size["files"]["img"]["bytes"] -= 1
        mutations.append(bad_size)
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                with self.assertRaises(ValueError):
                    validate_runtime_manifest(mutation)

        valid_text = RUNTIME_MANIFEST.read_text(encoding="ascii")
        malformed_texts = (
            valid_text + " trailing",
            valid_text.replace('"format": 1,', '"format": 1, "format": 1,'),
            valid_text.replace('"bullworth"', '"bullwörth"'),
            valid_text.replace('"bw.ide"', '"../bw.ide"'),
            valid_text.replace('"bw.ide"', '"\\u0062w.ide"'),
        )
        for malformed in malformed_texts:
            with self.assertRaises((ValueError, UnicodeEncodeError, json.JSONDecodeError)):
                parse_runtime_manifest(malformed)

    def test_runtime_reparses_payload_and_derives_buffer_floor(self) -> None:
        for token in ("ParseIde(idePath", "ValidateImg(imgPath", "ValidateBinaryIpls(imgPath"):
            self.assertIn(token, self.manager)
        self.assertIn("(Pack().largestImgEntryBlocks + 1) & ~1U", self.manager)
        self.assertNotIn("requiredStreamingBufferBlocks", self.header)

    def test_precommit_native_collision_and_budget_contracts(self) -> None:
        preflight = self.manager.index("bool PreflightRuntime")
        archive_commit = self.manager.index("g_streaming->AddArchive")
        self.assertLess(preflight, archive_commit)
        for token in (
            "modelStoreCapacities.atomic",
            "modelStoreCapacities.damageAtomic",
            "modelStoreCapacities.time",
            "model->ulHashKey",
            "DFF native-key collision",
            "DFF native-key collides with occupied stock model",
        ):
            self.assertIn(token, self.manager)

    def test_constrained_img_and_ipl_contracts(self) -> None:
        self.assertIn("firstDot != dot", self.manager)
        self.assertIn("instance.position[0] < MIN_STATIC_WORLD_XY", self.manager)
        self.assertIn("instance.position[0] > MAX_STATIC_WORLD_XY", self.manager)
        self.assertIn("instance.lodIndex != -1", self.manager)
        legacy_start = self.manager.index("void CNativeWorldPackManagerSA::InstallFromEnvironment")
        state_guard = self.manager.index("if (g_state != EState::Off)", legacy_start)
        manifest_load = self.manager.index("LoadRuntimeManifest(manifestPath", legacy_start)
        self.assertLess(state_guard, manifest_load)


if __name__ == "__main__":
    unittest.main()
