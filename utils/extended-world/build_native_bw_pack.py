#!/usr/bin/env python3
"""Build and round-trip validate a native GTA SA Bullworth streaming pack.

The input is the deterministic ``ug-bw`` resource emitted by
``build_ug_map.py``.  This tool deliberately keeps the runtime experiment out
of the resource: it produces ordinary IDE/IMG/COL/binary-IPL artifacts in a
caller-selected directory so the native loader can be integrated separately.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

from build_ug_map import (
    COL_MAGICS,
    COL_MODEL_ID_OFFSET,
    COL_MODEL_NAME_OFFSET,
    COL_MODEL_NAME_SIZE,
    ModelDefinition,
    Placement,
    parse_col_file_header,
    parse_ide,
)
from pack_img import DIRECTORY_ENTRY, HEADER, SECTOR_SIZE, sectors_for, write_padding
from native_world_manifest import build_runtime_manifest, dump_runtime_manifest


MODEL_ID_START = 18631
MODEL_ID_END = 19582
EXPECTED_MODELS = MODEL_ID_END - MODEL_ID_START + 1
EXPECTED_PLACEMENTS = 2962
EXPECTED_IPLS = 7
NATIVE_COL_BUFFER_CAPACITY = 327_680
RW_LIBRARY_ID = 0x1803FFFF
EXPECTED_DFF_UV_NORMALIZATIONS = {
    "40199.dff": 15,
    "40204.dff": 48,
    "40231.dff": 18,
    "40293.dff": 4,
    "40772.dff": 4,
    "40813.dff": 6,
    "41052.dff": 72,
    "41075.dff": 16,
    "41096.dff": 16,
}
EXPECTED_TXD_DUPLICATE_REMOVALS = {"0064.txd": 1}

# These are the audited stock 1.0 US occupied counts. Keeping the exact
# requirements beside the padded targets makes the runtime patch reviewable
# before any hook is added.
MODEL_STORE_STOCK_OCCUPIED = {
    "object": 13984,
    "object-damageable": 69,
    "timed-object": 160,
}
MODEL_STORE_PADDED_TARGETS = {
    "object": 15000,
    "object-damageable": 160,
    "timed-object": 200,
}
POOL_CAPACITIES = {"model_ids": 20000, "txd_slots": 5000, "col_slots": 255, "ipl_slots": 256}
# The standalone stock inventory contains 3607 TXDs. MTA's initialized runtime
# also registers stock cutscene TXD preq_cargo.txd, so its baseline is 3608. These
# values create a reviewable snapshot only; the registrar simulates CPool from
# its actual occupancy map and first-free cursor before loading the pack.
AUDITED_STANDALONE_TXD_SLOTS = 3607
AUDITED_MTA_RUNTIME_TXD_SLOTS = 3608
STOCK_IMG_PATHS = (
    "models/gta3.img",
    "models/gta_int.img",
    "models/player.img",
    "models/cutscene.img",
)
IDE_MODEL_SECTIONS = {"objs", "tobj", "anim", "cars", "peds", "weap", "hier"}

NUMBER = r"-?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"
FIELD_PATTERN = re.compile(rf"(\w+)\s*=\s*(\"(?:\\.|[^\"])*\"|false|true|{NUMBER})")
MODEL_LINE = re.compile(r"^\s*\[(\d+)\]\s*=\s*\{(.*)\},\s*$")
PLACEMENT_LINE = re.compile(r"^\s*\{\s*model\s*=\s*(\d+),(.*)\},\s*$")
BINARY_IPL_HEADER_SIZE = 0x4C
BINARY_IPL_INSTANCE = struct.Struct("<7fiIi")


@dataclass(frozen=True)
class ResourceModel:
    definition: ModelDefinition
    txd_path: str
    dff_path: str
    col_path: str


@dataclass(frozen=True)
class ImgEntry:
    name: str
    offset_sector: int
    size_sectors: int
    stream_sectors: int


@dataclass(frozen=True)
class RwChunk:
    chunk_type: int
    begin: int
    payload_begin: int
    end: int


@dataclass(frozen=True)
class ArchiveInput:
    name: str
    path: Path | None = None
    source_offset: int = 0
    size: int = 0
    data: bytes | None = None

    def byte_size(self) -> int:
        return len(self.data) if self.data is not None else self.size


def decode_lua_value(value: str) -> object:
    if value == "false":
        return False
    if value == "true":
        return True
    if value.startswith('"'):
        # build_ug_map.py emits the same escaping needed by JSON strings.
        return json.loads(value)
    if any(marker in value for marker in ".eE"):
        return float(value)
    return int(value)


def parse_fields(text: str) -> dict[str, object]:
    return {match.group(1): decode_lua_value(match.group(2)) for match in FIELD_PATTERN.finditer(text)}


def require_fields(fields: dict[str, object], required: set[str], context: str) -> None:
    missing = sorted(required - fields.keys())
    if missing:
        raise ValueError(f"{context} is missing fields: {', '.join(missing)}")


def parse_generated_map(path: Path) -> tuple[list[ResourceModel], list[Placement]]:
    """Parse only build_ug_map.py's generated schema, rejecting drift."""

    models: list[ResourceModel] = []
    placements: list[Placement] = []
    seen_model_ids: set[int] = set()
    in_models = False
    in_placements = False
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if line == "UG_BW_MODELS = {":
            in_models = True
            continue
        if line == "UG_BW_PLACEMENTS = {":
            in_placements = True
            continue
        if line == "}" and in_models:
            in_models = False
            continue
        if line == "}" and in_placements:
            in_placements = False
            continue

        if in_models:
            match = MODEL_LINE.match(line)
            if not match:
                raise ValueError(f"unrecognized model row at {path}:{line_number}")
            source_id = int(match.group(1))
            fields = parse_fields(match.group(2))
            require_fields(
                fields,
                {"name", "modelType", "txd", "dff", "col", "lodDistance", "ideFlags"},
                f"model {source_id}",
            )
            if source_id in seen_model_ids:
                raise ValueError(f"duplicate source model ID {source_id}")
            if not all(isinstance(fields[key], str) for key in ("name", "modelType", "txd", "dff", "col")):
                raise ValueError(f"model {source_id} does not have all native-pack assets")
            seen_model_ids.add(source_id)
            definition = ModelDefinition(
                source_id=source_id,
                name=str(fields["name"]),
                txd=Path(str(fields["txd"])).stem,
                lod_distance=float(fields["lodDistance"]),
                ide_flags=int(fields["ideFlags"]),
                model_type=str(fields["modelType"]),
                time_on=int(fields["timeOn"]) if "timeOn" in fields else None,
                time_off=int(fields["timeOff"]) if "timeOff" in fields else None,
            )
            models.append(
                ResourceModel(
                    definition=definition,
                    txd_path=str(fields["txd"]),
                    dff_path=str(fields["dff"]),
                    col_path=str(fields["col"]),
                )
            )
        elif in_placements:
            match = PLACEMENT_LINE.match(line)
            if not match:
                raise ValueError(f"unrecognized placement row at {path}:{line_number}")
            source_id = int(match.group(1))
            fields = parse_fields(match.group(2))
            require_fields(
                fields,
                {"x", "y", "z", "qx", "qy", "qz", "qw", "source", "sourceIndex"},
                f"placement {len(placements)}",
            )
            placements.append(
                Placement(
                    source_id=source_id,
                    name="",
                    area=0,
                    x=float(fields["x"]),
                    y=float(fields["y"]),
                    z=float(fields["z"]),
                    qx=float(fields["qx"]),
                    qy=float(fields["qy"]),
                    qz=float(fields["qz"]),
                    qw=float(fields["qw"]),
                    lod_index=-1,
                    source_file=str(fields["source"]),
                    source_index=int(fields["sourceIndex"]),
                )
            )

    models.sort(key=lambda model: model.definition.source_id)
    if len(models) != EXPECTED_MODELS:
        raise ValueError(f"expected {EXPECTED_MODELS} Bullworth models, found {len(models)}")
    if len(placements) != EXPECTED_PLACEMENTS:
        raise ValueError(f"expected {EXPECTED_PLACEMENTS} Bullworth placements, found {len(placements)}")
    if set(placement.source_id for placement in placements) != seen_model_ids:
        missing = sorted(set(placement.source_id for placement in placements) - seen_model_ids)
        unused = sorted(seen_model_ids - set(placement.source_id for placement in placements))
        raise ValueError(f"placement/model cross-reference drift: missing={missing[:10]} unused={unused[:10]}")
    groups = {placement.source_file.casefold() for placement in placements}
    if len(groups) != EXPECTED_IPLS:
        raise ValueError(f"expected {EXPECTED_IPLS} source IPL groups, found {sorted(groups)}")
    return models, placements


