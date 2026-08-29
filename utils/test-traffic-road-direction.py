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
    lanes_toward_attached = lane_flags & 0x07
    lanes_away_from_attached = (lane_flags >> 3) & 0x07

    # Match retail GenerateOneRandomCar: low bits when the car link is attached
    # to the target node, high bits otherwise. The old oracle was inverted.
    return lanes_toward_attached if attached_to == target else lanes_away_from_attached


def node_position(areas: dict[int, PathArea], address: Address) -> tuple[float, float, float]:
    area = areas[address.area]
    offset = address.node * NODE_SIZE + 0x08
    return tuple(value / 8.0 for value in struct.unpack_from("<hhh", area.nodes, offset))


def linked_nodes(areas: dict[int, PathArea], source: Address) -> tuple[Address, ...]:
    area = areas[source.area]
    node_offset = source.node * NODE_SIZE
    base_link = struct.unpack_from("<h", area.nodes, node_offset + 0x10)[0]
    link_count = area.nodes[node_offset + 0x18] & 0x0F
    if base_link < 0 or base_link + link_count > area.address_count:
        return ()
    return area.node_links[base_link : base_link + link_count]


def directed_lane_offsets(areas: dict[int, PathArea], source: Address, target: Address) -> tuple[float, ...]:
    area = areas[source.area]
    node_offset = source.node * NODE_SIZE
    base_link = struct.unpack_from("<h", area.nodes, node_offset + 0x10)[0]
    link_count = area.nodes[node_offset + 0x18] & 0x0F
    if base_link < 0 or base_link + link_count > area.address_count:
        return ()

    packed_navi = None
    for index in range(link_count):
        if area.node_links[base_link + index] == target:
            packed_navi = area.navi_links[base_link + index]
            break
    if packed_navi is None:
        return ()

    link_area = areas.get(packed_navi >> 10)
    car_link_id = packed_navi & 0x03FF
    if link_area is None or (car_link_id + 1) * CAR_LINK_SIZE > len(link_area.car_links):
        return ()

    link_offset = car_link_id * CAR_LINK_SIZE
    attached_to = Address(*struct.unpack_from("<HH", link_area.car_links, link_offset + 0x04))
    if attached_to not in (source, target):
        return ()
    lane_flags = link_area.car_links[link_offset + 0x0B]
    lanes_toward_attached = lane_flags & 0x07
    lanes_away_from_attached = (lane_flags >> 3) & 0x07
    lane_count = lanes_toward_attached if attached_to == target else lanes_away_from_attached
    if not lane_count:
        return ()

    signed_width = struct.unpack_from("<b", link_area.car_links, link_offset + 0x0A)[0]
    one_way_offset = (
        0.5 - 0.5 * lanes_away_from_attached
        if lanes_toward_attached == 0
        else 0.5 - 0.5 * lanes_toward_attached
        if lanes_away_from_attached == 0
        else signed_width / 86.4 + 0.5
    )
    return tuple((one_way_offset + lane) * 5.4 for lane in range(lane_count))


def route_score(
    areas: dict[int, PathArea],
    source: Address,
    target: Address,
    position: tuple[float, float, float],
    forward: tuple[float, float],
) -> float | None:
    lane_offsets = directed_lane_offsets(areas, source, target)
    if not lane_offsets:
        return None

    start = node_position(areas, source)
    end = node_position(areas, target)
    edge_x, edge_y = end[0] - start[0], end[1] - start[1]
    edge_length_squared = edge_x * edge_x + edge_y * edge_y
    front_length_squared = forward[0] * forward[0] + forward[1] * forward[1]
    if edge_length_squared < 0.01 or front_length_squared < 0.01:
        return None
    edge_length = edge_length_squared**0.5
    front_length = front_length_squared**0.5
    direction_x, direction_y = edge_x / edge_length, edge_y / edge_length
    forward_dot = (forward[0] * direction_x + forward[1] * direction_y) / front_length
    if forward_dot < 0.35:
        return None

    best_distance_squared = float("inf")
    best_progress = 0.0
    for lane_offset in lane_offsets:
        lane_start_x = start[0] + lane_offset * direction_y
        lane_start_y = start[1] - lane_offset * direction_x
        relative_x = position[0] - lane_start_x
        relative_y = position[1] - lane_start_y
        progress = max(0.0, min(1.0, (relative_x * edge_x + relative_y * edge_y) / edge_length_squared))
        distance_x = position[0] - (lane_start_x + progress * edge_x)
        distance_y = position[1] - (lane_start_y + progress * edge_y)
        distance_squared = distance_x * distance_x + distance_y * distance_y
        if distance_squared < best_distance_squared:
            best_distance_squared = distance_squared
            best_progress = progress

    path_z = start[2] + (end[2] - start[2]) * best_progress
    vertical_excess = max(0.0, abs(position[2] - path_z) - 3.0)
    return best_distance_squared + (1.0 - forward_dot) * 16.0 + vertical_excess * vertical_excess * 4.0


