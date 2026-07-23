#!/usr/bin/env python3
"""Closed-envelope tests for the canonical native-world v3 builder."""

from __future__ import annotations

import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

from build_native_bw_pack import ArchiveInput, RW_LIBRARY_ID, pack_inputs  # noqa: E402
from build_native_world_v3 import (  # noqa: E402
    BINARY_IPL_HEADER_SIZE,
    BINARY_IPL_INSTANCE,
    GeneratedModel,
    GeneratedPlacement,
    LOD_MAP_HEADER,
    LOD_MAP_MAGIC,
    LOD_MAP_VERSION,
    LOD_MAP_LINK,
    MAX_TEXTURE_GPU_BYTES,
    canonicalize_ide_flags,
    canonicalize_rw_version,
    canonicalize_timed_model,
    canonicalize_txd_duplicates,
    checked_u64_add,
    convert_col2_to_col3,
    empty_txd_v3,
    make_variants,
    normalize_dff_uvs,
    validate_binary_ipl_v3,
    validate_lod_map_v3,
    validate_static_col_record_v3,
    validate_static_txd_v3_grammar,
    verify_pack,
)
from native_world_manifest import STATIC_WORLD_V3_POLICY, build_runtime_manifest, dump_runtime_manifest  # noqa: E402


def rw_chunk(chunk_type: int, payload: bytes = b"") -> bytes:
    return struct.pack("<III", chunk_type, len(payload), RW_LIBRARY_ID) + payload


def native_texture(
    name: str,
    *,
    raster_format: int = 0x0200,
    d3d_format: int = 0x31545844,
    depth: int = 16,
    flags: int = 8,
    width: int = 4,
    height: int = 4,
    mip_payloads: tuple[bytes, ...] = (b"\x5A" * 8,),
) -> bytes:
    structure = bytearray(88)
    struct.pack_into("<II", structure, 0, 9, 0x1102)
    structure[8:40] = name.encode("ascii").ljust(32, b"\0")
    structure[40:72] = b"mask".ljust(32, b"\0")
    struct.pack_into("<II", structure, 72, raster_format, d3d_format)
    struct.pack_into("<HH4B", structure, 80, width, height, depth, len(mip_payloads), 4, flags)
    for payload in mip_payloads:
        structure.extend(struct.pack("<I", len(payload)))
        structure.extend(payload)
    return rw_chunk(0x15, rw_chunk(0x01, structure) + rw_chunk(0x03))


def txd(*textures: bytes) -> bytes:
    return rw_chunk(0x16, rw_chunk(0x01, struct.pack("<HH", len(textures), 2)) + b"".join(textures) + rw_chunk(0x03))


def col3_record(name: str = "a0m0000", model_id: int = 20_000, *, bad_shadow_index: bool = False) -> bytes:
    record = bytearray(120)
    record[:4] = b"COL3"
    record[8:30] = name.encode("ascii").ljust(22, b"\0")
    struct.pack_into("<H", record, 30, model_id)
    struct.pack_into("<10f", record, 32, -1, -1, -1, 1, 1, 1, 0, 0, 0, 1)
    struct.pack_into("<HHHB", record, 72, 0, 0, 1, 0)
    struct.pack_into("<I", record, 80, 18)
    struct.pack_into("<II", record, 96, 116, 136)
    struct.pack_into("<III", record, 108, 1, 144, 164)
    record.extend(struct.pack("<9h", 0, 0, 0, 8, 0, 0, 0, 8, 0))
    record.extend(b"\0\0")
    record.extend(struct.pack("<3H2B", 0, 1, 2, 0, 0))
    record.extend(struct.pack("<9h", 0, 0, 0, 8, 0, 0, 0, 8, 0))
    record.extend(b"\0\0")
    record.extend(struct.pack("<3H2B", 0, 1, 3 if bad_shadow_index else 2, 0, 0))
    struct.pack_into("<I", record, 4, len(record) - 8)
    return bytes(record)


def col2_record() -> bytes:
    source = bytearray(col3_record()[:148])
    source[:4] = b"COL2"
    struct.pack_into("<I", source, 80, 2)
    struct.pack_into("<III", source, 108, 0, 0, 0)
    del source[108:120]
    struct.pack_into("<I", source, 4, len(source) - 8)
    struct.pack_into("<I", source, 96, struct.unpack_from("<I", source, 96)[0] - 12)
    struct.pack_into("<I", source, 100, struct.unpack_from("<I", source, 100)[0] - 12)
    return bytes(source)