def read_img_directory(path: Path) -> list[ImgEntry]:
    with path.open("rb") as stream:
        magic, count = HEADER.unpack(stream.read(HEADER.size))
        if magic != b"VER2":
            raise ValueError(f"{path} is not an IMG VER2 archive")
        entries: list[ImgEntry] = []
        names: set[str] = set()
        for _ in range(count):
            raw = stream.read(DIRECTORY_ENTRY.size)
            if len(raw) != DIRECTORY_ENTRY.size:
                raise ValueError(f"truncated IMG directory in {path}")
            offset, size, stream_size, encoded_name = DIRECTORY_ENTRY.unpack(raw)
            name = encoded_name.split(b"\0", 1)[0].decode("ascii").casefold()
            if not name or name in names:
                raise ValueError(f"empty or duplicate IMG entry {name!r} in {path}")
            names.add(name)
            entries.append(ImgEntry(name, offset, size, stream_size))
    archive_sectors = sectors_for(path.stat().st_size)
    directory_sectors = sectors_for(HEADER.size + count * DIRECTORY_ENTRY.size)
    previous_end = directory_sectors
    for entry in sorted(entries, key=lambda item: item.offset_sector):
        if entry.offset_sector < previous_end or entry.offset_sector + entry.size_sectors > archive_sectors:
            raise ValueError(f"invalid or overlapping IMG entry {entry.name} in {path}")
        previous_end = entry.offset_sector + entry.size_sectors
    return entries


def entry_index(path: Path) -> dict[str, ImgEntry]:
    return {entry.name: entry for entry in read_img_directory(path)}


def read_archive_input(source: ArchiveInput, output: BinaryIO) -> None:
    if source.data is not None:
        output.write(source.data)
        return
    if source.path is None:
        raise RuntimeError(f"archive input {source.name} has no source")
    remaining = source.size
    with source.path.open("rb") as stream:
        stream.seek(source.source_offset)
        while remaining:
            block = stream.read(min(1024 * 1024, remaining))
            if not block:
                raise ValueError(f"truncated source data for {source.name}")
            output.write(block)
            remaining -= len(block)


def pack_inputs(path: Path, sources: list[ArchiveInput]) -> list[ImgEntry]:
    if not sources:
        raise ValueError("native archive has no inputs")
    sources = sorted(sources, key=lambda source: source.name.casefold())
    names = [source.name.casefold() for source in sources]
    duplicates = sorted(name for name, count in Counter(names).items() if count > 1)
    if duplicates:
        raise ValueError(f"duplicate native archive names: {duplicates}")

    next_sector = sectors_for(HEADER.size + len(sources) * DIRECTORY_ENTRY.size)
    entries: list[ImgEntry] = []
    for source in sources:
        encoded_name = source.name.casefold().encode("ascii")
        if len(encoded_name) > 23:
            raise ValueError(f"IMG entry name exceeds 23 bytes: {source.name}")
        size_sectors = sectors_for(source.byte_size())
        if not size_sectors or size_sectors > 0xFFFF:
            raise ValueError(f"invalid IMG entry size for {source.name}: {source.byte_size()}")
        entries.append(ImgEntry(source.name.casefold(), next_sector, size_sectors, size_sectors))
        next_sector += size_sectors

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as output:
        output.write(HEADER.pack(b"VER2", len(entries)))
        for entry in entries:
            name = entry.name.encode("ascii").ljust(24, b"\0")
            output.write(
                DIRECTORY_ENTRY.pack(entry.offset_sector, entry.size_sectors, entry.stream_sectors, name)
            )
        write_padding(output, output.tell())
        for entry, source in zip(entries, sources):
            if output.tell() != entry.offset_sector * SECTOR_SIZE:
                raise RuntimeError(f"archive offset mismatch for {entry.name}")
            before = output.tell()
            read_archive_input(source, output)
            write_padding(output, output.tell() - before)
    return entries


def parse_col_records(data: bytes) -> list[tuple[str, int, int, int]]:
    records: list[tuple[str, int, int, int]] = []
    offset = 0
    while offset < len(data):
        if not any(data[offset:]):
            break
        if offset + 32 > len(data) or data[offset : offset + 4] not in COL_MAGICS:
            raise ValueError(f"invalid COL record at byte {offset}")
        payload_size = struct.unpack_from("<I", data, offset + 4)[0]
        end = offset + 8 + payload_size
        if payload_size < 24 or end > len(data):
            raise ValueError(f"invalid COL record size at byte {offset}")
        name, model_id = parse_col_file_header(data[offset:end], f"record at byte {offset}")
        records.append((name, model_id, offset, end - offset))
        offset = end
    return records


def remap_col_record(data: bytes, native_id: int, native_name: str) -> bytes:
    # The extracted Bullworth source records carry Bully-specific metadata in
    # GTA's last four FileHeader bytes. This function is the conversion
    # boundary: validate the single record structurally, then replace the full
    # native name/ID area before applying the strict GTA parser below.
    if len(data) < 32 or data[:4] not in COL_MAGICS:
        raise ValueError("expected one complete COL record")
    payload_size = struct.unpack_from("<I", data, 4)[0]
    record_size = 8 + payload_size
    if payload_size < 24 or record_size > len(data) or any(data[record_size:]):
        raise ValueError("expected one complete COL record")
    encoded_name = native_name.encode("ascii")
    if len(encoded_name) >= COL_MODEL_NAME_SIZE:
        raise ValueError(f"COL model name exceeds {COL_MODEL_NAME_SIZE - 1} bytes: {native_name}")
    if not native_name or any(not (character.isalnum() or character == "_") for character in native_name):
        raise ValueError(f"unsafe COL model name: {native_name!r}")
    remapped = bytearray(data[:record_size])
    remapped[COL_MODEL_NAME_OFFSET:COL_MODEL_ID_OFFSET] = encoded_name.ljust(COL_MODEL_NAME_SIZE, b"\0")
    struct.pack_into("<H", remapped, COL_MODEL_ID_OFFSET, native_id)
    result = bytes(remapped)
    parsed = parse_col_records(result)
    if parsed != [(native_name.casefold(), native_id, 0, record_size)]:
        raise ValueError("remapped COL FileHeader did not round-trip")
    return result


def _rw_chunk(data: bytes | bytearray, begin: int, parent_end: int, context: str) -> RwChunk:
    if begin < 0 or begin + 12 > parent_end or parent_end > len(data):
        raise ValueError(f"{context}: truncated RenderWare chunk header")
    chunk_type, payload_bytes, library_id = struct.unpack_from("<III", data, begin)
    end = begin + 12 + payload_bytes
    if library_id != RW_LIBRARY_ID or end > parent_end:
        raise ValueError(f"{context}: invalid RenderWare library ID or boundary")
    return RwChunk(chunk_type, begin, begin + 12, end)


