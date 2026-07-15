#!/usr/bin/env python3
"""Static contract tests for the native static-world pack descriptor split."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
GAME_SA = REPOSITORY / "Client/game_sa"


class NativeWorldPackDescriptorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manager = (GAME_SA / "CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        cls.descriptor = (GAME_SA / "CNativeBullworthPackSA.cpp").read_text(encoding="utf-8")
        cls.header = (GAME_SA / "CNativeWorldPackSA.h").read_text(encoding="utf-8")

    def test_manager_is_pack_neutral_beyond_descriptor_selection(self) -> None:
        self.assertNotIn("[NativeBW]", self.manager)
        self.assertNotRegex(self.manager, r"bw\.(?:ide|img|col)")
        self.assertNotIn("MTA_NATIVE_BW_MODEL_STORES", self.manager)
        self.assertIn("GetNativeBullworthPackDescriptor()", self.manager)

    def test_bullworth_descriptor_preserves_reviewed_contract(self) -> None:
        for value in (
            '"[NativeBW]"',
            '"MTA_NATIVE_BW_MODEL_STORES"',
            '"MTA\\\\data\\\\extended-world\\\\bullworth"',
            '"bw.ide"',
            '"bw.img"',
            '"bw.col"',
            '"0bdf5aeb17eaefe6e2f42e47d38f82d65526c580f3eecc223b7b65f8b905eeb4"',
            '"bc7f3ad5ce47bbd8a9018c9743142582cd458875d2100f31c0d96aac7f4bbfc0"',
        ):
            self.assertIn(value, self.descriptor)

        numbers = [int(value) for value in re.findall(r"^\s+(\d+),$", self.descriptor, re.MULTILINE)]
        for expected in (18631, 19582, 952, 166, 5000, 252, 255, 191, 256, 1126, 82786, 4007, 6):
            self.assertIn(expected, numbers)
        self.assertNotIn(4008, numbers)

        expected_ipls = (
            "bw_tbusines",
            "bw_tcarni",
            "bw_tglobal",
            "bw_tindust",
            "bw_tjyard",
            "bw_trich",
            "bw_tschool",
        )
        positions = [self.descriptor.index(f'"{name}"') for name in expected_ipls]
        self.assertEqual(sorted(positions), positions)

    def test_buffer_floor_is_derived_from_validated_largest_entry(self) -> None:
        self.assertIn("largestImgEntryBlocks", self.header)
        self.assertNotIn("requiredStreamingBufferBlocks", self.header)
        self.assertIn("(Pack().largestImgEntryBlocks + 1) & ~1U", self.manager)
        self.assertIn("maxEntrySize != Pack().largestImgEntryBlocks", self.manager)


if __name__ == "__main__":
    unittest.main()
