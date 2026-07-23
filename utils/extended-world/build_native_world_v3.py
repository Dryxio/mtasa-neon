#!/usr/bin/env python3
"""Build deterministic, spatial, multi-IMG static-world v3 packs.

The v3 builder consumes only the narrow ``map_data.lua`` schema emitted by the
reviewed extended-world extractors. It never trusts source model IDs or asset
names as GTA runtime identities. Instead it assigns a contiguous model range,
short collision-resistant names, one COL/IPL pair per source IPL, and
deterministically partitions the resulting members across bounded IMG VER2
archives.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import subprocess
import tempfile
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from build_native_bw_pack import (
    ArchiveInput,
    BINARY_IPL_HEADER_SIZE,
    BINARY_IPL_INSTANCE,
    FIELD_PATTERN,
    MODEL_LINE,
    PLACEMENT_LINE,
    RW_LIBRARY_ID,
    _rw_children,
    _rw_chunk,
    decode_lua_value,
    pack_inputs,
    parse_col_records,
    parse_fields,
    read_img_directory,
    remap_col_record,
)
from native_world_manifest import (
    STATIC_WORLD_V3_POLICY,
    build_runtime_manifest,
    dump_runtime_manifest,
    parse_runtime_manifest,
)
from pack_img import DIRECTORY_ENTRY, HEADER, SECTOR_SIZE, sectors_for


FORMAT = 3
MAX_IMG_SECTORS = 131_072
MODEL_ID_LIMIT = 31_999
FIRST_CUSTOM_ID = 20_000
MAX_MODELS = 4_096
MAX_TXDS = 1_024
MAX_SPATIAL_GROUPS = 64
MAX_PLACEMENTS = 20_000
LOD_MAP_HEADER = struct.Struct("<8sIIIIII")
LOD_MAP_LINK = struct.Struct("<II")
LOD_MAP_MAGIC = b"NWL3LOD\0"
LOD_MAP_VERSION = 1
MAX_LOD_ANCHORS = 4_096
MAX_LOD_LINKS = 4_096
MAX_LOD_SCRATCH_ENTRIES = 4_096
MAX_COL_RECORD_BYTES = 327_680
UINT64_MAX = (1 << 64) - 1
MAX_TEXTURE_GPU_BYTES = 64 * 1024 * 1024
MAX_TEXTURE_DECODED_BYTES = 64 * 1024 * 1024
MAX_TXD_GPU_BYTES = 256 * 1024 * 1024
MAX_TXD_DECODED_BYTES = 512 * 1024 * 1024
MAX_CITY_GPU_BYTES = 2 * 1024 * 1024 * 1024
MAX_CITY_DECODED_BYTES = 8 * 1024 * 1024 * 1024
MAX_AGGREGATE_GPU_BYTES = 4 * 1024 * 1024 * 1024
MAX_AGGREGATE_DECODED_BYTES = 16 * 1024 * 1024 * 1024
MAX_COL_SPHERES = 256
MAX_COL_BOXES = 256
MAX_COL_VERTICES = 32_768
MAX_COL_FACES = 32_768
MAX_COL_FACE_GROUPS = 1_024
MAX_COL_SHADOW_VERTICES = 32_768
MAX_COL_SHADOW_FACES = 32_768
ALLOWED_RW_VERSIONS = {RW_LIBRARY_ID}
RW_CONTAINERS = {0x03, 0x06, 0x07, 0x08, 0x0E, 0x0F, 0x10, 0x12, 0x14, 0x15, 0x16, 0x1A}
SAFE_NAMESPACE = re.compile(r"^[a-z][a-z0-9]$")
BASE36 = "0123456789abcdefghijklmnopqrstuvwxyz"
# This is the complete (raster format, D3D format, depth, alpha/compression
# flags) vocabulary measured in the four frozen source catalogs. Treating the
# fields independently would admit combinations that the D3D9 reader has not
# been proved to accept.
TXD_D3D9_HEADER_TUPLES = {
    (0x0100, 0x31545844, 16, 8),
    (0x0100, 0x31545844, 16, 9),
    (0x0200, 0x31545844, 16, 8),
    (0x0300, 0x32545844, 16, 9),
    (0x0300, 0x33545844, 16, 9),
    (0x0300, 0x34545844, 16, 8),
    (0x0300, 0x34545844, 16, 9),
    (0x0300, 0x35545844, 16, 8),
    (0x0300, 0x35545844, 16, 9),
    (0x0500, 0x00000015, 32, 0),
    (0x0500, 0x00000015, 32, 1),
    (0x0600, 0x00000016, 32, 0),
    (0x8100, 0x31545844, 4, 9),
    (0x8100, 0x31545844, 16, 8),
    (0x8100, 0x31545844, 16, 9),
    (0x8200, 0x31545844, 4, 8),
    (0x8200, 0x31545844, 4, 9),
    (0x8200, 0x31545844, 16, 8),
    (0x8300, 0x33545844, 8, 9),
    (0x8300, 0x33545844, 16, 9),
    (0x8300, 0x34545844, 16, 8),
    (0x8300, 0x34545844, 16, 9),
    (0x8300, 0x35545844, 8, 9),
    (0x8300, 0x35545844, 16, 8),
    (0x8300, 0x35545844, 16, 9),
    (0x8500, 0x00000015, 32, 0),
    (0x8600, 0x00000016, 32, 0),
}
LEGACY_DFF_CONVERSIONS = {
    # Vice City kb_canopy_test (642.dff), upgraded by the pinned local librw
    # NULL backend. Both identities make the conversion fail closed if either
    # the source catalog or serializer changes.
    "12fa63a230d460b010181b6083437bd54059ea672a6bfdb15e040a147d977940": (
        "6ddf903f3ac102e2a4f07ea1dd28dadad79412b2cccbcdffde2ae0171e4dc1eb"
    ),
}
MALFORMED_2DFX_REPAIRS = {
    "assets/models/13410.dff": (
        "ffd0de9244464cb81e93dbe045e9951d83ec466552c479e7122e5c0d86ae462c",
        "bd2040760c0e5519ae6f48055f3ebc2b9c951b22a315119883b45d989865fab6",
    ),
    "assets/models/13448.dff": (
        "5cd1985c9a7916322cbf71f6a8578bb80886d72bd85be529a8e694cb26c6e59c",
        "e25a32fbf65f389b81a600c3b47fab6f1e2b48f18ba822dd49d5bf2b6974df54",
    ),
}
MALFORMED_TIMED_MODEL_REPAIRS = {
    # VC dt_nitelites2: the extractor fused the trailing 0x200000 source
    # metadata bit into timeOff. This is deliberately an identity-pinned
    # one-row repair, not a general bit mask.
    (
        "UG_VC",
        "map_data.lua",
        "677b91a08730b6e503cfa9ae0a4ae6bded252f4f3112a49505ae852c18eaca4f",
        20509,
        19,
        0x200005,
    ): (19, 5),
    (
        "UG_VC",
        "map_data.lua",
        "677b91a08730b6e503cfa9ae0a4ae6bded252f4f3112a49505ae852c18eaca4f",
        21489,
        20,
        0x200005,
    ): (20, 5),
    (
        "UG_VC",
        "map_data.lua",
        "677b91a08730b6e503cfa9ae0a4ae6bded252f4f3112a49505ae852c18eaca4f",
        21494,
        20,
        0x200005,
    ): (20, 5),
    (
        "UG_VC",
        "map_data.lua",
        "677b91a08730b6e503cfa9ae0a4ae6bded252f4f3112a49505ae852c18eaca4f",
        21802,
        24,
        5,
    ): (0, 5),
    (
        "UG_VC",
        "map_data.lua",
        "677b91a08730b6e503cfa9ae0a4ae6bded252f4f3112a49505ae852c18eaca4f",
        21804,
        24,
        5,
    ): (0, 5),
}
IDE_FLAG_REPAIRS = {
    # UG LC carries source-specific metadata in bits that stock GTA SA never
    # stores or consumes. Keep this a fingerprinted catalog conversion:
    # silently masking an unknown source would hide a future semantic change.
    (
        "UG_LC",
        "map_data.lua",
        "1597f9c3026fc8641202b03ccb03f019292e29a270d0c00052ac2c81c684feab",
        31057,
        0x20200000,
    ): 0x200000,
    (
        "UG_LC",
        "map_data.lua",
        "1597f9c3026fc8641202b03ccb03f019292e29a270d0c00052ac2c81c684feab",
        31058,
        0x10200000,
    ): 0x200000,
    (
        "UG_LC",
        "map_data.lua",
        "1597f9c3026fc8641202b03ccb03f019292e29a270d0c00052ac2c81c684feab",
        32443,
        0x01400002,
    ): 0x400002,
    (
        "UG_LC",
        "map_data.lua",
        "1597f9c3026fc8641202b03ccb03f019292e29a270d0c00052ac2c81c684feab",
        32557,
        0x0820000C,
    ): 0x20000C,
    **{
        (
            "UG_LC",
            "map_data.lua",
            "1597f9c3026fc8641202b03ccb03f019292e29a270d0c00052ac2c81c684feab",
            source_id,
            0x01200000,
        ): 0x200000
        for source_id in (32723, 32724, 32749, 32764, 32765, 32766, 32767, 32780)
    },
    **{
        (
            "UG_LC",
            "map_data.lua",
            "1597f9c3026fc8641202b03ccb03f019292e29a270d0c00052ac2c81c684feab",
            source_id,
            0x0120000C,
        ): 0x20000C
        for source_id in (32742, 32743, 32747)
    },
}
ZERO_TRIANGLE_DFFS = {
    "assets/models/23345.dff": (
        "59543cafefb5149127f27cc033a8d36408c3f9b0faec8d1ce839756d172a3528",
        "7446aecd58f54230f744de8d6b66d083cecc13e3913dff894b3259919342a8d3",
        2018,
    ),
    "assets/models/23346.dff": (
        "47e1deab00367590a092fc6b044fdc3cb80d0e9a1415260b6ae0aaa71b06851b",
        "527005cf4473958198a2dfecd5fb3fe12d6fa2a7119be8d51039dd614a7a7f24",
        2396,
    ),
}


@dataclass(frozen=True)
class GeneratedModel:
    source_id: int
    source_name: str
    model_type: str
    txd_path: str | None
    dff_path: str
    col_path: str | None
    draw_distance: float
    ide_flags: int
    time_on: int | None
    time_off: int | None


@dataclass(frozen=True)
class GeneratedPlacement:
    source_id: int
    native: bool
    position: tuple[float, float, float]
    quaternion: tuple[float, float, float, float]
    lod_global_index: int | None
    is_lod: bool
    source: str
    source_index: int
    global_index: int


@dataclass(frozen=True)
class ModelVariant:
    model: GeneratedModel
    source_group: str
    native_id: int
    native_name: str
    txd_name: str


def base36(value: int, width: int) -> str:
    if value < 0 or value >= 36**width:
        raise ValueError(f"value {value} does not fit {width} base36 digits")
    output = []
    for _ in range(width):
        output.append(BASE36[value % 36])
        value //= 36
    return "".join(reversed(output))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def canonicalize_timed_model(
    prefix: str,
    source_name: str,
    source_fingerprint: str,
    source_id: int,
    time_on: int | None,
    time_off: int | None,
) -> tuple[int | None, int | None, dict[str, object] | None]:
    repair_key = (prefix, source_name, source_fingerprint, source_id, time_on, time_off)
    repaired = MALFORMED_TIMED_MODEL_REPAIRS.get(repair_key)
    if repaired is None:
        return time_on, time_off, None
    repaired_time_on, repaired_time_off = repaired
    return repaired_time_on, repaired_time_off, {
        "source": source_name,
        "source_sha256": source_fingerprint,
        "source_id": source_id,
        "raw_time_on": time_on,
        "raw_time_off": time_off,
        "time_on": repaired_time_on,
        "time_off": repaired_time_off,
    }


def canonicalize_ide_flags(
    prefix: str,
    source_name: str,
    source_fingerprint: str,
    source_id: int,
    raw_flags: int,
) -> tuple[int, dict[str, object] | None]:
    if not 0 <= raw_flags <= 0xFFFFFFFF:
        raise ValueError(f"model {source_id} has IDE flags outside uint32")
    repair_key = (prefix, source_name, source_fingerprint, source_id, raw_flags)
    repaired = IDE_FLAG_REPAIRS.get(repair_key)
    if repaired is not None:
        return repaired, {
            "source": source_name,
            "source_sha256": source_fingerprint,
            "source_id": source_id,
            "raw_flags": raw_flags,
            "ide_flags": repaired,
            "removed_flags": raw_flags ^ repaired,
            "reason": "source metadata bits have no GTA SA model-info consumer",
        }
    if raw_flags & ~0x007FFFFF:
        raise ValueError(f"model {source_id} has unreviewed IDE flags 0x{raw_flags:08x}")
    return raw_flags, None


def upgrade_legacy_dff(path: Path, upgrader: Path | None) -> tuple[bytes, dict[str, str] | None]:
    data = path.read_bytes()
    source_sha = hashlib.sha256(data).hexdigest()
    expected_sha = LEGACY_DFF_CONVERSIONS.get(source_sha)
    if expected_sha is None:
        return data, None
    if upgrader is None or not upgrader.is_file():
        raise ValueError(f"{path}: known legacy DFF requires --librw-dff-upgrader")
    with tempfile.TemporaryDirectory(prefix="native-world-v3-dff-") as temporary:
        output = Path(temporary) / "upgraded.dff"
        completed = subprocess.run(
            [str(upgrader), str(path), str(output)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if completed.returncode or not output.is_file():
            raise ValueError(f"{path}: librw conversion failed: {completed.stderr.strip()}")
        upgraded = output.read_bytes()
    output_sha = hashlib.sha256(upgraded).hexdigest()
    if output_sha != expected_sha:
        raise ValueError(f"{path}: librw conversion identity changed ({output_sha})")
    return upgraded, {"source_sha256": source_sha, "output_sha256": output_sha}


def gta_uppercase_key(value: str) -> int:
    """Return the raw CRC-32 remainder used by CKeyGen::GetUppercaseKey."""

    key = 0xFFFFFFFF
    for byte in value.upper().encode("ascii"):
        key ^= byte
        for _ in range(8):
            key = (key >> 1) ^ (0xEDB88320 if key & 1 else 0)
    return key & 0xFFFFFFFF


def parse_generated_map(
    path: Path, prefix: str
) -> tuple[dict[int, GeneratedModel], list[GeneratedPlacement], list[dict[str, object]]]:
    model_open = f"{prefix}_MODELS = {{"
    placement_open = f"{prefix}_PLACEMENTS = {{"
    in_models = False
    in_placements = False
    models: dict[int, GeneratedModel] = {}
    placements: list[GeneratedPlacement] = []
    repairs: list[dict[str, object]] = []
    source_fingerprint = sha256_file(path)

    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if line == model_open:
            in_models = True
            continue
        if line == placement_open:
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
            values = parse_fields(match.group(2))
            required = {"name", "modelType", "txd", "dff", "col", "lodDistance", "ideFlags"}
            if missing := required - values.keys():
                raise ValueError(f"model {source_id} is missing fields {sorted(missing)}")
            if (
                source_id in models
                or not all(isinstance(values[key], str) for key in ("name", "modelType", "dff"))
                or not (isinstance(values["txd"], str) or values["txd"] is False)
                or not (isinstance(values["col"], str) or values["col"] is False)
            ):
                raise ValueError(f"model {source_id} has duplicate or non-native assets")
            model_type = str(values["modelType"])
            if model_type not in {"object", "object-damageable", "timed-object"}:
                raise ValueError(f"model {source_id} has unsupported type {model_type!r}")
            time_on = int(values["timeOn"]) if "timeOn" in values else None
            time_off = int(values["timeOff"]) if "timeOff" in values else None
            if model_type == "timed-object":
                time_on, time_off, repair = canonicalize_timed_model(
                    prefix, path.name, source_fingerprint, source_id, time_on, time_off
                )
                if repair:
                    repairs.append(repair)
            if model_type == "timed-object" and (
                time_on is None or time_off is None or not 0 <= time_on <= 23 or not 0 <= time_off <= 23 or time_on == time_off
            ):
                raise ValueError(f"timed model {source_id} has an invalid time range")
            ide_flags, ide_flag_repair = canonicalize_ide_flags(
                prefix,
                path.name,
                source_fingerprint,
                source_id,
                int(values["ideFlags"]),
            )
            if ide_flag_repair:
                repairs.append(ide_flag_repair)
            models[source_id] = GeneratedModel(
                source_id=source_id,
                source_name=str(values["name"]),
                model_type=model_type,
                txd_path=None if values["txd"] is False else str(values["txd"]),
                dff_path=str(values["dff"]),
                col_path=None if values["col"] is False else str(values["col"]),
                draw_distance=float(values["lodDistance"]),
                ide_flags=ide_flags,
                time_on=time_on,
                time_off=time_off,
            )
        elif in_placements:
            match = PLACEMENT_LINE.match(line)
            if not match:
                raise ValueError(f"unrecognized placement row at {path}:{line_number}")
            source_id = int(match.group(1))
            values = parse_fields(match.group(2))
            required = {"native", "x", "y", "z", "qx", "qy", "qz", "qw", "lod", "isLod", "source", "sourceIndex"}
            if missing := required - values.keys():
                raise ValueError(f"placement {len(placements)} is missing fields {sorted(missing)}")
            native = bool(values["native"])
            if not native and source_id not in models:
                raise ValueError(f"custom placement references undefined model {source_id}")
            lod_value = values["lod"]
            lod_index = None if lod_value is False else int(lod_value) - 1
            placements.append(
                GeneratedPlacement(
                    source_id=source_id,
                    native=native,
                    position=(float(values["x"]), float(values["y"]), float(values["z"])),
                    quaternion=(float(values["qx"]), float(values["qy"]), float(values["qz"]), float(values["qw"])),
                    lod_global_index=lod_index,
                    is_lod=bool(values["isLod"]),
                    source=str(values["source"]).casefold(),
                    source_index=int(values["sourceIndex"]),
                    global_index=len(placements),
                )
            )

    if in_models or in_placements or not models or not placements:
        raise ValueError(f"incomplete generated map in {path}")
    for placement in placements:
        if placement.lod_global_index is None:
            continue
        if placement.lod_global_index < 0 or placement.lod_global_index >= len(placements):
            raise ValueError(f"placement {placement.global_index} has an out-of-range LOD link")
        target = placements[placement.lod_global_index]
        if not target.is_lod:
            raise ValueError(f"placement {placement.global_index} has a non-LOD target")
    return models, placements, repairs


def canonicalize_rw_version(data: bytes, name: str) -> tuple[bytes, int, list[dict[str, int]]]:
    """Validate a current RenderWare tree and repair closed source defects.

    A legacy RenderWare version changes serialized layouts, so changing header
    words is not a conversion. Legacy files must first pass through the pinned
    librw deserializer/re-serializer. Plugin payloads stay opaque.
    """

    if len(data) < 12:
        raise ValueError(f"{name}: truncated RenderWare root")
    output = bytearray(data)
    source_sha = hashlib.sha256(data).hexdigest()
    root_type, root_length, root_version = struct.unpack_from("<III", output, 0)
    root_end = 12 + root_length
    if root_type not in (0x10, 0x16) or root_version not in ALLOWED_RW_VERSIONS or root_end > len(output) or any(output[root_end:]):
        raise ValueError(f"{name}: invalid RenderWare root/version/padding")
    changed = 0
    repaired_overruns: list[dict[str, int]] = []

    def walk(begin: int, end: int, parent_is_extension: bool) -> None:
        nonlocal changed
        offset = begin
        while offset < end:
            if offset + 12 > end:
                raise ValueError(f"{name}: truncated RenderWare child")
            chunk_type, payload_bytes, version = struct.unpack_from("<III", output, offset)
            child_end = offset + 12 + payload_bytes
            if (
                parent_is_extension
                and chunk_type == 0x0253F2F8
                and child_end > end
                and offset + 16 == end
                and struct.unpack_from("<I", output, offset + 12)[0] * 100 + 4 == payload_bytes
            ):
                # Two audited Carcer DFFs end a geometry extension with a
                # 2DFX header claiming records that are not present; the next
                # bytes are already clump children. Canonicalize the physically
                # present empty plugin rather than reading across its parent.
                repaired_overruns.append(
                    {
                        "offset": offset,
                        "claimed_effects": struct.unpack_from("<I", output, offset + 12)[0],
                        "claimed_bytes": payload_bytes,
                        "unrecoverable_source_2dfx": struct.unpack_from("<I", output, offset + 12)[0],
                    }
                )
                struct.pack_into("<I", output, offset + 4, 4)
                struct.pack_into("<I", output, offset + 12, 0)
                payload_bytes = 4
                child_end = end
            if version not in ALLOWED_RW_VERSIONS or child_end > end:
                raise ValueError(f"{name}: RenderWare child version/boundary is invalid")
            if not parent_is_extension and chunk_type in RW_CONTAINERS:
                walk(offset + 12, child_end, chunk_type == 0x03)
            offset = child_end
        if offset != end:
            raise ValueError(f"{name}: RenderWare children do not consume their parent")

    walk(12, root_end, False)
    result = bytes(output[:root_end])
    if repaired_overruns:
        expected = MALFORMED_2DFX_REPAIRS.get(name)
        result_sha = hashlib.sha256(result).hexdigest()
        if expected is None or source_sha != expected[0] or result_sha != expected[1] or len(repaired_overruns) != 1:
            raise ValueError(f"{name}: malformed 2DFX repair identity is not the closed reviewed defect")
    return result, changed, repaired_overruns


def _validate_bin_mesh_v3(
    data: bytearray,
    extension: object,
    *,
    vertices: int,
    triangles: int,
    materials: int,
    name: str,
) -> None:
    plugins = _rw_children(data, extension, name)
    if any(plugin.chunk_type not in (0x0105, 0x050E, 0x0253F2F8, 0x0253F2F9, 0x0253F2FD) for plugin in plugins):
        raise ValueError(f"{name}: geometry extension contains an unknown v3 plugin")
    if len({plugin.chunk_type for plugin in plugins}) != len(plugins):
        raise ValueError(f"{name}: geometry extension contains a duplicate plugin")
    bin_meshes = [plugin for plugin in plugins if plugin.chunk_type == 0x050E]
    if len(bin_meshes) != 1:
        raise ValueError(f"{name}: geometry extension must contain exactly one BinMesh")
    plugin = bin_meshes[0]
    if plugin.end - plugin.payload_begin < 12:
        raise ValueError(f"{name}: truncated BinMesh header")
    flags, meshes, total_indices = struct.unpack_from("<III", data, plugin.payload_begin)
    if flags not in (0, 1) or meshes != materials or meshes > 146 or total_indices > 65_577:
        raise ValueError(f"{name}: BinMesh header exceeds the frozen v3 profile")
    cursor = plugin.payload_begin + 12
    counted = 0
    seen_materials: set[int] = set()
    for _ in range(meshes):
        if cursor + 8 > plugin.end:
            raise ValueError(f"{name}: truncated BinMesh split header")
        split_indices, material = struct.unpack_from("<II", data, cursor)
        cursor += 8
        split_end = cursor + split_indices * 4
        if (
            split_end > plugin.end
            or material >= materials
            or material in seen_materials
            or (flags == 0 and split_indices % 3)
        ):
            raise ValueError(f"{name}: BinMesh split is invalid")
        seen_materials.add(material)
        if split_indices and any(index >= vertices for index in struct.unpack_from(f"<{split_indices}I", data, cursor)):
            raise ValueError(f"{name}: BinMesh vertex index is out of range")
        counted += split_indices
        cursor = split_end
    if (
        cursor != plugin.end
        or counted != total_indices
        or seen_materials != set(range(materials))
        or (flags == 0 and total_indices != triangles * 3)
    ):
        raise ValueError(f"{name}: BinMesh splits do not exactly consume their header")
    for extra_colors in (item for item in plugins if item.chunk_type == 0x0253F2F9):
        if extra_colors.end - extra_colors.payload_begin != 4 + vertices * 4:
            raise ValueError(f"{name}: ExtraColors payload differs from its vertex count")
    for morph in (item for item in plugins if item.chunk_type == 0x0105):
        if morph.end - morph.payload_begin != 4 or any(data[morph.payload_begin : morph.end]):
            raise ValueError(f"{name}: legacy Morph plugin is not the closed empty form")
    for effects in (item for item in plugins if item.chunk_type == 0x0253F2F8):
        _validate_2dfx_v3(data, effects, name)
    for breakable in (item for item in plugins if item.chunk_type == 0x0253F2FD):
        # Underground contains several engine-specific Breakable payload
        # dialects. Keep them opaque, but only after the RW chunk parser has
        # proved their exact parent boundary and the empty form is canonical.
        payload_bytes = breakable.end - breakable.payload_begin
        if payload_bytes < 4 or (struct.unpack_from("<I", data, breakable.payload_begin)[0] == 0 and payload_bytes != 4):
            raise ValueError(f"{name}: Breakable plugin boundary is invalid")


def _validate_2dfx_v3(data: bytearray, plugin: object, name: str) -> None:
    """Validate librwgta's count + Effect2dHeader + type-sized payload stream."""

    allowed_sizes = {
        0: {76, 80},  # light, two catalog stream revisions
        1: {24},      # particle
        3: {56},      # ped queue
        4: {0},       # sun glare
        9: {12},      # cover point
        10: {40},     # escalator
    }
    payload_bytes = plugin.end - plugin.payload_begin
    if payload_bytes < 4:
        raise ValueError(f"{name}: truncated 2DFX count")
    effects = struct.unpack_from("<I", data, plugin.payload_begin)[0]
    cursor = plugin.payload_begin + 4
    for index in range(effects):
        if cursor + 20 > plugin.end:
            raise ValueError(f"{name}: truncated 2DFX header at effect {index}")
        position = struct.unpack_from("<3f", data, cursor)
        effect_type, size = struct.unpack_from("<ii", data, cursor + 12)
        cursor += 20
        if (
            any(not math.isfinite(value) or abs(value) > 1_000_000.0 for value in position)
            or effect_type not in allowed_sizes
            or size not in allowed_sizes[effect_type]
            or cursor + size > plugin.end
        ):
            raise ValueError(f"{name}: 2DFX type/size/position is outside the frozen v3 profile")
        if effect_type == 0:
            semantic = struct.unpack_from("<4f", data, cursor + 4)
        elif effect_type == 3:
            semantic = struct.unpack_from("<9f", data, cursor + 4)
        elif effect_type == 9:
            semantic = struct.unpack_from("<2f", data, cursor)
        elif effect_type == 10:
            semantic = struct.unpack_from("<9f", data, cursor)
        else:
            semantic = ()
        if any(not math.isfinite(value) or abs(value) > 1_000_000.0 for value in semantic):
            raise ValueError(f"{name}: 2DFX semantic float is outside the frozen v3 profile")
        cursor += size
    if cursor != plugin.end:
        raise ValueError(f"{name}: 2DFX records do not consume their plugin")