def canonical_dff() -> bytes:
    frame = struct.pack("<I12fII", 1, *([1.0] * 12), 0xFFFFFFFF, 0)
    frame_list = rw_chunk(0x0E, rw_chunk(0x01, frame) + rw_chunk(0x03))
    material_struct = bytearray(28)
    struct.pack_into("<3f", material_struct, 16, 1.0, 1.0, 1.0)
    material = rw_chunk(0x07, rw_chunk(0x01, material_struct) + rw_chunk(0x03))
    material_list = rw_chunk(0x08, rw_chunk(0x01, struct.pack("<II", 1, 0xFFFFFFFF)) + material)
    geometry_struct = bytearray(struct.pack("<4I", 0x0001002E, 1, 3, 1))
    geometry_struct.extend(b"\xFF" * 12)
    geometry_struct.extend(struct.pack("<6f", *([0.5] * 6)))
    geometry_struct.extend(struct.pack("<4H", 0, 1, 0, 2))
    geometry_struct.extend(struct.pack("<4fII", 0.0, 0.0, 0.0, 1.0, 1, 0))
    geometry_struct.extend(struct.pack("<9f", *([1.0] * 9)))
    bin_mesh = rw_chunk(0x050E, struct.pack("<5I3I", 0, 1, 3, 3, 0, 0, 1, 2))
    geometry = rw_chunk(0x0F, rw_chunk(0x01, geometry_struct) + material_list + rw_chunk(0x03, bin_mesh))
    geometry_list = rw_chunk(0x1A, rw_chunk(0x01, struct.pack("<I", 1)) + geometry)
    return rw_chunk(
        0x10,
        rw_chunk(0x01, struct.pack("<III", 0, 0, 0)) + frame_list + geometry_list + rw_chunk(0x03),
    )


def binary_ipl(model_id: int = 20_000) -> bytes:
    header = bytearray(BINARY_IPL_HEADER_SIZE)
    header[:4] = b"bnry"
    struct.pack_into("<I", header, 4, 1)
    struct.pack_into("<I", header, 28, BINARY_IPL_HEADER_SIZE)
    return bytes(header) + BINARY_IPL_INSTANCE.pack(0, 0, 0, 0, 0, 0, 1, model_id, 0, -1)


