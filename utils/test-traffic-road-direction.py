#!/usr/bin/env python3
"""Closed regression harness for Neon traffic road-direction repair."""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path


HEADER = struct.Struct("<5I")
NODE_SIZE = 0x1C
CAR_LINK_SIZE = 0x0E
DYNAMIC_NODE_LINKS = 16 * 12


@dataclass(frozen=True)
class Address:
    area: int
    node: int


@dataclass
class PathArea:
    vehicle_nodes: int
    address_count: int
    nodes: bytes
    car_links: bytes
    node_links: tuple[Address, ...]
    navi_links: tuple[int, ...]


def load_area(path: Path) -> PathArea:
    data = path.read_bytes()
    node_count, vehicle_nodes, ped_nodes, car_link_count, address_count = HEADER.unpack_from(data)
    if node_count != vehicle_nodes + ped_nodes:
        raise ValueError(f"{path}: inconsistent node counts")

    offset = HEADER.size
    nodes = data[offset : offset + node_count * NODE_SIZE]
    offset += len(nodes)
    car_links = data[offset : offset + car_link_count * CAR_LINK_SIZE]
    offset += len(car_links)

    node_link_count = address_count + DYNAMIC_NODE_LINKS
    raw_node_links = data[offset : offset + node_link_count * 4]
    offset += len(raw_node_links)
    node_links = tuple(Address(*values) for values in struct.iter_unpack("<HH", raw_node_links))

    raw_navi_links = data[offset : offset + address_count * 2]
    navi_links = tuple(value[0] for value in struct.iter_unpack("<H", raw_navi_links))
    if len(nodes) != node_count * NODE_SIZE or len(car_links) != car_link_count * CAR_LINK_SIZE:
        raise ValueError(f"{path}: truncated path arrays")
    if len(node_links) != node_link_count or len(navi_links) != address_count:
        raise ValueError(f"{path}: truncated link arrays")
    return PathArea(vehicle_nodes, address_count, nodes, car_links, node_links, navi_links)


def directed_lane_count(areas: dict[int, PathArea], source: Address, target: Address) -> int | None:
    area = areas.get(source.area)
    if area is None or source.node >= area.vehicle_nodes:
        return None

    node_offset = source.node * NODE_SIZE
    base_link = struct.unpack_from("<h", area.nodes, node_offset + 0x10)[0]
    link_count = area.nodes[node_offset + 0x18] & 0x0F
    if base_link < 0 or not link_count or base_link + link_count > area.address_count:
        return None

    packed_navi = None
    for index in range(link_count):
        if area.node_links[base_link + index] == target:
            packed_navi = area.navi_links[base_link + index]
            break
    if packed_navi is None:
        return None

    car_link_area = packed_navi >> 10
    car_link_id = packed_navi & 0x03FF
    link_area = areas.get(car_link_area)
    if link_area is None or (car_link_id + 1) * CAR_LINK_SIZE > len(link_area.car_links):
        return None

    link_offset = car_link_id * CAR_LINK_SIZE
    attached_to = Address(*struct.unpack_from("<HH", link_area.car_links, link_offset + 0x04))
    if attached_to not in (source, target):
        return None
    lane_flags = link_area.car_links[link_offset + 0x0B]
    opposite_lanes = lane_flags & 0x07
    same_direction_lanes = (lane_flags >> 3) & 0x07
    return same_direction_lanes if attached_to == target else opposite_lanes


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--paths-dir", type=Path, required=True, help="GTA San Andreas data/Paths directory")
    args = parser.parse_args()

    area_paths = sorted(args.paths_dir.glob("NODES*.DAT"), key=lambda path: int(path.stem[5:]))
    if not area_paths:
        raise SystemExit(f"FAIL: no NODES*.DAT files under {args.paths_dir}")
    areas = {int(path.stem[5:]): load_area(path) for path in area_paths}

    # These are the two LS Airport tunnel edges from the public incident. The
    # generic GTA road join selected the reverse, zero-lane direction in both
    # cases; the runtime hook must swap it back to this legal direction.
    incidents = ((Address(6, 88), Address(6, 89)), (Address(6, 82), Address(6, 62)))
    for legal_source, legal_target in incidents:
        legal = directed_lane_count(areas, legal_source, legal_target)
        illegal = directed_lane_count(areas, legal_target, legal_source)
        if legal != 2 or illegal != 0:
            raise SystemExit(
                f"FAIL: unexpected lanes {legal_source}->{legal_target}: legal={legal}, reverse={illegal}"
            )

        selected_source, selected_target = legal_target, legal_source
        if directed_lane_count(areas, selected_source, selected_target) == 0:
            selected_source, selected_target = selected_target, selected_source
        repaired = directed_lane_count(areas, selected_source, selected_target)
        if (selected_source, selected_target) != (legal_source, legal_target) or repaired != 2:
            raise SystemExit(f"FAIL: repair did not recover {legal_source}->{legal_target}")

        print(
            f"PASS incident area={legal_source.area} legal={legal_source.node}->{legal_target.node} "
            f"lanes={repaired} rejected={legal_target.node}->{legal_source.node}"
        )

    print(f"PASS closed-road-direction-harness areas={len(areas)} incidents={len(incidents)}")


if __name__ == "__main__":
    main()