def _rw_children(data: bytes | bytearray, parent: RwChunk, context: str) -> list[RwChunk]:
    children: list[RwChunk] = []
    offset = parent.payload_begin
    while offset < parent.end:
        child = _rw_chunk(data, offset, parent.end, context)
        children.append(child)
        offset = child.end
    if offset != parent.end:
        raise ValueError(f"{context}: RenderWare children do not consume their container")
    return children


def _finite_semantic_floats(
    data: bytearray,
    begin: int,
    count: int,
    limit: int,
    context: str,
    *,
    normalize: bool = False,
) -> list[int]:
    end = begin + count * 4
    if begin < 0 or end > limit:
        raise ValueError(f"{context}: semantic float array crosses its reviewed structure")
    changed: list[int] = []
    for offset in range(begin, end, 4):
        value = struct.unpack_from("<f", data, offset)[0]
        if math.isfinite(value):
            continue
        if not normalize:
            raise ValueError(f"{context}: non-finite semantic float at byte {offset}")
        struct.pack_into("<I", data, offset, 0)
        changed.append(offset)
    return changed


def _scan_material_floats(data: bytearray, material: RwChunk, context: str) -> int:
    children = _rw_children(data, material, context)
    if len(children) not in (2, 3) or children[0].chunk_type != 0x01 or children[0].end - children[0].payload_begin != 28:
        raise ValueError(f"{context}: material grammar differs from the reviewed profile")
    semantic = 3
    _finite_semantic_floats(data, children[0].payload_begin + 16, 3, children[0].end, f"{context} surface properties")
    extension = children[-1]
    if extension.chunk_type != 0x03:
        raise ValueError(f"{context}: material extension is absent")
    for plugin in _rw_children(data, extension, context):
        if plugin.chunk_type != 0x0253F2FC or plugin.end - plugin.payload_begin != 24:
            raise ValueError(f"{context}: material plugin differs from the reviewed profile")
        _finite_semantic_floats(data, plugin.payload_begin, 5, plugin.end, f"{context} material plugin")
        semantic += 5
    return semantic


def _scan_geometry_plugin_floats(data: bytearray, extension: RwChunk, context: str) -> int:
    semantic = 0
    for plugin in _rw_children(data, extension, context):
        payload_bytes = plugin.end - plugin.payload_begin
        if plugin.chunk_type == 0x0253F2F8:
            if payload_bytes < 4:
                raise ValueError(f"{context}: truncated 2DFX plugin")
            effects = struct.unpack_from("<I", data, plugin.payload_begin)[0]
            if payload_bytes != 4 + effects * 100:
                raise ValueError(f"{context}: invalid 2DFX record size")
            for index in range(effects):
                effect = plugin.payload_begin + 4 + index * 100
                _finite_semantic_floats(data, effect, 3, plugin.end, f"{context} 2DFX position")
                _finite_semantic_floats(data, effect + 24, 4, plugin.end, f"{context} 2DFX light")
                semantic += 7
        elif plugin.chunk_type == 0x0253F2FD:
            if payload_bytes < 4:
                raise ValueError(f"{context}: truncated breakable plugin")
            section = struct.unpack_from("<I", data, plugin.payload_begin)[0]
            if section == 0:
                if payload_bytes != 4:
                    raise ValueError(f"{context}: invalid empty breakable plugin")
                continue
            if section not in (1, 0x64646464) or payload_bytes < 56:
                raise ValueError(f"{context}: invalid breakable plugin header")
            vertices = struct.unpack_from("<H", data, plugin.payload_begin + 8)[0]
            triangles = struct.unpack_from("<H", data, plugin.payload_begin + 24)[0]
            materials = struct.unpack_from("<H", data, plugin.payload_begin + 36)[0]
            expected = 56 + vertices * 24 + triangles * 8 + materials * 76
            if payload_bytes != expected:
                raise ValueError(f"{context}: invalid breakable plugin arrays")
            cursor = plugin.payload_begin + 56
            _finite_semantic_floats(data, cursor, vertices * 3, plugin.end, f"{context} breakable positions")
            semantic += vertices * 3
            cursor += vertices * 12
            _finite_semantic_floats(data, cursor, vertices * 2, plugin.end, f"{context} breakable UVs")
            semantic += vertices * 2
            cursor += vertices * 12 + triangles * 8 + materials * 64
            _finite_semantic_floats(data, cursor, materials * 3, plugin.end, f"{context} breakable materials")
            semantic += materials * 3
        elif plugin.chunk_type not in (0x0000050E, 0x0253F2F9):
            raise ValueError(f"{context}: unknown geometry plugin 0x{plugin.chunk_type:08X}")
    return semantic


def normalize_static_dff_semantic_floats(data: bytes, name: str, *, normalize_uv: bool) -> tuple[bytes, dict[str, object]]:
    """Normalize only non-finite geometry UVs and reject every other semantic float.

    Offsets come from the closed Bullworth v1 RenderWare grammar, never from a
    byte-pattern search. This keeps integer fields and opaque plugin bytes out
    of the rewrite surface.
    """

    output = bytearray(data)
    root = _rw_chunk(output, 0, len(output), name)
    if root.chunk_type != 0x10 or any(output[root.end:]):
        raise ValueError(f"{name}: invalid DFF root or nonzero sector padding")
    children = _rw_children(output, root, name)
    if len(children) < 4 or children[0].chunk_type != 0x01 or children[1].chunk_type != 0x0E or children[2].chunk_type != 0x1A:
        raise ValueError(f"{name}: clump grammar differs from the reviewed profile")

    semantic = 0
    frame_children = _rw_children(output, children[1], f"{name} frame list")
    if not frame_children or frame_children[0].chunk_type != 0x01:
        raise ValueError(f"{name}: frame-list struct is absent")
    frame_count = struct.unpack_from("<I", output, frame_children[0].payload_begin)[0]
    if frame_children[0].end - frame_children[0].payload_begin != 4 + frame_count * 56:
        raise ValueError(f"{name}: frame-list array has invalid bounds")
    for index in range(frame_count):
        begin = frame_children[0].payload_begin + 4 + index * 56
        _finite_semantic_floats(output, begin, 12, frame_children[0].end, f"{name} frame matrix {index}")
        semantic += 12

    geometry_children = _rw_children(output, children[2], f"{name} geometry list")
    if not geometry_children or geometry_children[0].chunk_type != 0x01 or geometry_children[0].end - geometry_children[0].payload_begin != 4:
        raise ValueError(f"{name}: geometry-list struct is invalid")
    geometry_count = struct.unpack_from("<I", output, geometry_children[0].payload_begin)[0]
    if len(geometry_children) != geometry_count + 1:
        raise ValueError(f"{name}: geometry-list count differs from its chunks")

    normalized_offsets: list[int] = []
    for geometry_index, geometry in enumerate(geometry_children[1:]):
        if geometry.chunk_type != 0x0F:
            raise ValueError(f"{name}: non-geometry chunk in geometry list")
        parts = _rw_children(output, geometry, f"{name} geometry {geometry_index}")
        if len(parts) != 3 or [part.chunk_type for part in parts] != [0x01, 0x08, 0x03]:
            raise ValueError(f"{name}: geometry grammar differs from the reviewed profile")
        structure = parts[0]
        if structure.end - structure.payload_begin < 40:
            raise ValueError(f"{name}: truncated geometry struct")
        flags, triangles, vertices, morph_targets = struct.unpack_from("<4I", output, structure.payload_begin)
        if flags not in (0x0001002E, 0x00010076, 0x0001007E) or not vertices or not triangles or morph_targets != 1:
            raise ValueError(f"{name}: geometry counts/flags differ from the reviewed profile")
        cursor = structure.payload_begin + 16
        if flags & 8:
            cursor += vertices * 4
        uv_offsets = _finite_semantic_floats(
            output,
            cursor,
            vertices * 2,
            structure.end,
            f"{name} geometry {geometry_index} UV",
            normalize=normalize_uv,
        )
        normalized_offsets.extend(uv_offsets)
        semantic += vertices * 2
        cursor += vertices * 8 + triangles * 8
        _finite_semantic_floats(output, cursor, 4, structure.end, f"{name} geometry {geometry_index} morph bounds")
        semantic += 4
        has_vertices, has_normals = struct.unpack_from("<II", output, cursor + 16)
        if has_vertices != 1 or has_normals != (1 if flags & 0x10 else 0):
            raise ValueError(f"{name}: geometry morph flags differ from the reviewed profile")
        cursor += 24
        _finite_semantic_floats(output, cursor, vertices * 3, structure.end, f"{name} geometry {geometry_index} positions")
        semantic += vertices * 3
        cursor += vertices * 12
        if has_normals:
            _finite_semantic_floats(output, cursor, vertices * 3, structure.end, f"{name} geometry {geometry_index} normals")
            semantic += vertices * 3
            cursor += vertices * 12
        if cursor != structure.end:
            raise ValueError(f"{name}: geometry struct has unreviewed trailing bytes")

        materials = _rw_children(output, parts[1], f"{name} material list")
        if not materials or materials[0].chunk_type != 0x01:
            raise ValueError(f"{name}: material-list struct is absent")
        material_count = struct.unpack_from("<I", output, materials[0].payload_begin)[0]
        if len(materials) != material_count + 1:
            raise ValueError(f"{name}: material-list count differs from its chunks")
        for material_index, material in enumerate(materials[1:]):
            if material.chunk_type != 0x07:
                raise ValueError(f"{name}: non-material chunk in material list")
            semantic += _scan_material_floats(output, material, f"{name} material {material_index}")
        semantic += _scan_geometry_plugin_floats(output, parts[2], f"{name} geometry {geometry_index}")

    for child in children[3:-1]:
        if child.chunk_type != 0x12:
            continue
        light = _rw_children(output, child, f"{name} light")
        if not light or light[0].chunk_type != 0x01 or light[0].end - light[0].payload_begin != 24:
            raise ValueError(f"{name}: light struct differs from the reviewed profile")
        _finite_semantic_floats(output, light[0].payload_begin, 4, light[0].end, f"{name} light color/radius")
        _finite_semantic_floats(output, light[0].payload_begin + 16, 1, light[0].end, f"{name} light angle")
        semantic += 5

    return bytes(output), {
        "normalized_uv_count": len(normalized_offsets),
        "normalized_uv_byte_offsets": normalized_offsets,
        "semantic_float_count": semantic,
    }


