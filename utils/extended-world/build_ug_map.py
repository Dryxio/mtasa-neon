#!/usr/bin/env python3
"""Build an MTA resource from GTA Underground static map data.

Each preset explicitly lists the IPL areas and directories that contain
exterior geometry; all remaining instances are rejected.
"""

from __future__ import annotations

import argparse
import csv
import shutil
import struct
from dataclasses import dataclass
from pathlib import Path


COL_MAGICS = {b"COLL", b"COL2", b"COL3", b"COL4"}
COL_MODEL_NAME_OFFSET = 8
COL_MODEL_NAME_SIZE = 22
COL_MODEL_ID_OFFSET = COL_MODEL_NAME_OFFSET + COL_MODEL_NAME_SIZE
ALPHA_IDE_FLAGS = 0x4 | 0x8 | 0x40 | 0x200 | 0x400 | 0x2000 | 0x4000 | 0x200000


@dataclass(frozen=True)
class MapPreset:
    ides: tuple[str, ...]
    ipls: str
    assets: tuple[str, ...]
    data_prefix: str
    info_name: str
    exterior_areas: frozenset[int]
    excluded_ipl_directories: frozenset[str]
    binary_ipls_required: bool = True
    excluded_placements: frozenset[tuple[str, int]] = frozenset()


PRESETS = {
    "vc": MapPreset(
        ides=("ug/Cdimages/vice/game_vc.ide", "ug/Cdimages/default/generic.ide"),
        ipls="ug/maps/vc",
        assets=("ug/Cdimages_decrypted/vice", "ug/Cdimages_decrypted/default"),
        data_prefix="UG_VC",
        info_name="GTA Underground Vice City exterior",
        # UG stores a coherent group of western Vice City exterior roads and
        # ground meshes in area 13 (VCw.ipl/VCw_stream5.ipl).  The other
        # non-zero areas in the VC data are actual interiors.
        exterior_areas=frozenset({0, 13}),
        excluded_ipl_directories=frozenset(),
    ),
    "lc": MapPreset(
        ides=(
            "ug/Cdimages/liberty/game_lc.ide",
            "ug/Cdimages/liberty/game_lcmp.ide",
            "ug/Cdimages/liberty/game_ups.ide",
            "ug/Cdimages/default/generic.ide",
            "ug/Cdimages/vice/game_vc.ide",
        ),
        ipls="ug/maps/lc",
        assets=(
            "ug/Cdimages_decrypted/liberty",
            "ug/Cdimages_decrypted/default",
            "ug/Cdimages_decrypted/vice",
        ),
        data_prefix="UG_LC",
        info_name="GTA Underground Liberty City exterior",
        # The text seabed IPL uses 1024 for exterior LOD instances. Binary IPL
        # instance types retain only the low byte, so their matching geometry
        # appears as area 0 after parsing.
        exterior_areas=frozenset({0, 1024}),
        # UG's multiplayer directory contains arenas and interiors, including
        # misleading area-0 entries, so area alone cannot classify it.
        excluded_ipl_directories=frozenset({"multiplayer"}),
    ),
    "bw": MapPreset(
        ides=("ug/Cdimages/bully/game_bw.ide",),
        ipls="ug/maps/bw",
        assets=("ug/Cdimages_decrypted/bully",),
        data_prefix="UG_BW",
        info_name="GTA Underground Bullworth exterior",
        # The dorm in isc_dorm.ipl is an area-5 interior at Z ~= 1032. The
        # contiguous town, school, carnival, industrial and rich districts
        # are the area-0 exterior.
        exterior_areas=frozenset({0}),
        excluded_ipl_directories=frozenset(),
        binary_ipls_required=False,
        # These two isolated ladders are the only placements thousands of
        # units outside Bullworth's otherwise contiguous bounds. Keeping them
        # would expand the map from ~1.6 km to ~15 km for no visible district.
        excluded_placements=frozenset({("tbusines.ipl", 458), ("tbusines.ipl", 459)}),
    ),
}


