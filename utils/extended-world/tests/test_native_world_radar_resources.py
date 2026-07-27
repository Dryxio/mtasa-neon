from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
MODULE_PATH = REPOSITORY / "utils/extended-world/build_native_world_radar_resources.py"
SPEC = importlib.util.spec_from_file_location("build_native_world_radar_resources", MODULE_PATH)
assert SPEC and SPEC.loader
radar = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = radar
SPEC.loader.exec_module(radar)


class NativeWorldRadarResourceTests(unittest.TestCase):
    def test_catalogs_are_complete_and_non_overlapping(self) -> None:
        expected_counts = {
            "bullworth": 15,
            "vice-city": 80,
            "liberty-city": 81,
            "carcer-city": 63,
        }
        occupied: dict[tuple[int, int], str] = {}

        for spec in radar.SPECS:
            source = REPOSITORY / "test-resources" / spec.source_resource / "radar_tiles.lua"
            tiles = radar.parse_catalog(source, spec.source_variable)
            self.assertEqual(expected_counts[spec.pack_id], len(tiles))
            radar.validate_catalog(spec, tiles, occupied)

        self.assertEqual(239, len(occupied))

    def test_generated_metadata_matches_reviewed_catalogs(self) -> None:
        template = radar.TEMPLATE.read_text(encoding="utf-8")
        for spec in radar.SPECS:
            source = REPOSITORY / "test-resources" / spec.source_resource / "radar_tiles.lua"
            tiles = radar.parse_catalog(source, spec.source_variable)
            target = REPOSITORY / "test-resources" / spec.target_resource
            self.assertEqual(radar.catalog_text(spec, tiles), (target / "catalog.lua").read_text(encoding="utf-8"))
            self.assertEqual(radar.meta_text(spec, tiles), (target / "meta.xml").read_text(encoding="utf-8"))
            self.assertEqual(template, (target / "client.lua").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
