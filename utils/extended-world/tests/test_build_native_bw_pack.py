#!/usr/bin/env python3
"""Round-trip tests for the generated Bullworth native streaming pack."""

from __future__ import annotations

import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1]
REPOSITORY = TOOLS.parents[1]
sys.path.insert(0, str(TOOLS))

from build_native_bw_pack import (  # noqa: E402
    EXPECTED_IPLS,
    EXPECTED_DFF_UV_NORMALIZATIONS,
    EXPECTED_MODELS,
    EXPECTED_PLACEMENTS,
    EXPECTED_TXD_DUPLICATE_REMOVALS,
    MODEL_ID_END,
    MODEL_ID_START,
    NATIVE_COL_BUFFER_CAPACITY,
    RW_LIBRARY_ID,
    build_pack,
    normalize_static_dff_semantic_floats,
    normalize_static_txd_duplicate_names,
    parse_binary_ipl_data,
    parse_col_records,
    parse_generated_map,
    read_img_directory,
    remap_col_record,
    verify_pack,
)
from build_ug_map import parse_col_archive, parse_col_file_header  # noqa: E402


def col_record(name: str = "source", model_id: int = 1234) -> bytes:
    record = bytearray(32)
    struct.pack_into("<4sI", record, 0, b"COLL", 24)
    record[8:30] = name.encode("ascii").ljust(22, b"\0")
    struct.pack_into("<H", record, 30, model_id)
    return bytes(record)


def rw_chunk(chunk_type: int, payload: bytes = b"") -> bytes:
    return struct.pack("<III", chunk_type, len(payload), RW_LIBRARY_ID) + payload


def dff_with_semantic_float(
    uv_value: float, position_value: float = 1.0, light_angle: float | None = None
) -> bytes:
    frame = struct.pack("<I12fII", 1, *([1.0] * 12), 0xFFFFFFFF, 0)
    frame_list = rw_chunk(0x0E, rw_chunk(0x01, frame) + rw_chunk(0x03, rw_chunk(0x0253F2FE, b"root")))

    material_struct = bytearray(28)
    struct.pack_into("<3f", material_struct, 16, 1.0, 1.0, 1.0)
    material = rw_chunk(0x07, rw_chunk(0x01, material_struct) + rw_chunk(0x03))
    material_list = rw_chunk(0x08, rw_chunk(0x01, struct.pack("<II", 1, 0xFFFFFFFF)) + material)

    geometry_struct = bytearray(struct.pack("<4I", 0x0001002E, 1, 3, 1))
    geometry_struct.extend(b"\xFF" * 12)
    geometry_struct.extend(struct.pack("<6f", uv_value, *([0.5] * 5)))
    geometry_struct.extend(struct.pack("<4H", 0, 1, 0, 2))
    geometry_struct.extend(struct.pack("<4fII", 0.0, 0.0, 0.0, 1.0, 1, 0))
    geometry_struct.extend(struct.pack("<9f", position_value, *([1.0] * 8)))
    geometry = rw_chunk(0x0F, rw_chunk(0x01, geometry_struct) + material_list + rw_chunk(0x03))
    geometry_list = rw_chunk(0x1A, rw_chunk(0x01, struct.pack("<I", 1)) + geometry)
    light_payload = b""
    if light_angle is not None:
        light_payload = rw_chunk(0x01, struct.pack("<I", 0))
        light_struct = struct.pack("<5fI", 1.0, 1.0, 1.0, 1.0, light_angle, 0x00800003)
        light_payload += rw_chunk(0x12, rw_chunk(0x01, light_struct) + rw_chunk(0x03))
    root_struct = rw_chunk(0x01, struct.pack("<III", 1, 1 if light_angle is not None else 0, 0))
    return rw_chunk(0x10, root_struct + frame_list + geometry_list + light_payload + rw_chunk(0x03))


def native_texture(name: str, marker: int = 0x31545844) -> bytes:
    structure = bytearray(92)
    structure[8:40] = name.encode("ascii").ljust(32, b"\0")
    struct.pack_into("<I", structure, 76, marker)
    return rw_chunk(0x15, rw_chunk(0x01, structure) + rw_chunk(0x03))


def txd_with_duplicate(second_marker: int = 0x31545844) -> bytes:
    first = native_texture("Duplicate_Name")
    second = native_texture("duplicate_name", second_marker)
    return rw_chunk(0x16, rw_chunk(0x01, struct.pack("<HH", 2, 0)) + first + second + rw_chunk(0x03))


class NativeColFileHeaderTest(unittest.TestCase):
    def test_standard_22_byte_name_and_id_at_30_round_trip(self) -> None:
        source = col_record()
        self.assertEqual(("source", 1234), parse_col_file_header(source, "fixture"))
        remapped = remap_col_record(source, MODEL_ID_START, "101")
        self.assertEqual(("101", MODEL_ID_START, 0, 32), parse_col_records(remapped)[0])
        self.assertEqual(b"\0" * 19, remapped[11:30])

    def test_old_20_byte_name_layout_is_rejected_as_nonzero_tail(self) -> None:
        old_layout = bytearray(col_record())
        struct.pack_into("<H", old_layout, 28, MODEL_ID_START)
        with self.assertRaisesRegex(ValueError, "nonzero COL model-name tail"):
            parse_col_file_header(bytes(old_layout), "old-layout fixture")

    def test_bully_source_marker_is_isolated_from_the_native_parser(self) -> None:
        source = bytearray(col_record())
        source[28:32] = b"CED2"
        with tempfile.TemporaryDirectory(prefix="bw-source-col-") as temporary:
            path = Path(temporary) / "source.col"
            path.write_bytes(source)
            with self.assertRaisesRegex(ValueError, "nonzero COL model-name tail"):
                parse_col_archive(path)
            by_name, by_id = parse_col_archive(path, allow_bully_source_header=True)
        self.assertEqual({"source"}, set(by_name))
        self.assertEqual({}, by_id)