def normalize_dff_uvs(
    data: bytes,
    name: str,
    *,
    allow_canonical_zero_triangle: bool = False,
    source_sha256: str | None = None,
) -> tuple[bytes, list[int], list[dict[str, object]]]:
    """Normalize only non-finite UV coordinates in a structurally parsed DFF."""

    output = bytearray(data)
    source_sha = hashlib.sha256(data).hexdigest()
    root = _rw_chunk(output, 0, len(output), name)
    children = _rw_children(output, root, name)
    if root.chunk_type != 0x10 or len(children) < 4 or children[2].chunk_type != 0x1A:
        raise ValueError(f"{name}: unsupported clump grammar")
    geometries = _rw_children(output, children[2], name)
    if not geometries or geometries[0].chunk_type != 0x01:
        raise ValueError(f"{name}: geometry-list struct is absent")
    geometry_count = struct.unpack_from("<I", output, geometries[0].payload_begin)[0]
    if geometry_count + 1 != len(geometries):
        raise ValueError(f"{name}: geometry-list count differs from its children")
    normalized: list[int] = []
    zero_triangle_exceptions: list[dict[str, object]] = []
    for geometry in geometries[1:]:
        parts = _rw_children(output, geometry, name)
        if geometry.chunk_type != 0x0F or len(parts) != 3 or parts[0].chunk_type != 0x01:
            raise ValueError(f"{name}: geometry grammar is invalid")
        structure = parts[0]
        if structure.end - structure.payload_begin < 40:
            raise ValueError(f"{name}: truncated geometry structure")
        flags, triangles, vertices, morph_targets = struct.unpack_from("<4I", output, structure.payload_begin)
        texture_sets = (flags >> 16) & 0xFF
        if not texture_sets:
            texture_sets = 2 if flags & 0x80 else 1 if flags & 0x04 else 0
        if not vertices or morph_targets != 1 or texture_sets > 8:
            raise ValueError(f"{name}: invalid geometry counts or texture-set count")
        material_parts = _rw_children(output, parts[1], name)
        if not material_parts or material_parts[0].chunk_type != 0x01:
            raise ValueError(f"{name}: material-list structure is absent")
        materials = struct.unpack_from("<I", output, material_parts[0].payload_begin)[0]
        if (
            material_parts[0].end - material_parts[0].payload_begin != 4 + materials * 4
            or len(material_parts) != materials + 1
            or any(material.chunk_type != 0x07 for material in material_parts[1:])
        ):
            raise ValueError(f"{name}: material-list count/boundary is invalid")
        if not triangles:
            expected = ZERO_TRIANGLE_DFFS.get(name)
            canonical_match = next(
                (
                    (source_path, identity)
                    for source_path, identity in ZERO_TRIANGLE_DFFS.items()
                    if allow_canonical_zero_triangle and identity[1:] == (source_sha, vertices)
                ),
                None,
            )
            source_match = (
                expected is not None
                and expected[0] == source_sha256
                and expected[1:] == (source_sha, vertices)
            )
            if not source_match and canonical_match is None:
                raise ValueError(f"{name}: zero-triangle geometry is not a closed reviewed exception")
            source_path = name if expected else canonical_match[0]
            zero_triangle_exceptions.append(
                {
                    "source": source_path,
                    "source_sha256": (expected or canonical_match[1])[0],
                    "canonical_sha256": source_sha,
                    "vertices": vertices,
                    "triangles": 0,
                }
            )
        _validate_bin_mesh_v3(
            output,
            parts[2],
            vertices=vertices,
            triangles=triangles,
            materials=materials,
            name=name,
        )
        cursor = structure.payload_begin + 16 + (vertices * 4 if flags & 0x08 else 0)
        uv_count = vertices * texture_sets * 2
        uv_end = cursor + uv_count * 4
        if uv_end > structure.end:
            raise ValueError(f"{name}: UV arrays cross the geometry structure")
        for offset in range(cursor, uv_end, 4):
            value = struct.unpack_from("<f", output, offset)[0]
            if not math.isfinite(value):
                struct.pack_into("<I", output, offset, 0)
                normalized.append(offset)
        for triangle in range(triangles):
            triangle_begin = uv_end + triangle * 8
            index_a, index_b, material, index_c = struct.unpack_from("<4H", output, triangle_begin)
            if max(index_a, index_b, index_c) >= vertices or material >= materials:
                raise ValueError(f"{name}: geometry triangle index is out of range")
        cursor = uv_end + triangles * 8
        if cursor + 24 > structure.end:
            raise ValueError(f"{name}: morph target header is truncated")
        semantic_ranges = [(cursor, 4)]
        has_vertices, has_normals = struct.unpack_from("<II", output, cursor + 16)
        cursor += 24
        if has_vertices:
            semantic_ranges.append((cursor, vertices * 3))
            cursor += vertices * 12
        if has_normals:
            semantic_ranges.append((cursor, vertices * 3))
            cursor += vertices * 12
        if cursor != structure.end:
            raise ValueError(f"{name}: geometry structure has unreviewed trailing bytes")
        for begin, count in semantic_ranges:
            for offset in range(begin, begin + count * 4, 4):
                value = struct.unpack_from("<f", output, offset)[0]
                if not math.isfinite(value) or abs(value) > 1_000_000.0:
                    raise ValueError(f"{name}: non-UV semantic float is invalid")
    return bytes(output), normalized, zero_triangle_exceptions