@dataclass(frozen=True)
class ModelDefinition:
    source_id: int
    name: str
    txd: str
    lod_distance: float
    ide_flags: int
    model_type: str
    time_on: int | None = None
    time_off: int | None = None


@dataclass
class Placement:
    source_id: int
    name: str
    area: int
    x: float
    y: float
    z: float
    qx: float
    qy: float
    qz: float
    qw: float
    lod_index: int
    lod_global_index: int | None = None
    is_lod: bool = False
    binary: bool = False
    source_file: str = ""
    source_index: int = -1


def parse_int(value: str) -> int:
    return int(value.strip(), 0)


def split_fields(line: str) -> list[str]:
    return next(csv.reader([line], skipinitialspace=True))


def parse_model_definition(section: str, fields: list[str]) -> ModelDefinition:
    if len(fields) < 5:
        raise ValueError(f"invalid {section} definition: {fields}")

    source_id = parse_int(fields[0])
    name = fields[1].strip()
    txd = fields[2].strip()
    tail = [value.strip() for value in fields[3:]]
    if section == "anim":
        if len(tail) == 2:
            animation_and_distance = tail[0].rsplit(maxsplit=1)
            if len(animation_and_distance) != 2:
                raise ValueError(f"invalid anim definition: {fields}")
            tail = [animation_and_distance[1], tail[1]]
        else:
            tail = tail[1:]
    timed = section == "tobj"
    time_on = parse_int(tail[-2]) if timed else None
    time_off = parse_int(tail[-1]) if timed else None
    object_fields = tail[:-2] if timed else tail

    if len(object_fields) < 2:
        raise ValueError(f"invalid {section} distances/flags: {fields}")

    mesh_count: int | None = None
    try:
        candidate = parse_int(object_fields[0])
        if 1 <= candidate <= 3 and len(object_fields) == candidate + 2:
            mesh_count = candidate
    except ValueError:
        pass

    if mesh_count is None:
        distances = [float(object_fields[0])]
        ide_flags = parse_int(object_fields[-1])
    else:
        distances = [float(value) for value in object_fields[1 : 1 + mesh_count]]
        ide_flags = parse_int(object_fields[1 + mesh_count])

    if timed:
        model_type = "timed-object"
    elif ide_flags & 0x1000:
        model_type = "object-damageable"
    else:
        model_type = "object"

    return ModelDefinition(
        source_id=source_id,
        name=name,
        txd=txd,
        lod_distance=max(distances),
        ide_flags=ide_flags,
        model_type=model_type,
        time_on=time_on,
        time_off=time_off,
    )


def parse_ide(path: Path) -> dict[int, ModelDefinition]:
    definitions: dict[int, ModelDefinition] = {}
    section: str | None = None
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        lowered = line.lower()
        if not line or line.startswith("#"):
            continue
        if lowered in {"objs", "tobj", "anim"}:
            section = lowered
            continue
        if lowered == "end":
            section = None
            continue
        if section not in {"objs", "tobj", "anim"}:
            continue

        definition = parse_model_definition(section, split_fields(line))
        previous = definitions.get(definition.source_id)
        if previous and previous != definition:
            raise ValueError(f"duplicate IDE model ID {definition.source_id}")
        definitions[definition.source_id] = definition
    return definitions


def parse_ipl(path: Path) -> list[Placement]:
    placements: list[Placement] = []
    in_instances = False
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        # Text IPLs stored inside an IMG are sector-padded with NUL bytes.
        line = raw_line.split("\0", 1)[0].strip()
        lowered = line.lower()
        if not line or line.startswith("#"):
            continue
        if lowered == "inst":
            in_instances = True
            continue
        if in_instances and lowered == "end":
            in_instances = False
            continue
        if not in_instances:
            continue

        fields = split_fields(line)
        if len(fields) < 11:
            raise ValueError(f"invalid IPL inst entry in {path}: {fields}")
        placements.append(
            Placement(
                source_id=parse_int(fields[0]),
                name=fields[1].strip(),
                area=parse_int(fields[2]),
                x=float(fields[3]),
                y=float(fields[4]),
                z=float(fields[5]),
                qx=float(fields[6]),
                qy=float(fields[7]),
                qz=float(fields[8]),
                qw=float(fields[9]),
                lod_index=parse_int(fields[10]),
                source_file=path.name,
                source_index=len(placements),
            )
        )
    return placements


