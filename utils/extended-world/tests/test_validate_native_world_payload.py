#!/usr/bin/env python3
"""Mutation tests for the native static-world payload reference validator."""

from __future__ import annotations

import os
import struct
import sys
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import validate_native_world_payload as validator  # noqa: E402
from validate_native_world_payload import (  # noqa: E402
    COL_BUFFER_CAPACITY,
    MAX_RW_NODES,
    MODEL_FIRST,
    RW_BUDGET,
    RW_VERSION,
    ValidationError,
    checked_add,
    checked_mul,
    read_img_directory,
    validate_archive_bytes,
    validate_col_member,
    validate_rw_member,
)


SECTOR = 2048


def rw_chunk(chunk_id: int, payload: bytes = b"", version: int = RW_VERSION) -> bytes:
    return struct.pack("<III", chunk_id, len(payload), version) + payload


def dff(payload: bytes | None = None, version: int = RW_VERSION) -> bytes:
    if payload is None:
        frame_values = (1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0)
        frame_struct = rw_chunk(1, struct.pack("<I12fII", 1, *frame_values, 0xFFFFFFFF, 0))
        frame_list = rw_chunk(0x0E, frame_struct + rw_chunk(3, rw_chunk(0x0253F2FE, b"root")))

        material_data = bytearray(28)
        struct.pack_into("<3f", material_data, 16, 1.0, 1.0, 1.0)
        material = rw_chunk(7, rw_chunk(1, material_data) + rw_chunk(3))
        material_list = rw_chunk(8, rw_chunk(1, struct.pack("<II", 1, 0xFFFFFFFF)) + material)

        geometry_data = bytearray(struct.pack("<4I", 0x0001002E, 1, 3, 1))
        geometry_data.extend(b"\xFF" * 12)
        geometry_data.extend(struct.pack("<6f", *([0.5] * 6)))
        geometry_data.extend(struct.pack("<4H", 0, 1, 0, 2))
        geometry_data.extend(struct.pack("<4fII", 0.0, 0.0, 0.0, 1.0, 1, 0))
        geometry_data.extend(struct.pack("<9f", *([1.0] * 9)))
        bin_mesh = rw_chunk(0x050E, struct.pack("<5I3I", 0, 1, 3, 3, 0, 0, 1, 2))
        breakable = rw_chunk(0x0253F2FD, struct.pack("<I", 0))
        geometry = rw_chunk(0x0F, rw_chunk(1, geometry_data) + material_list + rw_chunk(3, bin_mesh + breakable))
        geometry_list = rw_chunk(0x1A, rw_chunk(1, struct.pack("<I", 1)) + geometry)
        atomic = rw_chunk(0x14, rw_chunk(1, struct.pack("<4I", 0, 0, 5, 0)) + rw_chunk(3))
        payload = rw_chunk(1, struct.pack("<III", 1, 0, 0)) + frame_list + geometry_list + atomic + rw_chunk(3)
    return rw_chunk(0x10, payload, version)


def txd() -> bytes:
    texture = bytearray(100)
    struct.pack_into("<II", texture, 0, 9, 0x1106)
    texture[8:40] = b"fixture".ljust(32, b"\0")
    struct.pack_into("<IIHH4B", texture, 72, 0x0100, 0x31545844, 4, 4, 16, 1, 4, 8)
    struct.pack_into("<I", texture, 88, 8)
    native = rw_chunk(0x15, rw_chunk(1, texture) + rw_chunk(3))
    return rw_chunk(0x16, rw_chunk(1, struct.pack("<HH", 1, 0)) + native + rw_chunk(3))


def col_header(magic: bytes, body: bytes, name: str = "model", model_id: int = MODEL_FIRST) -> bytes:
    encoded = name.encode("ascii").ljust(22, b"\0")
    record = bytearray(magic + b"\0\0\0\0" + encoded + struct.pack("<H", model_id) + body)
    struct.pack_into("<I", record, 4, len(record) - 8)
    return bytes(record)


def coll_record(name: str = "model", model_id: int = MODEL_FIRST) -> bytes:
    body = bytearray(struct.pack("<10f", *([0.0] * 10)))
    body += struct.pack("<I", 0)  # spheres
    body += struct.pack("<I", 0)  # lines
    body += struct.pack("<I", 0)  # boxes
    body += struct.pack("<I", 3) + struct.pack("<9f", *([0.0] * 9))
    body += struct.pack("<I", 1) + struct.pack("<III4B", 0, 1, 2, 0, 0, 0, 0)
    return col_header(b"COLL", bytes(body), name, model_id)


