#!/usr/bin/env python3
"""Reference validator for the reviewed Bullworth v1 native IMG payload.

This is deliberately stricter than a general RenderWare reader. It validates
the measured static-world profile before GTA sees any IMG member: checked IMG
arithmetic, a closed semantic RenderWare grammar with per-member and aggregate
budgets, and the canonical COLL/COL3 layouts used by the generated pack.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Mapping


SECTOR_SIZE = 2048
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
IMG_HEADER = struct.Struct("<4sI")
IMG_ENTRY = struct.Struct("<IHH24s")
RW_HEADER = struct.Struct("<III")
RW_VERSION = 0x1803FFFF
RW_ROOTS = {"dff": 0x10, "txd": 0x16}
RW_LEAVES = {0x01, 0x02}
RW_CONTAINERS = {0x03, 0x06, 0x07, 0x08, 0x0E, 0x0F, 0x10, 0x12, 0x14, 0x15, 0x16, 0x1A}
RW_PLUGIN_IDS = {0x050E, 0x0253F2F8, 0x0253F2F9, 0x0253F2FC, 0x0253F2FD, 0x0253F2FE}
RW_EXTENSION = 0x03
MAX_RW_DEPTH = 16
MAX_RW_NODES = 100_000
MAX_IMG_ENTRIES = 4096
COL_MAGICS = {b"COLL", b"COL3"}
COL_BUFFER_CAPACITY = 327_680
COL_BUDGET = {
    "records": 2_000,
    "spheres": 8,
    "boxes": 16,
    "lines": 0,
    "vertices": 32_768,
    "faces": 32_768,
    "face_groups": 1_024,
    "shadow_faces": 0,
    "total_bytes": 9_437_184,
    "total_spheres": 32,
    "total_boxes": 128,
    "total_lines": 0,
    "total_vertices": 550_000,
    "total_faces": 600_000,
    "total_face_groups": 20_000,
}
MODEL_FIRST = 18_631
MODEL_LAST = 19_582
PROFILE_IMG_SHA256 = "23e596450bf0128fec49d2c31245c5a86269266915429bffbc57d61a113b3540"
PROFILE_IPLS = {
    "bw_tbusines.ipl",
    "bw_tcarni.ipl",
    "bw_tglobal.ipl",
    "bw_tindust.ipl",
    "bw_tjyard.ipl",
    "bw_trich.ipl",
    "bw_tschool.ipl",
}
SAFE_IMG_NAME = re.compile(r"[a-z0-9_]+\.(?:dff|txd|col|ipl)\Z")
SAFE_COL_NAME = re.compile(r"[A-Za-z0-9_]+\Z")
MAX_FLOAT = 1_000_000.0

RW_BUDGET = {
    "frames_per_clump": 16,
    "geometries_per_clump": 4,
    "atomics_per_clump": 16,
    "lights_per_clump": 16,
    "geometry_vertices": 32_768,
    "geometry_triangles": 32_768,
    "geometry_materials": 64,
    "total_geometry_vertices": 1_100_000,
    "total_geometry_triangles": 800_000,
    "total_geometry_materials": 6_000,
    "bin_meshes": 64,
    "bin_mesh_indices": 65_536,
    "total_bin_mesh_indices": 2_100_000,
    "effects_2d": 64,
    "total_effects_2d": 64,
    "breakable_vertices": 256,
    "breakable_triangles": 512,
    "breakable_materials": 16,
    "total_breakable_vertices": 2_048,
    "total_breakable_triangles": 2_048,
    "total_breakable_materials": 64,
    "native_textures": 5_000,
    "texture_width": 1_024,
    "texture_height": 1_024,
    "texture_levels": 12,
    "texture_gpu_bytes": 1_048_576,
    "total_texture_gpu_bytes": 134_217_728,
    "texture_decoded_bytes": 2_097_152,
    "total_texture_decoded_bytes": 1_073_741_824,
    "renderware_bytes": 268_435_456,
}


class ValidationError(ValueError):
    """A deterministic payload refusal reason."""


def checked_add(left: int, right: int, *, limit: int = UINT64_MAX, what: str) -> int:
    if left < 0 or right < 0 or left > limit - right:
        raise ValidationError(f"{what}: checked addition overflow")
    return left + right


def checked_mul(left: int, right: int, *, limit: int = UINT64_MAX, what: str) -> int:
    if left < 0 or right < 0 or (left and right > limit // left):
        raise ValidationError(f"{what}: checked multiplication overflow")
    return left * right


@dataclass(frozen=True)
class ImgMember:
    name: str
    offset_sector: int
    size_sectors: int
    stream_sectors: int

    @property
    def extension(self) -> str:
        return self.name.rsplit(".", 1)[1]


@dataclass
class RwStats:
    members: Counter[str] = field(default_factory=Counter)
    chunks: Counter[int] = field(default_factory=Counter)
    plugins: Counter[int] = field(default_factory=Counter)
    max_depth: int = 0
    max_nodes: int = 0
    allocation_bytes: int = 0
    geometry_vertices: int = 0
    geometry_triangles: int = 0
    geometry_materials: int = 0
    bin_mesh_indices: int = 0
    effects_2d: int = 0
    breakable_vertices: int = 0
    breakable_triangles: int = 0
    breakable_materials: int = 0
    native_textures: int = 0
    texture_gpu_bytes: int = 0
    texture_decoded_bytes: int = 0


@dataclass
class ColStats:
    records: int = 0
    magics: Counter[str] = field(default_factory=Counter)
    max_record_bytes: int = 0
    bytes: int = 0
    spheres: int = 0
    boxes: int = 0
    lines: int = 0
    vertices: int = 0
    faces: int = 0
    face_groups: int = 0
    max_vertices: int = 0
    max_faces: int = 0
    max_face_groups: int = 0


@dataclass
class ValidationSummary:
    archive_sha256: str
    entries: Counter[str]
    largest_member_blocks: int
    rw: RwStats
    col: ColStats

    def as_dict(self) -> dict[str, object]:
        return {
            "archive_sha256": self.archive_sha256,
            "col": {
                "boxes": self.col.boxes,
                "bytes": self.col.bytes,
                "face_groups": self.col.face_groups,
                "faces": self.col.faces,
                "lines": self.col.lines,
                "magics": dict(sorted(self.col.magics.items())),
                "max_face_groups": self.col.max_face_groups,
                "max_faces": self.col.max_faces,
                "max_record_bytes": self.col.max_record_bytes,
                "max_vertices": self.col.max_vertices,
                "records": self.col.records,
                "spheres": self.col.spheres,
                "vertices": self.col.vertices,
            },
            "entries": dict(sorted(self.entries.items())),
            "largest_member_blocks": self.largest_member_blocks,
            "profile": "bullworth-v1",
            "rw": {
                "allocation_bytes": self.rw.allocation_bytes,
                "bin_mesh_indices": self.rw.bin_mesh_indices,
                "breakable_materials": self.rw.breakable_materials,
                "breakable_triangles": self.rw.breakable_triangles,
                "breakable_vertices": self.rw.breakable_vertices,
                "chunks": {f"0x{key:08X}": value for key, value in sorted(self.rw.chunks.items())},
                "effects_2d": self.rw.effects_2d,
                "geometry_materials": self.rw.geometry_materials,
                "geometry_triangles": self.rw.geometry_triangles,
                "geometry_vertices": self.rw.geometry_vertices,
                "max_depth": self.rw.max_depth,
                "max_nodes_per_member": self.rw.max_nodes,
                "members": dict(sorted(self.rw.members.items())),
                "native_texture_count": self.rw.native_textures,
                "native_texture_decoded_bytes": self.rw.texture_decoded_bytes,
                "native_texture_gpu_bytes": self.rw.texture_gpu_bytes,
                "plugins": {f"0x{key:08X}": value for key, value in sorted(self.rw.plugins.items())},
                "version": f"0x{RW_VERSION:08X}",
            },
            "status": "ok",
        }


def _safe_ascii_name(raw: bytes, *, width: int, kind: str, pattern: re.Pattern[str]) -> str:
    terminator = raw.find(b"\0")
    if terminator < 0:
        raise ValidationError(f"{kind}: unterminated fixed-width name")
    if any(raw[terminator + 1 :]):
        raise ValidationError(f"{kind}: nonzero bytes after name terminator")
    try:
        name = raw[:terminator].decode("ascii")
    except UnicodeDecodeError as error:
        raise ValidationError(f"{kind}: name is not ASCII") from error
    if not name or len(name) >= width or not pattern.fullmatch(name):
        raise ValidationError(f"{kind}: unsafe name {name!r}")
    return name.casefold()


def read_img_directory(data: bytes | memoryview) -> list[ImgMember]:
    view = memoryview(data)
    if len(view) < IMG_HEADER.size:
        raise ValidationError("IMG: truncated header")
    magic, count = IMG_HEADER.unpack_from(view)
    if magic != b"VER2":
        raise ValidationError("IMG: wrong archive magic")
    if count > MAX_IMG_ENTRIES:
        raise ValidationError("IMG: entry count exceeds reviewed cap")
    directory_bytes = checked_mul(count, IMG_ENTRY.size, limit=UINT32_MAX, what="IMG directory")
    directory_end = checked_add(IMG_HEADER.size, directory_bytes, limit=UINT32_MAX, what="IMG directory")
    if directory_end > len(view):
        raise ValidationError("IMG: truncated directory")
    if len(view) % SECTOR_SIZE:
        raise ValidationError("IMG: archive length is not sector aligned")
    directory_sectors = (directory_end + SECTOR_SIZE - 1) // SECTOR_SIZE

    members: list[ImgMember] = []
    names: set[str] = set()
    ranges: list[tuple[int, int, str]] = []
    for index in range(count):
        offset, size, stream_size, raw_name = IMG_ENTRY.unpack_from(view, IMG_HEADER.size + index * IMG_ENTRY.size)
        name = _safe_ascii_name(raw_name, width=24, kind="IMG", pattern=SAFE_IMG_NAME)
        if name in names:
            raise ValidationError(f"IMG: duplicate member name {name}")
        names.add(name)
        if not size or stream_size != size:
            raise ValidationError(f"IMG: invalid size/stream size for {name}")
        if offset < directory_sectors:
            raise ValidationError(f"IMG: member {name} overlaps directory")
        offset_bytes = checked_mul(offset, SECTOR_SIZE, what=f"IMG member {name} offset")
        size_bytes = checked_mul(size, SECTOR_SIZE, what=f"IMG member {name} size")
        end = checked_add(offset_bytes, size_bytes, what=f"IMG member {name} end")
        if end > len(view):
            raise ValidationError(f"IMG: truncated member allocation {name}")
        ranges.append((offset_bytes, end, name))
        members.append(ImgMember(name, offset, size, stream_size))

    previous_end = directory_sectors * SECTOR_SIZE
    for start, end, name in sorted(ranges):
        if start < previous_end:
            raise ValidationError(f"IMG: overlapping member allocation {name}")
        previous_end = end
    return members


def _rw_chunk_end(offset: int, length: int, allocation_end: int, *, what: str) -> int:
    payload = checked_add(offset, RW_HEADER.size, limit=UINT32_MAX, what=what)
    end = checked_add(payload, length, limit=UINT32_MAX, what=what)
    if end > allocation_end:
        raise ValidationError(f"{what}: chunk crosses allocation/container")
    return end


def validate_rw_member(member: bytes | memoryview, extension: str, name: str, aggregate: RwStats | None = None) -> tuple[int, int]:
    view = memoryview(member)
    if len(view) < RW_HEADER.size:
        raise ValidationError(f"RW {name}: truncated root header")
    root_id, root_length, root_version = RW_HEADER.unpack_from(view)
    if root_id != RW_ROOTS[extension]:
        raise ValidationError(f"RW {name}: wrong root chunk 0x{root_id:08X}")
    if root_version != RW_VERSION:
        raise ValidationError(f"RW {name}: wrong root version 0x{root_version:08X}")
    root_end = _rw_chunk_end(0, root_length, len(view), what=f"RW {name} root")
    if any(view[root_end:]):
        raise ValidationError(f"RW {name}: nonzero allocation padding")

    stats = aggregate if aggregate is not None else RwStats()
    archive_nodes = sum(stats.chunks.values())
    member_chunks: Counter[int] = Counter({root_id: 1})
    member_plugins: Counter[int] = Counter()
    nodes = 1
    max_depth = 1
    if archive_nodes + nodes > MAX_RW_NODES:
        raise ValidationError(f"RW {name}: excessive aggregate chunk node count")

    def walk(start: int, end: int, parent: int, depth: int) -> None:
        nonlocal nodes, max_depth
        if depth > MAX_RW_DEPTH:
            raise ValidationError(f"RW {name}: excessive chunk depth")
        max_depth = max(max_depth, depth)
        offset = start
        while offset < end:
            if end - offset < RW_HEADER.size:
                raise ValidationError(f"RW {name}: truncated child header")
            chunk_id, length, version = RW_HEADER.unpack_from(view, offset)
            child_end = _rw_chunk_end(offset, length, end, what=f"RW {name} child")
            if version != RW_VERSION:
                raise ValidationError(f"RW {name}: wrong child version 0x{version:08X}")
            nodes += 1
            if archive_nodes + nodes > MAX_RW_NODES:
                raise ValidationError(f"RW {name}: excessive aggregate chunk node count")
            member_chunks[chunk_id] += 1
            if parent == RW_EXTENSION:
                if chunk_id not in RW_PLUGIN_IDS:
                    raise ValidationError(f"RW {name}: plugin outside closed grammar 0x{chunk_id:08X}")
                member_plugins[chunk_id] += 1
            elif chunk_id in RW_CONTAINERS:
                walk(offset + RW_HEADER.size, child_end, chunk_id, depth + 1)
            elif chunk_id not in RW_LEAVES:
                raise ValidationError(f"RW {name}: unknown core/container chunk 0x{chunk_id:08X}")
            offset = child_end
        if offset != end:
            raise ValidationError(f"RW {name}: child tree does not end exactly")

    walk(RW_HEADER.size, root_end, root_id, 1)
    validate_closed_rw_semantics(view, extension, name, stats)
    stats.members[extension] += 1
    stats.chunks.update(member_chunks)
    stats.plugins.update(member_plugins)
    stats.max_depth = max(stats.max_depth, max_depth)
    stats.max_nodes = max(stats.max_nodes, nodes)
    return nodes, max_depth


@dataclass(frozen=True)
class SemanticChunk:
    chunk_type: int
    begin: int
    payload: int
    end: int

    @property
    def size(self) -> int:
        return self.end - self.payload


def _semantic_chunk(view: memoryview, begin: int, parent_end: int, what: str) -> SemanticChunk:
    if begin < 0 or begin + 12 > parent_end:
        raise ValidationError(f"RW {what}: truncated semantic chunk")
    chunk_type, length, version = RW_HEADER.unpack_from(view, begin)
    end = checked_add(begin + 12, length, limit=UINT32_MAX, what=f"RW {what} chunk")
    if version != RW_VERSION or end > parent_end:
        raise ValidationError(f"RW {what}: semantic chunk version/boundary is invalid")
    return SemanticChunk(chunk_type, begin, begin + 12, end)


def _semantic_children(view: memoryview, parent: SemanticChunk, what: str) -> list[SemanticChunk]:
    result: list[SemanticChunk] = []
    offset = parent.payload
    while offset < parent.end:
        child = _semantic_chunk(view, offset, parent.end, what)
        result.append(child)
        offset = child.end
    if offset != parent.end:
        raise ValidationError(f"RW {what}: semantic container is not consumed exactly")
    return result


def _u32(view: memoryview, offset: int, end: int, what: str) -> int:
    if offset < 0 or offset + 4 > end:
        raise ValidationError(f"RW {what}: truncated u32")
    return struct.unpack_from("<I", view, offset)[0]


def _u16(view: memoryview, offset: int, end: int, what: str) -> int:
    if offset < 0 or offset + 2 > end:
        raise ValidationError(f"RW {what}: truncated u16")
    return struct.unpack_from("<H", view, offset)[0]


def _finite_values(view: memoryview, offset: int, count: int, end: int, what: str) -> tuple[float, ...]:
    bytes_ = checked_mul(count, 4, limit=UINT32_MAX, what=f"RW {what} floats")
    if offset < 0 or offset + bytes_ > end:
        raise ValidationError(f"RW {what}: semantic float array crosses structure")
    values = struct.unpack_from(f"<{count}f", view, offset)
    if any(not math.isfinite(value) or abs(value) > MAX_FLOAT for value in values):
        raise ValidationError(f"RW {what}: non-finite or excessive semantic float")
    return values


def _padded_ascii(view: memoryview, offset: int, size: int, end: int, what: str, allow_empty: bool = False) -> str:
    if offset < 0 or size <= 0 or offset + size > end:
        raise ValidationError(f"RW {what}: truncated fixed ASCII field")
    raw = bytes(view[offset : offset + size])
    terminator = raw.find(b"\0")
    if terminator < 0 or (not allow_empty and terminator == 0) or any(raw[terminator + 1 :]):
        raise ValidationError(f"RW {what}: invalid fixed ASCII padding")
    try:
        text = raw[:terminator].decode("ascii")
    except UnicodeDecodeError as error:
        raise ValidationError(f"RW {what}: non-ASCII fixed string") from error
    if any(ord(character) < 0x20 or ord(character) > 0x7E for character in text):
        raise ValidationError(f"RW {what}: unsafe fixed ASCII string")
    return text.casefold()


def _raw_ascii(view: memoryview, chunk: SemanticChunk, maximum: int, what: str) -> None:
    raw = bytes(view[chunk.payload : chunk.end])
    if not raw or len(raw) > maximum or any(value < 0x20 or value > 0x7E for value in raw):
        raise ValidationError(f"RW {what}: invalid raw ASCII plugin")


def _empty_extension(view: memoryview, chunk: SemanticChunk, what: str) -> None:
    if chunk.chunk_type != 0x03 or _semantic_children(view, chunk, what):
        raise ValidationError(f"RW {what}: extension must be empty")


def _budget_add(stats: RwStats, field: str, amount: int, maximum: int, what: str) -> None:
    value = getattr(stats, field) + amount
    if value > maximum:
        raise ValidationError(f"RW {what}: aggregate budget exceeded")
    setattr(stats, field, value)


def _validate_frame_list(view: memoryview, chunk: SemanticChunk, name: str) -> int:
    children = _semantic_children(view, chunk, name)
    if not children or children[0].chunk_type != 0x01:
        raise ValidationError(f"RW {name}: invalid frame-list grammar")
    count = _u32(view, children[0].payload, children[0].end, name)
    if not count or count > RW_BUDGET["frames_per_clump"] or children[0].size != 4 + count * 56 or len(children) != count + 1:
        raise ValidationError(f"RW {name}: frame count/bounds exceed profile")
    roots = 0
    for index in range(count):
        begin = children[0].payload + 4 + index * 56
        matrix = _finite_values(view, begin, 12, children[0].end, f"{name} frame matrix")
        if any(abs(value) > (10_000.0 if component < 9 else MAX_FLOAT) for component, value in enumerate(matrix)):
            raise ValidationError(f"RW {name}: frame matrix component exceeds profile")
        determinant = matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) - matrix[1] * (
            matrix[3] * matrix[8] - matrix[5] * matrix[6]
        ) + matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6])
        if abs(determinant) < 0.000001 or abs(determinant) > MAX_FLOAT:
            raise ValidationError(f"RW {name}: singular/excessive frame matrix")
        parent, flags = struct.unpack_from("<II", view, begin + 48)
        if flags not in (0, 3, 0x00020003) or (parent != UINT32_MAX and parent >= index):
            raise ValidationError(f"RW {name}: frame parent/flags invalid")
        roots += parent == UINT32_MAX
        extension = children[index + 1]
        plugins = _semantic_children(view, extension, name)
        if extension.chunk_type != 0x03 or len(plugins) != 1 or plugins[0].chunk_type != 0x0253F2FE:
            raise ValidationError(f"RW {name}: NodeName plugin grammar invalid")
        _raw_ascii(view, plugins[0], 23, name)
    if roots != 1:
        raise ValidationError(f"RW {name}: frame tree must have one root")
    return count


def _validate_texture_reference(view: memoryview, chunk: SemanticChunk, name: str) -> None:
    children = _semantic_children(view, chunk, name)
    if len(children) != 4 or [child.chunk_type for child in children] != [0x01, 0x02, 0x02, 0x03] or children[0].size != 4:
        raise ValidationError(f"RW {name}: material texture grammar invalid")
    flags = _u32(view, children[0].payload, children[0].end, name)
    if flags not in (0x00010106, 0x00011106):
        raise ValidationError(f"RW {name}: material texture flags invalid")
    _padded_ascii(view, children[1].payload, children[1].size, children[1].end, name)
    _padded_ascii(view, children[2].payload, children[2].size, children[2].end, name, True)
    _empty_extension(view, children[3], name)


def _validate_material(view: memoryview, chunk: SemanticChunk, name: str) -> None:
    children = _semantic_children(view, chunk, name)
    if len(children) not in (2, 3) or children[0].chunk_type != 0x01 or children[0].size != 28 or children[-1].chunk_type != 0x03:
        raise ValidationError(f"RW {name}: material grammar invalid")
    flags, unused, textured = _u32(view, children[0].payload, children[0].end, name), _u32(
        view, children[0].payload + 8, children[0].end, name
    ), _u32(view, children[0].payload + 12, children[0].end, name)
    _finite_values(view, children[0].payload + 16, 3, children[0].end, f"{name} material surface")
    if flags or unused not in (0, 16688092, 406430684) or textured > 1 or len(children) != textured + 2:
        raise ValidationError(f"RW {name}: material fields invalid")
    if textured:
        if children[1].chunk_type != 0x06:
            raise ValidationError(f"RW {name}: textured material lacks Texture chunk")
        _validate_texture_reference(view, children[1], name)
    plugins = _semantic_children(view, children[-1], name)
    if len(plugins) > 1:
        raise ValidationError(f"RW {name}: too many material plugins")
    if plugins:
        plugin = plugins[0]
        if plugin.chunk_type != 0x0253F2FC or plugin.size != 24:
            raise ValidationError(f"RW {name}: EnvMap plugin invalid")
        _finite_values(view, plugin.payload, 5, plugin.end, f"{name} EnvMap")
        if _u32(view, plugin.payload + 20, plugin.end, name) != 0:
            raise ValidationError(f"RW {name}: EnvMap serialized pointer is nonzero")


def _validate_material_list(view: memoryview, chunk: SemanticChunk, name: str) -> int:
    children = _semantic_children(view, chunk, name)
    if not children or children[0].chunk_type != 0x01:
        raise ValidationError(f"RW {name}: material-list grammar invalid")
    count = _u32(view, children[0].payload, children[0].end, name)
    if not count or count > RW_BUDGET["geometry_materials"] or children[0].size != 4 + count * 4 or len(children) != count + 1:
        raise ValidationError(f"RW {name}: material count/bounds exceed profile")
    for index in range(count):
        if _u32(view, children[0].payload + 4 + index * 4, children[0].end, name) != UINT32_MAX or children[index + 1].chunk_type != 0x07:
            raise ValidationError(f"RW {name}: material reference invalid")
        _validate_material(view, children[index + 1], name)
    return count


def _validate_bin_mesh(view: memoryview, plugin: SemanticChunk, vertices: int, triangles: int, materials: int, name: str, stats: RwStats) -> None:
    if plugin.size < 12:
        raise ValidationError(f"RW {name}: truncated BinMesh plugin")
    flags, meshes, indices = struct.unpack_from("<III", view, plugin.payload)
    if flags != 0 or meshes != materials or meshes > RW_BUDGET["bin_meshes"] or indices > RW_BUDGET["bin_mesh_indices"] or indices != triangles * 3:
        raise ValidationError(f"RW {name}: BinMesh counts/flags invalid")
    cursor, counted, seen = plugin.payload + 12, 0, set()
    for _ in range(meshes):
        mesh_indices, material = struct.unpack_from("<II", view, cursor)
        cursor += 8
        end = cursor + mesh_indices * 4
        if mesh_indices > RW_BUDGET["bin_mesh_indices"] or mesh_indices % 3 or material >= materials or material in seen or end > plugin.end:
            raise ValidationError(f"RW {name}: BinMesh section invalid")
        seen.add(material)
        if any(index >= vertices for index in struct.unpack_from(f"<{mesh_indices}I", view, cursor)):
            raise ValidationError(f"RW {name}: BinMesh vertex index invalid")
        counted += mesh_indices
        cursor = end
    if cursor != plugin.end or counted != indices:
        raise ValidationError(f"RW {name}: BinMesh payload not consumed")
    _budget_add(stats, "bin_mesh_indices", indices, RW_BUDGET["total_bin_mesh_indices"], name)


def _validate_breakable(view: memoryview, plugin: SemanticChunk, name: str, stats: RwStats) -> None:
    section = _u32(view, plugin.payload, plugin.end, name)
    if section == 0:
        if plugin.size != 4:
            raise ValidationError(f"RW {name}: empty Breakable plugin has trailing bytes")
        return
    if section not in (1, 0x64646464) or plugin.size < 56:
        raise ValidationError(f"RW {name}: Breakable header invalid")
    position_rule = _u32(view, plugin.payload + 4, plugin.end, name)
    vertices, padding1 = struct.unpack_from("<HH", view, plugin.payload + 8)
    triangles, padding2 = struct.unpack_from("<HH", view, plugin.payload + 24)
    materials, padding3 = struct.unpack_from("<HH", view, plugin.payload + 36)
    if position_rule > 1 or padding1 or padding2 or padding3 or not vertices or not triangles or not materials or vertices > RW_BUDGET[
        "breakable_vertices"
    ] or triangles > RW_BUDGET["breakable_triangles"] or materials > RW_BUDGET["breakable_materials"]:
        raise ValidationError(f"RW {name}: Breakable counts exceed profile")
    if any(_u32(view, plugin.payload + offset, plugin.end, name) for offset in (12, 16, 20, 28, 32, 40, 44, 48, 52)):
        raise ValidationError(f"RW {name}: Breakable serialized pointer is nonzero")
    if plugin.size != 56 + vertices * 24 + triangles * 8 + materials * 76:
        raise ValidationError(f"RW {name}: Breakable array sizes invalid")
    cursor = plugin.payload + 56
    _finite_values(view, cursor, vertices * 3, plugin.end, f"{name} Breakable positions")
    cursor += vertices * 12
    _finite_values(view, cursor, vertices * 2, plugin.end, f"{name} Breakable UVs")
    cursor += vertices * 12
    for _ in range(triangles):
        if any(index >= vertices for index in struct.unpack_from("<3H", view, cursor)):
            raise ValidationError(f"RW {name}: Breakable vertex index invalid")
        cursor += 6
    for _ in range(triangles):
        if _u16(view, cursor, plugin.end, name) >= materials:
            raise ValidationError(f"RW {name}: Breakable material index invalid")
        cursor += 2
    for _ in range(materials):
        _padded_ascii(view, cursor, 32, plugin.end, name)
        cursor += 32
    for _ in range(materials):
        _padded_ascii(view, cursor, 32, plugin.end, name, True)
        cursor += 32
    _finite_values(view, cursor, materials * 3, plugin.end, f"{name} Breakable materials")
    _budget_add(stats, "breakable_vertices", vertices, RW_BUDGET["total_breakable_vertices"], name)
    _budget_add(stats, "breakable_triangles", triangles, RW_BUDGET["total_breakable_triangles"], name)
    _budget_add(stats, "breakable_materials", materials, RW_BUDGET["total_breakable_materials"], name)


def _terminated_ascii(view: memoryview, offset: int, size: int, end: int, what: str) -> None:
    if offset + size > end:
        raise ValidationError(f"RW {what}: truncated terminated ASCII")
    raw = bytes(view[offset : offset + size])
    terminator = raw.find(b"\0")
    if terminator <= 0 or any(value < 0x20 or value > 0x7E for value in raw[:terminator]):
        raise ValidationError(f"RW {what}: invalid terminated ASCII")


def _validate_2dfx(view: memoryview, plugin: SemanticChunk, name: str, stats: RwStats) -> None:
    effects = _u32(view, plugin.payload, plugin.end, name)
    if not effects or effects > RW_BUDGET["effects_2d"] or plugin.size != 4 + effects * 100:
        raise ValidationError(f"RW {name}: 2DFX count/size invalid")
    for index in range(effects):
        cursor = plugin.payload + 4 + index * 100
        position = _finite_values(view, cursor, 3, plugin.end, f"{name} 2DFX position")
        type_, bytes_ = struct.unpack_from("<II", view, cursor + 12)
        values = _finite_values(view, cursor + 24, 4, plugin.end, f"{name} 2DFX light")
        if any(abs(value) > 100_000 for value in position) or type_ != 0 or bytes_ != 80 or not (
            0 <= values[0] <= 100_000 and 0 <= values[1] <= 100_000 and 0 <= values[2] <= 10_000 and 0 <= values[3] <= 10_000
        ):
            raise ValidationError(f"RW {name}: 2DFX semantic range invalid")
        light = cursor + 20
        flash, reflection, flare, shadow, flags_low = struct.unpack_from("<5B", view, light + 20)
        flags_high = view[light + 74]
        if flash not in (0, 5) or reflection > 1 or flare != 0 or shadow not in (40, 80) or flags_low not in (64, 66, 96) or flags_high not in (
            0,
            4,
        ) or any(view[light + 78 : light + 80]):
            raise ValidationError(f"RW {name}: 2DFX light flags invalid")
        _terminated_ascii(view, light + 25, 24, plugin.end, name)
        _terminated_ascii(view, light + 49, 24, plugin.end, name)
    _budget_add(stats, "effects_2d", effects, RW_BUDGET["total_effects_2d"], name)


def _validate_geometry_plugins(
    view: memoryview, extension: SemanticChunk, vertices: int, triangles: int, materials: int, name: str, stats: RwStats
) -> None:
    plugins = _semantic_children(view, extension, name)
    seen, bin_mesh, breakable = set(), False, False
    for plugin in plugins:
        if plugin.chunk_type in seen:
            raise ValidationError(f"RW {name}: duplicate geometry plugin")
        seen.add(plugin.chunk_type)
        if plugin.chunk_type == 0x050E:
            _validate_bin_mesh(view, plugin, vertices, triangles, materials, name, stats)
            bin_mesh = True
        elif plugin.chunk_type == 0x0253F2F8:
            _validate_2dfx(view, plugin, name, stats)
        elif plugin.chunk_type == 0x0253F2F9:
            if plugin.size != 4 + vertices * 4 or _u32(view, plugin.payload, plugin.end, name) == 0:
                raise ValidationError(f"RW {name}: ExtraVC plugin invalid")
        elif plugin.chunk_type == 0x0253F2FD:
            _validate_breakable(view, plugin, name, stats)
            breakable = True
        else:
            raise ValidationError(f"RW {name}: geometry plugin outside closed grammar")
    if not bin_mesh or not breakable:
        raise ValidationError(f"RW {name}: required BinMesh/Breakable plugin absent")


def _validate_geometry(view: memoryview, chunk: SemanticChunk, name: str, stats: RwStats) -> None:
    children = _semantic_children(view, chunk, name)
    if len(children) != 3 or [child.chunk_type for child in children] != [0x01, 0x08, 0x03] or children[0].size < 40:
        raise ValidationError(f"RW {name}: geometry grammar invalid")
    flags, triangles, vertices, morphs = struct.unpack_from("<4I", view, children[0].payload)
    if flags not in (0x0001002E, 0x00010076, 0x0001007E) or not vertices or vertices > RW_BUDGET["geometry_vertices"] or not triangles or triangles > RW_BUDGET[
        "geometry_triangles"
    ] or morphs != 1:
        raise ValidationError(f"RW {name}: geometry counts/flags invalid")
    _budget_add(stats, "geometry_vertices", vertices, RW_BUDGET["total_geometry_vertices"], name)
    _budget_add(stats, "geometry_triangles", triangles, RW_BUDGET["total_geometry_triangles"], name)
    cursor = children[0].payload + 16 + (vertices * 4 if flags & 8 else 0)
    _finite_values(view, cursor, vertices * 2, children[0].end, f"{name} geometry UV")
    cursor += vertices * 8
    triangle_begin = cursor
    cursor += triangles * 8
    bounds = _finite_values(view, cursor, 4, children[0].end, f"{name} morph bounds")
    has_vertices, has_normals = struct.unpack_from("<II", view, cursor + 16)
    if bounds[3] < 0 or has_vertices != 1 or has_normals != (1 if flags & 0x10 else 0):
        raise ValidationError(f"RW {name}: morph target invalid")
    cursor += 24
    _finite_values(view, cursor, vertices * 3, children[0].end, f"{name} positions")
    cursor += vertices * 12
    if has_normals:
        _finite_values(view, cursor, vertices * 3, children[0].end, f"{name} normals")
        cursor += vertices * 12
    if cursor != children[0].end:
        raise ValidationError(f"RW {name}: geometry struct trailing bytes")
    materials = _validate_material_list(view, children[1], name)
    _budget_add(stats, "geometry_materials", materials, RW_BUDGET["total_geometry_materials"], name)
    for index in range(triangles):
        first, second, material, third = struct.unpack_from("<4H", view, triangle_begin + index * 8)
        if first >= vertices or second >= vertices or third >= vertices or material >= materials:
            raise ValidationError(f"RW {name}: geometry triangle index invalid")
    _validate_geometry_plugins(view, children[2], vertices, triangles, materials, name, stats)


def _validate_geometry_list(view: memoryview, chunk: SemanticChunk, name: str, stats: RwStats) -> int:
    children = _semantic_children(view, chunk, name)
    if not children or children[0].chunk_type != 0x01 or children[0].size != 4:
        raise ValidationError(f"RW {name}: geometry-list grammar invalid")
    count = _u32(view, children[0].payload, children[0].end, name)
    if not count or count > RW_BUDGET["geometries_per_clump"] or len(children) != count + 1:
        raise ValidationError(f"RW {name}: geometry-list count invalid")
    for geometry in children[1:]:
        if geometry.chunk_type != 0x0F:
            raise ValidationError(f"RW {name}: non-geometry in geometry list")
        _validate_geometry(view, geometry, name, stats)
    return count


def _validate_dff(view: memoryview, root: SemanticChunk, name: str, stats: RwStats) -> None:
    children = _semantic_children(view, root, name)
    if len(children) < 4 or children[0].chunk_type != 0x01 or children[0].size != 12 or children[1].chunk_type != 0x0E or children[
        2
    ].chunk_type != 0x1A or children[-1].chunk_type != 0x03:
        raise ValidationError(f"RW {name}: clump grammar invalid")
    _empty_extension(view, children[-1], name)
    atomics, lights, cameras = struct.unpack_from("<III", view, children[0].payload)
    if not atomics or atomics > RW_BUDGET["atomics_per_clump"] or lights > RW_BUDGET["lights_per_clump"] or cameras or len(children) != atomics + lights * 2 + 4:
        raise ValidationError(f"RW {name}: clump counts invalid")
    frames = _validate_frame_list(view, children[1], name)
    geometries = _validate_geometry_list(view, children[2], name, stats)
    child_index = 3
    for _ in range(atomics):
        atomic = children[child_index]
        child_index += 1
        parts = _semantic_children(view, atomic, name)
        if atomic.chunk_type != 0x14 or len(parts) != 2 or parts[0].chunk_type != 0x01 or parts[0].size != 16 or parts[1].chunk_type != 0x03:
            raise ValidationError(f"RW {name}: atomic grammar invalid")
        frame, geometry, flags, unused = struct.unpack_from("<4I", view, parts[0].payload)
        if frame >= frames or geometry >= geometries or flags != 5 or unused != 0:
            raise ValidationError(f"RW {name}: atomic indices/flags invalid")
        _empty_extension(view, parts[1], name)
    for _ in range(lights):
        frame_link = children[child_index]
        child_index += 1
        if frame_link.chunk_type != 0x01 or frame_link.size != 4 or _u32(view, frame_link.payload, frame_link.end, name) >= frames:
            raise ValidationError(f"RW {name}: light frame link invalid")
        light = children[child_index]
        child_index += 1
        parts = _semantic_children(view, light, name)
        if light.chunk_type != 0x12 or len(parts) != 2 or parts[0].chunk_type != 0x01 or parts[0].size != 24 or parts[1].chunk_type != 0x03:
            raise ValidationError(f"RW {name}: light grammar invalid")
        radius, red, green, blue, angle = _finite_values(view, parts[0].payload, 5, parts[0].end, f"{name} light")
        if radius < 0 or any(value < 0 or value > 1 for value in (red, green, blue)) or angle < -1 or angle > 1 or _u32(
            view, parts[0].payload + 20, parts[0].end, name
        ) != 0x00800003:
            raise ValidationError(f"RW {name}: light values/type invalid")
        _empty_extension(view, parts[1], name)
    if child_index != len(children) - 1:
        raise ValidationError(f"RW {name}: clump child ordering invalid")


def _validate_native_texture(view: memoryview, chunk: SemanticChunk, name: str, stats: RwStats) -> str:
    children = _semantic_children(view, chunk, name)
    if len(children) != 2 or children[0].chunk_type != 0x01 or children[0].size < 92 or children[1].chunk_type != 0x03:
        raise ValidationError(f"RW {name}: NativeTexture grammar invalid")
    _empty_extension(view, children[1], name)
    begin = children[0].payload
    platform, filter_ = struct.unpack_from("<II", view, begin)
    texture_name = _padded_ascii(view, begin + 8, 32, children[0].end, name)
    _padded_ascii(view, begin + 40, 32, children[0].end, name, True)
    raster, d3d = struct.unpack_from("<II", view, begin + 72)
    width, height = struct.unpack_from("<HH", view, begin + 80)
    depth, levels, raster_type, flags = struct.unpack_from("<4B", view, begin + 84)
    if platform != 9 or filter_ not in (0x1102, 0x1106) or not width or width > RW_BUDGET["texture_width"] or not height or height > RW_BUDGET[
        "texture_height"
    ] or width & (width - 1) or height & (height - 1) or not levels or levels > RW_BUDGET["texture_levels"] or raster_type != 4 or flags not in (
        8,
        9,
    ) or depth not in (4, 8, 16):
        raise ValidationError(f"RW {name}: NativeTexture header invalid")
    dxt1, dxt5 = d3d == 0x31545844, d3d == 0x35545844
    maximum_levels = max(width, height).bit_length()
    if not (dxt1 or dxt5) or (dxt1 and (raster & 0x7F00) not in (0x0100, 0x0200)) or (dxt5 and (raster & 0x7F00) != 0x0300) or raster & ~0xFF00 or bool(
        raster & 0x8000
    ) != (levels > 1) or levels > maximum_levels or (dxt5 and not flags & 1):
        raise ValidationError(f"RW {name}: NativeTexture DXT/raster flags invalid")
    cursor, gpu, decoded = begin + 88, 0, 0
    for level in range(levels):
        level_width, level_height = max(1, width >> level), max(1, height >> level)
        expected = ((level_width + 3) // 4) * ((level_height + 3) // 4) * (8 if dxt1 else 16)
        serialized = _u32(view, cursor, children[0].end, name)
        cursor += 4
        if serialized != expected or cursor + expected > children[0].end:
            raise ValidationError(f"RW {name}: NativeTexture mip size invalid")
        cursor += expected
        gpu += expected
        decoded += level_width * level_height * 4
    if cursor != children[0].end or gpu > RW_BUDGET["texture_gpu_bytes"] or decoded > RW_BUDGET["texture_decoded_bytes"]:
        raise ValidationError(f"RW {name}: NativeTexture payload/budget invalid")
    stats.native_textures += 1
    if stats.native_textures > RW_BUDGET["native_textures"]:
        raise ValidationError(f"RW {name}: native texture count budget exceeded")
    _budget_add(stats, "texture_gpu_bytes", gpu, RW_BUDGET["total_texture_gpu_bytes"], name)
    _budget_add(stats, "texture_decoded_bytes", decoded, RW_BUDGET["total_texture_decoded_bytes"], name)
    return texture_name


def _validate_txd(view: memoryview, root: SemanticChunk, name: str, stats: RwStats) -> None:
    children = _semantic_children(view, root, name)
    if len(children) < 2 or children[0].chunk_type != 0x01 or children[0].size != 4 or children[-1].chunk_type != 0x03:
        raise ValidationError(f"RW {name}: TXD grammar invalid")
    textures, device = struct.unpack_from("<HH", view, children[0].payload)
    if device != 0 or len(children) != textures + 2:
        raise ValidationError(f"RW {name}: TXD count/device invalid")
    _empty_extension(view, children[-1], name)
    names = set()
    for texture in children[1:-1]:
        if texture.chunk_type != 0x15:
            raise ValidationError(f"RW {name}: non-NativeTexture in dictionary")
        texture_name = _validate_native_texture(view, texture, name, stats)
        if texture_name in names:
            raise ValidationError(f"RW {name}: duplicate case-insensitive texture name")
        names.add(texture_name)


def validate_closed_rw_semantics(view: memoryview, extension: str, name: str, stats: RwStats) -> None:
    root = _semantic_chunk(view, 0, len(view), name)
    if root.chunk_type != RW_ROOTS[extension]:
        raise ValidationError(f"RW {name}: wrong semantic root")
    if extension == "dff":
        _validate_dff(view, root, name, stats)
    else:
        _validate_txd(view, root, name, stats)


def _finite_floats(view: memoryview, offset: int, count: int, *, what: str) -> None:
    size = checked_mul(count, 4, limit=UINT32_MAX, what=what)
    end = checked_add(offset, size, limit=UINT32_MAX, what=what)
    if end > len(view):
        raise ValidationError(f"{what}: float array crosses record")
    if not all(math.isfinite(value) and abs(value) <= MAX_FLOAT for value in struct.unpack_from(f"<{count}f", view, offset)):
        raise ValidationError(f"{what}: NaN/Inf is not allowed")


def _take_counted(view: memoryview, offset: int, count: int, stride: int, end: int, *, what: str) -> int:
    size = checked_mul(count, stride, limit=UINT32_MAX, what=what)
    result = checked_add(offset, size, limit=UINT32_MAX, what=what)
    if result > end:
        raise ValidationError(f"{what}: count crosses record")
    return result


def _validate_coll(record: memoryview, name: str) -> dict[str, int]:
    if len(record) < 72:
        raise ValidationError(f"COLL {name}: truncated bounds")
    _finite_floats(record, 32, 10, what=f"COLL {name} bounds")
    bounds = struct.unpack_from("<10f", record, 32)
    if bounds[0] < 0 or any(bounds[4 + axis] > bounds[7 + axis] for axis in range(3)):
        raise ValidationError(f"COLL {name}: invalid canonical bounds")
    offset = 72

    def count() -> int:
        nonlocal offset
        if offset + 4 > len(record):
            raise ValidationError(f"COLL {name}: truncated count")
        value = struct.unpack_from("<I", record, offset)[0]
        offset += 4
        return value

    spheres = count()
    if spheres > COL_BUDGET["spheres"]:
        raise ValidationError(f"COLL {name}: sphere count exceeds budget")
    sphere_start = offset
    offset = _take_counted(record, offset, spheres, 20, len(record), what=f"COLL {name} spheres")
    for index in range(spheres):
        _finite_floats(record, sphere_start + index * 20, 4, what=f"COLL {name} sphere")
        if struct.unpack_from("<f", record, sphere_start + index * 20)[0] < 0:
            raise ValidationError(f"COLL {name}: negative sphere radius")
    lines = count()
    if lines > COL_BUDGET["lines"]:
        raise ValidationError(f"COLL {name}: line count exceeds budget")
    line_start = offset
    offset = _take_counted(record, offset, lines, 24, len(record), what=f"COLL {name} lines")
    for index in range(lines):
        _finite_floats(record, line_start + index * 24, 6, what=f"COLL {name} line")
    boxes = count()
    if boxes > COL_BUDGET["boxes"]:
        raise ValidationError(f"COLL {name}: box count exceeds budget")
    box_start = offset
    offset = _take_counted(record, offset, boxes, 28, len(record), what=f"COLL {name} boxes")
    for index in range(boxes):
        _finite_floats(record, box_start + index * 28, 6, what=f"COLL {name} box")
        values = struct.unpack_from("<6f", record, box_start + index * 28)
        if any(values[axis] > values[3 + axis] for axis in range(3)):
            raise ValidationError(f"COLL {name}: inverted box")
    vertices = count()
    if vertices > COL_BUDGET["vertices"]:
        raise ValidationError(f"COLL {name}: vertex count exceeds budget")
    vertex_start = offset
    offset = _take_counted(record, offset, vertices, 12, len(record), what=f"COLL {name} vertices")
    for index in range(vertices):
        _finite_floats(record, vertex_start + index * 12, 3, what=f"COLL {name} vertex")
    triangles = count()
    if triangles > COL_BUDGET["faces"]:
        raise ValidationError(f"COLL {name}: face count exceeds budget")
    face_start = offset
    offset = _take_counted(record, offset, triangles, 16, len(record), what=f"COLL {name} triangles")
    for index in range(triangles):
        indices = struct.unpack_from("<III", record, face_start + index * 16)
        if any(vertex >= vertices for vertex in indices):
            raise ValidationError(f"COLL {name}: triangle vertex index out of range")
    if offset != len(record):
        raise ValidationError(f"COLL {name}: trailing bytes after core arrays")
    return {"spheres": spheres, "lines": lines, "boxes": boxes, "vertices": vertices, "faces": triangles, "face_groups": 0}


def _validate_col3(record: memoryview, name: str) -> dict[str, int]:
    if len(record) < 120:
        raise ValidationError(f"COL3 {name}: truncated header")
    _finite_floats(record, 32, 10, what=f"COL3 {name} bounds")
    bounds = struct.unpack_from("<10f", record, 32)
    if bounds[9] < 0 or any(bounds[axis] > bounds[3 + axis] for axis in range(3)):
        raise ValidationError(f"COL3 {name}: invalid canonical bounds")
    spheres, boxes, faces, lines = struct.unpack_from("<HHHB", record, 72)
    flags = struct.unpack_from("<I", record, 80)[0]
    sphere_raw, box_raw, line_raw, vertex_raw, face_raw, plane_raw = struct.unpack_from("<6I", record, 84)
    shadow_faces, shadow_vertex_raw, shadow_face_raw = struct.unpack_from("<3I", record, 108)
    if spheres > COL_BUDGET["spheres"] or boxes > COL_BUDGET["boxes"] or lines > COL_BUDGET["lines"] or faces > COL_BUDGET[
        "faces"
    ] or shadow_faces > COL_BUDGET["shadow_faces"] or flags not in (0, 2, 10) or line_raw or plane_raw or shadow_vertex_raw or shadow_face_raw:
        raise ValidationError(f"COL3 {name}: counts/flags exceed closed profile")
    contents = bool(spheres or boxes or lines or faces or shadow_faces)
    if bool(flags & 2) != contents or (flags & 8 and not faces):
        raise ValidationError(f"COL3 {name}: content flags mismatch")

    cursor = 120

    def consume(count: int, raw: int, stride: int, label: str) -> tuple[int, int]:
        nonlocal cursor
        if not count:
            if raw:
                raise ValidationError(f"COL3 {name}: {label} offset without count")
            return cursor, cursor
        expected = 4 + raw
        if not raw or expected != cursor:
            raise ValidationError(f"COL3 {name}: noncanonical {label} offset")
        begin = cursor
        cursor = _take_counted(record, cursor, count, stride, len(record), what=f"COL3 {name} {label}")
        return begin, cursor

    sphere_begin, _ = consume(spheres, sphere_raw, 20, "spheres")
    box_begin, _ = consume(boxes, box_raw, 28, "boxes")
    if lines or line_raw:
        raise ValidationError(f"COL3 {name}: lines are outside profile")
    for index in range(spheres):
        values = struct.unpack_from("<4f", record, sphere_begin + index * 20)
        _finite_floats(record, sphere_begin + index * 20, 4, what=f"COL3 {name} sphere")
        if values[3] < 0:
            raise ValidationError(f"COL3 {name}: negative sphere radius")
    for index in range(boxes):
        values = struct.unpack_from("<6f", record, box_begin + index * 28)
        _finite_floats(record, box_begin + index * 28, 6, what=f"COL3 {name} box")
        if any(values[axis] > values[3 + axis] for axis in range(3)):
            raise ValidationError(f"COL3 {name}: inverted box")
    if not faces:
        if vertex_raw or face_raw or flags & 8 or cursor != len(record):
            raise ValidationError(f"COL3 {name}: empty face layout is noncanonical")
        return {"spheres": spheres, "boxes": boxes, "lines": lines, "vertices": 0, "faces": 0, "face_groups": 0}
    vertex_begin, face_begin = 4 + vertex_raw, 4 + face_raw
    if not vertex_raw or not face_raw or vertex_begin != cursor or face_begin > len(record):
        raise ValidationError(f"COL3 {name}: vertex/face offsets invalid")
    vertex_end, groups = face_begin, 0
    if flags & 8:
        groups = _u32(record, face_begin - 4, len(record), name)
        if not groups or groups > COL_BUDGET["face_groups"] or groups * 28 + 4 > face_begin - vertex_begin:
            raise ValidationError(f"COL3 {name}: face-group count/bounds invalid")
        vertex_end = face_begin - (groups * 28 + 4)
        previous_last = -1
        for index in range(groups):
            begin = vertex_end + index * 28
            values = struct.unpack_from("<6f", record, begin)
            _finite_floats(record, begin, 6, what=f"COL3 {name} face-group box")
            first, last = struct.unpack_from("<HH", record, begin + 24)
            if any(values[axis] > values[3 + axis] for axis in range(3)) or first != previous_last + 1 or first > last or last >= faces:
                raise ValidationError(f"COL3 {name}: noncanonical face-group coverage")
            previous_last = last
        if previous_last != faces - 1:
            raise ValidationError(f"COL3 {name}: face groups do not cover every face")
    vertex_bytes = vertex_end - vertex_begin
    padding, vertices = vertex_bytes % 6, vertex_bytes // 6
    if not vertices or vertices > COL_BUDGET["vertices"] or padding not in (0, 2) or any(record[vertex_begin + vertices * 6 : vertex_end]):
        raise ValidationError(f"COL3 {name}: vertex array/padding invalid")
    if face_begin + faces * 8 != len(record):
        raise ValidationError(f"COL3 {name}: face array does not consume record")
    for index in range(faces):
        if any(vertex >= vertices for vertex in struct.unpack_from("<3H", record, face_begin + index * 8)):
            raise ValidationError(f"COL3 {name}: triangle vertex index out of range")
    return {"spheres": spheres, "boxes": boxes, "lines": lines, "vertices": vertices, "faces": faces, "face_groups": groups}


def validate_col_member(data: bytes | memoryview, expected_models: Mapping[int, str] | None = None) -> ColStats:
    view = memoryview(data)
    stats = ColStats()
    seen_ids: set[int] = set()
    seen_names: set[str] = set()
    offset = 0
    while offset < len(view):
        if not any(view[offset:]):
            break
        if len(view) - offset < 32:
            raise ValidationError("COL: truncated record header")
        magic = bytes(view[offset : offset + 4])
        if magic not in COL_MAGICS:
            raise ValidationError(f"COL: bad magic {magic!r}")
        payload_size = struct.unpack_from("<I", view, offset + 4)[0]
        total_size = checked_add(payload_size, 8, limit=UINT32_MAX, what="COL record size")
        if payload_size < 24 or total_size > COL_BUFFER_CAPACITY:
            raise ValidationError("COL: invalid size or native buffer overflow")
        end = checked_add(offset, total_size, what="COL record end")
        if end > len(view):
            raise ValidationError("COL: record crosses member allocation")
        raw_name = bytes(view[offset + 8 : offset + 30])
        name = _safe_ascii_name(raw_name, width=22, kind="COL", pattern=SAFE_COL_NAME)
        model_id = struct.unpack_from("<H", view, offset + 30)[0]
        if model_id < MODEL_FIRST or model_id > MODEL_LAST:
            raise ValidationError(f"COL: model ID {model_id} outside Bullworth range")
        if model_id in seen_ids or name in seen_names:
            raise ValidationError("COL: duplicate model ID or name")
        seen_ids.add(model_id)
        seen_names.add(name)
        if expected_models is not None and expected_models.get(model_id, "").casefold() != name:
            raise ValidationError(f"COL: model name mismatch for ID {model_id}")
        record = view[offset:end]
        counts = _validate_coll(record, name) if magic == b"COLL" else _validate_col3(record, name)
        stats.records += 1
        if stats.records > COL_BUDGET["records"]:
            raise ValidationError("COL: record count exceeds aggregate budget")
        stats.magics[magic.decode("ascii")] += 1
        stats.max_record_bytes = max(stats.max_record_bytes, total_size)
        stats.bytes += total_size
        stats.spheres += counts["spheres"]
        stats.boxes += counts["boxes"]
        stats.lines += counts["lines"]
        stats.vertices += counts["vertices"]
        stats.faces += counts["faces"]
        stats.face_groups += counts["face_groups"]
        stats.max_vertices = max(stats.max_vertices, counts["vertices"])
        stats.max_faces = max(stats.max_faces, counts["faces"])
        stats.max_face_groups = max(stats.max_face_groups, counts["face_groups"])
        for field in ("bytes", "spheres", "boxes", "lines", "vertices", "faces", "face_groups"):
            if getattr(stats, field) > COL_BUDGET[f"total_{field}"]:
                raise ValidationError(f"COL: aggregate {field} budget exceeded")
        offset = end
    if any(view[offset:]):
        raise ValidationError("COL: nonzero allocation padding")
    if expected_models is not None and seen_ids != set(expected_models):
        raise ValidationError("COL: record IDs do not exactly match IDE models")
    return stats


def parse_static_ide(text: str) -> tuple[dict[int, str], set[str]]:
    models: dict[int, str] = {}
    txds: set[str] = set()
    section = ""
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line in {"objs", "tobj"}:
            section = line
            continue
        if line == "end":
            section = ""
            continue
        if not section:
            raise ValidationError("IDE: row outside objs/tobj")
        fields = [field.strip() for field in line.split(",")]
        if len(fields) != (6 if section == "objs" else 8):
            raise ValidationError("IDE: malformed row")
        try:
            model_id = int(fields[0], 10)
        except ValueError as error:
            raise ValidationError("IDE: invalid model ID") from error
        if model_id < MODEL_FIRST or model_id > MODEL_LAST or model_id in models:
            raise ValidationError("IDE: duplicate or out-of-range model ID")
        if not SAFE_COL_NAME.fullmatch(fields[1]) or not SAFE_COL_NAME.fullmatch(fields[2]):
            raise ValidationError("IDE: unsafe model/TXD name")
        models[model_id] = fields[1].casefold()
        txds.add(fields[2].casefold())
    return models, txds


def validate_archive_bytes(data: bytes, *, ide_text: str | None = None, enforce_profile: bool = False) -> ValidationSummary:
    members = read_img_directory(data)
    expected_models: dict[int, str] | None = None
    expected_txds: set[str] | None = None
    if ide_text is not None:
        expected_models, expected_txds = parse_static_ide(ide_text)
    by_extension = Counter(member.extension for member in members)
    rw_stats = RwStats()
    col_stats = ColStats()
    names = {member.name for member in members}
    for member in members:
        start = member.offset_sector * SECTOR_SIZE
        end = start + member.size_sectors * SECTOR_SIZE
        payload = memoryview(data)[start:end]
        if member.extension in RW_ROOTS:
            rw_stats.allocation_bytes += len(payload)
            if rw_stats.allocation_bytes > RW_BUDGET["renderware_bytes"]:
                raise ValidationError("RW: aggregate allocation-byte budget exceeded")
            validate_rw_member(payload, member.extension, member.name, rw_stats)
        elif member.extension == "col":
            if member.name != "bw.col":
                raise ValidationError(f"profile: unexpected COL member {member.name}")
            col_stats = validate_col_member(payload, expected_models)

    digest = hashlib.sha256(data).hexdigest()
    if enforce_profile:
        if digest != PROFILE_IMG_SHA256:
            raise ValidationError("profile: IMG SHA-256 mismatch")
        if by_extension != Counter({"dff": 952, "txd": 166, "col": 1, "ipl": 7}):
            raise ValidationError("profile: IMG member counts differ from Bullworth v1")
        if max(member.size_sectors for member in members) != 4007:
            raise ValidationError("profile: largest member is not 4007 blocks")
        if expected_models is None or expected_txds is None or set(expected_models) != set(range(MODEL_FIRST, MODEL_LAST + 1)):
            raise ValidationError("profile: IDE does not cover the compact model range")
        if {f"{name}.dff" for name in expected_models.values()} != {name for name in names if name.endswith(".dff")}:
            raise ValidationError("profile: DFF names do not match IDE")
        if {f"{name}.txd" for name in expected_txds} != {name for name in names if name.endswith(".txd")}:
            raise ValidationError("profile: TXD names do not match IDE")
        if {name for name in names if name.endswith(".ipl")} != PROFILE_IPLS:
            raise ValidationError("profile: IPL names differ from Bullworth v1")
    return ValidationSummary(digest, by_extension, max(member.size_sectors for member in members), rw_stats, col_stats)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--img", type=Path, required=True)
    parser.add_argument("--ide", type=Path)
    parser.add_argument("--profile", choices=("bullworth-v1", "structural"), default="bullworth-v1")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    data = args.img.read_bytes()
    ide_text = args.ide.read_text(encoding="ascii") if args.ide else None
    summary = validate_archive_bytes(data, ide_text=ide_text, enforce_profile=args.profile == "bullworth-v1")
    result = summary.as_dict()
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(
            "native world payload OK: "
            f"entries={sum(summary.entries.values())} dff={summary.entries['dff']} txd={summary.entries['txd']} "
            f"colRecords={summary.col.records} rwDepth={summary.rw.max_depth} rwNodes={summary.rw.max_nodes} "
            f"largestBlocks={summary.largest_member_blocks} sha256={summary.archive_sha256}"
        )


if __name__ == "__main__":
    main()