class NativeDffFloatNormalizationTest(unittest.TestCase):
    def test_only_nonfinite_geometry_uv_is_canonicalized(self) -> None:
        source = dff_with_semantic_float(float("nan"))
        with self.assertRaisesRegex(ValueError, "geometry 0 UV: non-finite"):
            normalize_static_dff_semantic_floats(source, "fixture.dff", normalize_uv=False)
        normalized, audit = normalize_static_dff_semantic_floats(source, "fixture.dff", normalize_uv=True)
        self.assertEqual(1, audit["normalized_uv_count"])
        offset = audit["normalized_uv_byte_offsets"][0]
        self.assertEqual(b"\0\0\0\0", normalized[offset : offset + 4])
        _, clean_audit = normalize_static_dff_semantic_floats(normalized, "fixture.dff", normalize_uv=False)
        self.assertEqual(0, clean_audit["normalized_uv_count"])

    def test_nonfinite_position_is_rejected_not_normalized(self) -> None:
        source = dff_with_semantic_float(0.5, float("inf"))
        with self.assertRaisesRegex(ValueError, "positions: non-finite"):
            normalize_static_dff_semantic_floats(source, "fixture.dff", normalize_uv=True)

    def test_nonfinite_light_angle_is_rejected_not_normalized(self) -> None:
        source = dff_with_semantic_float(0.5, light_angle=float("nan"))
        with self.assertRaisesRegex(ValueError, "light angle: non-finite"):
            normalize_static_dff_semantic_floats(source, "fixture.dff", normalize_uv=True)


class NativeTxdDuplicateNormalizationTest(unittest.TestCase):
    def test_later_casefold_identical_texture_is_removed(self) -> None:
        source = txd_with_duplicate()
        with self.assertRaisesRegex(ValueError, "duplicate case-insensitive texture"):
            normalize_static_txd_duplicate_names(source, "fixture.txd", drop_identical_duplicates=False)
        normalized, audit = normalize_static_txd_duplicate_names(
            source, "fixture.txd", drop_identical_duplicates=True
        )
        self.assertEqual(1, audit["dropped_duplicate_count"])
        self.assertEqual(0, audit["records"][0]["kept_index"])
        self.assertEqual(1, audit["records"][0]["dropped_index"])
        self.assertEqual(len(source), len(normalized))
        root_payload = struct.unpack_from("<I", normalized, 4)[0]
        self.assertTrue(all(value == 0 for value in normalized[12 + root_payload :]))
        self.assertEqual(1, struct.unpack_from("<H", normalized, 24)[0])
        normalize_static_txd_duplicate_names(normalized, "fixture.txd", drop_identical_duplicates=False)

    def test_semantically_different_duplicate_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "non-identical case-insensitive duplicate"):
            normalize_static_txd_duplicate_names(
                txd_with_duplicate(0x35545844), "fixture.txd", drop_identical_duplicates=True
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
            self.assertEqual(199, report["normalization"]["geometry_uv_nonfinite_replaced"])
            self.assertEqual("0x00000000", report["normalization"]["geometry_uv_replacement_bits"])
            self.assertEqual(4_840_324, report["normalization"]["verified_dff_semantic_floats"])
            self.assertEqual(0, report["normalization"]["verified_txd_semantic_floats"])
            self.assertEqual(EXPECTED_DFF_UV_NORMALIZATIONS, report["normalization"]["geometry_uv_files"])
            self.assertEqual(1, report["normalization"]["txd_casefold_duplicates_removed"])
            self.assertEqual(EXPECTED_TXD_DUPLICATE_REMOVALS, report["normalization"]["txd_duplicate_files"])
            self.assertTrue(report["budgets"]["pools"]["txd_slots"]["runtime_verification_required"])
            txd_budget = report["budgets"]["pools"]["txd_slots"]
            self.assertEqual(3607, txd_budget["standalone_archive_inventory"])
            self.assertEqual(3608, txd_budget["mta_runtime_audited_occupied"])
            self.assertEqual(3774, txd_budget["projected_used"])

            manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(3608, manifest["txd_slot_plan"]["base"])
            self.assertEqual([3608, 3773], [min(manifest["txd_slot_plan"]["slots"].values()), max(manifest["txd_slot_plan"]["slots"].values())])

            runtime_manifest = json.loads((output / "native-world.json").read_text(encoding="ascii"))
            self.assertEqual(1, runtime_manifest["format"])
            self.assertEqual("bullworth", runtime_manifest["pack_id"])
            self.assertEqual((output / "bw.ide").stat().st_size, runtime_manifest["files"]["ide"]["bytes"])
            self.assertEqual((output / "bw.img").stat().st_size, runtime_manifest["files"]["img"]["bytes"])

            entries = read_img_directory(output / "bw.img")
            self.assertEqual(report["counts"]["archive_entries"], len(entries))
            for ipl in report["ipls"]:
                parsed = parse_binary_ipl_data((output / "ipls" / ipl["name"]).read_bytes())
                self.assertEqual(ipl["placements"], len(parsed))
                self.assertTrue(all(instance.lod_index == -1 for instance in parsed))


if __name__ == "__main__":
    unittest.main()
