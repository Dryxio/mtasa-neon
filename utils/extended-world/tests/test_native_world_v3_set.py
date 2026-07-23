#!/usr/bin/env python3

from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

from native_world_v3_set import (  # noqa: E402
    CANONICAL_PACK_ORDER,
    MAX_ENVELOPE_BYTES,
    POLICY,
    build_set_envelope,
    calculate_set_id,
    canonical_set_envelope_bytes,
    parse_set_envelope,
)


class NativeWorldV3SetTest(unittest.TestCase):
    def setUp(self) -> None:
        self.packs = [
            {"pack_id": pack_id, "content_id": hashlib.sha256(pack_id.encode("ascii")).hexdigest()}
            for pack_id in CANONICAL_PACK_ORDER
        ]
        self.envelope = {
            "format": 3,
            "policy": POLICY,
            "set_id": calculate_set_id(self.packs),
            "packs": self.packs,
        }

    def test_golden_set_id_binds_order_and_every_identity(self) -> None:
        self.assertEqual(
            self.envelope["set_id"],
            "493cb1a86f9af63214f3ddba28d482814067f2967c7315cb6da728d005b6fcae",
        )
        for index in range(4):
            changed = copy.deepcopy(self.packs)
            changed[index]["content_id"] = "0" * 64
            self.assertNotEqual(self.envelope["set_id"], calculate_set_id(changed))
        with self.assertRaises(ValueError):
            calculate_set_id(list(reversed(self.packs)))

    def test_parser_is_closed_and_canonical(self) -> None:
        encoded = canonical_set_envelope_bytes(self.envelope)
        self.assertEqual(parse_set_envelope(encoded.decode("ascii")), self.envelope)
        self.assertLess(len(encoded), MAX_ENVELOPE_BYTES)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            parse_set_envelope(encoded.decode("ascii").replace('"format": 3,', '"format": 3,\n  "format": 3,'))
        with self.assertRaises(ValueError):
            parse_set_envelope(encoded.decode("ascii") + "x")
        with self.assertRaises(ValueError):
            parse_set_envelope(json.dumps({**self.envelope, "extra": 1}))
        reordered = {
            "packs": [{"content_id": pack["content_id"], "pack_id": pack["pack_id"]} for pack in self.packs],
            "set_id": self.envelope["set_id"],
            "policy": POLICY,
            "format": 3,
        }
        self.assertEqual(encoded, canonical_set_envelope_bytes(reordered))

    def test_set_id_mismatch_and_pack_count_fail_closed(self) -> None:
        for changed in (
            {**self.envelope, "set_id": "0" * 64},
            {**self.envelope, "packs": self.packs[:-1]},
            {**self.envelope, "policy": "static-world-v3"},
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                parse_set_envelope(json.dumps(changed))

    def test_builder_derives_pack_content_ids_in_canonical_order(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directories = []
            for index, pack_id in enumerate(CANONICAL_PACK_ORDER):
                directory = root / pack_id
                directory.mkdir()
                ide = b"objs\nend\n"
                lod = b"NWL3LOD\0" + b"\x01\0\0\0" + b"\0" * 20
                image = bytes((65 + index,)) * 2048
                (directory / "world.ide").write_bytes(ide)
                (directory / "world.lod").write_bytes(lod)
                (directory / "w000.img").write_bytes(image)
                manifest = {
                    "format": 3,
                    "policy": "static-world-v3",
                    "pack_id": pack_id,
                    "files": {
                        "ide": {"name": "world.ide", "bytes": len(ide), "sha256": hashlib.sha256(ide).hexdigest()},
                        "lod": {"name": "world.lod", "bytes": len(lod), "sha256": hashlib.sha256(lod).hexdigest()},
                        "images": [
                            {
                                "name": "w000.img",
                                "bytes": len(image),
                                "sha256": hashlib.sha256(image).hexdigest(),
                            }
                        ],
                    },
                }
                (directory / "native-world.json").write_text(json.dumps(manifest), encoding="ascii")
                directories.append(directory)
            envelope = build_set_envelope(directories)
            self.assertEqual([pack["pack_id"] for pack in envelope["packs"]], list(CANONICAL_PACK_ORDER))
            self.assertEqual(envelope["set_id"], calculate_set_id(envelope["packs"]))


if __name__ == "__main__":
    unittest.main()