def validate_static_txd_float_grammar(data: bytes, name: str) -> None:
    """Prove that the reviewed D3D9 native-TXD grammar has no float fields."""

    root = _rw_chunk(data, 0, len(data), name)
    if root.chunk_type != 0x16 or any(data[root.end:]):
        raise ValueError(f"{name}: invalid TXD root or nonzero sector padding")
    children = _rw_children(data, root, name)
    if len(children) < 2 or children[0].chunk_type != 0x01 or children[-1].chunk_type != 0x03:
        raise ValueError(f"{name}: TXD grammar differs from the reviewed profile")
    textures = struct.unpack_from("<H", data, children[0].payload_begin)[0]
    if len(children) != textures + 2 or _rw_children(data, children[-1], name):
        raise ValueError(f"{name}: TXD count or root extension differs from the reviewed profile")
    for texture in children[1:-1]:
        texture_children = _rw_children(data, texture, name)
        if texture.chunk_type != 0x15 or len(texture_children) != 2 or texture_children[0].chunk_type != 0x01 or texture_children[1].chunk_type != 0x03:
            raise ValueError(f"{name}: native texture grammar differs from the reviewed profile")
        if texture_children[0].end - texture_children[0].payload_begin < 92 or _rw_children(data, texture_children[1], name):
            raise ValueError(f"{name}: native texture header/extension differs from the reviewed profile")


def normalize_static_txd_duplicate_names(
    data: bytes, name: str, *, drop_identical_duplicates: bool
) -> tuple[bytes, dict[str, object]]:
    """Remove only later case-insensitive duplicates with identical payloads.

    GTA resolves texture names case-insensitively. A later duplicate is safe
    to remove only when its complete NativeTexture chunk matches the first
    after canonicalizing the fixed name field; any semantic difference is a
    hard refusal rather than an implicit first/last-wins choice.
    """

    validate_static_txd_float_grammar(data, name)
    root = _rw_chunk(data, 0, len(data), name)
    children = _rw_children(data, root, name)
    textures = children[1:-1]
    seen: dict[str, tuple[int, RwChunk, bytes]] = {}
    dropped: set[int] = set()
    records: list[dict[str, object]] = []

    for index, texture in enumerate(textures):
        parts = _rw_children(data, texture, name)
        structure = parts[0]
        raw_name = data[structure.payload_begin + 8 : structure.payload_begin + 40]
        terminator = raw_name.find(b"\0")
        if terminator <= 0 or any(raw_name[terminator + 1 :]):
            raise ValueError(f"{name}: unsafe native texture name at index {index}")
        try:
            texture_name = raw_name[:terminator].decode("ascii")
        except UnicodeDecodeError as error:
            raise ValueError(f"{name}: non-ASCII native texture name at index {index}") from error
        key = texture_name.casefold()
        canonical = bytearray(data[texture.begin : texture.end])
        relative_name = structure.payload_begin + 8 - texture.begin
        canonical[relative_name : relative_name + 32] = key.encode("ascii").ljust(32, b"\0")
        canonical_bytes = bytes(canonical)
        previous = seen.get(key)
        if previous is None:
            seen[key] = (index, texture, canonical_bytes)
            continue
        previous_index, previous_texture, previous_canonical = previous
        if canonical_bytes != previous_canonical:
            raise ValueError(
                f"{name}: non-identical case-insensitive duplicate texture {texture_name!r} "
                f"at indices {previous_index} and {index}"
            )
        if not drop_identical_duplicates:
            raise ValueError(
                f"{name}: duplicate case-insensitive texture {texture_name!r} at indices {previous_index} and {index}"
            )
        dropped.add(index)
        records.append(
            {
                "key": key,
                "kept_index": previous_index,
                "dropped_index": index,
                "kept_chunk_offset": previous_texture.begin,
                "dropped_chunk_offset": texture.begin,
                "chunk_bytes": texture.end - texture.begin,
                "kept_chunk_sha256": hashlib.sha256(data[previous_texture.begin : previous_texture.end]).hexdigest(),
                "dropped_chunk_sha256": hashlib.sha256(data[texture.begin : texture.end]).hexdigest(),
            }
        )

    if not dropped:
        return data, {"dropped_duplicate_count": 0, "records": []}

    rebuilt_children: list[bytes] = []
    for child_index, child in enumerate(children):
        if 1 <= child_index <= len(textures) and child_index - 1 in dropped:
            continue
        encoded = bytearray(data[child.begin : child.end])
        if child_index == 0:
            struct.pack_into("<H", encoded, child.payload_begin - child.begin, len(textures) - len(dropped))
        rebuilt_children.append(bytes(encoded))
    payload = b"".join(rebuilt_children)
    root_header = bytearray(data[:12])
    struct.pack_into("<I", root_header, 4, len(payload))
    rebuilt_root = bytes(root_header) + payload
    if len(rebuilt_root) > len(data):
        raise ValueError(f"{name}: duplicate removal unexpectedly grew the TXD")
    result = rebuilt_root.ljust(len(data), b"\0")
    validate_static_txd_float_grammar(result, name)
    # The second pass must prove the rebuilt dictionary has no remaining
    # duplicate key; it cannot recurse into another mutation.
    normalize_static_txd_duplicate_names(result, name, drop_identical_duplicates=False)
    return result, {"dropped_duplicate_count": len(dropped), "records": records}


