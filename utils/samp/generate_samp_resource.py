#!/usr/bin/env python3
"""Build the redistributable runtime assets for the SA-MP map loader resource.

The original SA-MP archive keeps every collision in one aggregate COL file,
while MTA's resource replacement API consumes one collision model at a time.
This generator preserves SAMP.img for lazy DFF/TXD streaming and only splits
the collision aggregate into the entries addressed by SAMP.ide.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
from dataclasses import dataclass
from pathlib import Path
from xml.sax.saxutils import escape


SECTOR_SIZE = 2048
IMG_ENTRY_SIZE = 32
SUPPORTED_COL_VERSIONS = {b"COLL", b"COL2", b"COL3", b"COL4"}


@dataclass(frozen=True)
class ImgEntry:
    name: str
    offset: int
    size: int


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_img(path: Path) -> tuple[bytes, dict[str, ImgEntry]]:
    data = path.read_bytes()
    if len(data) < 8 or data[:4] != b"VER2":
        raise ValueError(f"{path} is not a VER2 IMG archive")

    count = struct.unpack_from("<I", data, 4)[0]
    directory_end = 8 + count * IMG_ENTRY_SIZE
    if directory_end > len(data):
        raise ValueError(f"{path} has a truncated directory")

    entries: dict[str, ImgEntry] = {}
    for index in range(count):
        entry_offset = 8 + index * IMG_ENTRY_SIZE
        sector, stream_sectors, archive_sectors = struct.unpack_from("<IHH", data, entry_offset)
        raw_name = data[entry_offset + 8 : entry_offset + 32]
        name = raw_name.split(b"\0", 1)[0].decode("ascii")
        size = (stream_sectors or archive_sectors) * SECTOR_SIZE
        byte_offset = sector * SECTOR_SIZE
        if not name or byte_offset + size > len(data):
            raise ValueError(f"{path} has an invalid entry at index {index}")
        entries[name.casefold()] = ImgEntry(name=name, offset=byte_offset, size=size)
    return data, entries


def parse_ide(path: Path) -> dict[int, dict[str, object]]:
    section = ""
    models: dict[int, dict[str, object]] = {}
    for line_number, raw_line in enumerate(path.read_text(encoding="latin-1").splitlines(), 1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        lowered = line.casefold()
        if lowered in {"objs", "tobj", "anim", "peds", "path", "2dfx", "txdp"}:
            section = lowered
            continue
        if lowered == "end":
            section = ""
            continue
        if section not in {"objs", "anim"}:
            continue

        fields = [field.strip() for field in line.split(",")]
        try:
            model_id = int(fields[0], 0)
            if section == "objs":
                name, txd_name = fields[1], fields[2]
                draw_distance, flags = float(fields[3]), int(fields[4], 0)
                model_type, animation = "object", None
            else:
                name, txd_name, animation = fields[1], fields[2], fields[3]
                draw_distance, flags = float(fields[4]), int(fields[5], 0)
                model_type = "clump"
        except (IndexError, ValueError) as error:
            raise ValueError(f"invalid {section} row at {path}:{line_number}") from error

        if model_id in models:
            raise ValueError(f"duplicate model ID {model_id} at {path}:{line_number}")
        models[model_id] = {
            "name": name,
            "txd": txd_name,
            "type": model_type,
            "drawDistance": draw_distance,
            "flags": flags,
            "animation": animation,
        }
    return models


def parse_col_chunks(data: bytes) -> dict[str, bytes]:
    chunks: dict[str, bytes] = {}
    offset = 0
    while offset + 32 <= len(data):
        version = data[offset : offset + 4]
        if version not in SUPPORTED_COL_VERSIONS:
            if any(data[offset:]):
                raise ValueError(f"invalid COL chunk signature at byte {offset}")
            break
        payload_size = struct.unpack_from("<I", data, offset + 4)[0]
        end = offset + 8 + payload_size
        if end > len(data) or end <= offset + 8:
            raise ValueError(f"truncated COL chunk at byte {offset}")
        name = data[offset + 8 : offset + 30].split(b"\0", 1)[0].decode("ascii")
        if not name:
            raise ValueError(f"unnamed COL chunk at byte {offset}")
        chunks[name.casefold()] = data[offset:end]
        offset = end
    return chunks


def write_single_entry_img(path: Path, name: str, payload: bytes) -> None:
    encoded_name = name.encode("ascii")
    if len(encoded_name) > 23:
        raise ValueError("IMG entry name exceeds 23 bytes")
    directory = bytearray(SECTOR_SIZE)
    directory[:4] = b"VER2"
    struct.pack_into("<I", directory, 4, 1)
    sectors = (len(payload) + SECTOR_SIZE - 1) // SECTOR_SIZE
    struct.pack_into("<IHH", directory, 8, 1, sectors, sectors)
    directory[16 : 16 + len(encoded_name)] = encoded_name
    padded_payload = payload + bytes(sectors * SECTOR_SIZE - len(payload))
    path.write_bytes(directory + padded_payload)


def write_meta(path: Path, collision_paths: list[str]) -> None:
    lines = [
        "<meta>",
        '    <info author="MTA Neon" name="SA-MP 0.3.7 map loader" type="script" version="1.0.0" />',
        '    <script src="models.lua" type="client" />',
        '    <script src="loader.lua" type="client" />',
        '    <script src="client.lua" type="client" />',
        '    <file src="catalog.json" />',
        '    <file src="assets/SAMP.img" />',
        '    <file src="assets/support.img" />',
    ]
    lines.extend(f'    <file src="{escape(item)}" />' for item in collision_paths)
    lines.extend(
        [
            '    <export function="loadSAMPMap" type="client" />',
            '    <export function="unloadSAMPMap" type="client" />',
            '    <export function="getSAMPModelRuntimeID" type="client" />',
            '    <export function="getSAMPMapLoaderStats" type="client" />',
            "</meta>",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def generate(samp_dir: Path, output: Path) -> None:
    ide_path = samp_dir / "SAMP.ide"
    samp_img_path = samp_dir / "SAMP.img"
    col_img_path = samp_dir / "SAMPCOL.img"
    blanktex_path = samp_dir / "blanktex.txd"
    required = (ide_path, samp_img_path, col_img_path, blanktex_path)
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing inputs: " + ", ".join(missing))

    models = parse_ide(ide_path)
    samp_data, samp_entries = parse_img(samp_img_path)
    col_archive, col_entries = parse_img(col_img_path)
    aggregate = col_entries.get("allsampcols.col")
    if not aggregate:
        raise ValueError("SAMPCOL.img does not contain AllSAMPCOLs.col")
    col_chunks = parse_col_chunks(col_archive[aggregate.offset : aggregate.offset + aggregate.size])
    assets = output / "assets"
    collisions = assets / "col"
    collisions.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(samp_img_path, assets / "SAMP.img")
    write_single_entry_img(assets / "support.img", "blanktex.txd", blanktex_path.read_bytes())

    catalog_models: dict[str, dict[str, object]] = {}
    collision_paths: list[str] = []
    failures: list[str] = []
    for model_id, definition in sorted(models.items()):
        name = str(definition["name"])
        txd_name = str(definition["txd"])
        model: dict[str, object] = dict(definition)
        model["logicalId"] = model_id

        if name.casefold() == "nomodelfile":
            model["supported"] = False
            model["reason"] = "SA-MP internal NoModelFile placeholder"
            catalog_models[str(model_id)] = model
            continue

        dff = samp_entries.get(f"{name}.dff".casefold())
        if not dff:
            failures.append(f"model {model_id} ({name}) has no DFF")
            continue
        model["dff"] = dff.name

        txd = samp_entries.get(f"{txd_name}.txd".casefold())
        if txd:
            model["txdEntry"] = txd.name

        collision = col_chunks.get(name.casefold())
        if collision:
            relative = f"assets/col/{model_id}.col"
            (output / relative).write_bytes(collision)
            model["collision"] = relative
            collision_paths.append(relative)
        elif definition["type"] == "object":
            failures.append(f"model {model_id} ({name}) has no collision")
            continue

        if definition.get("animation"):
            ifp = samp_entries.get(f"{definition['animation']}.ifp".casefold())
            if not ifp:
                failures.append(f"model {model_id} ({name}) has no IFP for {definition['animation']}")
                continue
            model["ifp"] = ifp.name

        model["supported"] = True
        catalog_models[str(model_id)] = model

    if failures:
        raise ValueError("asset catalog validation failed:\n" + "\n".join(failures))

    catalog = {
        "format": 1,
        "source": "SA-MP 0.3.7-R5",
        "sourceHashes": {
            "SAMP.ide": sha256(ide_path),
            "SAMP.img": sha256(samp_img_path),
            "SAMPCOL.img": sha256(col_img_path),
            "blanktex.txd": sha256(blanktex_path),
        },
        "modelCount": len(catalog_models),
        "models": catalog_models,
    }
    (output / "catalog.json").write_text(json.dumps(catalog, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    write_meta(output / "meta.xml", collision_paths)

    print(
        f"generated {len(catalog_models)} model definitions, {len(collision_paths)} collision files, "
        f"SAMP.img sha256={catalog['sourceHashes']['SAMP.img']}"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--samp-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    generate(arguments.samp_dir.resolve(), arguments.output.resolve())


if __name__ == "__main__":
    main()