def col3_record(name: str = "model", model_id: int = MODEL_FIRST) -> bytes:
    header = bytearray(88)
    struct.pack_into("<10f", header, 0, *([0.0] * 10))
    struct.pack_into("<HHHB", header, 40, 0, 0, 1, 0)
    struct.pack_into("<I", header, 48, 2)
    struct.pack_into("<6I", header, 52, 0, 0, 0, 116, 140, 0)
    struct.pack_into("<3I", header, 76, 0, 0, 0)
    vertices = struct.pack("<12h", *([0] * 12))
    face = struct.pack("<HHHBB", 0, 1, 2, 0, 0)
    return col_header(b"COL3", bytes(header) + vertices + face, name, model_id)


def build_img(members: list[tuple[str, bytes]]) -> bytes:
    directory_bytes = 8 + len(members) * 32
    next_sector = (directory_bytes + SECTOR - 1) // SECTOR
    entries: list[tuple[int, int, str, bytes]] = []
    for name, payload in members:
        sectors = (len(payload) + SECTOR - 1) // SECTOR
        entries.append((next_sector, sectors, name, payload))
        next_sector += sectors
    result = bytearray(next_sector * SECTOR)
    struct.pack_into("<4sI", result, 0, b"VER2", len(entries))
    for index, (offset, sectors, name, _) in enumerate(entries):
        struct.pack_into("<IHH24s", result, 8 + index * 32, offset, sectors, sectors, name.encode("ascii").ljust(24, b"\0"))
    for offset, _, _, payload in entries:
        result[offset * SECTOR : offset * SECTOR + len(payload)] = payload
    return bytes(result)