def checked_u64_add(total: int, value: int, limit: int, context: str) -> int:
    if total < 0 or value < 0 or total > UINT64_MAX - value:
        raise ValueError(f"{context}: unsigned 64-bit byte accounting overflow")
    result = total + value
    if result > limit:
        raise ValueError(f"{context}: compiled byte budget exceeded ({result} > {limit})")
    return result


def merge_profile(
    aggregate: Counter[str],
    member: Counter[str],
    *,
    context: str,
    byte_limits: dict[str, int] | None = None,
) -> None:
    byte_limits = byte_limits or {}
    for key, value in member.items():
        if key.startswith("max_"):
            aggregate[key] = max(aggregate[key], value)
        else:
            aggregate[key] = checked_u64_add(
                aggregate[key],
                value,
                byte_limits.get(key, UINT64_MAX),
                f"{context} {key}",
            )


def canonicalize_txd_duplicates(data: bytes, name: str) -> tuple[bytes, list[dict[str, object]]]:
    """Drop later case-insensitive texture duplicates, preserving GTA lookup.

    RenderWare appends textures while reading a dictionary and name lookup
    returns the first match. Later duplicates are therefore unreachable. The
    report retains both hashes so this first-wins canonicalization is auditable
    even when the dead duplicate has different pixels.
    """

    validate_static_txd_v3_grammar(data, name)
    canonical = bytearray(data)
    root = _rw_chunk(canonical, 0, len(canonical), name)
    children = _rw_children(canonical, root, name)
    textures = children[1:-1]
    seen: dict[str, tuple[int, object]] = {}
    dropped: set[int] = set()
    records: list[dict[str, object]] = []
    for index, texture in enumerate(textures):
        structure = _rw_children(canonical, texture, name)[0]
        for field_offset in (8, 40):
            begin = structure.payload_begin + field_offset
            raw = canonical[begin : begin + 32]
            terminator = raw.find(0)
            if any(raw[terminator + 1 :]):
                records.append({"kind": "padding-canonicalized", "texture_index": index, "field_offset": field_offset})
                canonical[begin + terminator + 1 : begin + 32] = b"\0" * (31 - terminator)
        raw_name = canonical[structure.payload_begin + 8 : structure.payload_begin + 40]
        terminator = raw_name.find(b"\0")
        if terminator <= 0 or any(raw_name[terminator + 1 :]):
            raise ValueError(f"{name}: invalid native texture name at index {index}")
        texture_name = raw_name[:terminator].decode("ascii")
        key = texture_name.casefold()
        previous = seen.get(key)
        if previous is None:
            seen[key] = (index, texture)
            continue
        previous_index, previous_texture = previous
        dropped.add(index)
        previous_bytes = canonical[previous_texture.begin : previous_texture.end]
        dropped_bytes = canonical[texture.begin : texture.end]
        records.append(
            {
                "kind": "first-wins-duplicate",
                "key": key,
                "kept_index": previous_index,
                "dropped_index": index,
                "identical": previous_bytes == dropped_bytes,
                "kept_sha256": hashlib.sha256(previous_bytes).hexdigest(),
                "dropped_sha256": hashlib.sha256(dropped_bytes).hexdigest(),
            }
        )
    if not dropped:
        result = bytes(canonical)
        validate_static_txd_v3_grammar(result, name)
        return result, records
    rebuilt = []
    for child_index, child in enumerate(children):
        if 1 <= child_index <= len(textures) and child_index - 1 in dropped:
            continue
        encoded = bytearray(canonical[child.begin : child.end])
        if child_index == 0:
            struct.pack_into("<H", encoded, child.payload_begin - child.begin, len(textures) - len(dropped))
        rebuilt.append(bytes(encoded))
    payload = b"".join(rebuilt)
    header = bytearray(canonical[:12])
    struct.pack_into("<I", header, 4, len(payload))
    result = bytes(header) + payload
    validate_static_txd_v3_grammar(result, name)
    return result, records


