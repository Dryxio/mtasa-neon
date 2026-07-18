#!/usr/bin/env python3
"""Deterministic cache identity, publication, and recovery tests."""

from __future__ import annotations

import hashlib
import json
import copy
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
GAME_SA = REPOSITORY / "Client/game_sa"
TOOLS = REPOSITORY / "utils/extended-world"
sys.path.insert(0, str(TOOLS))

from native_world_cache import (  # noqa: E402
    canonical_manifest_bytes,
    content_id,
    open_existing_cache,
    publish_local_seed,
    validate_cache_object,
)
from native_world_manifest import STATIC_WORLD_V1_POLICY  # noqa: E402


class NativeWorldCacheTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.seed = self.root / "seed"
        self.cache = self.root / "cache"
        self.seed.mkdir()
        (self.seed / "test.ide").write_bytes(b"objs\nend\n")
        (self.seed / "test.img").write_bytes(b"I" * 2048)
        self.manifest = {
            "format": 1,
            "pack_id": "bullworth",
            "files": {
                "ide": {
                    "name": "test.ide",
                    "bytes": 9,
                    "sha256": hashlib.sha256(b"objs\nend\n").hexdigest(),
                },
                "img": {
                    "name": "test.img",
                    "bytes": 2048,
                    "sha256": hashlib.sha256(b"I" * 2048).hexdigest(),
                },
            },
        }
        (self.seed / "native-world.json").write_text(json.dumps(self.manifest), encoding="ascii")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_content_id_is_stable_across_json_layout(self) -> None:
        compact = json.dumps(self.manifest, separators=(",", ":"))
        reordered = json.dumps(
            {"files": self.manifest["files"], "pack_id": "bullworth", "format": 1},
            indent=4,
        )
        self.assertNotEqual(hashlib.sha256(compact.encode()).digest(), hashlib.sha256(reordered.encode()).digest())
        self.assertEqual(content_id(json.loads(compact), "bullworth"), content_id(json.loads(reordered), "bullworth"))
        self.assertEqual(canonical_manifest_bytes(json.loads(compact), "bullworth"), canonical_manifest_bytes(json.loads(reordered), "bullworth"))

        renamed = copy.deepcopy(self.manifest)
        renamed["files"]["ide"]["name"] = "another.ide"
        renamed["files"]["img"]["name"] = "another.img"
        self.assertEqual(content_id(self.manifest, "bullworth"), content_id(renamed, "bullworth"))
        self.assertEqual(canonical_manifest_bytes(self.manifest, "bullworth"), canonical_manifest_bytes(renamed, "bullworth"))

    def test_bullworth_content_id_golden_vector(self) -> None:
        manifest_path = REPOSITORY / "Shared/data/MTA San Andreas/MTA/data/extended-world/bullworth/native-world.json"
        manifest = json.loads(manifest_path.read_text(encoding="ascii"))
        self.assertEqual("6a090231416e0298eb78e671eba91d4c58ed1f9c16dfae94d162a81a52464824", content_id(manifest, "bullworth"))

    def test_format_2_identity_and_cache_path_include_policy_not_pack_parent(self) -> None:
        manifest = copy.deepcopy(self.manifest)
        manifest.update(format=2, policy=STATIC_WORLD_V1_POLICY, pack_id="test-city")
        (self.seed / "native-world.json").write_text(json.dumps(manifest), encoding="ascii")

        identity = content_id(manifest, STATIC_WORLD_V1_POLICY)
        self.assertEqual("0a68d3f704fb6736f76070351ce86a3d20a717063ad1041d632ab1a142f47bee", identity)
        renamed = copy.deepcopy(manifest)
        renamed["files"]["ide"]["name"] = "renamed.ide"
        renamed["files"]["img"]["name"] = "renamed.img"
        self.assertEqual(identity, content_id(renamed, STATIC_WORLD_V1_POLICY))

        another_pack = copy.deepcopy(manifest)
        another_pack["pack_id"] = "other-city"
        self.assertNotEqual(identity, content_id(another_pack, STATIC_WORLD_V1_POLICY))
        resized = copy.deepcopy(manifest)
        resized["files"]["ide"]["bytes"] += 1
        self.assertNotEqual(identity, content_id(resized, STATIC_WORLD_V1_POLICY))
        with self.assertRaisesRegex(ValueError, "manifest policy differs"):
            content_id(manifest, "bullworth")

        final, disposition = publish_local_seed(self.seed, self.cache, STATIC_WORLD_V1_POLICY)
        self.assertEqual("published", disposition)
        self.assertEqual(self.cache / "v2" / STATIC_WORLD_V1_POLICY / identity, final)
        self.assertNotIn(manifest["pack_id"], final.parts[:-1])
        self.assertEqual(final, open_existing_cache(self.cache, manifest, STATIC_WORLD_V1_POLICY, identity))
        validate_cache_object(final, manifest, STATIC_WORLD_V1_POLICY)

        canonical = json.loads(canonical_manifest_bytes(manifest, STATIC_WORLD_V1_POLICY))
        self.assertEqual((2, STATIC_WORLD_V1_POLICY, "test-city"), (canonical["format"], canonical["policy"], canonical["pack_id"]))

    def test_corrupt_existing_object_is_quarantined_and_rebuilt(self) -> None:
        final, disposition = publish_local_seed(self.seed, self.cache, "bullworth")
        self.assertEqual("published", disposition)
        (final / "world.img").write_bytes(b"corrupt")
        repaired, disposition = publish_local_seed(self.seed, self.cache, "bullworth")
        self.assertEqual(final, repaired)
        self.assertEqual("published", disposition)
        validate_cache_object(repaired, self.manifest, "bullworth")

    def test_cache_hit_needs_only_the_seed_manifest_selector(self) -> None:
        final, _ = publish_local_seed(self.seed, self.cache, "bullworth")
        (self.seed / "test.ide").unlink()
        (self.seed / "test.img").unlink()
        selected, disposition = publish_local_seed(self.seed, self.cache, "bullworth")
        self.assertEqual(final, selected)
        self.assertEqual("hit", disposition)
        validate_cache_object(final, self.manifest, "bullworth")

    def test_existing_lookup_never_repairs_or_quarantines(self) -> None:
        identity = content_id(self.manifest, "bullworth")
        with self.assertRaises(ValueError):
            open_existing_cache(self.cache, self.manifest, "bullworth", identity)
        self.assertFalse(self.cache.exists())

        final, _ = publish_local_seed(self.seed, self.cache, "bullworth")
        (final / "world.img").write_bytes(b"corrupt")
        before = sorted(path.name for path in final.parent.iterdir())
        with self.assertRaises(ValueError):
            open_existing_cache(self.cache, self.manifest, "bullworth", identity)
        self.assertEqual(before, sorted(path.name for path in final.parent.iterdir()))
        self.assertEqual(b"corrupt", (final / "world.img").read_bytes())

    def test_existing_lookup_rejects_extra_siblings(self) -> None:
        final, _ = publish_local_seed(self.seed, self.cache, "bullworth")
        (final / "extra.bin").write_bytes(b"x")
        with self.assertRaises(ValueError):
            open_existing_cache(self.cache, self.manifest, "bullworth", content_id(self.manifest, "bullworth"))

    def test_cpp_contract_uses_pending_guards_and_programdata(self) -> None:
        source = (GAME_SA / "CNativeWorldCacheSA.cpp").read_text(encoding="utf-8")
        self.assertIn("SharedUtil::GetMTADataPath()", source)
        self.assertIn("g_pendingLocks", source)
        self.assertIn("CommitNativeWorldCacheLease", source)
        self.assertIn("ReleaseNativeWorldCacheLease", source)
        self.assertIn("GetFinalPathNameByHandleW", source)
        self.assertIn("FILE_FLAG_OPEN_REPARSE_POINT", source)
        validation = source.index("LockAndValidatePublishedFiles(request, quarantine")
        close = source.index("publishedFileLocks.Close()", validation)
        rename = source.index("MoveFileExW(", close)
        final_validation = source.index("LockAndValidatePublishedFiles(request, paths, publishedLocks", rename)
        self.assertLess(validation, close)
        self.assertLess(close, rename)
        self.assertLess(rename, final_validation)


if __name__ == "__main__":
    unittest.main()
