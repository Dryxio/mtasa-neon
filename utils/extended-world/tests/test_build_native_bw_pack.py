#!/usr/bin/env python3
"""Round-trip tests for the generated Bullworth native streaming pack."""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1]
REPOSITORY = TOOLS.parents[1]
sys.path.insert(0, str(TOOLS))

from build_native_bw_pack import (  # noqa: E402
    EXPECTED_IPLS,
    EXPECTED_MODELS,
    EXPECTED_PLACEMENTS,
    MODEL_ID_END,
    MODEL_ID_START,
    NATIVE_COL_BUFFER_CAPACITY,
    build_pack,
    parse_binary_ipl_data,
    parse_generated_map,
    read_img_directory,
    verify_pack,
)


class NativeBullworthPackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.resource = REPOSITORY / "test-resources/ug-bw"
        candidate = Path(os.environ.get("GTA_SA_ROOT", "/Users/salimtrouve/Documents/GTA-SanAndreas"))
        required = (
            cls.resource / "map_data.lua",
            cls.resource / "assets/bw_models.img",
            cls.resource / "assets/bw_textures.img",
            candidate / "models/gta3.img",
        )
        if not all(path.is_file() for path in required):
            raise unittest.SkipTest("local ignored ug-bw assets or stock GTA root are unavailable")
        cls.gta_root = candidate

    def test_build_and_parse_every_native_artifact(self) -> None:
        models, placements = parse_generated_map(self.resource / "map_data.lua")
        self.assertEqual(EXPECTED_MODELS, len(models))
        self.assertEqual(EXPECTED_PLACEMENTS, len(placements))

        with tempfile.TemporaryDirectory(prefix="bw-native-pack-") as temporary:
            output = Path(temporary) / "pack"
            report = build_pack(self.resource, output, self.gta_root)
            reparsed = verify_pack(output, models, placements)

            self.assertEqual("ok", report["status"])
            self.assertEqual(report["counts"], reparsed["counts"])
            self.assertEqual([MODEL_ID_START, MODEL_ID_END], report["counts"]["model_id_range"])
            self.assertEqual(EXPECTED_IPLS, report["counts"]["ipls"])
            self.assertEqual(0, report["counts"]["non_negative_lods"])
            self.assertEqual([], report["missing_assets"])
            self.assertEqual(198, report["budgets"]["pools"]["ipl_slots"]["projected_used"])
            self.assertEqual(14854, report["budgets"]["model_stores"]["object"]["exact_required"])
            self.assertEqual(256716, report["counts"]["max_col_record_bytes"])
            self.assertEqual(NATIVE_COL_BUFFER_CAPACITY, report["collision_io"]["buffer_capacity"])
            self.assertGreater(
                report["collision_io"]["buffer_capacity"], report["counts"]["max_col_record_bytes"]
            )
            self.assertTrue(report["budgets"]["pools"]["txd_slots"]["runtime_verification_required"])
            txd_budget = report["budgets"]["pools"]["txd_slots"]
            self.assertEqual(3607, txd_budget["standalone_archive_inventory"])
            self.assertEqual(3608, txd_budget["mta_runtime_audited_occupied"])
            self.assertEqual(3774, txd_budget["projected_used"])

            manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(3608, manifest["txd_slot_plan"]["base"])
            self.assertEqual([3608, 3773], [min(manifest["txd_slot_plan"]["slots"].values()), max(manifest["txd_slot_plan"]["slots"].values())])

            entries = read_img_directory(output / "bw.img")
            self.assertEqual(report["counts"]["archive_entries"], len(entries))
            for ipl in report["ipls"]:
                parsed = parse_binary_ipl_data((output / "ipls" / ipl["name"]).read_bytes())
                self.assertEqual(ipl["placements"], len(parsed))
                self.assertTrue(all(instance.lod_index == -1 for instance in parsed))


if __name__ == "__main__":
    unittest.main()