def parse_binary_ipl(path: Path) -> list[Placement]:
    data = path.read_bytes()
    if len(data) < 0x4C or data[:4] != b"bnry":
        raise ValueError(f"invalid binary IPL header in {path}")

    instance_count = struct.unpack_from("<I", data, 4)[0]
    instance_offset = struct.unpack_from("<I", data, 28)[0]
    placements: list[Placement] = []
    for index in range(instance_count):
        offset = instance_offset + index * 40
        if offset + 40 > len(data):
            raise ValueError(f"truncated binary IPL instance {index} in {path}")
        x, y, z, qx, qy, qz, qw, source_id, instance_type, lod_index = struct.unpack_from(
            "<7fiIi", data, offset
        )
        placements.append(
            Placement(
                source_id=source_id,
                name="",
                area=instance_type & 0xFF,
                x=x,
                y=y,
                z=z,
                qx=qx,
                qy=qy,
                qz=qz,
                qw=qw,
                lod_index=lod_index,
                binary=True,
                source_file=path.name,
                source_index=index,
            )
        )
    return placements


def parse_stream_ipl(path: Path) -> list[Placement]:
    if path.read_bytes()[:4] == b"bnry":
        return parse_binary_ipl(path)
    placements = parse_ipl(path)
    for placement in placements:
        placement.binary = True
    return placements


def deduplicate_text_ipls(paths: list[Path]) -> list[Path]:
    result: list[Path] = []
    seen: set[tuple[str, bytes]] = set()
    for path in paths:
        key = (path.stem.casefold(), path.read_bytes())
        if key in seen:
            continue
        seen.add(key)
        result.append(path)
    return result


def select_exterior_placements(
    text_ipl_paths: list[Path],
    binary_ipl_paths: list[Path],
    exterior_areas: frozenset[int],
    excluded_placements: frozenset[tuple[str, int]],
) -> tuple[list[Placement], int, int, int]:
    selected: list[Placement] = []
    rejected_interiors = 0
    rejected_lod_links = 0
    rejected_placements = 0
    text_indices_by_name: dict[str, dict[int, int]] = {}

    for path in text_ipl_paths:
        source = parse_ipl(path)
        selected_by_source_index: dict[int, int] = {}
        file_selected: list[tuple[int, Placement]] = []
        for source_index, placement in enumerate(source):
            if (placement.source_file.casefold(), placement.source_index) in excluded_placements:
                rejected_placements += 1
                continue
            if placement.area not in exterior_areas:
                rejected_interiors += 1
                continue
            selected_by_source_index[source_index] = len(selected) + len(file_selected)
            file_selected.append((source_index, placement))

        for _, placement in file_selected:
            if placement.lod_index >= 0:
                target = selected_by_source_index.get(placement.lod_index)
                if target is None:
                    rejected_lod_links += 1
                else:
                    placement.lod_global_index = target
            selected.append(placement)
        text_indices_by_name[path.stem.casefold()] = selected_by_source_index

    for path in binary_ipl_paths:
        source = parse_stream_ipl(path)
        base_name = path.stem.casefold().split("_stream", 1)[0]
        lod_targets = text_indices_by_name.get(base_name, {})
        for placement in source:
            if (placement.source_file.casefold(), placement.source_index) in excluded_placements:
                rejected_placements += 1
                continue
            if placement.area not in exterior_areas:
                rejected_interiors += 1
                continue
            if placement.lod_index >= 0:
                target = lod_targets.get(placement.lod_index)
                if target is None:
                    rejected_lod_links += 1
                else:
                    placement.lod_global_index = target
            selected.append(placement)

    for placement in selected:
        if placement.lod_global_index is not None:
            selected[placement.lod_global_index].is_lod = True
    return selected, rejected_interiors, rejected_lod_links, rejected_placements