def best_route(
    areas: dict[int, PathArea], position: tuple[float, float, float], forward: tuple[float, float]
) -> tuple[Address, Address, float] | None:
    best = None
    for area_id, area in areas.items():
        for node_id in range(area.vehicle_nodes):
            source = Address(area_id, node_id)
            for target in linked_nodes(areas, source):
                score = route_score(areas, source, target, position, forward)
                if score is not None and (best is None or score < best[2]):
                    best = (source, target, score)
    return best


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--paths-dir", type=Path, required=True, help="GTA San Andreas data/Paths directory")
    args = parser.parse_args()

    area_paths = sorted(args.paths_dir.glob("NODES*.DAT"), key=lambda path: int(path.stem[5:]))
    if not area_paths:
        raise SystemExit(f"FAIL: no NODES*.DAT files under {args.paths_dir}")
    areas = {int(path.stem[5:]): load_area(path) for path in area_paths}

    # Cover both LS Airport tunnel carriageways and all five one-way San
    # Fierro freeway candidates captured in the public build 183 session. The
    # previous oracle asserted every one of these directions in reverse.
    incidents = (
        (Address(6, 89), Address(6, 88), 2),
        (Address(6, 62), Address(6, 82), 2),
        (Address(25, 34), Address(25, 630), 2),
        (Address(33, 436), Address(33, 437), 1),
        (Address(33, 367), Address(33, 366), 2),
        (Address(33, 458), Address(33, 453), 2),
        (Address(33, 378), Address(33, 377), 2),
    )
    for legal_source, legal_target, expected_lanes in incidents:
        legal = directed_lane_count(areas, legal_source, legal_target)
        illegal = directed_lane_count(areas, legal_target, legal_source)
        if legal != expected_lanes or illegal != 0:
            raise SystemExit(
                f"FAIL: unexpected lanes {legal_source}->{legal_target}: legal={legal}, reverse={illegal}"
            )

        selected_source, selected_target = legal_target, legal_source
        if directed_lane_count(areas, selected_source, selected_target) == 0:
            selected_source, selected_target = selected_target, selected_source
        repaired = directed_lane_count(areas, selected_source, selected_target)
        if (selected_source, selected_target) != (legal_source, legal_target) or repaired != expected_lanes:
            raise SystemExit(f"FAIL: repair did not recover {legal_source}->{legal_target}")

        print(
            f"PASS incident area={legal_source.area} legal={legal_source.node}->{legal_target.node} "
            f"lanes={repaired} rejected={legal_target.node}->{legal_source.node}"
        )

    # Studio project 9 actor 87 was generated on 342->298, but retail's generic
    # script-car join selected the shorter adjacent 343->342 edge behind it.
    # The pose-aware search must recover the real lane without a location or
    # vehicle-model special case.
    stallion_position = (2399.6240234375, -1151.7384033203125, 29.05023193359375)
    stallion_forward = (-0.9998825788497925, 0.01532400492578745)
    stock_route = (Address(23, 343), Address(23, 342))
    expected_route = (Address(23, 342), Address(23, 298))
    selected = best_route(areas, stallion_position, stallion_forward)
    stock_score = route_score(areas, *stock_route, stallion_position, stallion_forward)
    if selected is None or selected[:2] != expected_route or stock_score is None or selected[2] + 1.0 >= stock_score:
        raise SystemExit(f"FAIL: pose-aware join selected={selected} stock_score={stock_score}")
    print(
        f"PASS pose-aware-join selected={selected[0].node}->{selected[1].node} score={selected[2]:.3f} "
        f"rejected={stock_route[0].node}->{stock_route[1].node} score={stock_score:.3f}"
    )

    print(f"PASS closed-road-direction-harness areas={len(areas)} incidents={len(incidents)}")


if __name__ == "__main__":
    main()
