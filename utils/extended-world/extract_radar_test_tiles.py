#!/usr/bin/env python3
"""Extract a 3x3 stock radar sample for the extended-radar test resource."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


SECTOR_SIZE = 2048
DIRECTORY_ENTRY = struct.Struct("<IHH24s")
SOURCE_TILES = {
    "tile_37_18.txd": "radar53.txd",
    "tile_38_18.txd": "radar54.txd",
    "tile_39_18.txd": "radar55.txd",
    "tile_37_19.txd": "radar65.txd",
    "tile_38_19.txd": "radar66.txd",
    "tile_39_19.txd": "radar67.txd",
    "tile_37_20.txd": "radar77.txd",
    "tile_38_20.txd": "radar78.txd",
    "tile_39_20.txd": "radar79.txd",
}


def read_directory(image: bytes) -> dict[str, tuple[int, int]]:
    if image[:4] != b"VER2":
        raise ValueError("expected a GTA IMG v2 archive (VER2)")

    entry_count = struct.unpack_from("<I", image, 4)[0]
    directory: dict[str, tuple[int, int]] = {}
    for index in range(entry_count):
        offset = 8 + index * DIRECTORY_ENTRY.size
        if offset + DIRECTORY_ENTRY.size > len(image):
            raise ValueError("truncated IMG directory")
        sector, streaming_sectors, archive_sectors, raw_name = DIRECTORY_ENTRY.unpack_from(image, offset)
        name = raw_name.split(b"\0", 1)[0].decode("ascii", errors="strict").lower()
        sector_count = archive_sectors or streaming_sectors
        directory[name] = (sector * SECTOR_SIZE, sector_count * SECTOR_SIZE)
    return directory


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gta3-img", type=Path, required=True, help="path to GTA San Andreas models/gta3.img")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("test-resources/extended-radar-test/assets"),
        help="destination assets directory",
    )
    args = parser.parse_args()

    image = args.gta3_img.read_bytes()
    directory = read_directory(image)
    args.output.mkdir(parents=True, exist_ok=True)

    for destination_name, source_name in SOURCE_TILES.items():
        try:
            offset, size = directory[source_name]
        except KeyError as error:
            raise ValueError(f"{source_name} is missing from {args.gta3_img}") from error
        data = image[offset : offset + size]
        if len(data) != size or data[:4] != b"\x16\x00\x00\x00":
            raise ValueError(f"invalid or truncated TXD entry: {source_name}")
        (args.output / destination_name).write_bytes(data)
        print(f"{source_name} -> {args.output / destination_name} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