def index_assets(roots: list[Path]) -> dict[tuple[str, str], Path]:
    result: dict[tuple[str, str], Path] = {}
    for root in roots:
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            key = (path.stem.casefold(), path.suffix.casefold())
            result.setdefault(key, path)
    return result


def txd_has_textures(path: Path) -> bool:
    """Return whether a RenderWare texture dictionary contains any textures.

    GTA III ships an intentionally empty dictionary for its untextured glass
    effect models. MTA rejects an empty dictionary in engineLoadTXD, while the
    corresponding DFFs load correctly without importing one.
    """
    data = path.read_bytes()
    if len(data) < 28 or struct.unpack_from("<I", data, 0)[0] != 0x16:
        raise ValueError(f"invalid TXD dictionary header in {path}")
    if struct.unpack_from("<I", data, 12)[0] != 0x01:
        raise ValueError(f"missing TXD dictionary struct in {path}")
    return struct.unpack_from("<H", data, 24)[0] > 0


def normalized_dff_data(path: Path) -> bytes:
    """Repair clumps whose modern header still contains the old 4-byte struct.

    UG's Liberty City railtracks DFF declares RenderWare 3.4 but retains the
    pre-3.4 clump struct containing only the atomic count. SA consequently
    reads the following chunk IDs as light/camera counts and rejects the DFF.
    Supplying the two missing zero counts makes the stream self-consistent.
    """
    data = path.read_bytes()
    if len(data) < 28:
        return data
    chunk_id, chunk_size, version = struct.unpack_from("<III", data, 0)
    struct_id, struct_size = struct.unpack_from("<II", data, 12)
    if chunk_id != 0x10 or struct_id != 0x01 or struct_size != 4 or version != 0x1003FFFF:
        return data

    end = 12 + chunk_size
    if end > len(data):
        raise ValueError(f"truncated malformed DFF clump in {path}")
    repaired = bytearray(data[:end])
    struct.pack_into("<I", repaired, 4, chunk_size + 8)
    struct.pack_into("<I", repaired, 16, 12)
    repaired[28:28] = b"\0" * 8
    return bytes(repaired)


def _parse_fixed_col_name(raw_name: bytes, context: str) -> str:
    terminator = raw_name.find(b"\0")
    if terminator < 0:
        raise ValueError(f"unterminated COL model name in {context}")
    if any(raw_name[terminator + 1 :]):
        raise ValueError(f"nonzero COL model-name tail in {context}")
    try:
        name = raw_name[:terminator].decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError(f"non-ASCII COL model name in {context}") from error
    if not name or any(not (character.isalnum() or character == "_") for character in name):
        raise ValueError(f"unsafe COL model name {name!r} in {context}")
    return name.casefold()


def parse_col_file_header(record: bytes, context: str) -> tuple[str, int]:
    """Parse GTA SA's fixed FileHeader without accepting ambiguous names.

    The native layout is ``char modelName[22]`` followed by the 16-bit model
    ID. Requiring a terminator and zero-filled name tail makes generated and
    imported records deterministic before GTA copies the fixed-width field.
    """

    if len(record) < COL_MODEL_ID_OFFSET + 2:
        raise ValueError(f"truncated COL file header in {context}")
    name = _parse_fixed_col_name(record[COL_MODEL_NAME_OFFSET:COL_MODEL_ID_OFFSET], context)
    model_id = struct.unpack_from("<H", record, COL_MODEL_ID_OFFSET)[0]
    return name, model_id