def write_ide(path: Path, models: list[ResourceModel], native_ids: dict[int, int]) -> None:
    ordinary = [model for model in models if model.definition.model_type != "timed-object"]
    timed = [model for model in models if model.definition.model_type == "timed-object"]
    lines = ["# Generated by utils/extended-world/build_native_bw_pack.py.", "objs"]
    for model in ordinary:
        definition = model.definition
        native_id = native_ids[definition.source_id]
        lines.append(
            f"{native_id}, {definition.source_id}, {Path(model.txd_path).stem}, 1, "
            f"{definition.lod_distance:.9g}, {definition.ide_flags}"
        )
    lines.extend(("end", "tobj"))
    for model in timed:
        definition = model.definition
        if definition.time_on is None or definition.time_off is None:
            raise ValueError(f"timed model {definition.source_id} has no time range")
        native_id = native_ids[definition.source_id]
        lines.append(
            f"{native_id}, {definition.source_id}, {Path(model.txd_path).stem}, 1, "
            f"{definition.lod_distance:.9g}, {definition.ide_flags}, "
            f"{definition.time_on}, {definition.time_off}"
        )
    lines.extend(("end", ""))
    path.write_text("\n".join(lines), encoding="ascii")


def binary_ipl_data(placements: list[Placement], native_ids: dict[int, int]) -> bytes:
    header = bytearray(BINARY_IPL_HEADER_SIZE)
    header[:4] = b"bnry"
    struct.pack_into("<I", header, 4, len(placements))
    struct.pack_into("<I", header, 28, BINARY_IPL_HEADER_SIZE)
    data = bytearray(header)
    for placement in placements:
        data.extend(
            BINARY_IPL_INSTANCE.pack(
                placement.x,
                placement.y,
                placement.z,
                placement.qx,
                placement.qy,
                placement.qz,
                placement.qw,
                native_ids[placement.source_id],
                0,
                -1,
            )
        )
    return bytes(data)


def parse_binary_ipl_data(data: bytes) -> list[Placement]:
    if len(data) < BINARY_IPL_HEADER_SIZE or data[:4] != b"bnry":
        raise ValueError("invalid binary IPL header")
    count = struct.unpack_from("<I", data, 4)[0]
    offset = struct.unpack_from("<I", data, 28)[0]
    if offset < BINARY_IPL_HEADER_SIZE or offset + count * BINARY_IPL_INSTANCE.size > len(data):
        raise ValueError("truncated binary IPL instances")
    placements: list[Placement] = []
    for index in range(count):
        values = BINARY_IPL_INSTANCE.unpack_from(data, offset + index * BINARY_IPL_INSTANCE.size)
        placements.append(
            Placement(
                source_id=values[7],
                name="",
                area=values[8] & 0xFF,
                x=values[0],
                y=values[1],
                z=values[2],
                qx=values[3],
                qy=values[4],
                qz=values[5],
                qw=values[6],
                lod_index=values[9],
                binary=True,
                source_index=index,
            )
        )
    return placements


def active_directive_count(path: Path, directive: str) -> int:
    if not path.is_file():
        return 0
    count = 0
    for raw_line in path.read_text(encoding="ascii", errors="ignore").splitlines():
        line = raw_line.strip()
        if line and not line.startswith("#") and line.split(maxsplit=1)[0].casefold() == directive.casefold():
            count += 1
    return count


def scan_ide_model_ids(path: Path) -> set[int]:
    """Read IDs from every stock model-bearing IDE section.

    build_ug_map.parse_ide intentionally returns only static object metadata.
    The native free-ID proof must also reject collisions with vehicles, peds,
    weapons, and hierarchy models even though Bullworth does not add them.
    """

    result: set[int] = set()
    section: str | None = None
    for raw_line in path.read_text(encoding="ascii", errors="ignore").splitlines():
        line = raw_line.strip()
        lowered = line.casefold()
        if lowered in IDE_MODEL_SECTIONS:
            section = lowered
            continue
        if lowered == "end":
            section = None
            continue
        if not line or line.startswith("#") or section not in IDE_MODEL_SECTIONS:
            continue
        try:
            result.add(int(line.split(",", 1)[0].strip(), 0))
        except ValueError as error:
            raise ValueError(f"invalid model ID in {path}: {line}") from error
    return result


def stock_inventory(gta_root: Path) -> dict[str, object]:
    occupied_ids: set[int] = set()
    for ide_path in sorted((gta_root / "data").rglob("*.ide"), key=lambda path: str(path).casefold()):
        occupied_ids.update(scan_ide_model_ids(ide_path))

    txd_names: set[str] = set()
    col_entries = 0
    ipl_entries = 0
    archives: list[dict[str, object]] = []
    for relative in STOCK_IMG_PATHS:
        path = gta_root / relative
        if not path.is_file():
            raise FileNotFoundError(f"missing stock archive {path}")
        entries = read_img_directory(path)
        suffix_counts = Counter(Path(entry.name).suffix for entry in entries)
        txd_names.update(Path(entry.name).stem for entry in entries if entry.name.endswith(".txd"))
        col_entries += suffix_counts[".col"]
        ipl_entries += suffix_counts[".ipl"]
        archives.append({"path": relative, "entries": len(entries), "types": dict(sorted(suffix_counts.items()))})

    standalone_txd_names = {
        path.stem.casefold()
        for path in gta_root.rglob("*.txd")
        if path.is_file() and "modloader" not in {part.casefold() for part in path.parts}
    }
    txd_names.update(standalone_txd_names)
    data_files = (gta_root / "data/default.dat", gta_root / "data/gta.dat")
    col_directives = sum(active_directive_count(path, "COLFILE") for path in data_files)
    ipl_directives = sum(active_directive_count(path, "IPL") for path in data_files)
    return {
        "occupied_model_ids": len(occupied_ids),
        "highest_model_id": max(occupied_ids) if occupied_ids else None,
        "requested_model_id_collisions": sorted(
            occupied_ids.intersection(range(MODEL_ID_START, MODEL_ID_END + 1))
        ),
        "txd_filename_inventory": len(txd_names),
        "col_img_entries": col_entries,
        "colfile_directives": col_directives,
        "ipl_img_entries": ipl_entries,
        "ipl_directives": ipl_directives,
        "ipl_generic_slot": 1,
        "audited_standalone_txd_slots": AUDITED_STANDALONE_TXD_SLOTS,
        "audited_mta_runtime_txd_slots": AUDITED_MTA_RUNTIME_TXD_SLOTS,
        "archives": archives,
    }


def archive_slice(path: Path, entry: ImgEntry) -> bytes:
    with path.open("rb") as stream:
        stream.seek(entry.offset_sector * SECTOR_SIZE)
        data = stream.read(entry.size_sectors * SECTOR_SIZE)
    if len(data) != entry.size_sectors * SECTOR_SIZE:
        raise ValueError(f"truncated archive entry {entry.name}")
    return data


def bounds(placements: list[Placement]) -> dict[str, list[float]]:
    return {
        "x": [min(item.x for item in placements), max(item.x for item in placements)],
        "y": [min(item.y for item in placements), max(item.y for item in placements)],
        "z": [min(item.z for item in placements), max(item.z for item in placements)],
    }