def validate_static_txd_v3_grammar(data: bytes, name: str) -> Counter[str]:
    """Validate the exact D3D9 static-texture dialect present in the catalog."""

    root = _rw_chunk(data, 0, len(data), name)
    if root.chunk_type != 0x16 or any(data[root.end:]):
        raise ValueError(f"{name}: invalid TXD root or nonzero padding")
    children = _rw_children(data, root, name)
    if len(children) < 2 or children[0].chunk_type != 0x01 or children[0].end - children[0].payload_begin != 4 or children[-1].chunk_type != 0x03:
        raise ValueError(f"{name}: invalid TXD root grammar")
    texture_count, device = struct.unpack_from("<HH", data, children[0].payload_begin)
    if device not in (0, 2) or texture_count + 2 != len(children) or _rw_children(data, children[-1], name):
        raise ValueError(f"{name}: invalid TXD count, device, or root extension")

    stats: Counter[str] = Counter()
    stats["txds"] = 1
    stats["max_txd_textures"] = texture_count
    for index, texture in enumerate(children[1:-1]):
        parts = _rw_children(data, texture, name)
        if texture.chunk_type != 0x15 or len(parts) != 2 or parts[0].chunk_type != 0x01 or parts[1].chunk_type != 0x03:
            raise ValueError(f"{name}: invalid native texture grammar at {index}")
        structure = parts[0]
        begin = structure.payload_begin
        if structure.end - begin < 92:
            raise ValueError(f"{name}: truncated native texture header at {index}")
        platform, filter_flags = struct.unpack_from("<II", data, begin)
        raster_format, d3d_format = struct.unpack_from("<II", data, begin + 72)
        width, height = struct.unpack_from("<HH", data, begin + 80)
        depth, levels, raster_type, flags = struct.unpack_from("<4B", data, begin + 84)
        if (
            platform != 9
            or filter_flags not in (0x1101, 0x1102, 0x1105, 0x1106, 0x1202, 0x2102)
            or not width
            or width > 2048
            or not height
            or height > 2048
            or not levels
            or levels > 12
            or levels > max(width, height).bit_length()
            or raster_type != 4
            or (raster_format, d3d_format, depth, flags) not in TXD_D3D9_HEADER_TUPLES
        ):
            raise ValueError(f"{name}: native texture header is outside static-world-v3 at {index}")
        for field_begin in (begin + 8, begin + 40):
            raw = data[field_begin : field_begin + 32]
            terminator = raw.find(b"\0")
            if terminator < 0 or (field_begin == begin + 8 and terminator == 0):
                raise ValueError(f"{name}: invalid padded texture name at {index}")
            raw[:terminator].decode("ascii")

        compressed = d3d_format in (0x31545844, 0x32545844, 0x33545844, 0x34545844, 0x35545844)
        raw32 = d3d_format in (0x15, 0x16)
        if not compressed and not raw32:
            raise ValueError(f"{name}: unsupported D3D format at {index}")
        cursor = begin + 88
        total_gpu = 0
        total_decoded = 0
        for level in range(levels):
            level_width = max(1, width >> level)
            level_height = max(1, height >> level)
            if compressed:
                block_bytes = 8 if d3d_format == 0x31545844 else 16
                expected = ((level_width + 3) // 4) * ((level_height + 3) // 4) * block_bytes
            else:
                expected = level_width * level_height * 4
            if cursor + 4 > structure.end:
                raise ValueError(f"{name}: truncated mip length at {index}")
            serialized = struct.unpack_from("<I", data, cursor)[0]
            cursor += 4
            allow_legacy_empty_tail = (
                serialized == 0
                and d3d_format in (0x31545844, 0x33545844)
                and min(level_width, level_height) < 4
            )
            if (serialized != expected and not allow_legacy_empty_tail) or cursor + serialized > structure.end:
                raise ValueError(f"{name}: native mip byte count differs from its D3D format at {index}")
            cursor += serialized
            total_gpu = checked_u64_add(total_gpu, serialized, MAX_TEXTURE_GPU_BYTES, f"{name} texture {index} GPU")
            total_decoded = checked_u64_add(
                total_decoded,
                level_width * level_height * 4,
                MAX_TEXTURE_DECODED_BYTES,
                f"{name} texture {index} decoded",
            )
        if cursor != structure.end:
            raise ValueError(f"{name}: native texture payload exceeds the v3 member budget at {index}")
        plugins = _rw_children(data, parts[1], name)
        if len(plugins) > 1 or any(
            plugin.chunk_type != 0x127 or plugin.end - plugin.payload_begin != 4 or data[plugin.payload_begin : plugin.end] != b"\x10\0\0\0"
            for plugin in plugins
        ):
            raise ValueError(f"{name}: native texture extension is outside static-world-v3 at {index}")
        stats["textures"] += 1
        stats["serialized_gpu_bytes"] = checked_u64_add(
            stats["serialized_gpu_bytes"], total_gpu, MAX_TXD_GPU_BYTES, f"{name} TXD GPU"
        )
        stats["decoded_rgba_bytes"] = checked_u64_add(
            stats["decoded_rgba_bytes"], total_decoded, MAX_TXD_DECODED_BYTES, f"{name} TXD decoded"
        )
        stats["max_texture_gpu_bytes"] = max(stats["max_texture_gpu_bytes"], total_gpu)
        stats["max_texture_decoded_bytes"] = max(stats["max_texture_decoded_bytes"], total_decoded)
        stats["max_width"] = max(stats["max_width"], width)
        stats["max_height"] = max(stats["max_height"], height)
        stats["max_levels"] = max(stats["max_levels"], levels)
        stats["npot"] += int(width & (width - 1) != 0 or height & (height - 1) != 0)
        stats["over_1024"] += int(width > 1024 or height > 1024)
        stats[f"filter_0x{filter_flags:08x}"] += 1
        stats[f"format_0x{d3d_format:08x}"] += 1
    return stats


def _finite_col_floats(record: bytes, offset: int, count: int, name: str) -> tuple[float, ...]:
    end = offset + count * 4
    if offset < 0 or end > len(record):
        raise ValueError(f"{name}: COL float array crosses its record")
    values = struct.unpack_from(f"<{count}f", record, offset)
    if any(not math.isfinite(value) or abs(value) > 1_000_000.0 for value in values):
        raise ValueError(f"{name}: COL semantic float is outside the v3 profile")
    return values


def _validate_coll_v3(record: bytes, name: str) -> Counter[str]:
    if len(record) < 72:
        raise ValueError(f"{name}: truncated COLL bounds")
    bounds = _finite_col_floats(record, 32, 10, name)
    if bounds[0] < 0 or any(bounds[4 + axis] > bounds[7 + axis] for axis in range(3)):
        raise ValueError(f"{name}: invalid COLL bounds")
    cursor = 72

    def count(label: str, limit: int) -> int:
        nonlocal cursor
        if cursor + 4 > len(record):
            raise ValueError(f"{name}: truncated COLL {label} count")
        value = struct.unpack_from("<I", record, cursor)[0]
        cursor += 4
        if value > limit:
            raise ValueError(f"{name}: COLL {label} count exceeds the v3 profile")
        return value

    def consume(amount: int, stride: int, label: str) -> int:
        nonlocal cursor
        begin = cursor
        cursor += amount * stride
        if cursor > len(record):
            raise ValueError(f"{name}: COLL {label} crosses its record")
        return begin

    spheres = count("sphere", MAX_COL_SPHERES)
    sphere_begin = consume(spheres, 20, "spheres")
    for index in range(spheres):
        values = _finite_col_floats(record, sphere_begin + index * 20, 4, name)
        if values[0] < 0:
            raise ValueError(f"{name}: negative COLL sphere radius")
    lines = count("line", 0)
    line_begin = consume(lines, 24, "lines")
    for index in range(lines):
        _finite_col_floats(record, line_begin + index * 24, 6, name)
    boxes = count("box", MAX_COL_BOXES)
    box_begin = consume(boxes, 28, "boxes")
    for index in range(boxes):
        values = _finite_col_floats(record, box_begin + index * 28, 6, name)
        if any(values[axis] > values[3 + axis] for axis in range(3)):
            raise ValueError(f"{name}: inverted COLL box")
    vertices = count("vertex", MAX_COL_VERTICES)
    vertex_begin = consume(vertices, 12, "vertices")
    for index in range(vertices):
        _finite_col_floats(record, vertex_begin + index * 12, 3, name)
    faces = count("face", MAX_COL_FACES)
    face_begin = consume(faces, 16, "faces")
    for index in range(faces):
        if any(vertex >= vertices for vertex in struct.unpack_from("<3I", record, face_begin + index * 16)):
            raise ValueError(f"{name}: COLL face vertex index is out of range")
    if cursor != len(record):
        raise ValueError(f"{name}: trailing bytes after COLL arrays")
    return Counter(
        records=1,
        bytes=len(record),
        spheres=spheres,
        boxes=boxes,
        lines=lines,
        vertices=vertices,
        faces=faces,
        face_groups=0,
        shadow_vertices=0,
        shadow_faces=0,
    )


def _validate_col3_v3(record: bytes, name: str) -> Counter[str]:
    if len(record) < 120:
        raise ValueError(f"{name}: truncated COL3 header")
    bounds = _finite_col_floats(record, 32, 10, name)
    if bounds[9] < 0 or any(bounds[axis] > bounds[3 + axis] for axis in range(3)):
        raise ValueError(f"{name}: invalid COL3 bounds")
    spheres, boxes, faces, lines = struct.unpack_from("<HHHB", record, 72)
    flags = struct.unpack_from("<I", record, 80)[0]
    sphere_raw, box_raw, line_raw, vertex_raw, face_raw, plane_raw = struct.unpack_from("<6I", record, 84)
    shadow_faces, shadow_vertex_raw, shadow_face_raw = struct.unpack_from("<3I", record, 108)
    if (
        record[79] != 0
        or spheres > MAX_COL_SPHERES
        or boxes > MAX_COL_BOXES
        or faces > MAX_COL_FACES
        or lines
        or flags not in (0, 2, 10, 18)
        or line_raw
        or plane_raw
        or shadow_faces > MAX_COL_SHADOW_FACES
    ):
        raise ValueError(f"{name}: COL3 counts/flags exceed the v3 profile")
    contents = bool(spheres or boxes or faces or shadow_faces)
    if bool(flags & 2) != contents or bool(flags & 8) != bool(faces and flags & 8) or bool(flags & 0x10) != bool(shadow_faces):
        raise ValueError(f"{name}: COL3 content flags do not match its arrays")

    cursor = 120

    def consume(count: int, raw: int, stride: int, label: str) -> int:
        nonlocal cursor
        if not count:
            if raw:
                raise ValueError(f"{name}: COL3 {label} offset without a count")
            return cursor
        begin = 4 + raw
        if not raw or begin != cursor:
            raise ValueError(f"{name}: COL3 {label} offset is noncanonical")
        cursor += count * stride
        if cursor > len(record):
            raise ValueError(f"{name}: COL3 {label} crosses its record")
        return begin

    sphere_begin = consume(spheres, sphere_raw, 20, "spheres")
    box_begin = consume(boxes, box_raw, 28, "boxes")
    for index in range(spheres):
        values = _finite_col_floats(record, sphere_begin + index * 20, 4, name)
        if values[3] < 0:
            raise ValueError(f"{name}: negative COL3 sphere radius")
    for index in range(boxes):
        values = _finite_col_floats(record, box_begin + index * 28, 6, name)
        if any(values[axis] > values[3 + axis] for axis in range(3)):
            raise ValueError(f"{name}: inverted COL3 box")

    vertices = 0
    face_groups = 0
    if faces:
        vertex_begin = 4 + vertex_raw
        face_begin = 4 + face_raw
        if not vertex_raw or not face_raw or vertex_begin != cursor or face_begin > len(record):
            raise ValueError(f"{name}: COL3 vertex/face offsets are invalid")
        vertex_end = face_begin
        if flags & 8:
            if face_begin < 4:
                raise ValueError(f"{name}: COL3 face-group count underflows")
            face_groups = struct.unpack_from("<I", record, face_begin - 4)[0]
            group_bytes = face_groups * 28 + 4
            if not face_groups or face_groups > MAX_COL_FACE_GROUPS or group_bytes > face_begin - vertex_begin:
                raise ValueError(f"{name}: COL3 face-group table is invalid")
            vertex_end = face_begin - group_bytes
            previous_last = -1
            for index in range(face_groups):
                begin = vertex_end + index * 28
                values = _finite_col_floats(record, begin, 6, name)
                first, last = struct.unpack_from("<HH", record, begin + 24)
                if (
                    any(values[axis] > values[3 + axis] for axis in range(3))
                    or first != previous_last + 1
                    or first > last
                    or last >= faces
                ):
                    raise ValueError(f"{name}: COL3 face-group coverage is invalid")
                previous_last = last
            if previous_last != faces - 1:
                raise ValueError(f"{name}: COL3 face groups do not cover every face")
        vertex_bytes = vertex_end - vertex_begin
        padding = vertex_bytes % 6
        vertices = vertex_bytes // 6
        if (
            not vertices
            or vertices > MAX_COL_VERTICES
            or padding not in (0, 2)
            or any(record[vertex_begin + vertices * 6 : vertex_end])
        ):
            raise ValueError(f"{name}: COL3 vertex array/padding is invalid")
        core_end = face_begin + faces * 8
        if core_end > len(record):
            raise ValueError(f"{name}: COL3 face array crosses its record")
        for index in range(faces):
            if any(vertex >= vertices for vertex in struct.unpack_from("<3H", record, face_begin + index * 8)):
                raise ValueError(f"{name}: COL3 face vertex index is out of range")
    else:
        if vertex_raw or face_raw or flags & 8:
            raise ValueError(f"{name}: empty COL3 face layout is noncanonical")
        core_end = cursor

    shadow_vertices = 0
    if shadow_faces:
        shadow_vertex_begin = 4 + shadow_vertex_raw
        shadow_face_begin = 4 + shadow_face_raw
        if (
            not shadow_vertex_raw
            or not shadow_face_raw
            or shadow_vertex_begin != core_end
            or shadow_face_begin > len(record)
        ):
            raise ValueError(f"{name}: COL3 shadow offsets are invalid")
        shadow_vertex_bytes = shadow_face_begin - shadow_vertex_begin
        padding = shadow_vertex_bytes % 6
        shadow_vertices = shadow_vertex_bytes // 6
        if (
            not shadow_vertices
            or shadow_vertices > MAX_COL_SHADOW_VERTICES
            or padding not in (0, 2)
            or any(record[shadow_vertex_begin + shadow_vertices * 6 : shadow_face_begin])
            or shadow_face_begin + shadow_faces * 8 != len(record)
        ):
            raise ValueError(f"{name}: COL3 shadow vertex/face layout is invalid")
        for index in range(shadow_faces):
            if any(vertex >= shadow_vertices for vertex in struct.unpack_from("<3H", record, shadow_face_begin + index * 8)):
                raise ValueError(f"{name}: COL3 shadow face vertex index is out of range")
    elif shadow_vertex_raw or shadow_face_raw or core_end != len(record):
        raise ValueError(f"{name}: empty COL3 shadow layout is noncanonical")

    return Counter(
        records=1,
        bytes=len(record),
        spheres=spheres,
        boxes=boxes,
        lines=lines,
        vertices=vertices,
        faces=faces,
        face_groups=face_groups,
        shadow_vertices=shadow_vertices,
        shadow_faces=shadow_faces,
    )


def validate_static_col_record_v3(record: bytes, name: str) -> Counter[str]:
    if len(record) < 32 or len(record) > MAX_COL_RECORD_BYTES:
        raise ValueError(f"{name}: COL record size is outside the native read buffer")
    record_bytes = 8 + struct.unpack_from("<I", record, 4)[0]
    if record_bytes != len(record):
        raise ValueError(f"{name}: COL record boundary is invalid")
    if record[:4] == b"COLL":
        return _validate_coll_v3(record, name)
    if record[:4] == b"COL3":
        return _validate_col3_v3(record, name)
    raise ValueError(f"{name}: unsupported canonical COL magic {record[:4]!r}")


def validate_static_col_member_v3(data: bytes, name: str) -> tuple[Counter[str], list[tuple[str, int, int, int]]]:
    records = parse_col_records(data)
    if not records:
        raise ValueError(f"{name}: COL member contains no records")
    stats: Counter[str] = Counter()
    seen_names: set[str] = set()
    seen_ids: set[int] = set()
    for model_name, model_id, offset, size in records:
        if model_name in seen_names or model_id in seen_ids:
            raise ValueError(f"{name}: duplicate COL model name or ID")
        seen_names.add(model_name)
        seen_ids.add(model_id)
        member = validate_static_col_record_v3(data[offset : offset + size], f"{name}:{model_name}")
        for key, value in member.items():
            stats[key] = checked_u64_add(stats[key], value, UINT64_MAX, f"{name} COL {key}")
            stats[f"max_{key}"] = max(stats[f"max_{key}"], value)
    consumed = max(offset + size for _, _, offset, size in records)
    if any(data[consumed:]):
        raise ValueError(f"{name}: nonzero COL allocation padding")
    return stats, records


def _convert_col2_bytes(record: bytes) -> bytes:
    output = bytearray(record[: 32 + 0x4C])
    output[:4] = b"COL3"
    output.extend(b"\0" * 12)
    output.extend(record[32 + 0x4C :])
    struct.pack_into("<I", output, 4, len(output) - 8)
    flags_offset = 32 + 0x30
    struct.pack_into("<I", output, flags_offset, struct.unpack_from("<I", output, flags_offset)[0] & ~0x10)
    for offset in range(32 + 0x34, 32 + 0x4C, 4):
        value = struct.unpack_from("<I", output, offset)[0]
        if value:
            struct.pack_into("<I", output, offset, value + 12)
    return bytes(output)


def convert_col2_to_col3(record: bytes, name: str) -> tuple[bytes, bool]:
    if len(record) < 32:
        raise ValueError(f"{name}: truncated COL record")
    record_bytes = 8 + struct.unpack_from("<I", record, 4)[0]
    if record_bytes > len(record) or any(record[record_bytes:]):
        raise ValueError(f"{name}: invalid COL record boundary")
    record = record[:record_bytes]
    if record[:4] != b"COL2":
        validate_static_col_record_v3(record, name)
        return record, False
    if len(record) < 32 + 0x4C:
        raise ValueError(f"{name}: truncated COL2 header")
    output = _convert_col2_bytes(record)
    if len(output) != len(record) + 12 or output[32 + 0x4C : 32 + 0x58] != b"\0" * 12:
        raise ValueError(f"{name}: COL2-to-COL3 conversion postcondition failed")
    # Parsing the virtual COL3 form validates every source count, offset,
    # payload array, index and padding byte before those bytes are admitted.
    validate_static_col_record_v3(output, name)
    return output, True


def make_variants(
    models: dict[int, GeneratedModel],
    placements: list[GeneratedPlacement],
    namespace: str,
    model_id_start: int,
) -> tuple[list[ModelVariant], dict[tuple[int, str], ModelVariant], dict[str | None, str]]:
    groups_by_model: dict[int, set[str]] = defaultdict(set)
    for placement in placements:
        if not placement.native:
            groups_by_model[placement.source_id].add(placement.source)
    primary_keys = [(source_id, sorted(groups)[0]) for source_id, groups in sorted(groups_by_model.items())]
    extra_keys = sorted((source_id, group) for source_id, groups in groups_by_model.items() for group in sorted(groups)[1:])
    variant_keys = primary_keys + extra_keys
    if not variant_keys or len(variant_keys) > MAX_MODELS or model_id_start + len(variant_keys) - 1 > MODEL_ID_LIMIT:
        raise ValueError("v3 model variants exceed the compiled model range")
    real_txd_paths = sorted(
        {models[source_id].txd_path for source_id, _ in variant_keys if models[source_id].txd_path is not None},
        key=str.casefold,
    )
    txd_paths: list[str | None] = list(real_txd_paths)
    if any(models[source_id].txd_path is None for source_id, _ in variant_keys):
        txd_paths.append(None)
    if len(txd_paths) > MAX_TXDS:
        raise ValueError("v3 TXD count exceeds the compiled policy")
    txd_names = {path: f"{namespace}t{base36(index, 3)}" for index, path in enumerate(txd_paths)}
    txd_hashes: dict[int, str] = {}
    for txd_name in txd_names.values():
        key = gta_uppercase_key(txd_name)
        if key in txd_hashes:
            raise ValueError(f"generated TXD key collision: {txd_hashes[key]} and {txd_name}")
        txd_hashes[key] = txd_name
    variants: list[ModelVariant] = []
    by_key: dict[tuple[int, str], ModelVariant] = {}
    hashes: dict[int, str] = {}
    for index, (source_id, group) in enumerate(variant_keys):
        native_name = f"{namespace}m{base36(index, 4)}"
        key = gta_uppercase_key(native_name)
        if key in hashes:
            raise ValueError(f"generated DFF key collision: {hashes[key]} and {native_name}")
        hashes[key] = native_name
        variant = ModelVariant(
            model=models[source_id],
            source_group=group,
            native_id=model_id_start + index,
            native_name=native_name,
            txd_name=txd_names[models[source_id].txd_path],
        )
        variants.append(variant)
        by_key[(source_id, group)] = variant
    return variants, by_key, txd_names


def write_ide(path: Path, variants: list[ModelVariant]) -> None:
    ordinary = [variant for variant in variants if variant.model.model_type != "timed-object"]
    timed = [variant for variant in variants if variant.model.model_type == "timed-object"]
    lines = ["# Generated by utils/extended-world/build_native_world_v3.py.", "objs"]
    for variant in ordinary:
        model = variant.model
        lines.append(
            f"{variant.native_id}, {variant.native_name}, {variant.txd_name}, 1, "
            f"{model.draw_distance:.9g}, {model.ide_flags}"
        )
    lines.extend(("end", "tobj"))
    for variant in timed:
        model = variant.model
        lines.append(
            f"{variant.native_id}, {variant.native_name}, {variant.txd_name}, 1, "
            f"{model.draw_distance:.9g}, {model.ide_flags}, {model.time_on}, {model.time_off}"
        )
    lines.extend(("end", ""))
    path.write_text("\n".join(lines), encoding="ascii")


def empty_txd_v3() -> bytes:
    return (
        struct.pack("<III", 0x16, 28, RW_LIBRARY_ID)
        + struct.pack("<IIIHH", 0x01, 4, RW_LIBRARY_ID, 0, 2)
        + struct.pack("<III", 0x03, 0, RW_LIBRARY_ID)
    )


def binary_ipl(
    group: list[GeneratedPlacement],
    variants: dict[tuple[int, str], ModelVariant],
) -> bytes:
    header = bytearray(BINARY_IPL_HEADER_SIZE)
    header[:4] = b"bnry"
    struct.pack_into("<I", header, 4, len(group))
    struct.pack_into("<I", header, 28, BINARY_IPL_HEADER_SIZE)
    output = bytearray(header)
    for placement in group:
        model_id = placement.source_id if placement.native else variants[(placement.source_id, placement.source)].native_id
        output.extend(
            BINARY_IPL_INSTANCE.pack(
                *placement.position,
                *placement.quaternion,
                model_id,
                0,
                -1,
            )
        )
    return bytes(output)


def build_lod_map(groups: dict[str, list[GeneratedPlacement]]) -> tuple[bytes, dict[str, int]]:
    """Encode pack-global LOD intent without exposing positive indices to GTA."""

    group_counts = [len(rows) for rows in groups.values()]
    if not 1 <= len(group_counts) <= MAX_SPATIAL_GROUPS or any(count == 0 for count in group_counts):
        raise ValueError("v3 LOD map requires bounded non-empty spatial groups")
    if not 1 <= sum(group_counts) <= MAX_PLACEMENTS:
        raise ValueError("v3 LOD map placement count exceeds the closed policy")
    canonical_by_global: dict[int, int] = {}
    placements_by_ordinal: list[GeneratedPlacement] = []
    for rows in groups.values():
        for placement in rows:
            canonical_by_global[placement.global_index] = len(placements_by_ordinal)
            placements_by_ordinal.append(placement)

    anchor_globals = sorted(
        {
            placement.lod_global_index
            for placement in placements_by_ordinal
            if placement.lod_global_index is not None
        }
    )
    anchor_ordinals = sorted(canonical_by_global[int(global_index)] for global_index in anchor_globals)
    anchor_index_by_ordinal = {ordinal: index for index, ordinal in enumerate(anchor_ordinals)}
    links = sorted(
        (
            canonical_by_global[placement.global_index],
            anchor_index_by_ordinal[canonical_by_global[int(placement.lod_global_index)]],
        )
        for placement in placements_by_ordinal
        if placement.lod_global_index is not None
    )
    if (
        len(anchor_ordinals) > MAX_LOD_ANCHORS
        or len(links) > MAX_LOD_LINKS
        or len(anchor_ordinals) + len(links) > MAX_LOD_SCRATCH_ENTRIES
    ):
        raise ValueError("v3 LOD map exceeds the registrar scratch contract")
    children = [child for child, _ in links]
    if (
        len(set(children)) != len(children)
        or set(children).intersection(anchor_ordinals)
        or sorted(anchor_index for _, anchor_index in links) != list(range(len(anchor_ordinals)))
        or len(links) != len(anchor_ordinals)
    ):
        raise ValueError("v3 LOD map is not one-to-one, acyclic, and child/anchor-disjoint")

    output = bytearray(
        LOD_MAP_HEADER.pack(
            LOD_MAP_MAGIC,
            LOD_MAP_VERSION,
            len(placements_by_ordinal),
            len(group_counts),
            len(anchor_ordinals),
            len(links),
            0,
        )
    )
    output.extend(struct.pack(f"<{len(group_counts)}I", *group_counts))
    output.extend(struct.pack(f"<{len(anchor_ordinals)}I", *anchor_ordinals))
    for child_ordinal, anchor_index in links:
        output.extend(LOD_MAP_LINK.pack(child_ordinal, anchor_index))

    boundaries: list[int] = []
    running = 0
    for count in group_counts:
        boundaries.append(running)
        running += count
    cross_group = 0
    for child, anchor_index in links:
        anchor = anchor_ordinals[anchor_index]
        child_group = max(index for index, start in enumerate(boundaries) if start <= child)
        anchor_group = max(index for index, start in enumerate(boundaries) if start <= anchor)
        cross_group += child_group != anchor_group
    return bytes(output), {
        "anchors": len(anchor_ordinals),
        "links": len(links),
        "cross_group_links": cross_group,
        "same_group_links": len(links) - cross_group,
        "scratch_entries": len(anchor_ordinals) + len(links),
    }


def validate_lod_map_v3(data: bytes, group_counts: list[int], name: str = "world.lod") -> dict[str, int]:
    if len(data) < LOD_MAP_HEADER.size:
        raise ValueError(f"{name}: truncated LOD map")
    magic, version, placement_count, group_count, anchor_count, link_count, reserved = LOD_MAP_HEADER.unpack_from(data)
    expected_bytes = LOD_MAP_HEADER.size + group_count * 4 + anchor_count * 4 + link_count * LOD_MAP_LINK.size
    if (
        magic != LOD_MAP_MAGIC
        or version != LOD_MAP_VERSION
        or reserved != 0
        or not 1 <= group_count <= MAX_SPATIAL_GROUPS
        or group_count != len(group_counts)
        or any(count == 0 for count in group_counts)
        or not 1 <= placement_count <= MAX_PLACEMENTS
        or placement_count != sum(group_counts)
        or anchor_count > MAX_LOD_ANCHORS
        or link_count > MAX_LOD_LINKS
        or anchor_count + link_count > MAX_LOD_SCRATCH_ENTRIES
        or len(data) != expected_bytes
    ):
        raise ValueError(f"{name}: LOD header, length, or budget is invalid")
    offset = LOD_MAP_HEADER.size
    encoded_group_counts = list(struct.unpack_from(f"<{group_count}I", data, offset))
    offset += group_count * 4
    anchors = list(struct.unpack_from(f"<{anchor_count}I", data, offset))
    offset += anchor_count * 4
    links = [LOD_MAP_LINK.unpack_from(data, offset + index * LOD_MAP_LINK.size) for index in range(link_count)]
    children = [child for child, _ in links]
    anchor_indices = [anchor_index for _, anchor_index in links]
    if (
        encoded_group_counts != group_counts
        or anchors != sorted(set(anchors))
        or any(anchor >= placement_count for anchor in anchors)
        or children != sorted(set(children))
        or any(child >= placement_count for child in children)
        or set(children).intersection(anchors)
        or any(anchor_index >= anchor_count for anchor_index in anchor_indices)
        or sorted(anchor_indices) != list(range(anchor_count))
        or anchor_count != link_count
    ):
        raise ValueError(f"{name}: LOD groups, ordinals, or one-to-one links are invalid")

    boundaries: list[int] = []
    running = 0
    for count in group_counts:
        boundaries.append(running)
        running += count
    cross_group = 0
    for child, anchor_index in links:
        anchor = anchors[anchor_index]
        child_group = max(index for index, start in enumerate(boundaries) if start <= child)
        anchor_group = max(index for index, start in enumerate(boundaries) if start <= anchor)
        cross_group += child_group != anchor_group
    return {
        "anchors": anchor_count,
        "links": link_count,
        "cross_group_links": cross_group,
        "same_group_links": link_count - cross_group,
        "scratch_entries": anchor_count + link_count,
    }


def partition_inputs(inputs: list[ArchiveInput]) -> list[list[ArchiveInput]]:
    ordered = sorted(inputs, key=lambda item: (-sectors_for(item.byte_size()), item.name.casefold()))
    bins: list[list[ArchiveInput]] = []
    for item in ordered:
        item_sectors = sectors_for(item.byte_size())
        if not item_sectors or item_sectors > 0xFFFF:
            raise ValueError(f"IMG member {item.name} exceeds the VER2 16-bit sector field")
        for bin_items in bins:
            directory = sectors_for(HEADER.size + (len(bin_items) + 1) * DIRECTORY_ENTRY.size)
            if directory + sum(sectors_for(existing.byte_size()) for existing in bin_items) + item_sectors <= MAX_IMG_SECTORS:
                bin_items.append(item)
                break
        else:
            directory = sectors_for(HEADER.size + DIRECTORY_ENTRY.size)
            if directory + item_sectors > MAX_IMG_SECTORS:
                raise ValueError(f"IMG member {item.name} cannot fit an empty v3 archive")
            bins.append([item])
    return bins


def _parse_v3_ide(path: Path) -> tuple[dict[int, str], set[str]]:
    models: dict[int, str] = {}
    txds: set[str] = set()
    section = ""
    seen_sections: set[str] = set()
    for raw_line in path.read_text(encoding="ascii").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line in ("objs", "tobj"):
            if section or line in seen_sections:
                raise ValueError("v3 IDE contains a duplicate or nested section")
            section = line
            seen_sections.add(line)
            continue
        if line == "end":
            if not section:
                raise ValueError("v3 IDE contains an unmatched end")
            section = ""
            continue
        if not section:
            raise ValueError("v3 IDE row is outside a reviewed section")
        fields = [field.strip() for field in line.split(",")]
        if len(fields) != (6 if section == "objs" else 8):
            raise ValueError("v3 IDE row has an invalid field count")
        model_id = int(fields[0], 10)
        model_name, txd_name = fields[1].casefold(), fields[2].casefold()
        mesh_count = int(fields[3], 10)
        draw_distance = float(fields[4])
        flags = int(fields[5], 10)
        timed_valid = section != "tobj" or (
            0 <= int(fields[6], 10) <= 23
            and 0 <= int(fields[7], 10) <= 23
            and int(fields[6], 10) != int(fields[7], 10)
        )
        if (
            model_id < FIRST_CUSTOM_ID
            or model_id > MODEL_ID_LIMIT
            or model_id in models
            or mesh_count != 1
            or not math.isfinite(draw_distance)
            or not 0 < draw_distance <= 1_000_000
            or not 0 <= flags <= 0x007FFFFF
            or not timed_valid
            or not re.fullmatch(r"[a-z0-9_]{1,15}", model_name)
            or not re.fullmatch(r"[a-z0-9_]{1,15}", txd_name)
        ):
            raise ValueError("v3 IDE contains a duplicate or unsafe identity")
        models[model_id] = model_name
        txds.add(txd_name)
    if section or seen_sections != {"objs", "tobj"} or not models or not txds:
        raise ValueError("v3 IDE sections or counts are incomplete")
    first_model = min(models)
    if sorted(models) != list(range(first_model, first_model + len(models))):
        raise ValueError("v3 IDE model IDs are not contiguous")
    namespace = models[first_model][:2]
    for ordinal, model_id in enumerate(range(first_model, first_model + len(models))):
        if models[model_id] != f"{namespace}m{base36(ordinal, 4)}":
            raise ValueError("v3 IDE model ID/name remap is not contiguous")
    if txds != {f"{namespace}t{base36(ordinal, 3)}" for ordinal in range(len(txds))}:
        raise ValueError("v3 IDE TXD names are not contiguous")
    return models, txds


def _read_img_member(path: Path, entry: object) -> bytes:
    with path.open("rb") as stream:
        stream.seek(entry.offset_sector * SECTOR_SIZE)
        data = stream.read(entry.size_sectors * SECTOR_SIZE)
    if len(data) != entry.size_sectors * SECTOR_SIZE:
        raise ValueError(f"{path.name}:{entry.name} is truncated")
    return data


def validate_binary_ipl_v3(data: bytes, name: str) -> Counter[str]:
    if len(data) < BINARY_IPL_HEADER_SIZE or data[:4] != b"bnry":
        raise ValueError(f"{name}: invalid binary IPL header")
    count = struct.unpack_from("<I", data, 4)[0]
    offset = struct.unpack_from("<I", data, 28)[0]
    expected_header = bytearray(BINARY_IPL_HEADER_SIZE)
    expected_header[:4] = b"bnry"
    struct.pack_into("<I", expected_header, 4, count)
    struct.pack_into("<I", expected_header, 28, BINARY_IPL_HEADER_SIZE)
    semantic_end = offset + count * BINARY_IPL_INSTANCE.size
    if (
        count > MAX_PLACEMENTS
        or offset != BINARY_IPL_HEADER_SIZE
        or data[:BINARY_IPL_HEADER_SIZE] != expected_header
        or semantic_end > len(data)
        or any(data[semantic_end:])
    ):
        raise ValueError(f"{name}: binary IPL layout is outside static-world-v3")
    native = 0
    for index in range(count):
        values = BINARY_IPL_INSTANCE.unpack_from(data, offset + index * BINARY_IPL_INSTANCE.size)
        if (
            any(not math.isfinite(value) or abs(value) > 1_000_000.0 for value in values[:7])
            or values[7] < 0
            or values[7] > MODEL_ID_LIMIT
            or values[8] != 0
            or values[9] != -1
        ):
            raise ValueError(f"{name}: binary IPL instance {index} is outside static-world-v3")
        native += int(values[7] < 18_631)
    return Counter(ipls=1, placements=count, native_placements=native, max_placements=count)


def verify_pack(output: Path) -> dict[str, object]:
    manifest_path = output / "native-world.json"
    manifest = parse_runtime_manifest(manifest_path.read_text(encoding="ascii"))
    if manifest["format"] != FORMAT or manifest["policy"] != STATIC_WORLD_V3_POLICY:
        raise ValueError("runtime manifest is not static-world-v3")
    ide_path = output / manifest["files"]["ide"]["name"]
    if (
        ide_path.stat().st_size != manifest["files"]["ide"]["bytes"]
        or sha256_file(ide_path) != manifest["files"]["ide"]["sha256"]
    ):
        raise ValueError("IDE hash differs from the runtime manifest")
    ide_models, ide_txds = _parse_v3_ide(ide_path)
    namespace = next(iter(ide_models.values()))[:2]
    if (
        not re.fullmatch(r"[a-z][a-z0-9]", namespace)
        or any(not re.fullmatch(re.escape(namespace) + r"m[a-z0-9]{4}", name) for name in ide_models.values())
        or any(not re.fullmatch(re.escape(namespace) + r"t[a-z0-9]{3}", name) for name in ide_txds)
    ):
        raise ValueError("v3 IDE model or TXD namespace is not canonical")
    lod_path = output / manifest["files"]["lod"]["name"]
    if (
        lod_path.stat().st_size != manifest["files"]["lod"]["bytes"]
        or sha256_file(lod_path) != manifest["files"]["lod"]["sha256"]
    ):
        raise ValueError("LOD map identity differs from the runtime manifest")
    all_entries: dict[str, tuple[str, object]] = {}
    archive_reports = []
    semantic_profile: dict[str, Counter[str]] = {
        "dff": Counter(),
        "txd": Counter(),
        "col": Counter(),
        "ipl": Counter(),
    }
    col_models: dict[int, str] = {}
    ipl_counts: dict[str, int] = {}
    for image in manifest["files"]["images"]:
        path = output / image["name"]
        if path.stat().st_size != image["bytes"] or sha256_file(path) != image["sha256"]:
            raise ValueError(f"{image['name']} identity differs from the runtime manifest")
        entries = read_img_directory(path)
        for entry in entries:
            if entry.name in all_entries:
                raise ValueError(f"duplicate cross-IMG entry {entry.name}")
            all_entries[entry.name] = (image["name"], entry)
            payload = _read_img_member(path, entry)
            extension = Path(entry.name).suffix
            if extension == ".dff":
                canonical, _, repairs = canonicalize_rw_version(payload, entry.name)
                normalized, normalized_offsets, zero_triangles = normalize_dff_uvs(
                    canonical, entry.name, allow_canonical_zero_triangle=True
                )
                if repairs or normalized_offsets or normalized != canonical:
                    raise ValueError(f"{entry.name}: generated DFF is not canonical")
                semantic_profile["dff"]["members"] += 1
                semantic_profile["dff"]["zero_triangle_exceptions"] += len(zero_triangles)
                semantic_profile["dff"]["max_member_bytes"] = max(
                    semantic_profile["dff"]["max_member_bytes"], len(canonical)
                )
            elif extension == ".txd":
                normalized, records = canonicalize_txd_duplicates(payload, entry.name)
                if records or normalized != payload:
                    raise ValueError(f"{entry.name}: generated TXD is not canonical")
                merge_profile(
                    semantic_profile["txd"],
                    validate_static_txd_v3_grammar(payload, entry.name),
                    context="generated TXD profile",
                    byte_limits={
                        "serialized_gpu_bytes": MAX_CITY_GPU_BYTES,
                        "decoded_rgba_bytes": MAX_CITY_DECODED_BYTES,
                    },
                )
            elif extension == ".col":
                member_stats, records = validate_static_col_member_v3(payload, entry.name)
                merge_profile(semantic_profile["col"], member_stats, context="generated COL profile")
                for model_name, model_id, _, _ in records:
                    if model_id in col_models:
                        raise ValueError(f"{entry.name}: duplicate cross-IMG COL model ID")
                    col_models[model_id] = model_name
            elif extension == ".ipl":
                member_ipl = validate_binary_ipl_v3(payload, entry.name)
                merge_profile(semantic_profile["ipl"], member_ipl, context="generated IPL profile")
                ipl_counts[entry.name] = member_ipl["placements"]
            else:
                raise ValueError(f"{entry.name}: unsupported v3 IMG member type")
        archive_reports.append(
            {
                "name": image["name"],
                "bytes": image["bytes"],
                "sha256": image["sha256"],
                "entries": len(entries),
                "sectors": sectors_for(image["bytes"]),
            }
        )
    expected_types = Counter(Path(name).suffix for name in all_entries)
    if not expected_types[".dff"] or not expected_types[".txd"] or not expected_types[".col"] or not expected_types[".ipl"]:
        raise ValueError("v3 archive set is missing a required static-world member type")
    expected_dffs = {name + ".dff" for name in ide_models.values()}
    expected_txds = {name + ".txd" for name in ide_txds}
    if (
        {name for name in all_entries if name.endswith(".dff")} != expected_dffs
        or {name for name in all_entries if name.endswith(".txd")} != expected_txds
        or any(ide_models.get(model_id) != model_name for model_id, model_name in col_models.items())
    ):
        raise ValueError("v3 IDE/DFF/TXD/COL identities do not form a closed set")
    if not ide_models:
        raise ValueError("v3 IDE has no model namespace")
    expected_ipl_names = [f"{namespace}i{base36(index, 2)}.ipl" for index in range(len(ipl_counts))]
    if sorted(ipl_counts, key=str.casefold) != expected_ipl_names:
        raise ValueError("v3 IPL members are not the exact contiguous namespace sequence")
    lod_profile = validate_lod_map_v3(lod_path.read_bytes(), [ipl_counts[name] for name in expected_ipl_names])
    return {
        "status": "ok",
        "format": FORMAT,
        "policy": STATIC_WORLD_V3_POLICY,
        "pack_id": manifest["pack_id"],
        "manifest_sha256": sha256_file(manifest_path),
        "archives": archive_reports,
        "entries": dict(sorted(expected_types.items())),
        "semantic_profile": {
            kind: dict(sorted(profile.items())) for kind, profile in semantic_profile.items()
        },
        "lod_profile": lod_profile,
        "models_without_col": [
            {"native_id": model_id, "name": ide_models[model_id]}
            for model_id in sorted(set(ide_models) - set(col_models))
        ],
        "total_payload_bytes": manifest["files"]["ide"]["bytes"]
        + manifest["files"]["lod"]["bytes"]
        + sum(image["bytes"] for image in manifest["files"]["images"]),
    }


def build_pack(
    resource: Path,
    output: Path,
    *,
    prefix: str,
    pack_id: str,
    namespace: str,
    model_id_start: int,
    librw_dff_upgrader: Path | None = None,
) -> dict[str, object]:
    if not SAFE_NAMESPACE.fullmatch(namespace):
        raise ValueError("namespace must be two lowercase alphanumeric characters beginning with a letter")
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"output directory must be empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    models, placements, source_definition_repairs = parse_generated_map(resource / "map_data.lua", prefix)
    if len(placements) > MAX_PLACEMENTS:
        raise ValueError("placement count exceeds the compiled v3 policy")
    groups = {name: rows for name, rows in sorted(_group_placements(placements).items())}
    if not 1 <= len(groups) <= MAX_SPATIAL_GROUPS:
        raise ValueError("spatial IPL group count exceeds the compiled v3 policy")
    variants, variants_by_key, txd_names = make_variants(models, placements, namespace, model_id_start)
    write_ide(output / "world.ide", variants)
    lod_data, lod_profile = build_lod_map(groups)
    (output / "world.lod").write_bytes(lod_data)

    inputs: list[ArchiveInput] = []
    texture_profile: Counter[str] = Counter()
    collision_profile: Counter[str] = Counter()
    conversions: dict[str, object] = {
        "rw_version_headers": {},
        "librw_dff_upgrades": {},
        "dff_extension_overruns": {},
        "dff_uv_nonfinite": {},
        "dff_zero_triangle_exceptions": {},
        "txd_first_wins_duplicates": {},
        "col2_to_col3": [],
        "timed_model_repairs": [
            repair for repair in source_definition_repairs if "raw_time_on" in repair
        ],
        "ide_flag_repairs": [
            repair for repair in source_definition_repairs if "raw_flags" in repair
        ],
        "generated_empty_txd": None,
        "models_without_col": [],
    }

    # Every spatial model variant has its own short DFF identity. Shared source
    # bytes are deliberately duplicated only when the source model crosses an
    # IPL boundary, keeping COL ownership spatial and unambiguous.
    dff_cache: dict[
        str,
        tuple[bytes, int, list[int], list[dict[str, int]], list[dict[str, object]]],
    ] = {}
    for variant in variants:
        cached = dff_cache.get(variant.model.dff_path)
        if cached is None:
            source_path = resource / variant.model.dff_path
            source_data, upgrade = upgrade_legacy_dff(source_path, librw_dff_upgrader)
            versioned, rewritten, repaired_overruns = canonicalize_rw_version(source_data, variant.model.dff_path)
            normalized, offsets, zero_triangles = normalize_dff_uvs(
                versioned,
                variant.model.dff_path,
                source_sha256=sha256_file(source_path),
            )
            cached = (normalized, rewritten, offsets, repaired_overruns, zero_triangles)
            dff_cache[variant.model.dff_path] = cached
            if rewritten:
                conversions["rw_version_headers"][variant.model.dff_path] = rewritten
            if upgrade:
                conversions["librw_dff_upgrades"][variant.model.dff_path] = upgrade
            if offsets:
                conversions["dff_uv_nonfinite"][variant.model.dff_path] = offsets
            if repaired_overruns:
                conversions["dff_extension_overruns"][variant.model.dff_path] = repaired_overruns
            if zero_triangles:
                conversions["dff_zero_triangle_exceptions"][variant.model.dff_path] = zero_triangles
        inputs.append(ArchiveInput(name=variant.native_name + ".dff", data=cached[0]))

    for txd_path, txd_name in sorted(txd_names.items(), key=lambda item: item[1]):
        if txd_path is None:
            normalized, records = empty_txd_v3(), []
            txd_context = "<generated-empty-txd>"
            conversions["generated_empty_txd"] = {
                "name": txd_name + ".txd",
                "models": sorted(variant.native_id for variant in variants if variant.model.txd_path is None),
                "sha256": hashlib.sha256(normalized).hexdigest(),
            }
        else:
            source = (resource / txd_path).read_bytes()
            normalized, records = canonicalize_txd_duplicates(source, txd_path)
            txd_context = txd_path
        member_texture = validate_static_txd_v3_grammar(normalized, txd_context)
        if txd_path is not None and not member_texture["textures"]:
            raise ValueError(f"{txd_path}: empty source TXD is not admitted; only the generated shared artifact is allowed")
        merge_profile(
            texture_profile,
            member_texture,
            context=f"{pack_id} texture profile",
            byte_limits={
                "serialized_gpu_bytes": MAX_CITY_GPU_BYTES,
                "decoded_rgba_bytes": MAX_CITY_DECODED_BYTES,
            },
        )
        if records:
            conversions["txd_first_wins_duplicates"][txd_context] = records
        inputs.append(ArchiveInput(name=txd_name + ".txd", data=normalized))

    spatial_report = []
    for group_index, (group_name, group_placements) in enumerate(groups.items()):
        col_name = f"{namespace}c{base36(group_index, 2)}.col"
        ipl_name = f"{namespace}i{base36(group_index, 2)}.ipl"
        group_variants = sorted(
            {variants_by_key[(placement.source_id, group_name)] for placement in group_placements if not placement.native},
            key=lambda variant: variant.native_id,
        )
        col_data = bytearray()
        for variant in group_variants:
            if variant.model.col_path is None:
                conversions["models_without_col"].append(
                    {
                        "source_id": variant.model.source_id,
                        "native_id": variant.native_id,
                        "name": variant.native_name,
                        "source": group_name,
                    }
                )
                continue
            source = (resource / variant.model.col_path).read_bytes()
            converted, changed = convert_col2_to_col3(source, variant.model.col_path)
            if changed:
                conversions["col2_to_col3"].append(variant.model.col_path)
            record = remap_col_record(converted, variant.native_id, variant.native_name)
            member_collision = validate_static_col_record_v3(record, variant.model.col_path)
            merge_profile(collision_profile, member_collision, context=f"{pack_id} collision profile")
            col_data.extend(record)
        ipl_data = binary_ipl(group_placements, variants_by_key)
        if col_data:
            inputs.append(ArchiveInput(name=col_name, data=bytes(col_data)))
        inputs.append(ArchiveInput(name=ipl_name, data=ipl_data))
        xs = [placement.position[0] for placement in group_placements]
        ys = [placement.position[1] for placement in group_placements]
        zs = [placement.position[2] for placement in group_placements]
        spatial_report.append(
            {
                "source": group_name,
                "col": col_name if col_data else None,
                "ipl": ipl_name,
                "models": len(group_variants),
                "placements": len(group_placements),
                "native_placements": sum(placement.native for placement in group_placements),
                "bounds": {"x": [min(xs), max(xs)], "y": [min(ys), max(ys)], "z": [min(zs), max(zs)]},
            }
        )

    conversions["col2_to_col3"] = sorted(set(conversions["col2_to_col3"]), key=str.casefold)
    conversions["models_without_col"] = sorted(
        conversions["models_without_col"], key=lambda record: (record["native_id"], record["source"])
    )
    names = [item.name.casefold() for item in inputs]
    duplicates = sorted(name for name, count in Counter(names).items() if count != 1)
    if duplicates:
        raise ValueError(f"duplicate canonical IMG members: {duplicates[:20]}")
    bins = partition_inputs(inputs)
    image_paths: list[Path] = []
    for index, bin_inputs in enumerate(bins):
        image_path = output / f"w{index:03d}.img"
        pack_inputs(image_path, bin_inputs)
        if image_path.stat().st_size > MAX_IMG_SECTORS * SECTOR_SIZE:
            raise ValueError(f"{image_path.name} exceeds the per-IMG v3 policy")
        image_paths.append(image_path)

    runtime_manifest = build_runtime_manifest(
        {},
        output / "world.ide",
        img_paths=image_paths,
        format_version=FORMAT,
        policy=STATIC_WORLD_V3_POLICY,
        pack_id=pack_id,
    )
    dump_runtime_manifest(output / "native-world.json", runtime_manifest)
    verification = verify_pack(output)
    if verification["lod_profile"] != lod_profile:
        raise ValueError("generated LOD map profile differs from its independent verification")
    declared_without_col = [
        {"native_id": record["native_id"], "name": record["name"]}
        for record in conversions["models_without_col"]
    ]
    if declared_without_col != verification["models_without_col"]:
        raise ValueError("generated COL subset differs from the source-declared models_without_col")
    report = {
        **verification,
        "generated_by": "utils/extended-world/build_native_world_v3.py",
        "namespace": namespace,
        "model_id_range": [variants[0].native_id, variants[-1].native_id],
        "counts": {
            "source_models": len(models),
            "model_variants": len(variants),
            "cross_spatial_variants": len(variants) - len(models),
            "txds": len(txd_names),
            "spatial_groups": len(groups),
            "placements": len(placements),
            "native_placements": sum(placement.native for placement in placements),
            "images": len(image_paths),
        },
        "texture_profile": dict(sorted(texture_profile.items())),
        "collision_profile": dict(sorted(collision_profile.items())),
        "spatial": spatial_report,
        "conversions": conversions,
        "source_to_native": {
            str(source_id): [
                {"source": group, "native_id": variants_by_key[(source_id, group)].native_id}
                for group in sorted(groups_by_source)
            ]
            for source_id, groups_by_source in sorted(
                (
                    (source_id, {variant.source_group for variant in variants if variant.model.source_id == source_id})
                    for source_id in models
                ),
                key=lambda item: item[0],
            )
        },
    }
    (output / "validation.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return report


def _group_placements(placements: Iterable[GeneratedPlacement]) -> dict[str, list[GeneratedPlacement]]:
    result: dict[str, list[GeneratedPlacement]] = defaultdict(list)
    for placement in placements:
        result[placement.source].append(placement)
    return dict(result)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resource", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prefix", required=True, help="generated Lua symbol prefix, for example CARCER_CITY")
    parser.add_argument("--pack-id", required=True)
    parser.add_argument("--namespace", required=True, help="two-character canonical short namespace")
    parser.add_argument("--model-id-start", type=int, required=True)
    parser.add_argument("--librw-dff-upgrader", type=Path)
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    if args.verify_only:
        report = verify_pack(args.output)
    else:
        report = build_pack(
            args.resource,
            args.output,
            prefix=args.prefix,
            pack_id=args.pack_id,
            namespace=args.namespace,
            model_id_start=args.model_id_start,
            librw_dff_upgrader=args.librw_dff_upgrader,
        )
    print(
        f"native world v3 OK: pack={report['pack_id']} images={len(report['archives'])} "
        f"payload={report['total_payload_bytes']} bytes"
    )


if __name__ == "__main__":
    main()