def parse_col_archive(
    path: Path, allow_bully_source_header: bool = False
) -> tuple[dict[str, bytes], dict[int, bytes]]:
    data = path.read_bytes()
    by_name: dict[str, bytes] = {}
    by_id: dict[int, bytes] = {}
    offset = 0
    while offset < len(data):
        if not any(data[offset:]):
            break
        if offset + 32 > len(data):
            raise ValueError(f"truncated COL record at offset {offset} in {path}")
        magic = data[offset : offset + 4]
        if magic not in COL_MAGICS:
            raise ValueError(f"unknown COL magic {magic!r} at offset {offset} in {path}")
        payload_size = struct.unpack_from("<I", data, offset + 4)[0]
        end = offset + 8 + payload_size
        if payload_size < 24 or end > len(data):
            raise ValueError(f"invalid COL size {payload_size} at offset {offset} in {path}")
        record = data[offset:end]
        context = f"record at offset {offset} in {path}"
        if allow_bully_source_header and record[28:32] == b"CED2":
            # GTA Underground's extracted Bully collision source stores this
            # marker where GTA SA expects the end of name[22] and modelId. It
            # is accepted only while selecting source assets; native pack
            # generation replaces the complete field and validates it again.
            name = _parse_fixed_col_name(record[8:28], context)
            model_id = None
        else:
            name, model_id = parse_col_file_header(record, context)
        by_name[name] = record
        if model_id is not None:
            by_id[model_id] = record
        offset = end
    return by_name, by_id


def lua_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def lua_number(value: float) -> str:
    return f"{value:.9g}"


def write_data(
    output: Path,
    prefix: str,
    models: list[ModelDefinition],
    placements: list[Placement],
    model_assets: dict[int, dict[str, str | None]],
    translation: tuple[float, float, float],
    rejected_interiors: int,
    rejected_lod_links: int,
    rejected_placements: int,
    native_model_ids: set[int],
    generated_by: str = "utils/extended-world/build_ug_map.py",
) -> None:
    lines = [
        f"-- Generated by {generated_by}.",
        f"{prefix}_TRANSLATION = {{ x = {lua_number(translation[0])}, y = {lua_number(translation[1])}, z = {lua_number(translation[2])} }}",
        (
            f"{prefix}_STATS = {{ placements = {len(placements)}, models = {len(models) + len(native_model_ids)}, "
            f"customModels = {len(models)}, nativeModels = {len(native_model_ids)}, "
            f"interiorsRejected = {rejected_interiors}, lodLinksRejected = {rejected_lod_links}, "
            f"placementsRejected = {rejected_placements} }}"
        ),
        f"{prefix}_MODELS = {{",
    ]
    for model in models:
        assets = model_assets[model.source_id]
        optional = ""
        if model.time_on is not None and model.time_off is not None:
            optional = f", timeOn = {model.time_on}, timeOff = {model.time_off}"
        txd_value = lua_string(assets["txd"]) if assets["txd"] else "false"
        col_value = lua_string(assets["col"]) if assets["col"] else "false"
        lines.append(
            "    [%d] = { name = %s, modelType = %s, txd = %s, dff = %s, col = %s, "
            "lodDistance = %s, ideFlags = %d, alpha = %s%s },"
            % (
                model.source_id,
                lua_string(model.name),
                lua_string(model.model_type),
                txd_value,
                lua_string(assets["dff"]),
                col_value,
                lua_number(model.lod_distance),
                model.ide_flags,
                "true" if model.ide_flags & ALPHA_IDE_FLAGS else "false",
                optional,
            )
        )
    lines.extend(("}", f"{prefix}_PLACEMENTS = {{"))
    for placement in placements:
        lod = str(placement.lod_global_index + 1) if placement.lod_global_index is not None else "false"
        native = "true" if placement.source_id in native_model_ids else "false"
        lines.append(
            "    { model = %d, native = %s, x = %s, y = %s, z = %s, qx = %s, qy = %s, qz = %s, qw = %s, lod = %s, isLod = %s, source = %s, sourceIndex = %d },"
            % (
                placement.source_id,
                native,
                lua_number(placement.x + translation[0]),
                lua_number(placement.y + translation[1]),
                lua_number(placement.z + translation[2]),
                lua_number(placement.qx),
                lua_number(placement.qy),
                lua_number(placement.qz),
                lua_number(placement.qw),
                lod,
                "true" if placement.is_lod else "false",
                lua_string(placement.source_file),
                placement.source_index,
            )
        )
    lines.extend(("}", ""))
    (output / "map_data.lua").write_text("\n".join(lines), encoding="utf-8")


