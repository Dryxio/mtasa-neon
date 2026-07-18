#!/usr/bin/env python3
"""Closed-schema tests for native-world runtime manifests."""

from __future__ import annotations

import copy
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
TOOLS = REPOSITORY / "utils/extended-world"
sys.path.insert(0, str(TOOLS))

from native_world_manifest import (  # noqa: E402
    STATIC_WORLD_V1_POLICY,
    build_runtime_manifest,
    parse_runtime_manifest,
    validate_runtime_manifest,
)


def manifest(format_version: int = 2) -> dict[str, object]:
    value: dict[str, object] = {
        "format": format_version,
        "pack_id": "test-city" if format_version == 2 else "bullworth",
        "files": {
            "ide": {"name": "test.ide", "bytes": 9, "sha256": hashlib.sha256(b"objs\nend\n").hexdigest()},
            "img": {"name": "test.img", "bytes": 2048, "sha256": hashlib.sha256(b"I" * 2048).hexdigest()},
        },
    }
    if format_version == 2:
        value["policy"] = STATIC_WORLD_V1_POLICY
    return value


class NativeWorldManifestTest(unittest.TestCase):
    def test_format_1_schema_remains_accepted_without_policy(self) -> None:
        value = manifest(1)
        self.assertEqual(value, validate_runtime_manifest(value))
        with self.assertRaisesRegex(ValueError, "root must contain exactly"):
            validate_runtime_manifest({**value, "policy": STATIC_WORLD_V1_POLICY})

        with self.assertRaisesRegex(ValueError, "format 1 pack_id must be bullworth"):
            validate_runtime_manifest({**value, "pack_id": "test-city"})

    def test_format_2_schema_and_policy_are_closed(self) -> None:
        value = manifest()
        self.assertEqual(value, validate_runtime_manifest(value))
        self.assertEqual(value, parse_runtime_manifest(json.dumps(value, separators=(",", ":"))))

        for mutation in (
            lambda item: item.update(policy="bullworth"),
            lambda item: item.pop("policy"),
            lambda item: item.update(extra=True),
        ):
            invalid = copy.deepcopy(value)
            mutation(invalid)
            with self.assertRaises(ValueError):
                validate_runtime_manifest(invalid)

    def test_format_2_pack_id_is_a_bounded_slug(self) -> None:
        for pack_id in ("", "Uppercase", "has.dot", "sixteen-chars-id"):
            invalid = manifest()
            invalid["pack_id"] = pack_id
            with self.assertRaisesRegex(ValueError, "pack_id is invalid"):
                validate_runtime_manifest(invalid)

    def test_builder_emits_both_closed_formats(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            ide = root / "test.ide"
            img = root / "test.img"
            ide.write_bytes(b"objs\nend\n")
            img.write_bytes(b"I" * 2048)

            legacy = build_runtime_manifest({}, ide, img)
            self.assertEqual({"format": 1, "pack_id": "bullworth"}, {key: legacy[key] for key in ("format", "pack_id")})
            current = build_runtime_manifest(
                {}, ide, img, format_version=2, policy=STATIC_WORLD_V1_POLICY, pack_id="test-city"
            )
            self.assertEqual((2, STATIC_WORLD_V1_POLICY, "test-city"), (current["format"], current["policy"], current["pack_id"]))


if __name__ == "__main__":
    unittest.main()
