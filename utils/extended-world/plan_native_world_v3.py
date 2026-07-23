#!/usr/bin/env python3
"""Plan all frozen static-world v3 cities without mutating GTA or pack data.

The planner is deliberately an admission gate, not a pack builder.  It binds
the source maps, derives the same spatial model variants and short identities
as the v3 builder, assigns one deterministic aggregate FileID range, and then
proves every capacity it can from immutable inputs.  Unknown runtime
concurrency is reported as a blocker instead of being converted into a larger
constant or an optimistic estimate.
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
from typing import Iterable

from audit_native_world_catalog import DEFAULT_CITIES, EXCLUDED_COMPONENTS
from build_native_bw_pack import IDE_MODEL_SECTIONS, STOCK_IMG_PATHS, read_img_directory
from build_native_world_v3 import (
    BINARY_IPL_HEADER_SIZE,
    BINARY_IPL_INSTANCE,
    DIRECTORY_ENTRY,
    HEADER,
    MAX_AGGREGATE_DECODED_BYTES,
    MAX_AGGREGATE_GPU_BYTES,
    MAX_IMG_SECTORS,
    MAX_PLACEMENTS,
    MAX_SPATIAL_GROUPS,
    MAX_TXDS,
    ModelVariant,
    base36,
    gta_uppercase_key,
    make_variants,
    parse_generated_map,
)
from pack_img import SECTOR_SIZE, sectors_for


SCHEMA = 1
UINT64_MAX = (1 << 64) - 1

MODEL_ID_FIRST = 20_000
MODEL_ID_LAST = 31_999
FUTURE_CITY_MODEL_RESERVE = 4_096
MTA_LOGICAL_MODEL_ID_FIRST = 30_000
MTA_CLOTHES_MODEL_ID_RANGE = (30_000, 30_151)
NATIVE_MODEL_ARENA_FIRST = 20_000
NATIVE_MODEL_ARENA_LAST = 29_999
NATIVE_MODEL_ARENA_CAPACITY = NATIVE_MODEL_ARENA_LAST - NATIVE_MODEL_ARENA_FIRST + 1
V3_CACHE_OBJECT_LIMIT = 8

CITY_NAMESPACES = {
    "bullworth": "bw",
    "vice-city": "vc",
    "liberty-city": "lc",
    "carcer-city": "cc",
}
STOCK_IDENTITY_IMG_PATHS = STOCK_IMG_PATHS + ("data/Paths/carrec.img", "data/script/script.img")

FILE_ID_LAYOUT = {
    "dff": [0, 31_999],
    "txd": [32_000, 39_999],
    "col": [40_000, 40_511],
    "ipl": [40_512, 41_535],
    "dat": [41_536, 41_599],
    "ifp": [41_600, 41_779],
    "rrr": [41_780, 42_254],
    "scm": [42_255, 42_336],
    "loaded_list": [42_337, 42_338],
    "requested_list": [42_339, 42_340],
}
TOTAL_FILE_IDS = 42_341

CAPACITIES = {
    "model_store_atomic": 32_000,
    "model_store_damage_atomic": 512,
    "model_store_timed": 1_024,
    "txd": 8_000,
    "col": 512,
    "ipl": 1_024,
    "col_model": 30_000,
    "building": 32_000,
    "quad_tree_node": 2_048,
    "archive": 245,
    "stream_handle": 255,
}

STOCK_OCCUPANCY = {
    "model_store_atomic": 13_984,
    "model_store_damage_atomic": 69,
    "model_store_timed": 160,
    "txd": 3_608,
    "col": 252,
    "ipl": 191,
    "archive": 6,
}

# These are measurements, not constants used to admit an activation.  The pool
# high-water checkpoint observed them with Bullworth active.
OBSERVED_BULLWORTH_HIGH_WATER = {
    "building": 12_128,
    "col_model": 10_932,
    "quad_tree_node": 225,
}
DERIVED_STOCK_BASELINE = {
    # The live Bullworth gate allocated 2,962 buildings and 952 ColModels.
    # These derived values are planning baselines, not executable constants.
    "building": 12_128 - 2_962,
    "col_model": 10_932 - 952,
}

# Frozen by the v3 semantic admission checkpoint.  Re-running the admission
# audit is the authority if source fingerprints change.
ADMISSION_TEXTURE_PROFILE = {
    "serialized_gpu_bytes": 928_578_564,
    "decoded_rgba_bytes": 6_191_178_736,
    "textures": 31_330,
    "non_power_of_two_textures": 66,
    "textures_over_1024": 11,
}
ADMISSION_COLLISION_PROFILE = {
    "records": 11_835,
    "bytes": 50_150_532,
    "vertices": 2_474_463,
    "faces": 3_715_706,
    "face_groups": 122_224,
    "shadow_vertices": 16_478,
    "shadow_faces": 27_679,
}
ADMISSION_MAP_SHA256 = {
    "bullworth": "b5fe44847754d24060fc809cac2acc451ff6019d2d4ebd3c432eab76616070ad",
    "vice-city": "677b91a08730b6e503cfa9ae0a4ae6bded252f4f3112a49505ae852c18eaca4f",
    "liberty-city": "1597f9c3026fc8641202b03ccb03f019292e29a270d0c00052ac2c81c684feab",
    "carcer-city": "702b33ef689e28fe22c2d782d5a288fb83481c5f033007786e52584501dd6ed1",
}
ADMISSION_ASSET_SHA256 = {
    "bullworth": "f830f5fa5713ab375d011f71df96e893e95f70fc26ec73707c2f85336f9821ba",
    "vice-city": "99a3c7654c7739f34f2ff8201036ed53cbdb266383e518c7addd1b86fe0d384e",
    "liberty-city": "b3af590cd1253c3536ae5dd26633ed1056bd3c3637bb3429252a0893a55b8e03",
    "carcer-city": "ab1ee6b0a8e9bcc92fa1c1911d2878cf67a537a7fbb46ca00f63d5b1c6ed968b",
}
ADMISSION_CITY_PROFILES = {
    "bullworth": {
        "texture": {"serialized_gpu_bytes": 119_043_248, "decoded_rgba_bytes": 845_073_700, "textures": 4_705},
        "collision": {"records": 1_054, "bytes": 8_243_056},
        "largest_txd": {"source_bytes": 6_993_920, "serialized_gpu_bytes": 6_967_608, "decoded_rgba_bytes": 50_217_216},
    },
    "vice-city": {
        "texture": {"serialized_gpu_bytes": 153_251_072, "decoded_rgba_bytes": 1_116_161_788, "textures": 9_865},
        "collision": {"records": 3_800, "bytes": 4_624_100},
        "largest_txd": {"source_bytes": 4_085_760, "serialized_gpu_bytes": 4_069_760, "decoded_rgba_bytes": 16_837_176},
    },
    "liberty-city": {
        "texture": {"serialized_gpu_bytes": 50_593_372, "decoded_rgba_bytes": 355_389_828, "textures": 6_852},
        "collision": {"records": 3_488, "bytes": 4_464_832},
        "largest_txd": {"source_bytes": 1_236_152, "serialized_gpu_bytes": 1_224_032, "decoded_rgba_bytes": 8_350_356},
    },
    "carcer-city": {
        "texture": {"serialized_gpu_bytes": 605_690_872, "decoded_rgba_bytes": 3_874_553_420, "textures": 9_908},
        "collision": {"records": 3_493, "bytes": 32_818_544},
        "largest_txd": {"source_bytes": 51_322_104, "serialized_gpu_bytes": 51_167_280, "decoded_rgba_bytes": 388_623_304},
    },
}


@dataclass(frozen=True)
class SizedMember:
    name: str
    size: int

    def byte_size(self) -> int:
        return self.size


def checked_sum(values: Iterable[int], what: str) -> int:
    total = 0
    for value in values:
        if value < 0 or total > UINT64_MAX - value:
            raise ValueError(f"{what} exceeds uint64")
        total += value
    return total


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def asset_fingerprint(resource: Path, models: dict[int, object]) -> dict[str, int | str]:
    paths = {
        path
        for model in models.values()
        for path in (model.dff_path, model.txd_path, model.col_path)
        if path is not None
    }
    digest = hashlib.sha256()
    byte_count = 0
    for relative in sorted(paths, key=str.casefold):
        path = resource / relative
        size = path.stat().st_size
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(size).encode("ascii"))
        digest.update(b"\0")
        digest.update(sha256_file(path).encode("ascii"))
        digest.update(b"\n")
        byte_count += size
    return {"sha256": digest.hexdigest(), "files": len(paths), "bytes": byte_count}


def group_placements(placements: Iterable[object]) -> dict[str, list[object]]:
    result: dict[str, list[object]] = defaultdict(list)
    for placement in placements:
        result[placement.source].append(placement)
    return dict(sorted(result.items()))


def placement_bounds(placements: Iterable[object]) -> dict[str, list[float]]:
    rows = list(placements)
    return {
        axis: [
            min(placement.position[index] for placement in rows),
            max(placement.position[index] for placement in rows),
        ]
        for index, axis in enumerate(("x", "y", "z"))
    }


def lod_dependency_report(placements: list[object]) -> dict[str, object]:
    edges: Counter[tuple[str, str]] = Counter()
    targets: set[int] = set()
    children_per_target: Counter[int] = Counter()
    cross_group = 0
    same_group = 0
    for placement in placements:
        if placement.lod_global_index is None:
            continue
        target = placements[placement.lod_global_index]
        edge = (placement.source, target.source)
        edges[edge] += 1
        targets.add(placement.lod_global_index)
        children_per_target[placement.lod_global_index] += 1
        if edge[0] == edge[1]:
            same_group += 1
        else:
            cross_group += 1
    target_model_variants = {
        (placements[index].source_id, placements[index].source)
        for index in targets
        if not placements[index].native
    }
    native_targets = sum(placements[index].native for index in targets)
    return {
        "links": cross_group + same_group,
        "unique_targets": len(targets),
        "native_targets": native_targets,
        "unique_target_model_variants": len(target_model_variants),
        "target_groups": len({placements[index].source for index in targets}),
        "maximum_children_per_target": max(children_per_target.values(), default=0),
        "scratch_entries": len(targets) + cross_group + same_group,
        "cross_group_links": cross_group,
        "same_group_links": same_group,
        "group_edges": [
            {"stream_group": source, "lod_group": target, "links": count}
            for (source, target), count in sorted(edges.items())
        ],
    }


def variant_keys(models: dict[int, object], placements: list[object]) -> list[tuple[int, str]]:
    groups_by_model: dict[int, set[str]] = defaultdict(set)
    for placement in placements:
        if not placement.native:
            groups_by_model[placement.source_id].add(placement.source)
    primary = [(source_id, sorted(groups)[0]) for source_id, groups in sorted(groups_by_model.items())]
    extra = sorted((source_id, group) for source_id, groups in groups_by_model.items() for group in sorted(groups)[1:])
    keys = primary + extra
    if len(keys) != len(models) + len(extra):
        raise ValueError("source-first variant postcondition failed")
    return keys


def ide_bytes(variants: list[ModelVariant]) -> int:
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
    return len("\n".join(lines).encode("ascii"))


def estimate_members(resource: Path, variants: list[ModelVariant], txd_names: dict[str | None, str],
                     groups: dict[str, list[object]], namespace: str) -> list[SizedMember]:
    """Return a conservative source-derived sizing set.

    DFF and TXD bytes use their source lengths, while COL2 receives its exact
    twelve-byte COL3 header growth and IPL sizes are exact.  Canonical TXD
    duplicate removal and the one librw DFF conversion can only be made
    authoritative by the pack build, so this estimate is never used as an
    activation proof.
    """

    members: list[SizedMember] = []
    for variant in variants:
        members.append(SizedMember(variant.native_name + ".dff", (resource / variant.model.dff_path).stat().st_size))
    for txd_path, txd_name in txd_names.items():
        size = 44 if txd_path is None else (resource / txd_path).stat().st_size
        members.append(SizedMember(txd_name + ".txd", size))

    variants_by_group: dict[str, list[ModelVariant]] = defaultdict(list)
    for variant in variants:
        variants_by_group[variant.source_group].append(variant)
    for index, (group_name, placements) in enumerate(groups.items()):
        col_bytes = 0
        for variant in variants_by_group[group_name]:
            if variant.model.col_path is None:
                continue
            path = resource / variant.model.col_path
            with path.open("rb") as stream:
                magic = stream.read(4)
            col_bytes += path.stat().st_size + (12 if magic == b"COL2" else 0)
        if col_bytes:
            members.append(SizedMember(f"{namespace}c{base36(index, 2)}.col", col_bytes))
        members.append(
            SizedMember(
                f"{namespace}i{base36(index, 2)}.ipl",
                BINARY_IPL_HEADER_SIZE + len(placements) * BINARY_IPL_INSTANCE.size,
            )
        )
    return members


def partition_sizes(members: list[SizedMember]) -> list[dict[str, int | str]]:
    ordered = sorted(members, key=lambda item: (-sectors_for(item.size), item.name.casefold()))
    bins: list[list[SizedMember]] = []
    for item in ordered:
        item_sectors = sectors_for(item.size)
        if not item_sectors or item_sectors > 0xFFFF:
            raise ValueError(f"IMG member {item.name} exceeds the VER2 16-bit sector field")
        for items in bins:
            directory = sectors_for(HEADER.size + (len(items) + 1) * DIRECTORY_ENTRY.size)
            used = checked_sum((sectors_for(existing.size) for existing in items), "IMG sectors")
            if directory + used + item_sectors <= MAX_IMG_SECTORS:
                items.append(item)
                break
        else:
            directory = sectors_for(HEADER.size + DIRECTORY_ENTRY.size)
            if directory + item_sectors > MAX_IMG_SECTORS:
                raise ValueError(f"IMG member {item.name} cannot fit an empty archive")
            bins.append([item])
    reports = []
    for items in bins:
        assignment = [
            {"name": item.name.casefold(), "bytes": item.size, "sectors": sectors_for(item.size)}
            for item in items
        ]
        reports.append(
            {
                "entries": len(items),
                "sectors": sectors_for(HEADER.size + len(items) * DIRECTORY_ENTRY.size)
                + checked_sum((sectors_for(item.size) for item in items), "IMG sectors"),
                "members_sha256": hashlib.sha256(
                    json.dumps(assignment, separators=(",", ":"), sort_keys=True).encode("utf-8")
                ).hexdigest(),
            }
        )
    return reports


def iter_loose_stock_files(gta_root: Path):
    """Yield loose files only from directories owned by the stock install."""

    for stock_root in (gta_root / "models", gta_root / "data"):
        if stock_root.is_dir():
            yield from stock_root.rglob("*")


def stock_identity(gta_root: Path | None) -> dict[str, object]:
    if gta_root is None:
        return {
            "status": "not-provided",
            "collision_authority": False,
            "runtime_occupancy_authority": False,
            "reason": "--gta-root is required to prove stock ID/name/hash collisions",
        }
    occupied_ids: set[int] = set()
    model_names: set[str] = set()
    txd_names: set[str] = set()
    col_names: set[str] = set()
    ipl_names: set[str] = set()
    for ide_path in sorted((gta_root / "data").rglob("*.ide"), key=lambda path: str(path).casefold()):
        section: str | None = None
        for raw_line in ide_path.read_text(encoding="ascii", errors="ignore").splitlines():
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
            fields = [field.strip() for field in line.split(",")]
            try:
                occupied_ids.add(int(fields[0], 0))
            except (IndexError, ValueError) as error:
                raise ValueError(f"invalid stock IDE row in {ide_path}: {line}") from error
            if len(fields) > 1:
                model_names.add(fields[1].casefold())
            if len(fields) > 2:
                txd_names.add(fields[2].casefold())
    for relative in STOCK_IDENTITY_IMG_PATHS:
        path = gta_root / relative
        if not path.is_file():
            raise FileNotFoundError(path)
        for entry in read_img_directory(path):
            suffix = Path(entry.name).suffix.casefold()
            if suffix == ".dff":
                model_names.add(Path(entry.name).stem.casefold())
            elif suffix == ".txd":
                txd_names.add(Path(entry.name).stem.casefold())
            elif suffix == ".col":
                col_names.add(Path(entry.name).stem.casefold())
            elif suffix == ".ipl":
                ipl_names.add(Path(entry.name).stem.casefold())
    loose_destinations = {".dff": model_names, ".txd": txd_names, ".col": col_names, ".ipl": ipl_names}
    # Only models/ and data/ belong to the installed GTA corpus. Scanning the
    # entire installation would let unrelated backup or tool-output folders
    # silently change the supposedly frozen stock identity.
    for path in iter_loose_stock_files(gta_root):
        destination = loose_destinations.get(path.suffix.casefold())
        if destination is not None and path.is_file():
            destination.add(path.stem.casefold())
    directive_name = re.compile(r"([^,\s\\/]+)\.(col|ipl)\b", re.IGNORECASE)
    for relative in ("data/default.dat", "data/gta.dat"):
        path = gta_root / relative
        if not path.is_file():
            continue
        for raw_line in path.read_text(encoding="ascii", errors="ignore").splitlines():
            line = raw_line.split("#", 1)[0].strip()
            if not line:
                continue
            match = directive_name.search(line)
            if match and match.group(2).casefold() == "col":
                col_names.add(match.group(1).casefold())
            elif match:
                ipl_names.add(match.group(1).casefold())
    identity_digest = hashlib.sha256()
    for label, values in (
        ("id", map(str, sorted(occupied_ids))),
        ("model", sorted(model_names)),
        ("txd", sorted(txd_names)),
        ("col", sorted(col_names)),
        ("ipl", sorted(ipl_names)),
    ):
        for value in values:
            identity_digest.update(label.encode("ascii"))
            identity_digest.update(b"\0")
            identity_digest.update(value.encode("ascii"))
            identity_digest.update(b"\n")
    return {
        "status": "ok",
        "collision_authority": True,
        "runtime_occupancy_authority": False,
        "identity_sha256": identity_digest.hexdigest(),
        "occupied_model_ids": len(occupied_ids),
        "highest_model_id": max(occupied_ids),
        "observed_free_ids_below_20000": 20_000 - len({value for value in occupied_ids if 0 <= value < 20_000}),
        "model_name_count": len(model_names),
        "txd_name_count": len(txd_names),
        "col_name_count": len(col_names),
        "ipl_name_count": len(ipl_names),
        "model_names": model_names,
        "txd_names": txd_names,
        "col_names": col_names,
        "ipl_names": ipl_names,
        "note": (
            "IDE-free IDs below 20000 are observations, not allocatable slots; "
            "runtime reservations and every signed-ID consumer remain unproved."
        ),
    }


def collisions(values: dict[str, list[str]]) -> list[dict[str, object]]:
    return [
        {"identity": identity, "owners": sorted(owners)}
        for identity, owners in sorted(values.items())
        if len(set(owners)) > 1
    ]


def usage(capacity: int, baseline: int, additions: int) -> dict[str, int | bool]:
    projected = baseline + additions
    return {
        "capacity": capacity,
        "baseline": baseline,
        "additions": additions,
        "projected": projected,
        "remaining": capacity - projected,
        "fits": projected <= capacity,
    }


def boundary_proofs() -> list[dict[str, object]]:
    values = (31_999, 32_000, 39_999, 40_000, 40_511, 40_512)
    result = []
    for value in values:
        owners = [name for name, (first, last) in FILE_ID_LAYOUT.items() if first <= value <= last]
        result.append({"file_id": value, "partition": owners[0] if len(owners) == 1 else None, "owner_count": len(owners)})
    return result


def full_layout_proof() -> dict[str, object]:
    ordered = list(FILE_ID_LAYOUT.items())
    adjacency = [
        {
            "left": left_name,
            "right": right_name,
            "left_last": left_range[1],
            "right_first": right_range[0],
            "adjacent": left_range[1] + 1 == right_range[0],
        }
        for (left_name, left_range), (right_name, right_range) in zip(ordered, ordered[1:])
    ]
    terminal = ordered[-1][1][1]
    return {
        "starts_at_zero": ordered[0][1][0] == 0,
        "adjacency": adjacency,
        "terminal_file_id": terminal,
        "exclusive_end": TOTAL_FILE_IDS,
        "terminal_matches_total": terminal + 1 == TOTAL_FILE_IDS,
        "valid": (
            ordered[0][1][0] == 0
            and all(record["adjacent"] for record in adjacency)
            and terminal + 1 == TOTAL_FILE_IDS
        ),
    }


def plan(repository: Path, gta_root: Path | None = None) -> dict[str, object]:
    stock = stock_identity(gta_root)
    next_model_id = MODEL_ID_FIRST
    city_reports: list[dict[str, object]] = []
    generated_names: dict[str, list[str]] = defaultdict(list)
    generated_hashes: dict[str, list[str]] = defaultdict(list)
    generated_col_names: dict[str, list[str]] = defaultdict(list)
    generated_ipl_names: dict[str, list[str]] = defaultdict(list)
    source_ids: dict[str, list[str]] = defaultdict(list)
    source_names: dict[str, list[str]] = defaultdict(list)
    aggregate = Counter()
    all_members: list[tuple[str, SizedMember]] = []

    for city in DEFAULT_CITIES:
        namespace = CITY_NAMESPACES[city.pack_id]
        resource = repository / city.resource
        map_path = resource / "map_data.lua"
        map_sha256 = sha256_file(map_path)
        models, placements, repairs = parse_generated_map(map_path, city.prefix)
        assets = asset_fingerprint(resource, models)
        keys = variant_keys(models, placements)
        model_id_end = next_model_id + len(keys) - 1
        if model_id_end > MODEL_ID_LAST:
            raise ValueError(f"{city.pack_id} exceeds the aggregate DFF allocation")
        variants, _, txd_names = make_variants(models, placements, namespace, next_model_id)
        groups = group_placements(placements)
        members = estimate_members(resource, variants, txd_names, groups, namespace)
        archives = partition_sizes(members)
        remap_rows = [
            (
                variant.model.source_id,
                variant.source_group,
                variant.native_id,
                variant.native_name,
                variant.txd_name,
                variant.model.model_type,
            )
            for variant in variants
        ]
        remap_sha256 = hashlib.sha256(
            json.dumps(remap_rows, separators=(",", ":")).encode("utf-8")
        ).hexdigest()
        archive_assignment_sha256 = hashlib.sha256(
            json.dumps(archives, separators=(",", ":"), sort_keys=True).encode("utf-8")
        ).hexdigest()
        group_variant_ids: dict[str, set[int]] = defaultdict(set)
        for variant in variants:
            group_variant_ids[variant.source_group].add(variant.native_id)
        col_groups = sum(
            any(variant.model.col_path is not None for variant in variants if variant.source_group == group)
            for group in groups
        )
        model_types = Counter(variant.model.model_type for variant in variants)
        col_models = sum(variant.model.col_path is not None for variant in variants)
        lod_dependencies = lod_dependency_report(placements)
        positive_lods = lod_dependencies["links"]
        largest_group = max((len(rows), name) for name, rows in groups.items())
        spatial_groups = [
            {"name": name, "placements": len(rows), "bounds": placement_bounds(rows)}
            for name, rows in groups.items()
        ]

        for source_id, model in models.items():
            source_ids[str(source_id)].append(city.pack_id)
            source_names[model.source_name.casefold()].append(city.pack_id)
        for variant in variants:
            identity = variant.native_name.casefold()
            generated_names[identity].append(f"{city.pack_id}:dff")
            generated_hashes[f"dff:{gta_uppercase_key(identity):08x}"].append(f"{city.pack_id}:{identity}")
        for txd_name in txd_names.values():
            identity = txd_name.casefold()
            generated_names[identity].append(f"{city.pack_id}:txd")
            generated_hashes[f"txd:{gta_uppercase_key(identity):08x}"].append(f"{city.pack_id}:{identity}")
        for member in members:
            generated_names[member.name.casefold()].append(f"{city.pack_id}:member")
            suffix = Path(member.name).suffix.casefold()
            if suffix == ".col":
                generated_col_names[Path(member.name).stem.casefold()].append(city.pack_id)
            elif suffix == ".ipl":
                generated_ipl_names[Path(member.name).stem.casefold()].append(city.pack_id)
            all_members.append((city.pack_id, member))

        city_ide_bytes = ide_bytes(variants)
        city_img_payload = checked_sum(
            (report["sectors"] * SECTOR_SIZE for report in archives),
            f"{city.pack_id} IMG payload",
        )
        city_payload = checked_sum((city_ide_bytes, city_img_payload), f"{city.pack_id} payload")
        city_report = {
            "pack_id": city.pack_id,
            "label": city.label,
            "resource": city.resource,
            "namespace": namespace,
            "map_sha256": map_sha256,
            "asset_fingerprint": assets,
            "model_id_range": [next_model_id, model_id_end],
            "remap_sha256": remap_sha256,
            "archive_assignment_sha256": archive_assignment_sha256,
            "counts": {
                "source_models": len(models),
                "model_variants": len(variants),
                "cross_spatial_variants": len(variants) - len(models),
                "model_types": dict(sorted(model_types.items())),
                "txds": len(txd_names),
                "col_models": col_models,
                "col_groups": col_groups,
                "spatial_groups": len(groups),
                "placements": len(placements),
                "native_placements": sum(placement.native for placement in placements),
                "positive_lod_links": positive_lods,
                "timed_model_repairs": sum("raw_time_on" in repair for repair in repairs),
                "ide_flag_repairs": sum("raw_flags" in repair for repair in repairs),
            },
            "largest_spatial_group": {"name": largest_group[1], "placements": largest_group[0]},
            "bounds": placement_bounds(placements),
            "spatial_group_bounds": spatial_groups,
            "lod_dependencies": lod_dependencies,
            "admission_profile": ADMISSION_CITY_PROFILES[city.pack_id],
            "archive_estimate": {
                "authority": "source-derived-sizing-only",
                "images": len(archives),
                "img_payload_bytes": city_img_payload,
                "total_with_ide_bytes": city_payload,
                "largest_member_bytes": max(member.size for member in members),
                "archives": [
                    {"name": f"w{index:03d}.img", **report} for index, report in enumerate(archives)
                ],
                "caveat": "canonical TXD deduplication and the pinned librw conversion are authoritative only in a pack build",
            },
            "ide_bytes": city_ide_bytes,
        }
        city_reports.append(city_report)

        aggregate["source_models"] += len(models)
        aggregate["model_variants"] += len(variants)
        aggregate["cross_spatial_variants"] += len(variants) - len(models)
        aggregate["model_store_atomic"] += model_types["object"]
        aggregate["model_store_damage_atomic"] += model_types["object-damageable"]
        aggregate["model_store_timed"] += model_types["timed-object"]
        aggregate["txd"] += len(txd_names)
        aggregate["col"] += col_groups
        aggregate["ipl"] += len(groups)
        aggregate["col_model"] += col_models
        aggregate["building"] += len(placements)
        aggregate["spatial_groups"] += len(groups)
        aggregate["placements"] += len(placements)
        aggregate["positive_lod_links"] += positive_lods
        aggregate["archives"] += len(archives)
        aggregate["payload_bytes"] += city_payload
        next_model_id = model_id_end + 1

    pairwise_city_bounds = []
    for left_index, left in enumerate(city_reports):
        for right in city_reports[left_index + 1 :]:
            gaps = {
                axis: max(
                    right["bounds"][axis][0] - left["bounds"][axis][1],
                    left["bounds"][axis][0] - right["bounds"][axis][1],
                    0.0,
                )
                for axis in ("x", "y", "z")
            }
            pairwise_city_bounds.append(
                {
                    "cities": [left["pack_id"], right["pack_id"]],
                    "axis_gaps": gaps,
                    "planar_gap": math.hypot(gaps["x"], gaps["y"]),
                    "overlaps_xy": gaps["x"] == 0.0 and gaps["y"] == 0.0,
                }
            )

    aggregate["generated_identities"] = (
        aggregate["model_variants"] + aggregate["txd"] + aggregate["col"] + aggregate["ipl"]
    )
    generated_identity_collisions = collisions(generated_names)
    generated_hash_collisions = collisions(generated_hashes)
    source_id_collisions = collisions(source_ids)
    source_name_collisions = collisions(source_names)

    stock_model_collisions: list[str] = []
    stock_txd_collisions: list[str] = []
    stock_col_collisions: list[str] = []
    stock_ipl_collisions: list[str] = []
    stock_id_collisions: list[int] = []
    if stock["status"] == "ok":
        stock_model_names = stock.pop("model_names")
        stock_txd_names = stock.pop("txd_names")
        stock_col_names = stock.pop("col_names")
        stock_ipl_names = stock.pop("ipl_names")
        stock_model_hashes = {gta_uppercase_key(name) for name in stock_model_names}
        stock_txd_hashes = {gta_uppercase_key(name) for name in stock_txd_names}
        stock_model_collisions = sorted(
            owner
            for key, owners in generated_hashes.items()
            if key.startswith("dff:") and int(key[4:], 16) in stock_model_hashes
            for owner in owners
        )
        stock_txd_collisions = sorted(
            owner
            for key, owners in generated_hashes.items()
            if key.startswith("txd:") and int(key[4:], 16) in stock_txd_hashes
            for owner in owners
        )
        stock_col_collisions = sorted(set(generated_col_names).intersection(stock_col_names))
        stock_ipl_collisions = sorted(set(generated_ipl_names).intersection(stock_ipl_names))
        occupied_ids: set[int] = set()
        for ide_path in sorted((gta_root / "data").rglob("*.ide"), key=lambda path: str(path).casefold()):
            section: str | None = None
            for raw_line in ide_path.read_text(encoding="ascii", errors="ignore").splitlines():
                line = raw_line.strip()
                lowered = line.casefold()
                if lowered in IDE_MODEL_SECTIONS:
                    section = lowered
                elif lowered == "end":
                    section = None
                elif line and not line.startswith("#") and section in IDE_MODEL_SECTIONS:
                    occupied_ids.add(int(line.split(",", 1)[0].strip(), 0))
        stock_id_collisions = sorted(occupied_ids.intersection(range(MODEL_ID_FIRST, next_model_id)))

    model_remaining = MODEL_ID_LAST - next_model_id + 1
    observed_post_stock_start = (
        int(stock["highest_model_id"]) + 1 if stock["status"] == "ok" else MODEL_ID_FIRST
    )
    observed_post_stock_end = observed_post_stock_start + aggregate["model_variants"] - 1
    observed_post_stock_remaining = MODEL_ID_LAST - observed_post_stock_end
    pool_usage = {
        name: usage(CAPACITIES[name], STOCK_OCCUPANCY[name], aggregate[name])
        for name in ("model_store_atomic", "model_store_damage_atomic", "model_store_timed", "txd", "col", "ipl")
    }
    pool_usage["archive"] = usage(CAPACITIES["archive"], STOCK_OCCUPANCY["archive"], aggregate["archives"])
    # Ten non-image streams are reserved and the six stock IMG archives also
    # own handles before any native-world archive is registered.
    pool_usage["stream_handle"] = usage(
        CAPACITIES["stream_handle"],
        10 + STOCK_OCCUPANCY["archive"],
        aggregate["archives"],
    )
    pool_usage["col_model"] = usage(
        CAPACITIES["col_model"],
        DERIVED_STOCK_BASELINE["col_model"],
        aggregate["col_model"],
    )
    pool_usage["building_all_city_resident"] = usage(
        CAPACITIES["building"],
        DERIVED_STOCK_BASELINE["building"],
        aggregate["building"],
    )
    largest_city_placements = max(city["counts"]["placements"] for city in city_reports)
    pool_usage["building_mutually_exclusive_city"] = usage(
        CAPACITIES["building"],
        DERIVED_STOCK_BASELINE["building"],
        largest_city_placements,
    )

    largest_member = max(member.size for _, member in all_members)
    largest_member_blocks = sectors_for(largest_member)
    per_channel_blocks = (largest_member_blocks + 1) & ~1
    required_total_streaming_blocks = per_channel_blocks * 2
    fixed_native_bytes = {
        "streaming_info_table": (TOTAL_FILE_IDS + 1) * 0x14,
        "model_pointer_table": (CAPACITIES["model_store_atomic"] + 1) * 4,
        "extended_full_width_side_table": (1 << 17) * 8,
        "model_stores": 32_000 * 0x20 + 512 * 0x24 + 1_024 * 0x24,
        "txd_store": 8_000 * 0x0C,
        "col_store": 512 * 0x2C,
        "ipl_store": 1_024 * 0x34,
        "col_model_pool": 30_000 * 0x30,
        "building_pool": 32_000 * 0x38,
        "quad_tree_node_pool": 2_048 * 0x28,
    }
    fixed_native_bytes["known_total"] = checked_sum(fixed_native_bytes.values(), "fixed native memory")

    cache_transaction_headroom = max(512 * 1024 * 1024, aggregate["payload_bytes"] // 8)
    city_variant_counts = {
        city["pack_id"]: city["counts"]["model_variants"] for city in city_reports
    }
    current_transition_pairs = [
        {
            "cities": [left["pack_id"], right["pack_id"]],
            "required_slots": left["counts"]["model_variants"]
            + right["counts"]["model_variants"],
        }
        for index, left in enumerate(city_reports)
        for right in city_reports[index + 1 :]
    ]
    worst_current_transition = max(
        current_transition_pairs, key=lambda pair: pair["required_slots"]
    )
    largest_current_city = max(
        city_reports, key=lambda city: city["counts"]["model_variants"]
    )
    future_transition_slots = (
        largest_current_city["counts"]["model_variants"] + FUTURE_CITY_MODEL_RESERVE
    )
    model_residency = {
        "identity": "content-id + pack-id + pack-local-model-id",
        "physical_arena": [NATIVE_MODEL_ARENA_FIRST, NATIVE_MODEL_ARENA_LAST],
        "physical_capacity": NATIVE_MODEL_ARENA_CAPACITY,
        "city_variant_counts": city_variant_counts,
        "current_transition_pairs": current_transition_pairs,
        "worst_current_transition": worst_current_transition,
        "worst_current_transition_remaining": (
            NATIVE_MODEL_ARENA_CAPACITY - worst_current_transition["required_slots"]
        ),
        "future_city_working_set": FUTURE_CITY_MODEL_RESERVE,
        "largest_current_plus_future_slots": future_transition_slots,
        "largest_current_plus_future_remaining": (
            NATIVE_MODEL_ARENA_CAPACITY - future_transition_slots
        ),
        "same_city_generation_rollover_max": FUTURE_CITY_MODEL_RESERVE * 2,
        "same_city_generation_rollover_remaining": (
            NATIVE_MODEL_ARENA_CAPACITY - FUTURE_CITY_MODEL_RESERVE * 2
        ),
        "maximum_concurrent_working_sets": 2,
        "concurrency_rule": "city-transition XOR generation-rollover; a third working set is refused",
        "mta_dynamic_allocator_range": [0, NATIVE_MODEL_ARENA_FIRST - 1],
        "observed_ide_free_slots_below_arena": stock.get(
            "observed_free_ids_below_20000"
        ),
        "generation_fence_required": True,
        "permanent_global_assignment": False,
        "lod_anchor_policy": {
            "entity_index_arrays_process_lifetime": [
                city["pack_id"]
                for city in city_reports
                if city["lod_dependencies"]["links"]
            ],
            "entity_index_array_capacity": 40,
            "observed_stock_arrays": 30,
            "required_additional_arrays": sum(
                bool(city["lod_dependencies"]["links"]) for city in city_reports
            ),
            "anchors_are_city_scoped": True,
            "global_pinned_anchor_variants_rejected": sum(
                city["lod_dependencies"]["unique_target_model_variants"]
                for city in city_reports
            ),
            "scratch_entity_capacity": 4096,
            "maximum_city_scratch_entries": max(
                city["lod_dependencies"]["scratch_entries"] for city in city_reports
            ),
        },
    }
    blockers: list[dict[str, object]] = []

    def block(code: str, reason: str, **evidence: object) -> None:
        blockers.append({"code": code, "reason": reason, "evidence": evidence})

    if stock["status"] != "ok":
        block("stock-identity-unproved", "stock ID/name/hash collision proof requires --gta-root")
    drifted_admission_maps = sorted(
        city["pack_id"]
        for city in city_reports
        if (
            city["map_sha256"] != ADMISSION_MAP_SHA256[city["pack_id"]]
            or city["asset_fingerprint"]["sha256"] != ADMISSION_ASSET_SHA256[city["pack_id"]]
        )
    )
    if drifted_admission_maps:
        block(
            "v3-admission-profile-drift",
            "the frozen texture/collision budgets do not bind the current source maps and assets",
            cities=drifted_admission_maps,
        )
    if (
        generated_identity_collisions
        or generated_hash_collisions
        or stock_model_collisions
        or stock_txd_collisions
        or stock_col_collisions
        or stock_ipl_collisions
        or stock_id_collisions
    ):
        block(
            "identity-collision",
            "one or more generated IDs, names, or GTA uppercase keys collide",
            generated_names=len(generated_identity_collisions),
            generated_hashes=len(generated_hash_collisions),
            stock_models=len(stock_model_collisions),
            stock_txds=len(stock_txd_collisions),
            stock_cols=len(stock_col_collisions),
            stock_ipls=len(stock_ipl_collisions),
            stock_ids=len(stock_id_collisions),
        )
    if model_remaining < FUTURE_CITY_MODEL_RESERVE:
        block(
            "future-model-reserve",
            "the compact DFF range cannot preserve one full v3 city reserve",
            available=model_remaining,
            required=FUTURE_CITY_MODEL_RESERVE,
            shortfall=FUTURE_CITY_MODEL_RESERVE - model_remaining,
        )
    logical_overlap_first = max(MTA_LOGICAL_MODEL_ID_FIRST, MODEL_ID_FIRST)
    logical_overlap_last = min(MODEL_ID_LAST, next_model_id - 1)
    if logical_overlap_first <= logical_overlap_last:
        clothes_overlap_first = max(MTA_CLOTHES_MODEL_ID_RANGE[0], logical_overlap_first)
        clothes_overlap_last = min(MTA_CLOTHES_MODEL_ID_RANGE[1], logical_overlap_last)
        block(
            "mta-model-namespace-collision",
            "the contiguous native range overlaps MTA logical server models and GTA clothes pseudo-model IDs",
            overlap=[logical_overlap_first, logical_overlap_last],
            overlap_ids=logical_overlap_last - logical_overlap_first + 1,
            logical_model_first=MTA_LOGICAL_MODEL_ID_FIRST,
            clothes_overlap=(
                [clothes_overlap_first, clothes_overlap_last]
                if clothes_overlap_first <= clothes_overlap_last
                else []
            ),
        )
    block(
        "native-model-residency-binder",
        "multi-city activation requires a generation-fenced logical-to-physical model binder and IPL/COL buffer remap",
        arena=model_residency["physical_arena"],
        capacity=model_residency["physical_capacity"],
        worst_current_transition=model_residency["worst_current_transition"],
        largest_current_plus_future_slots=model_residency[
            "largest_current_plus_future_slots"
        ],
    )
    block(
        "mta-dynamic-model-headroom-unproved",
        "the native arena must be excluded from MTA allocation and the remaining pre-arena slots need runtime high-water proof",
        allocator_range=model_residency["mta_dynamic_allocator_range"],
        observed_ide_free_slots=model_residency[
            "observed_ide_free_slots_below_arena"
        ],
    )
    if (
        worst_current_transition["required_slots"] > NATIVE_MODEL_ARENA_CAPACITY
        or future_transition_slots > NATIVE_MODEL_ARENA_CAPACITY
        or FUTURE_CITY_MODEL_RESERVE * 2 > NATIVE_MODEL_ARENA_CAPACITY
    ):
        block(
            "native-model-arena-capacity",
            "the physical arena cannot hold the proved transition working set",
            capacity=NATIVE_MODEL_ARENA_CAPACITY,
            worst_current_transition=worst_current_transition,
            largest_current_plus_future_slots=future_transition_slots,
            same_city_generation_rollover_slots=FUTURE_CITY_MODEL_RESERVE * 2,
        )
    lod_policy = model_residency["lod_anchor_policy"]
    if (
        lod_policy["observed_stock_arrays"]
        + lod_policy["required_additional_arrays"]
        > lod_policy["entity_index_array_capacity"]
    ):
        block(
            "lod-entity-array-capacity",
            "the stock IPL entity-index pointer table has insufficient reviewed slots",
            policy=lod_policy,
        )
    if (
        lod_policy["maximum_city_scratch_entries"]
        > lod_policy["scratch_entity_capacity"]
    ):
        block(
            "lod-scratch-capacity",
            "a city exceeds GTA's anchor plus linked-child scratch capacity",
            policy=lod_policy,
        )
    lod_fanout = {
        city["pack_id"]: city["lod_dependencies"]["maximum_children_per_target"]
        for city in city_reports
        if city["lod_dependencies"]["maximum_children_per_target"] > 1
    }
    if lod_fanout:
        block(
            "lod-child-fanout",
            "the current unload contract admits at most one streamed child per LOD target",
            cities=lod_fanout,
        )
    if aggregate["positive_lod_links"]:
        block(
            "streamed-ipl-lod-bootstrap",
            "standalone streamed IPLs cannot resolve non-negative LOD links without registrar-owned entity indices",
            links=aggregate["positive_lod_links"],
        )
    if not pool_usage["building_all_city_resident"]["fits"]:
        block(
            "building-concurrency",
            "all-city concurrent building residency exceeds the installed pool; a spatial overlap proof is required",
            all_city=pool_usage["building_all_city_resident"],
            mutually_exclusive_city=pool_usage["building_mutually_exclusive_city"],
        )
    block(
        "quad-tree-concurrency",
        "QuadTreeNode demand cannot be derived from placement totals; translated bounds and runtime overlap high-water are required",
        capacity=CAPACITIES["quad_tree_node"],
        observed_bullworth_peak=OBSERVED_BULLWORTH_HIGH_WATER["quad_tree_node"],
    )
    block(
        "renderware-ram-high-water-unproved",
        "serialized corpus budgets do not prove simultaneous RenderWare CPU/GPU residency or allocator overhead",
        serialized_gpu_bytes=ADMISSION_TEXTURE_PROFILE["serialized_gpu_bytes"],
        decoded_rgba_bytes=ADMISSION_TEXTURE_PROFILE["decoded_rgba_bytes"],
    )
    block(
        "cache-generation-reclamation",
        "the eight-object cache supports one replacement bank but has no safe reclamation path for later inactive generations",
        active_objects=4,
        replacement_objects=4,
        object_limit=V3_CACHE_OBJECT_LIMIT,
    )

    failed_pools = sorted(name for name, report in pool_usage.items() if not report["fits"])
    expected_scenario_failures = {"building_all_city_resident"}
    unexpected_failed_pools = sorted(set(failed_pools) - expected_scenario_failures)
    if unexpected_failed_pools:
        block("installed-capacity", "one or more installed stores/pools are too small", pools=failed_pools)
    if any(city["counts"]["spatial_groups"] > MAX_SPATIAL_GROUPS for city in city_reports):
        block("per-city-spatial-groups", "a city exceeds the compiled v3 spatial-group limit")
    if any(city["counts"]["txds"] > MAX_TXDS for city in city_reports):
        block("per-city-txds", "a city exceeds the compiled v3 TXD limit")
    if any(city["counts"]["placements"] > MAX_PLACEMENTS for city in city_reports):
        block("per-city-placements", "a city exceeds the compiled v3 placement limit")
    if any(city["archive_estimate"]["images"] > 32 for city in city_reports):
        block("per-city-archives", "a city exceeds the compiled v3 archive-count limit")

    boundaries = boundary_proofs()
    layout_proof = full_layout_proof()
    if any(proof["owner_count"] != 1 for proof in boundaries) or not layout_proof["valid"]:
        block("partition-boundary", "a required FileID boundary does not have exactly one owner")

    return {
        "schema": SCHEMA,
        "generated_by": "utils/extended-world/plan_native_world_v3.py",
        "mode": "read-only-no-mutation",
        "status": "blocked" if blockers else "activable",
        "scope": {
            "included": ["dff", "txd", "col", "ipl", "img", "streaming", "stores", "pools", "memory", "cache"],
            "excluded": list(EXCLUDED_COMPONENTS),
            "preserved_stock_partitions": ["dat", "ifp", "rrr", "scm"],
        },
        "source_order": [city.pack_id for city in DEFAULT_CITIES],
        "stock_identity": stock,
        "file_ids": {
            "layout": FILE_ID_LAYOUT,
            "total": TOTAL_FILE_IDS,
            "custom_model_range": [MODEL_ID_FIRST, next_model_id - 1],
            "remaining_custom_model_ids": model_remaining,
            "future_city_reserve_required": FUTURE_CITY_MODEL_RESERVE,
            "dispersed_ids_policy": "observe-only-never-auto-allocate",
            "observed_post_stock_scenario": {
                "activation_authority": False,
                "range": [observed_post_stock_start, observed_post_stock_end],
                "remaining_contiguous_ids": observed_post_stock_remaining,
                "future_city_reserve_fits": observed_post_stock_remaining >= FUTURE_CITY_MODEL_RESERVE,
                "reason": "IDE occupancy does not prove runtime-reserved and hardcoded semantics below 20000",
            },
            "boundaries": boundaries,
            "full_layout_proof": layout_proof,
        },
        "cities": city_reports,
        "spatial": {
            "pairwise_city_bounds": pairwise_city_bounds,
            "authority": "source coordinates before registrar translation",
            "activation_requires_translated_overlap_sets": True,
        },
        "model_residency": model_residency,
        "collisions": {
            "generated_identity": generated_identity_collisions,
            "generated_gta_uppercase_key": generated_hash_collisions,
            "stock_model_key": stock_model_collisions,
            "stock_txd_key": stock_txd_collisions,
            "stock_col_name": stock_col_collisions,
            "stock_ipl_name": stock_ipl_collisions,
            "stock_model_id": stock_id_collisions,
            "source_model_id_diagnostic": source_id_collisions,
            "source_model_name_diagnostic": source_name_collisions,
            "archive_filename_policy": (
                "wNNN.img is pack-scoped; runtime archive identity is (content_id, pack_id, filename), "
                "while every IMG member identity is globally namespaced"
            ),
        },
        "aggregate": dict(sorted(aggregate.items())),
        "capacity": pool_usage,
        "budgets": {
            "cpu_and_native_ram_known_lower_bound": {
                "fixed_allocations_bytes": fixed_native_bytes,
                "known_fixed_lower_bound_bytes": fixed_native_bytes["known_total"],
                "collision_serialized_bytes": ADMISSION_COLLISION_PROFILE["bytes"],
                "placement_records_bytes": aggregate["placements"] * BINARY_IPL_INSTANCE.size,
                "activation_authority": False,
                "note": "RenderWare object graphs and allocator overhead require runtime high-water telemetry",
            },
            "vram_and_texture_ram": {
                **ADMISSION_TEXTURE_PROFILE,
                "serialized_limit": MAX_AGGREGATE_GPU_BYTES,
                "decoded_limit": MAX_AGGREGATE_DECODED_BYTES,
                "serialized_remaining": MAX_AGGREGATE_GPU_BYTES - ADMISSION_TEXTURE_PROFILE["serialized_gpu_bytes"],
                "decoded_remaining": MAX_AGGREGATE_DECODED_BYTES - ADMISSION_TEXTURE_PROFILE["decoded_rgba_bytes"],
                "authority": "frozen-v3-admission-profile",
            },
            "collision": ADMISSION_COLLISION_PROFILE,
            "streaming": {
                "estimated_archives": aggregate["archives"],
                "largest_source_derived_member_bytes": largest_member,
                "largest_member_blocks": largest_member_blocks,
                "minimum_per_channel_blocks": per_channel_blocks,
                "minimum_total_double_buffer_blocks": required_total_streaming_blocks,
                "minimum_double_buffer_bytes": required_total_streaming_blocks * SECTOR_SIZE,
                "archive_id_bits": 8,
                "member_size_sector_bits": 16,
                "archive_offset_sector_bits": 32,
            },
            "disk_and_cache": {
                "source_derived_payload_bytes": aggregate["payload_bytes"],
                "transaction_headroom_bytes": cache_transaction_headroom,
                "minimum_free_bytes_for_fresh_publish": aggregate["payload_bytes"] + cache_transaction_headroom,
                "cache_object_limit": V3_CACHE_OBJECT_LIMIT,
                "active_city_objects": 4,
                "production_double_bank_target": 8,
                "transactional_replacement_bank_fits": V3_CACHE_OBJECT_LIMIT >= 8,
                "continuous_generation_rotation_supported": False,
                "cache_total_limit_bytes": 32 * 1024 * 1024 * 1024,
                "authority": "planning estimate; emitted manifests remain authoritative",
            },
        },
        "blockers": blockers,
        "postconditions": {
            "source_order_is_canonical": True,
            "model_ranges_are_contiguous_and_nonoverlapping": True,
            "source_first_variants": True,
            "generated_names_are_globally_namespaced": not generated_identity_collisions,
            "generated_hashes_are_collision_free": not generated_hash_collisions,
            "required_partition_boundaries_have_one_owner": all(proof["owner_count"] == 1 for proof in boundaries),
            "no_source_or_runtime_mutation": True,
            "activation_requires_zero_blockers": True,
            "native_arena_precedes_mta_logical_namespace": (
                NATIVE_MODEL_ARENA_LAST < MTA_LOGICAL_MODEL_ID_FIRST
            ),
            "worst_current_transition_fits_native_arena": (
                worst_current_transition["required_slots"]
                <= NATIVE_MODEL_ARENA_CAPACITY
            ),
            "largest_current_plus_future_fits_native_arena": (
                future_transition_slots <= NATIVE_MODEL_ARENA_CAPACITY
            ),
            "future_generation_rollover_fits_native_arena": (
                FUTURE_CITY_MODEL_RESERVE * 2 <= NATIVE_MODEL_ARENA_CAPACITY
            ),
            "lod_entity_index_arrays_fit_stock_capacity": (
                model_residency["lod_anchor_policy"]["observed_stock_arrays"]
                + model_residency["lod_anchor_policy"]["required_additional_arrays"]
                <= model_residency["lod_anchor_policy"]["entity_index_array_capacity"]
            ),
            "lod_scratch_fits_stock_capacity": (
                model_residency["lod_anchor_policy"]["maximum_city_scratch_entries"]
                <= model_residency["lod_anchor_policy"]["scratch_entity_capacity"]
            ),
            "lod_children_per_target_supported": all(
                city["lod_dependencies"]["maximum_children_per_target"] <= 1
                for city in city_reports
            ),
        },
    }


def baseline_projection(report: dict[str, object]) -> dict[str, object]:
    """Keep source identity and key conclusions plus a digest of the full plan."""

    cities = []
    for city in report["cities"]:
        lod = city["lod_dependencies"]
        edge_bytes = json.dumps(lod["group_edges"], separators=(",", ":"), sort_keys=True).encode("utf-8")
        spatial_bytes = json.dumps(
            city["spatial_group_bounds"], separators=(",", ":"), sort_keys=True
        ).encode("utf-8")
        cities.append(
            {
                "pack_id": city["pack_id"],
                "namespace": city["namespace"],
                "map_sha256": city["map_sha256"],
                "asset_fingerprint": city["asset_fingerprint"],
                "model_id_range": city["model_id_range"],
                "remap_sha256": city["remap_sha256"],
                "archive_assignment_sha256": city["archive_assignment_sha256"],
                "bounds": city["bounds"],
                "spatial_group_bounds_sha256": hashlib.sha256(spatial_bytes).hexdigest(),
                "counts": city["counts"],
                "lod_dependencies": {
                    key: value for key, value in lod.items() if key != "group_edges"
                },
                "lod_group_edges_sha256": hashlib.sha256(edge_bytes).hexdigest(),
            }
        )
    reviewed = {
        "schema": report["schema"],
        "mode": report["mode"],
        "status": report["status"],
        "scope": report["scope"],
        "source_order": report["source_order"],
        "stock_identity": report["stock_identity"],
        "file_ids": report["file_ids"],
        "model_residency": report["model_residency"],
        "cities": report["cities"],
        "spatial": report["spatial"],
        "collisions": {
            key: value
            for key, value in report["collisions"].items()
            if key not in ("source_model_id_diagnostic", "source_model_name_diagnostic")
        },
        "aggregate": report["aggregate"],
        "capacity": report["capacity"],
        "budgets": report["budgets"],
        "blockers": report["blockers"],
        "postconditions": report["postconditions"],
    }
    reviewed_sha256 = hashlib.sha256(
        json.dumps(reviewed, separators=(",", ":"), sort_keys=True).encode("utf-8")
    ).hexdigest()
    file_ids = report["file_ids"]
    return {
        "schema": report["schema"],
        "mode": report["mode"],
        "status": report["status"],
        "source_order": report["source_order"],
        "stock_identity": report["stock_identity"],
        "cities": cities,
        "file_ids": {
            key: file_ids[key]
            for key in (
                "total",
                "custom_model_range",
                "remaining_custom_model_ids",
                "future_city_reserve_required",
                "dispersed_ids_policy",
                "observed_post_stock_scenario",
                "boundaries",
                "full_layout_proof",
            )
        },
        "aggregate": report["aggregate"],
        "model_residency": report["model_residency"],
        "blocker_codes": [blocker["code"] for blocker in report["blockers"]],
        "reviewed_plan_sha256": reviewed_sha256,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--gta-root", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--baseline-output", type=Path)
    parser.add_argument("--verify", type=Path)
    parser.add_argument("--require-activable", action="store_true")
    args = parser.parse_args()

    result = plan(
        args.repository.resolve(),
        args.gta_root.resolve() if args.gta_root else None,
    )
    projection = baseline_projection(result)
    if args.verify:
        expected = json.loads(args.verify.read_text(encoding="utf-8"))
        if projection != expected:
            raise SystemExit(f"native-world aggregate plan drifted from {args.verify}")
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")
    if args.baseline_output:
        args.baseline_output.write_text(json.dumps(projection, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.require_activable and result["status"] != "activable":
        raise SystemExit("native-world aggregate plan is blocked")


if __name__ == "__main__":
    main()