def write_meta(
    output: Path,
    asset_paths: list[str],
    info_name: str,
) -> None:
    lines = [
        "<meta>",
        f'    <info author="MTA Neon" name="{info_name}" type="script" version="1.0.0" />',
        '    <script src="server.lua" type="server" />',
        '    <script src="map_data.lua" type="client" cache="false" />',
        '    <script src="client.lua" type="client" cache="false" />',
    ]
    lines.extend(f'    <file src="{path}" />' for path in sorted(asset_paths))
    lines.extend(("</meta>", ""))
    (output / "meta.xml").write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gta", type=Path, required=True, help="GTA Underground installation root")
    parser.add_argument("--output", type=Path, required=True, help="existing MTA resource directory")
    parser.add_argument("--map", choices=sorted(PRESETS), default="vc")
    parser.add_argument("--translate-x", type=float, default=0.0)
    parser.add_argument("--translate-y", type=float, default=0.0)
    parser.add_argument("--translate-z", type=float, default=0.0)
    parser.add_argument("--analyze-only", action="store_true")
    args = parser.parse_args()

    preset = PRESETS[args.map]
    ide_paths = [args.gta / value for value in preset.ides]
    ipl_root = args.gta / preset.ipls
    asset_roots = [args.gta / value for value in preset.assets]
    primary_asset_root = asset_roots[0]
    text_ipl_paths = [
        path
        for path in sorted(ipl_root.rglob("*.ipl"), key=lambda path: str(path).casefold())
        if not preset.excluded_ipl_directories.intersection(
            part.casefold() for part in path.relative_to(ipl_root).parts[:-1]
        )
    ]
    text_ipl_paths = deduplicate_text_ipls(text_ipl_paths)
    binary_ipl_paths = sorted(primary_asset_root.rglob("*.ipl"), key=lambda path: str(path).casefold())
    if (
        not all(path.is_file() for path in ide_paths)
        or not text_ipl_paths
        or (preset.binary_ipls_required and not binary_ipl_paths)
        or not all(path.is_dir() for path in asset_roots)
    ):
        raise FileNotFoundError(
            "UG source is incomplete: "
            f"ides={[(str(path), path.is_file()) for path in ide_paths]}, "
            f"text-ipls={ipl_root} count={len(text_ipl_paths)}, "
            f"binary-ipls={primary_asset_root} count={len(binary_ipl_paths)}, "
            f"assets={[(str(path), path.is_dir()) for path in asset_roots]}"
        )

    definitions: dict[int, ModelDefinition] = {}
    for ide_path in ide_paths:
        for source_id, definition in parse_ide(ide_path).items():
            previous = definitions.get(source_id)
            if previous and previous != definition:
                raise ValueError(f"conflicting IDE definition for model {source_id}")
            definitions[source_id] = definition
    placements, rejected_interiors, rejected_lod_links, rejected_placements = select_exterior_placements(
        text_ipl_paths, binary_ipl_paths, preset.exterior_areas, preset.excluded_placements
    )
    selected_ids = sorted({placement.source_id for placement in placements})
    native_model_ids = {source_id for source_id in selected_ids if source_id not in definitions and 0 <= source_id < 20000}
    missing_definitions = [
        source_id for source_id in selected_ids if source_id not in definitions and source_id not in native_model_ids
    ]
    if missing_definitions:
        preview = ", ".join(str(value) for value in missing_definitions[:20])
        raise ValueError(f"{len(missing_definitions)} area-0 models have no IDE definition: {preview}")
    models = [definitions[source_id] for source_id in selected_ids if source_id in definitions]
    for placement in placements:
        definition = definitions.get(placement.source_id)
        if definition:
            placement.name = definition.name

    xs = [placement.x + args.translate_x for placement in placements]
    ys = [placement.y + args.translate_y for placement in placements]
    summary = (
        f"{args.map}: {len(placements)} exterior placements, {len(models)} custom + "
        f"{len(native_model_ids)} native models, "
        f"text={sum(not placement.binary for placement in placements)} binary={sum(placement.binary for placement in placements)}, "
        f"{rejected_interiors} interiors rejected, {rejected_lod_links} LOD links rejected, "
        f"{rejected_placements} placements rejected; "
        f"X={min(xs):.1f}..{max(xs):.1f}, Y={min(ys):.1f}..{max(ys):.1f}"
    )
    if args.analyze_only:
        print(summary)
        return

    args.output.mkdir(parents=True, exist_ok=True)
    assets_output = args.output / "assets"
    model_output = assets_output / "models"
    texture_output = assets_output / "textures"
    collision_output = assets_output / "collisions"
    # Radar tiles are generated separately and should survive a geometry
    # rebuild. Packed archives are invalidated because their source files may
    # have changed and must be rebuilt explicitly.
    for generated_directory in (model_output, texture_output, collision_output):
        if generated_directory.exists():
            shutil.rmtree(generated_directory)
    for packed_archive in (assets_output / f"{args.map}_models.img", assets_output / f"{args.map}_textures.img"):
        packed_archive.unlink(missing_ok=True)
    model_output.mkdir(parents=True)
    texture_output.mkdir(parents=True)
    collision_output.mkdir(parents=True)

    asset_index = index_assets(asset_roots)
    col_archives = sorted(
        (path for root in asset_roots for path in root.rglob("*.col")), key=lambda path: str(path).casefold()
    )
    col_by_name: dict[str, bytes] = {}
    col_by_id: dict[int, bytes] = {}
    for archive in col_archives:
        archive_by_name, archive_by_id = parse_col_archive(archive, allow_bully_source_header=args.map == "bw")
        for name, record in archive_by_name.items():
            col_by_name.setdefault(name, record)
        for source_id, record in archive_by_id.items():
            col_by_id.setdefault(source_id, record)

    texture_names = sorted({model.txd.casefold() for model in models})
    texture_paths: dict[str, str | None] = {}
    asset_paths: list[str] = []
    for index, texture_name in enumerate(texture_names, start=1):
        source = asset_index.get((texture_name, ".txd"))
        if source is None:
            raise FileNotFoundError(f"missing TXD {texture_name}.txd")
        if not txd_has_textures(source):
            texture_paths[texture_name] = None
            continue
        relative = f"assets/textures/{index:04d}.txd"
        shutil.copy2(source, args.output / relative)
        texture_paths[texture_name] = relative
        asset_paths.append(relative)

    model_assets: dict[int, dict[str, str | None]] = {}
    missing_collisions = 0
    for model in models:
        source = asset_index.get((model.name.casefold(), ".dff")) or asset_index.get(
            (model.name.casefold(), ".dffw")
        )
        if source is None:
            raise FileNotFoundError(f"missing DFF {model.name}.dff for model {model.source_id}")
        # UG's decrypted Bully directory labels a small set of otherwise valid
        # RenderWare clumps as .dffw. They are referenced by ordinary IDE model
        # names, so normalize the extension when producing the resource.
        dff_relative = f"assets/models/{model.source_id}.dff"
        (args.output / dff_relative).write_bytes(normalized_dff_data(source))
        asset_paths.append(dff_relative)

        col_record = col_by_name.get(model.name.casefold()) or col_by_id.get(model.source_id)
        col_relative: str | None = None
        if col_record is not None:
            col_relative = f"assets/collisions/{model.source_id}.col"
            (args.output / col_relative).write_bytes(col_record)
            asset_paths.append(col_relative)
        else:
            missing_collisions += 1
        model_assets[model.source_id] = {
            "txd": texture_paths[model.txd.casefold()],
            "dff": dff_relative,
            "col": col_relative,
        }

    write_data(
        args.output,
        preset.data_prefix,
        models,
        placements,
        model_assets,
        (args.translate_x, args.translate_y, args.translate_z),
        rejected_interiors,
        rejected_lod_links,
        rejected_placements,
        native_model_ids,
    )
    write_meta(args.output, asset_paths, preset.info_name)
    total_bytes = sum((args.output / path).stat().st_size for path in asset_paths)
    print(summary)
    print(
        f"generated {len(texture_names)} TXDs and {len(models) - missing_collisions}/{len(models)} COLs; "
        f"resource assets={total_bytes / (1024 * 1024):.1f} MiB"
    )


if __name__ == "__main__":
    main()