def budget_report(stock: dict[str, object], models: list[ResourceModel], txd_count: int) -> dict[str, object]:
    model_types = Counter(model.definition.model_type for model in models)
    stock_counts = {
        "txd_slots": int(stock["audited_mta_runtime_txd_slots"]),
        "col_slots": int(stock["col_img_entries"]) + int(stock["colfile_directives"]),
        # Text IPL directives populate the generic/file loader but do not each
        # consume a native CIplStore streaming slot. IMG entries plus slot 0
        # are the exact native streaming baseline.
        "ipl_slots": int(stock["ipl_img_entries"]) + int(stock["ipl_generic_slot"]),
    }
    additions = {"txd_slots": txd_count, "col_slots": 1, "ipl_slots": EXPECTED_IPLS}
    pools: dict[str, object] = {}
    for pool_name in ("txd_slots", "col_slots", "ipl_slots"):
        used = stock_counts[pool_name] + additions[pool_name]
        pools[pool_name] = {
            "capacity": POOL_CAPACITIES[pool_name],
            "stock_inventory": stock_counts[pool_name],
            "bullworth_addition": additions[pool_name],
            "projected_used": used,
            "remaining": POOL_CAPACITIES[pool_name] - used,
        }
    pools["txd_slots"]["conservative_filename_inventory"] = stock["txd_filename_inventory"]
    pools["txd_slots"]["standalone_archive_inventory"] = stock["audited_standalone_txd_slots"]
    pools["txd_slots"]["mta_runtime_audited_occupied"] = stock["audited_mta_runtime_txd_slots"]
    pools["txd_slots"]["runtime_verification_required"] = True
    pools["ipl_slots"]["streamed_img_entries"] = stock["ipl_img_entries"]
    pools["ipl_slots"]["generic_slot"] = stock["ipl_generic_slot"]
    pools["ipl_slots"]["text_ipl_directives_not_counted"] = stock["ipl_directives"]
    pools["model_ids"] = {
        "capacity": POOL_CAPACITIES["model_ids"],
        "stock_highest": stock["highest_model_id"],
        "bullworth_range": [MODEL_ID_START, MODEL_ID_END],
        "remaining_above_range": POOL_CAPACITIES["model_ids"] - MODEL_ID_END - 1,
    }
    stores: dict[str, object] = {}
    for model_type, occupied in MODEL_STORE_STOCK_OCCUPIED.items():
        exact_required = occupied + model_types[model_type]
        padded_target = MODEL_STORE_PADDED_TARGETS[model_type]
        if padded_target < exact_required:
            raise ValueError(f"padded {model_type} store target is below the exact requirement")
        stores[model_type] = {
            "stock_occupied": occupied,
            "bullworth_addition": model_types[model_type],
            "exact_required": exact_required,
            "padded_target": padded_target,
            "target_headroom": padded_target - exact_required,
        }
    return {"pools": pools, "model_stores": stores}


def write_text_report(path: Path, report: dict[str, object]) -> None:
    counts = report["counts"]
    lines = [
        "Bullworth native pack validation: OK",
        f"models: {counts['models']} ({counts['model_id_range'][0]}..{counts['model_id_range'][1]})",
        f"TXDs: {counts['txds']}; COL records: {counts['col_records']}",
        f"largest COL record: {counts['max_col_record_bytes']} bytes; "
        f"native read buffer: {report['collision_io']['buffer_capacity']} bytes",
        f"placements: {counts['placements']} in {counts['ipls']} binary IPLs; "
        f"non--1 LODs: {counts['non_negative_lods']}",
        f"IMG entries: {counts['archive_entries']}; archive sectors: {counts['archive_sectors']}",
        f"normalized non-finite geometry UVs: {report['normalization']['geometry_uv_nonfinite_replaced']} "
        f"to {report['normalization']['geometry_uv_replacement_bits']}; "
        f"verified DFF semantic floats: {report['normalization']['verified_dff_semantic_floats']}",
        f"removed case-insensitive identical TXD duplicates: "
        f"{report['normalization']['txd_casefold_duplicates_removed']}",
        "",
        "Projected native pool budgets:",
    ]
    for name, values in report["budgets"]["pools"].items():
        lines.append(f"- {name}: {values}")
    lines.extend(("", "Required model-store capacities:"))
    for name, values in report["budgets"]["model_stores"].items():
        lines.append(
            f"- {name}: exact {values['exact_required']} "
            f"({values['stock_occupied']} + {values['bullworth_addition']}), "
            f"padded target {values['padded_target']}"
        )
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def verify_pack(
    output: Path, source_models: list[ResourceModel], source_placements: list[Placement]
) -> dict[str, object]:
    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    ide_definitions = parse_ide(output / "bw.ide")
    archive_path = output / "bw.img"
    archive_entries = read_img_directory(archive_path)
    archive_by_name = {entry.name: entry for entry in archive_entries}

    expected_native_ids = set(range(MODEL_ID_START, MODEL_ID_END + 1))
    if set(ide_definitions) != expected_native_ids:
        raise ValueError("remapped IDE does not cover the compact native ID range")
    if len(manifest["models"]) != EXPECTED_MODELS:
        raise ValueError("manifest model count mismatch")

    dff_names: set[str] = set()
    txd_names: set[str] = set()
    for item in manifest["models"]:
        native_id = int(item["native_id"])
        source_id = int(item["source_id"])
        if native_id != MODEL_ID_START + sorted(model.definition.source_id for model in source_models).index(source_id):
            raise ValueError(f"non-deterministic model remap for {source_id}")
        definition = ide_definitions[native_id]
        if definition.model_type != item["model_type"] or definition.ide_flags != item["ide_flags"]:
            raise ValueError(f"IDE metadata mismatch for native model {native_id}")
        if definition.name != str(source_id):
            raise ValueError(f"IDE/archive name mismatch for native model {native_id}")
        dff_name = item["archive"]["dff"]["name"]
        txd_name = item["archive"]["txd"]["name"]
        dff_names.add(dff_name)
        txd_names.add(txd_name)
        for metadata in (item["archive"]["dff"], item["archive"]["txd"]):
            entry = archive_by_name.get(metadata["name"])
            if entry is None or [entry.offset_sector, entry.size_sectors, entry.stream_sectors] != [
                metadata["offset_sector"],
                metadata["size_sectors"],
                metadata["stream_sectors"],
            ]:
                raise ValueError(f"archive metadata mismatch for {metadata['name']}")

    verified_semantic_floats = 0
    for name in dff_names:
        dff_data = archive_slice(archive_path, archive_by_name[name])
        if struct.unpack_from("<I", dff_data, 0)[0] != 0x10:
            raise ValueError(f"invalid RenderWare DFF root chunk in {name}")
        _, float_audit = normalize_static_dff_semantic_floats(dff_data, name, normalize_uv=False)
        verified_semantic_floats += int(float_audit["semantic_float_count"])
    for name in txd_names:
        txd_data = archive_slice(archive_path, archive_by_name[name])
        if struct.unpack_from("<I", txd_data, 0)[0] != 0x16:
            raise ValueError(f"invalid RenderWare TXD root chunk in {name}")
        validate_static_txd_float_grammar(txd_data, name)
        normalize_static_txd_duplicate_names(txd_data, name, drop_identical_duplicates=False)

    normalization = manifest.get("normalization", {})
    if normalization.get("geometry_uv_nonfinite_replaced") != sum(EXPECTED_DFF_UV_NORMALIZATIONS.values()):
        raise ValueError("manifest UV-normalization count differs from the audited source profile")
    if normalization.get("geometry_uv_files") != EXPECTED_DFF_UV_NORMALIZATIONS:
        raise ValueError("manifest UV-normalization files differ from the audited source profile")
    if normalization.get("verified_dff_semantic_floats") != verified_semantic_floats:
        raise ValueError("manifest semantic-float count differs from the generated DFFs")
    if normalization.get("txd_duplicate_files") != EXPECTED_TXD_DUPLICATE_REMOVALS:
        raise ValueError("manifest TXD duplicate-removal files differ from the audited source profile")
    if normalization.get("txd_casefold_duplicates_removed") != sum(EXPECTED_TXD_DUPLICATE_REMOVALS.values()):
        raise ValueError("manifest TXD duplicate-removal count differs from the audited source profile")

    col_data = (output / "bw.col").read_bytes()
    col_records = parse_col_records(col_data)
    max_col_record_bytes = max(record[3] for record in col_records)
    collision_io = manifest.get("collision_io", {})
    if collision_io.get("buffer_capacity") != NATIVE_COL_BUFFER_CAPACITY:
        raise ValueError("manifest collision read-buffer capacity does not match the runtime contract")
    if max_col_record_bytes > NATIVE_COL_BUFFER_CAPACITY:
        raise ValueError(
            f"largest COL record ({max_col_record_bytes}) exceeds the native read buffer "
            f"({NATIVE_COL_BUFFER_CAPACITY})"
        )
    col_ids = {record[1] for record in col_records}
    col_names = {record[0] for record in col_records}
    if col_ids != expected_native_ids or col_names != {str(model.definition.source_id) for model in source_models}:
        raise ValueError("merged COL names/IDs do not match the IDE remap")
    col_entry = archive_by_name.get("bw.col")
    if col_entry is None or archive_slice(archive_path, col_entry)[: len(col_data)] != col_data:
        raise ValueError("standalone and archived bw.col differ")

    source_by_group: dict[str, list[Placement]] = defaultdict(list)
    for placement in source_placements:
        source_by_group[placement.source_file.casefold()].append(placement)
    ipl_reports: list[dict[str, object]] = []
    parsed_placements: list[Placement] = []
    for source_name, expected in sorted(source_by_group.items()):
        name = f"bw_{Path(source_name).stem}.ipl"
        path = output / "ipls" / name
        parsed = parse_binary_ipl_data(path.read_bytes())
        entry = archive_by_name.get(name)
        if entry is None or archive_slice(archive_path, entry)[: path.stat().st_size] != path.read_bytes():
            raise ValueError(f"standalone and archived IPL differ: {name}")
        if len(parsed) != len(expected):
            raise ValueError(f"IPL placement count mismatch for {name}")
        if any(item.lod_index != -1 for item in parsed):
            raise ValueError(f"non--1 LOD survived in {name}")
        if any(item.source_id not in expected_native_ids for item in parsed):
            raise ValueError(f"unmapped model ID in {name}")
        for actual, original in zip(parsed, expected):
            expected_id = manifest["source_to_native"][str(original.source_id)]
            if actual.source_id != expected_id:
                raise ValueError(f"IPL model cross-reference mismatch in {name}")
            for actual_value, expected_value in zip(
                (actual.x, actual.y, actual.z, actual.qx, actual.qy, actual.qz, actual.qw),
                (original.x, original.y, original.z, original.qx, original.qy, original.qz, original.qw),
            ):
                if abs(actual_value - expected_value) > 0.001:
                    raise ValueError(f"IPL transform changed beyond float32 precision in {name}")
        parsed_placements.extend(parsed)
        ipl_reports.append({"name": name, "placements": len(parsed), "bounds": bounds(parsed)})

    expected_entries = dff_names | txd_names | {"bw.col"} | {item["name"] for item in ipl_reports}
    actual_entries = set(archive_by_name)
    if actual_entries != expected_entries:
        raise ValueError(
            f"archive entry set mismatch: missing={sorted(expected_entries - actual_entries)} "
            f"unexpected={sorted(actual_entries - expected_entries)}"
        )
    archive_size = archive_path.stat().st_size
    archive_sectors = sectors_for(archive_size)
    report = {
        "status": "ok",
        "counts": {
            "models": len(source_models),
            "model_id_range": [min(expected_native_ids), max(expected_native_ids)],
            "model_types": dict(sorted(Counter(model.definition.model_type for model in source_models).items())),
            "txds": len(txd_names),
            "col_records": len(col_records),
            "max_col_record_bytes": max_col_record_bytes,
            "placements": len(parsed_placements),
            "ipls": len(ipl_reports),
            "non_negative_lods": sum(item.lod_index != -1 for item in parsed_placements),
            "archive_entries": len(archive_entries),
            "archive_sectors": archive_sectors,
            "archive_bytes": archive_size,
        },
        "duplicates": {"archive_names": [], "native_model_ids": [], "source_model_ids": []},
        "missing_assets": [],
        "ipls": ipl_reports,
        "archive": {
            "format": "VER2",
            "sector_size": SECTOR_SIZE,
            "entries": [entry.__dict__ for entry in archive_entries],
        },
        "collision_io": collision_io,
        "normalization": normalization,
        "budgets": manifest["budgets"],
        "stock_inventory": manifest["stock_inventory"],
    }
    return report