class NativeWorldV3GrammarTest(unittest.TestCase):
    def test_lod_sidecar_is_closed_and_rejects_structural_corruption(self) -> None:
        empty_links = LOD_MAP_HEADER.pack(LOD_MAP_MAGIC, LOD_MAP_VERSION, 1, 1, 0, 0, 0) + struct.pack("<I", 1)
        self.assertEqual(0, validate_lod_map_v3(empty_links, [1])["links"])

        linked = (
            LOD_MAP_HEADER.pack(LOD_MAP_MAGIC, LOD_MAP_VERSION, 2, 1, 1, 1, 0)
            + struct.pack("<II", 2, 1)
            + LOD_MAP_LINK.pack(0, 0)
        )
        self.assertEqual({"anchors": 1, "links": 1}, {key: validate_lod_map_v3(linked, [2])[key] for key in ("anchors", "links")})

        mutations = [
            b"BADLOD!!" + linked[8:],
            linked[:8] + struct.pack("<I", 2) + linked[12:],
            linked[:28] + struct.pack("<I", 1) + linked[32:],
            linked + b"\0",
            LOD_MAP_HEADER.pack(LOD_MAP_MAGIC, 1, 0, 1, 0, 0, 0) + struct.pack("<I", 0),
            LOD_MAP_HEADER.pack(LOD_MAP_MAGIC, 1, 2, 1, 1, 1, 0)
            + struct.pack("<II", 2, 0)
            + LOD_MAP_LINK.pack(0, 0),
            LOD_MAP_HEADER.pack(LOD_MAP_MAGIC, 1, 2, 1, 1, 1, 0)
            + struct.pack("<II", 2, 1)
            + LOD_MAP_LINK.pack(0, 1),
            LOD_MAP_HEADER.pack(LOD_MAP_MAGIC, 1, 4097, 1, 2049, 2048, 0)
            + struct.pack("<I", 4097)
            + b"\0" * (2049 * 4 + 2048 * LOD_MAP_LINK.size),
        ]
        for mutation in mutations:
            with self.subTest(bytes=len(mutation)), self.assertRaises(ValueError):
                validate_lod_map_v3(mutation, [2] if len(mutation) != 32 + 4 + 2049 * 4 + 2048 * 8 else [4097])

    def test_txd_header_tuple_and_legacy_empty_tail_are_closed(self) -> None:
        valid = txd(native_texture("valid"))
        self.assertEqual(8, validate_static_txd_v3_grammar(valid, "valid.txd")["serialized_gpu_bytes"])

        invalid_tuple = txd(native_texture("bad", raster_format=0xDEAD))
        with self.assertRaisesRegex(ValueError, "header is outside"):
            validate_static_txd_v3_grammar(invalid_tuple, "tuple.txd")

        invalid_empty_dxt5 = txd(
            native_texture(
                "bad",
                raster_format=0x0300,
                d3d_format=0x35545844,
                flags=9,
                width=2,
                height=2,
                mip_payloads=(b"",),
            )
        )
        with self.assertRaisesRegex(ValueError, "mip byte count"):
            validate_static_txd_v3_grammar(invalid_empty_dxt5, "empty.txd")

    def test_txd_duplicate_canonicalization_keeps_first(self) -> None:
        source = txd(native_texture("Same"), native_texture("same"))
        canonical, records = canonicalize_txd_duplicates(source, "duplicate.txd")
        self.assertEqual(1, validate_static_txd_v3_grammar(canonical, "duplicate.txd")["textures"])
        self.assertEqual(["first-wins-duplicate"], [record["kind"] for record in records])

    def test_col3_shadow_mesh_indices_are_proved(self) -> None:
        stats = validate_static_col_record_v3(col3_record(), "shadow.col")
        self.assertEqual((3, 1), (stats["shadow_vertices"], stats["shadow_faces"]))
        with self.assertRaisesRegex(ValueError, "shadow face vertex index"):
            validate_static_col_record_v3(col3_record(bad_shadow_index=True), "bad-shadow.col")

    def test_col2_conversion_is_full_col3_equivalent(self) -> None:
        converted, changed = convert_col2_to_col3(col2_record(), "source.col")
        self.assertTrue(changed)
        self.assertEqual(b"COL3", converted[:4])
        self.assertEqual(1, validate_static_col_record_v3(converted, "converted.col")["faces"])

    def test_checked_u64_budget_fails_closed(self) -> None:
        with self.assertRaisesRegex(ValueError, "compiled byte budget"):
            checked_u64_add(MAX_TEXTURE_GPU_BYTES, 1, MAX_TEXTURE_GPU_BYTES, "texture")

    def test_vc_timed_model_repair_is_exactly_identity_pinned(self) -> None:
        fingerprint = "677b91a08730b6e503cfa9ae0a4ae6bded252f4f3112a49505ae852c18eaca4f"
        repairs = (
            (20509, 19, 0x200005, 19, 5),
            (21489, 20, 0x200005, 20, 5),
            (21494, 20, 0x200005, 20, 5),
            (21802, 24, 5, 0, 5),
            (21804, 24, 5, 0, 5),
        )
        for source_id, raw_on, raw_off, expected_on, expected_off in repairs:
            repaired_on, repaired_off, record = canonicalize_timed_model(
                "UG_VC", "map_data.lua", fingerprint, source_id, raw_on, raw_off
            )
            self.assertEqual((expected_on, expected_off), (repaired_on, repaired_off))
            self.assertEqual((raw_on, raw_off), (record["raw_time_on"], record["raw_time_off"]))
        for values in (
            ("UG_LC", "map_data.lua", fingerprint, 20509, 19, 0x200005),
            ("UG_VC", "other.lua", fingerprint, 20509, 19, 0x200005),
            ("UG_VC", "map_data.lua", "0" * 64, 20509, 19, 0x200005),
            ("UG_VC", "map_data.lua", fingerprint, 20510, 19, 0x200005),
            ("UG_VC", "map_data.lua", fingerprint, 20509, 18, 0x200005),
            ("UG_VC", "map_data.lua", fingerprint, 20509, 19, 0x200006),
        ):
            unchanged_on, unchanged_off, rejected_record = canonicalize_timed_model(*values)
            self.assertEqual(values[-2:], (unchanged_on, unchanged_off))
            self.assertIsNone(rejected_record)

    def test_lc_ide_flag_repairs_are_exactly_identity_pinned(self) -> None:
        fingerprint = "1597f9c3026fc8641202b03ccb03f019292e29a270d0c00052ac2c81c684feab"
        canonical, record = canonicalize_ide_flags(
            "UG_LC", "map_data.lua", fingerprint, 31057, 0x20200000
        )
        self.assertEqual(0x200000, canonical)
        self.assertEqual(0x20000000, record["removed_flags"])
        self.assertEqual(0x200000, canonicalize_ide_flags("FIXTURE", "map_data.lua", fingerprint, 1, 0x200000)[0])
        for values in (
            ("UG_VC", "map_data.lua", fingerprint, 31057, 0x20200000),
            ("UG_LC", "other.lua", fingerprint, 31057, 0x20200000),
            ("UG_LC", "map_data.lua", "0" * 64, 31057, 0x20200000),
            ("UG_LC", "map_data.lua", fingerprint, 31056, 0x20200000),
            ("UG_LC", "map_data.lua", fingerprint, 31057, 0x20200001),
        ):
            with self.assertRaisesRegex(ValueError, "unreviewed IDE flags"):
                canonicalize_ide_flags(*values)

    def test_generated_empty_txd_is_the_only_zero_texture_dictionary(self) -> None:
        empty = empty_txd_v3()
        self.assertEqual(0, validate_static_txd_v3_grammar(empty, "generated-empty.txd")["textures"])

    def test_zero_triangle_dffs_are_path_and_hash_pinned(self) -> None:
        repository = TOOLS.parents[1]
        for relative in ("assets/models/23345.dff", "assets/models/23346.dff"):
            data = (repository / "test-resources/ug-vc" / relative).read_bytes()
            canonical, _, _ = canonicalize_rw_version(data, relative)
            _, _, exceptions = normalize_dff_uvs(
                canonical,
                relative,
                source_sha256=__import__("hashlib").sha256(data).hexdigest(),
            )
            self.assertEqual(1, len(exceptions))
            with self.assertRaisesRegex(ValueError, "zero-triangle geometry"):
                normalize_dff_uvs(canonical, "assets/models/not-pinned.dff")

    def test_txd_keygen_collisions_fail_independently_of_dff_names(self) -> None:
        models = {
            source_id: GeneratedModel(
                source_id=source_id,
                source_name=f"model{source_id}",
                model_type="object",
                txd_path=f"assets/textures/{source_id}.txd",
                dff_path=f"assets/models/{source_id}.dff",
                col_path=f"assets/collisions/{source_id}.col",
                draw_distance=100,
                ide_flags=0,
                time_on=None,
                time_off=None,
            )
            for source_id in (1, 2)
        }
        placements = [
            GeneratedPlacement(
                source_id=source_id,
                native=False,
                position=(0, 0, 0),
                quaternion=(0, 0, 0, 1),
                lod_global_index=None,
                is_lod=False,
                source="group.ipl",
                source_index=source_id,
                global_index=source_id - 1,
            )
            for source_id in models
        ]
        with patch("build_native_world_v3.gta_uppercase_key", return_value=1):
            with self.assertRaisesRegex(ValueError, "generated TXD key collision"):
                make_variants(models, placements, "aa", 20_000)

    def test_verify_pack_semantically_reaudits_every_member_type(self) -> None:
        with tempfile.TemporaryDirectory(prefix="native-world-v3-test-") as temporary:
            output = Path(temporary)
            ide = output / "world.ide"
            ide.write_text(
                "objs\n"
                "20000, a0m0000, a0t000, 1, 300, 0\n"
                "20001, a0m0001, a0t000, 1, 300, 0\n"
                "end\n"
                "tobj\n"
                "end\n",
                encoding="ascii",
            )
            image = output / "w000.img"
            pack_inputs(
                image,
                [
                    ArchiveInput(name="a0m0000.dff", data=canonical_dff()),
                    ArchiveInput(name="a0m0001.dff", data=canonical_dff()),
                    ArchiveInput(name="a0t000.txd", data=txd(native_texture("valid"))),
                    ArchiveInput(name="a0c00.col", data=col3_record()),
                    ArchiveInput(name="a0i00.ipl", data=binary_ipl()),
                ],
            )
            (output / "world.lod").write_bytes(
                LOD_MAP_HEADER.pack(LOD_MAP_MAGIC, LOD_MAP_VERSION, 1, 1, 0, 0, 0) + struct.pack("<I", 1)
            )
            manifest = build_runtime_manifest(
                {},
                ide,
                img_paths=[image],
                format_version=3,
                policy=STATIC_WORLD_V3_POLICY,
                pack_id="test",
            )
            dump_runtime_manifest(output / "native-world.json", manifest)
            report = verify_pack(output)
            self.assertEqual(1, report["semantic_profile"]["col"]["shadow_faces"])
            self.assertEqual(1, report["semantic_profile"]["ipl"]["placements"])
            self.assertEqual([{"native_id": 20001, "name": "a0m0001"}], report["models_without_col"])


if __name__ == "__main__":
    unittest.main()
