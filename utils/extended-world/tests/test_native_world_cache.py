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
    publish_local_seed,
    validate_cache_object,
)


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