def build_pack(resource: Path, output: Path, gta_root: Path) -> dict[str, object]:
    map_path = resource / "map_data.lua"
    models_archive = resource / "assets/bw_models.img"
    textures_archive = resource / "assets/bw_textures.img"
    if not map_path.is_file() or not models_archive.is_file() or not textures_archive.is_file():
        raise FileNotFoundError("ug-bw resource is missing map_data.lua or packed model/texture assets")

    models, placements = parse_generated_map(map_path)
    source_ids = [model.definition.source_id for model in models]
    native_ids = {source_id: MODEL_ID_START + index for index, source_id in enumerate(source_ids)}
    if set(native_ids.values()) != set(range(MODEL_ID_START, MODEL_ID_END + 1)):
        raise ValueError("compact native model remap is not exact")

    stock = stock_inventory(gta_root)
    if stock["requested_model_id_collisions"]:
        raise ValueError(f"native model range collides with stock IDs: {stock['requested_model_id_collisions']}")
    if stock["highest_model_id"] != MODEL_ID_START - 1:
        raise ValueError(
            f"expected stock highest model ID {MODEL_ID_START - 1}, found {stock['highest_model_id']}"
        )

    model_entries = entry_index(models_archive)
    texture_entries = entry_index(textures_archive)
    archive_inputs: list[ArchiveInput] = []
    missing_assets: list[str] = []
    dff_float_audits: list[dict[str, object]] = []
    for model in models:
        name = Path(model.dff_path).name.casefold()
        entry = model_entries.get(name)
        if entry is None:
            missing_assets.append(model.dff_path)
            continue
        normalized, audit = normalize_static_dff_semantic_floats(
            archive_slice(models_archive, entry), name, normalize_uv=True
        )
        if audit["normalized_uv_count"]:
            dff_float_audits.append({"name": name, **audit})
        archive_inputs.append(ArchiveInput(name=name, data=normalized))
    actual_normalizations = {
        str(audit["name"]): int(audit["normalized_uv_count"]) for audit in dff_float_audits
    }
    if actual_normalizations != EXPECTED_DFF_UV_NORMALIZATIONS:
        raise ValueError(
            "source DFF UV-normalization profile changed: "
            f"expected={EXPECTED_DFF_UV_NORMALIZATIONS} actual={actual_normalizations}"
        )
    verified_semantic_floats = sum(int(audit["semantic_float_count"]) for audit in dff_float_audits)
    normalized_names = set(actual_normalizations)
    for model in models:
        name = Path(model.dff_path).name.casefold()
        if name in normalized_names:
            continue
        entry = model_entries[name]
        _, audit = normalize_static_dff_semantic_floats(
            archive_slice(models_archive, entry), name, normalize_uv=False
        )
        verified_semantic_floats += int(audit["semantic_float_count"])
    unique_txd_paths = sorted({model.txd_path for model in models}, key=str.casefold)
    txd_duplicate_audits: list[dict[str, object]] = []
    for txd_path in unique_txd_paths:
        name = Path(txd_path).name.casefold()
        entry = texture_entries.get(name)
        if entry is None:
            missing_assets.append(txd_path)
            continue
        source_data = archive_slice(textures_archive, entry)
        normalized_txd, duplicate_audit = normalize_static_txd_duplicate_names(
            source_data, name, drop_identical_duplicates=True
        )
        if duplicate_audit["dropped_duplicate_count"]:
            txd_duplicate_audits.append({"name": name, **duplicate_audit})
            archive_inputs.append(ArchiveInput(name=name, data=normalized_txd))
        else:
            archive_inputs.append(
                ArchiveInput(
                    name=name,
                    path=textures_archive,
                    source_offset=entry.offset_sector * SECTOR_SIZE,
                    size=entry.size_sectors * SECTOR_SIZE,
                )
            )
    actual_duplicate_removals = {
        str(audit["name"]): int(audit["dropped_duplicate_count"]) for audit in txd_duplicate_audits
    }
    if actual_duplicate_removals != EXPECTED_TXD_DUPLICATE_REMOVALS:
        raise ValueError(
            "source TXD duplicate-removal profile changed: "
            f"expected={EXPECTED_TXD_DUPLICATE_REMOVALS} actual={actual_duplicate_removals}"
        )
    if missing_assets:
        raise FileNotFoundError(f"missing packed assets: {missing_assets[:20]}")

    if output.exists():
        if any(output.iterdir()):
            raise ValueError(f"output directory must be empty: {output}")
    else:
        output.mkdir(parents=True)
    (output / "ipls").mkdir(parents=True)
    write_ide(output / "bw.ide", models, native_ids)

    merged_col = bytearray()
    col_metadata: dict[int, dict[str, int]] = {}
    for model in models:
        source_id = model.definition.source_id
        path = resource / model.col_path
        if not path.is_file():
            raise FileNotFoundError(f"missing COL {path}")
        record = remap_col_record(path.read_bytes(), native_ids[source_id], str(source_id))
        col_metadata[source_id] = {"offset": len(merged_col), "size": len(record)}
        merged_col.extend(record)
    col_data = bytes(merged_col)
    max_col_record_bytes = max(metadata["size"] for metadata in col_metadata.values())
    if max_col_record_bytes > NATIVE_COL_BUFFER_CAPACITY:
        raise ValueError(
            f"largest COL record ({max_col_record_bytes}) exceeds the native read buffer "
            f"({NATIVE_COL_BUFFER_CAPACITY})"
        )
    (output / "bw.col").write_bytes(col_data)
    archive_inputs.append(ArchiveInput(name="bw.col", data=col_data))

    grouped_placements: dict[str, list[Placement]] = defaultdict(list)
    for placement in placements:
        grouped_placements[placement.source_file.casefold()].append(placement)
    ipl_names: dict[str, str] = {}
    for source_name, group in sorted(grouped_placements.items()):
        name = f"bw_{Path(source_name).stem}.ipl"
        data = binary_ipl_data(group, native_ids)
        (output / "ipls" / name).write_bytes(data)
        archive_inputs.append(ArchiveInput(name=name, data=data))
        ipl_names[source_name] = name

    packed_entries = pack_inputs(output / "bw.img", archive_inputs)
    packed_by_name = {entry.name: entry for entry in packed_entries}
    txd_slot_base = int(stock["audited_mta_runtime_txd_slots"])
    txd_slots = {
        Path(path).stem.casefold(): txd_slot_base + index for index, path in enumerate(unique_txd_paths)
    }
    budgets = budget_report(stock, models, len(unique_txd_paths))
    negative_budgets = {
        name: values["remaining"]
        for name, values in budgets["pools"].items()
        if "remaining" in values and values["remaining"] < 0
    }
    if negative_budgets:
        raise ValueError(f"native pool budget exceeded: {negative_budgets}")

    def entry_metadata(name: str) -> dict[str, object]:
        entry = packed_by_name[name.casefold()]
        return {
            "name": entry.name,
            "offset_sector": entry.offset_sector,
            "size_sectors": entry.size_sectors,
            "stream_sectors": entry.stream_sectors,
        }

    manifest_models: list[dict[str, object]] = []
    for model in models:
        definition = model.definition
        source_id = definition.source_id
        txd_name = Path(model.txd_path).stem.casefold()
        manifest_models.append(
            {
                "source_id": source_id,
                "source_name": definition.name,
                "native_id": native_ids[source_id],
                "native_name": str(source_id),
                "model_type": definition.model_type,
                "txd": {"native_name": txd_name, "native_slot_plan": txd_slots[txd_name]},
                "draw_distance": definition.lod_distance,
                "ide_flags": definition.ide_flags,
                "time_on": definition.time_on,
                "time_off": definition.time_off,
                "archive": {
                    "dff": entry_metadata(Path(model.dff_path).name),
                    "txd": entry_metadata(Path(model.txd_path).name),
                    "col": {
                        "archive_entry": "bw.col",
                        "record_offset": col_metadata[source_id]["offset"],
                        "record_size": col_metadata[source_id]["size"],
                    },
                },
            }
        )
    manifest = {
        "format": 1,
        "generated_by": "utils/extended-world/build_native_bw_pack.py",
        "model_id_range": [MODEL_ID_START, MODEL_ID_END],
        "source_to_native": {str(source_id): native_ids[source_id] for source_id in source_ids},
        "txd_slot_plan": {
            "basis": "audited MTA runtime CTxdStore snapshot; runtime simulates the live pool",
            "base": txd_slot_base,
            "slots": txd_slots,
            "runtime_verification_required": True,
            "conservative_filename_inventory": stock["txd_filename_inventory"],
        },
        "district_ipls": [
            {"source": source, "name": name, "placements": len(grouped_placements[source])}
            for source, name in sorted(ipl_names.items())
        ],
        "models": manifest_models,
        "archive": {
            "name": "bw.img",
            "format": "VER2",
            "sector_size": SECTOR_SIZE,
            "entries": [entry.__dict__ for entry in packed_entries],
        },
        "collision_io": {
            "stock_buffer_capacity": 32_768,
            "buffer_capacity": NATIVE_COL_BUFFER_CAPACITY,
            "max_col_record_bytes": max_col_record_bytes,
            "remaining": NATIVE_COL_BUFFER_CAPACITY - max_col_record_bytes,
        },
        "normalization": {
            "geometry_uv_nonfinite_replaced": sum(actual_normalizations.values()),
            "geometry_uv_replacement_bits": "0x00000000",
            "geometry_uv_files": actual_normalizations,
            "geometry_uv_records": dff_float_audits,
            "verified_dff_semantic_floats": verified_semantic_floats,
            "verified_txd_semantic_floats": 0,
            "txd_casefold_duplicates_removed": sum(actual_duplicate_removals.values()),
            "txd_duplicate_files": actual_duplicate_removals,
            "txd_duplicate_records": txd_duplicate_audits,
        },
        "stock_inventory": stock,
        "budgets": budgets,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    report = verify_pack(output, models, placements)
    dump_runtime_manifest(output / "native-world.json", build_runtime_manifest(report, output / "bw.ide", output / "bw.img"))
    (output / "validation.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_text_report(output / "validation.txt", report)
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resource", type=Path, required=True, help="generated test-resources/ug-bw directory")
    parser.add_argument("--stock-gta", type=Path, help="unmodified GTA San Andreas root for budgets")
    parser.add_argument("--output", type=Path, required=True, help="caller-selected output directory")
    parser.add_argument("--verify-only", action="store_true", help="round-trip validate an existing output")
    args = parser.parse_args()

    models, placements = parse_generated_map(args.resource / "map_data.lua")
    if args.verify_only:
        report = verify_pack(args.output, models, placements)
    else:
        if args.stock_gta is None:
            parser.error("--stock-gta is required when generating a pack")
        report = build_pack(args.resource, args.output, args.stock_gta)
    counts = report["counts"]
    print(
        f"native Bullworth pack OK: {counts['models']} models, {counts['txds']} TXDs, "
        f"{counts['placements']} placements in {counts['ipls']} IPLs, "
        f"{counts['archive_entries']} IMG entries"
    )


if __name__ == "__main__":
    main()