class NativeWorldPayloadValidatorTest(unittest.TestCase):
    def test_small_synthetic_archive_and_deterministic_summary(self) -> None:
        archive = build_img([("model.dff", dff()), ("tex.txd", txd()), ("bw.col", col3_record())])
        first = validate_archive_bytes(archive).as_dict()
        second = validate_archive_bytes(archive).as_dict()
        self.assertEqual(first, second)
        self.assertEqual({"col": 1, "dff": 1, "txd": 1}, first["entries"])
        self.assertEqual(1, first["col"]["records"])

    def test_img_truncation_and_checked_arithmetic(self) -> None:
        with self.assertRaisesRegex(ValidationError, "truncated header"):
            read_img_directory(b"VER2")
        with self.assertRaisesRegex(ValidationError, "truncated directory"):
            read_img_directory(struct.pack("<4sI", b"VER2", 1))
        with self.assertRaisesRegex(ValidationError, "sector aligned"):
            read_img_directory(build_img([]) + b"\0")
        with self.assertRaisesRegex(ValidationError, "multiplication overflow"):
            checked_mul(0xFFFFFFFF, 32, limit=0xFFFFFFFF, what="fixture")
        with self.assertRaisesRegex(ValidationError, "addition overflow"):
            checked_add(0xFFFFFFFF, 1, limit=0xFFFFFFFF, what="fixture")

    def test_img_truncated_member_and_unsafe_name(self) -> None:
        archive = bytearray(build_img([("model.dff", dff())]))
        struct.pack_into("<H", archive, 8 + 4, 2)
        struct.pack_into("<H", archive, 8 + 6, 2)
        with self.assertRaisesRegex(ValidationError, "truncated member"):
            read_img_directory(archive)
        archive = bytearray(build_img([("model.dff", dff())]))
        archive[8 + 8 : 8 + 32] = b"x" * 24
        with self.assertRaisesRegex(ValidationError, "unterminated"):
            read_img_directory(archive)

    def test_rw_wrong_root_version_crossing_and_padding(self) -> None:
        with self.assertRaisesRegex(ValidationError, "wrong root chunk"):
            validate_rw_member(rw_chunk(0x16, rw_chunk(1)).ljust(SECTOR, b"\0"), "dff", "bad.dff")
        with self.assertRaisesRegex(ValidationError, "wrong root version"):
            validate_rw_member(dff(version=0).ljust(SECTOR, b"\0"), "dff", "bad.dff")
        crossing = struct.pack("<III", 0x10, SECTOR, RW_VERSION).ljust(SECTOR, b"\0")
        with self.assertRaisesRegex(ValidationError, "crosses allocation"):
            validate_rw_member(crossing, "dff", "bad.dff")
        padded = bytearray(dff().ljust(SECTOR, b"\0"))
        padded[-1] = 1
        with self.assertRaisesRegex(ValidationError, "nonzero allocation padding"):
            validate_rw_member(padded, "dff", "bad.dff")

    def test_rw_length_overflow_depth_and_nodes(self) -> None:
        overflow = struct.pack("<III", 0x10, 0xFFFFFFFF, RW_VERSION).ljust(SECTOR, b"\0")
        with self.assertRaisesRegex(ValidationError, "addition overflow"):
            validate_rw_member(overflow, "dff", "bad.dff")
        nested = rw_chunk(1)
        for _ in range(16):
            nested = rw_chunk(6, nested)
        with self.assertRaisesRegex(ValidationError, "excessive chunk depth"):
            validate_rw_member(dff(nested).ljust(SECTOR, b"\0"), "dff", "deep.dff")
        many = b"".join(rw_chunk(1) for _ in range(MAX_RW_NODES))
        with self.assertRaisesRegex(ValidationError, "excessive aggregate chunk node count"):
            validate_rw_member(dff(many).ljust(len(dff(many)) + SECTOR, b"\0"), "dff", "wide.dff")

    def test_rw_node_budget_is_archive_wide_and_accepts_exact_boundary(self) -> None:
        original = validator.MAX_RW_NODES
        try:
            validator.MAX_RW_NODES = 44
            boundary = build_img([("one.dff", dff()), ("two.dff", dff())])
            self.assertEqual(44, sum(validate_archive_bytes(boundary).rw.chunks.values()))
            over = build_img([("one.dff", dff()), ("two.dff", dff()), ("tex.txd", txd())])
            with self.assertRaisesRegex(ValidationError, "excessive aggregate chunk node count"):
                validate_archive_bytes(over)
        finally:
            validator.MAX_RW_NODES = original

    def test_rw_unknown_core_and_plugin_are_refused(self) -> None:
        with self.assertRaisesRegex(ValidationError, "unknown core/container"):
            validate_rw_member(dff(rw_chunk(0x999)).ljust(SECTOR, b"\0"), "dff", "bad.dff")
        extension = rw_chunk(3, rw_chunk(0x123456))
        with self.assertRaisesRegex(ValidationError, "plugin outside closed grammar"):
            validate_rw_member(dff(extension).ljust(SECTOR, b"\0"), "dff", "bad.dff")

    def test_rw_semantic_frame_atomic_binmesh_texture_and_aggregate_mutations(self) -> None:
        frame = bytearray(dff())
        identity = struct.pack("<12f", 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0)
        frame_offset = frame.index(identity)
        struct.pack_into("<f", frame, frame_offset, float("nan"))
        with self.assertRaisesRegex(ValidationError, "non-finite"):
            validate_rw_member(frame.ljust(SECTOR, b"\0"), "dff", "frame.dff")

        atomic = bytearray(dff())
        atomic_offset = atomic.index(struct.pack("<4I", 0, 0, 5, 0))
        struct.pack_into("<I", atomic, atomic_offset + 8, 4)
        with self.assertRaisesRegex(ValidationError, "atomic indices/flags"):
            validate_rw_member(atomic.ljust(SECTOR, b"\0"), "dff", "atomic.dff")

        bin_mesh = bytearray(dff())
        bin_mesh_offset = bin_mesh.index(struct.pack("<5I3I", 0, 1, 3, 3, 0, 0, 1, 2))
        struct.pack_into("<I", bin_mesh, bin_mesh_offset + 28, 99)
        with self.assertRaisesRegex(ValidationError, "BinMesh vertex index"):
            validate_rw_member(bin_mesh.ljust(SECTOR, b"\0"), "dff", "binmesh.dff")

        texture = bytearray(txd())
        texture_name = texture.index(b"fixture\0")
        struct.pack_into("<H", texture, texture_name + 72, 2048)
        with self.assertRaisesRegex(ValidationError, "NativeTexture header"):
            validate_rw_member(texture.ljust(SECTOR, b"\0"), "txd", "texture.txd")

        original = RW_BUDGET["total_texture_gpu_bytes"]
        try:
            RW_BUDGET["total_texture_gpu_bytes"] = 1
            with self.assertRaisesRegex(ValidationError, "aggregate budget"):
                validate_rw_member(txd().ljust(SECTOR, b"\0"), "txd", "budget.txd")
        finally:
            RW_BUDGET["total_texture_gpu_bytes"] = original

    def test_col_bad_magic_size_and_native_buffer_overflow(self) -> None:
        record = bytearray(col3_record())
        record[:4] = b"NOPE"
        with self.assertRaisesRegex(ValidationError, "bad magic"):
            validate_col_member(record)
        record = bytearray(col3_record())
        struct.pack_into("<I", record, 4, 1)
        with self.assertRaisesRegex(ValidationError, "invalid size"):
            validate_col_member(record)
        record = bytearray(col3_record())
        struct.pack_into("<I", record, 4, COL_BUFFER_CAPACITY)
        with self.assertRaisesRegex(ValidationError, "buffer overflow"):
            validate_col_member(record)

    def test_col_duplicate_range_name_and_termination(self) -> None:
        record = col3_record()
        with self.assertRaisesRegex(ValidationError, "duplicate"):
            validate_col_member(record + record)
        outside = bytearray(record)
        struct.pack_into("<H", outside, 30, MODEL_FIRST - 1)
        with self.assertRaisesRegex(ValidationError, "outside Bullworth range"):
            validate_col_member(outside)
        with self.assertRaisesRegex(ValidationError, "name mismatch"):
            validate_col_member(record, {MODEL_FIRST: "different"})
        unterminated = bytearray(record)
        unterminated[8:30] = b"x" * 22
        with self.assertRaisesRegex(ValidationError, "unterminated"):
            validate_col_member(unterminated)
        nonzero_tail = bytearray(record)
        nonzero_tail[14] = ord("x")
        with self.assertRaisesRegex(ValidationError, "nonzero bytes after name terminator"):
            validate_col_member(nonzero_tail)

    def test_coll_count_triangle_and_nonfinite_mutations(self) -> None:
        record = bytearray(coll_record())
        struct.pack_into("<I", record, 72, 0xFFFFFFFF)
        with self.assertRaisesRegex(ValidationError, "overflow|budget"):
            validate_col_member(record)
        record = bytearray(coll_record())
        face_offset = len(record) - 16
        struct.pack_into("<I", record, face_offset, 99)
        with self.assertRaisesRegex(ValidationError, "triangle vertex index"):
            validate_col_member(record)
        record = bytearray(coll_record())
        struct.pack_into("<f", record, 32, float("inf"))
        with self.assertRaisesRegex(ValidationError, "NaN/Inf"):
            validate_col_member(record)

    def test_col3_offsets_counts_overlap_indices_and_nonfinite(self) -> None:
        record = bytearray(col3_record())
        struct.pack_into("<I", record, 32 + 52 + 4 * 4, 0xFFFFFFF0)
        with self.assertRaisesRegex(ValidationError, "crosses record|offsets invalid"):
            validate_col_member(record)
        record = bytearray(col3_record())
        struct.pack_into("<H", record, 32 + 44, 0xFFFF)
        with self.assertRaisesRegex(ValidationError, "crosses record|budget|closed profile"):
            validate_col_member(record)
        record = bytearray(col3_record())
        struct.pack_into("<I", record, 32 + 52 + 4 * 4, 116)
        with self.assertRaisesRegex(ValidationError, "overlap|vertex array/padding"):
            validate_col_member(record)
        record = bytearray(col3_record())
        struct.pack_into("<H", record, len(record) - 8, 99)
        with self.assertRaisesRegex(ValidationError, "overlap|crosses record|triangle vertex index"):
            validate_col_member(record)
        record = bytearray(col3_record())
        struct.pack_into("<f", record, 32, float("nan"))
        with self.assertRaisesRegex(ValidationError, "NaN/Inf"):
            validate_col_member(record)

    def test_optional_actual_vm_golden_archive(self) -> None:
        img = os.environ.get("NATIVE_BW_IMG")
        ide = os.environ.get("NATIVE_BW_IDE")
        if not img or not ide:
            self.skipTest("set NATIVE_BW_IMG and NATIVE_BW_IDE to the copied VM runtime pack")
        summary = validate_archive_bytes(
            Path(img).read_bytes(),
            ide_text=Path(ide).read_text(encoding="ascii"),
            enforce_profile=True,
        )
        self.assertEqual(1126, sum(summary.entries.values()))
        self.assertEqual(952, summary.col.records)
        self.assertEqual(7, summary.rw.max_depth)
        self.assertEqual(570, summary.rw.max_nodes)
        self.assertEqual(161_175_552, summary.rw.allocation_bytes)
        self.assertEqual((951_680, 660_547, 4_785), (summary.rw.geometry_vertices, summary.rw.geometry_triangles, summary.rw.geometry_materials))
        self.assertEqual(1_981_641, summary.rw.bin_mesh_indices)
        self.assertEqual((34, 1_437, 1_025, 28), (summary.rw.effects_2d, summary.rw.breakable_vertices, summary.rw.breakable_triangles, summary.rw.breakable_materials))
        self.assertEqual((4_705, 119_043_248, 845_073_700), (summary.rw.native_textures, summary.rw.texture_gpu_bytes, summary.rw.texture_decoded_bytes))
        self.assertEqual((8_205_604, 12, 88, 0, 496_133, 566_811, 18_452), (summary.col.bytes, summary.col.spheres, summary.col.boxes, summary.col.lines, summary.col.vertices, summary.col.faces, summary.col.face_groups))
        self.assertEqual((12_745, 20_536, 661), (summary.col.max_vertices, summary.col.max_faces, summary.col.max_face_groups))
        self.assertEqual({"COL3": 948, "COLL": 4}, dict(summary.col.magics))


if __name__ == "__main__":
    unittest.main()
