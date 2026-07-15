#!/usr/bin/env python3
"""Validate the native extended-world HOODLUM patch manifest off-game."""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
from dataclasses import dataclass
from pathlib import Path


EXPECTED_IMAGE_BASE = 0x00400000
EXPECTED_COUNTS = {
    "definitions": 3,
    "constructors": 3,
    "crt_routines": 6,
    "pointers": 57,
    "growers": 3,
    "collision_definitions": 1,
    "collision_pointers": 12,
    "collision_nops": 2,
}

TOOLS = Path(__file__).resolve().parent
REPOSITORY = TOOLS.parents[1]
DEFAULT_MANIFEST = REPOSITORY / "Client/game_sa/CNativeModelStoreSA.Manifest.inc"

CALL = re.compile(r"^(NATIVE_[A-Z_]+)\((.*)\)$")


@dataclass(frozen=True)
class Section:
    virtual_address: int
    virtual_size: int
    raw_offset: int
    raw_size: int
    characteristics: int


@dataclass(frozen=True)
class ExecutableIdentity:
    name: str
    sha256: str
    machine: int
    magic: int
    image_base: int
    image_size: int
    timestamp: int
    checksum: int

    @property
    def pe_tuple(self) -> tuple[int, int, int, int, int, int]:
        return (
            self.machine,
            self.magic,
            self.image_base,
            self.image_size,
            self.timestamp,
            self.checksum,
        )


EXECUTABLE_IDENTITIES = (
    ExecutableIdentity(
        "hoodlum-raw",
        "72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b",
        0x14C,
        0x10B,
        EXPECTED_IMAGE_BASE,
        0x008B1000,
        0x427101CA,
        0x00DC5BEA,
    ),
    ExecutableIdentity(
        "mta-programdata",
        "77485627b4ef17f92819318050d501e171c7ab84ceffe5091b973b9e29f9cc98",
        0x14C,
        0x10B,
        EXPECTED_IMAGE_BASE,
        0x01177000,
        0x437101CA,
        0x00DC29E6,
    ),
)


class PeImage:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        pe_offset = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[pe_offset : pe_offset + 4] != b"PE\0\0":
            raise ValueError("not a PE executable")
        self.machine, section_count, self.timestamp = struct.unpack_from("<HHI", self.data, pe_offset + 4)
        optional_size = struct.unpack_from("<H", self.data, pe_offset + 20)[0]
        optional = pe_offset + 24
        self.magic = struct.unpack_from("<H", self.data, optional)[0]
        self.image_base = struct.unpack_from("<I", self.data, optional + 28)[0]
        self.image_size = struct.unpack_from("<I", self.data, optional + 56)[0]
        self.checksum = struct.unpack_from("<I", self.data, optional + 64)[0]
        section_table = optional + optional_size
        self.sections: list[Section] = []
        for index in range(section_count):
            offset = section_table + index * 40
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", self.data, offset + 8
            )
            characteristics = struct.unpack_from("<I", self.data, offset + 36)[0]
            self.sections.append(
                Section(virtual_address, virtual_size, raw_offset, raw_size, characteristics)
            )

    def read_va(self, address: int, size: int) -> bytes:
        relative = address - self.image_base
        for section in self.sections:
            mapped_size = max(section.virtual_size, section.raw_size)
            if section.virtual_address <= relative and relative + size <= section.virtual_address + mapped_size:
                raw = section.raw_offset + relative - section.virtual_address
                return self.data[raw : raw + size]
        raise ValueError(f"address outside file-backed PE sections: 0x{address:08X}")

    def executable_dword_occurrences(self, value: int) -> set[int]:
        needle = struct.pack("<I", value)
        occurrences: set[int] = set()
        for section in self.sections:
            if not section.characteristics & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
                continue
            data = self.data[section.raw_offset : section.raw_offset + section.raw_size]
            start = 0
            while True:
                offset = data.find(needle, start)
                if offset < 0:
                    break
                occurrences.add(self.image_base + section.virtual_address + offset)
                start = offset + 1
        return occurrences


def split_arguments(text: str) -> list[str]:
    return [item.strip() for item in text.split(",")]


def integer(text: str) -> int:
    return int(text, 0)


def parse_manifest(path: Path = DEFAULT_MANIFEST) -> dict[str, list[dict[str, object]]]:
    records: dict[str, list[dict[str, object]]] = {
        "definitions": [],
        "constructors": [],
        "crt_routines": [],
        "pointers": [],
        "growers": [],
        "collision_definitions": [],
        "collision_pointers": [],
        "collision_nops": [],
    }
    for line_number, original in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = original.strip()
        if not line or line.startswith("//"):
            continue
        match = CALL.fullmatch(line)
        if not match:
            raise ValueError(f"unparsed manifest line {line_number}: {original}")
        macro, body = match.groups()
        args = split_arguments(body)
        if macro == "NATIVE_MODEL_STORE_DEFINITION":
            if len(args) != 7:
                raise ValueError(f"bad store definition on line {line_number}")
            records["definitions"].append(
                {
                    "kind": args[0],
                    "base": integer(args[1]),
                    "stock_capacity": integer(args[2]),
                    "new_capacity": integer(args[3]),
                    "stride": integer(args[4]),
                    "constructor": integer(args[5]),
                    "vtable": integer(args[6]),
                }
            )
        elif macro == "NATIVE_MODEL_STORE_CONSTRUCTOR":
            records["constructors"].append(
                {"kind": args[0], "address": integer(args[1]), "bytes": bytes(map(integer, args[2:]))}
            )
        elif macro == "NATIVE_MODEL_STORE_CRT_ROUTINE":
            records["crt_routines"].append(
                {
                    "kind": args[0],
                    "role": args[1],
                    "address": integer(args[2]),
                    "bytes": bytes(map(integer, args[3:])),
                }
            )
        elif macro == "NATIVE_MODEL_STORE_POINTER":
            records["pointers"].append(
                {
                    "kind": args[0],
                    "address": integer(args[1]),
                    "size": integer(args[2]),
                    "operand_offset": integer(args[3]),
                    "displacement": integer(args[4]),
                    "action": args[5],
                    "bytes": bytes(map(integer, args[6:])),
                }
            )
        elif macro == "NATIVE_MODEL_STORE_GROWER":
            records["growers"].append(
                {"kind": args[0], "address": integer(args[1]), "bytes": bytes(map(integer, args[2:]))}
            )
        elif macro == "NATIVE_COLLISION_BUFFER_DEFINITION":
            records["collision_definitions"].append(
                {"stock_capacity": integer(args[0]), "new_capacity": integer(args[1])}
            )
        elif macro == "NATIVE_COLLISION_BUFFER_POINTER":
            records["collision_pointers"].append(
                {
                    "address": integer(args[0]),
                    "size": integer(args[1]),
                    "operand_offset": integer(args[2]),
                    "displacement": integer(args[3]),
                    "bytes": bytes(map(integer, args[4:])),
                }
            )
        elif macro == "NATIVE_COLLISION_BUFFER_NOP":
            records["collision_nops"].append(
                {"address": integer(args[0]), "bytes": bytes(map(integer, args[1:]))}
            )
        else:
            raise ValueError(f"unknown manifest macro on line {line_number}: {macro}")
    return records


def validate_manifest(records: dict[str, list[dict[str, object]]]) -> None:
    actual_counts = {name: len(items) for name, items in records.items()}
    if actual_counts != EXPECTED_COUNTS:
        raise ValueError(f"manifest record counts changed: {actual_counts}, expected {EXPECTED_COUNTS}")

    definitions = {item["kind"]: item for item in records["definitions"]}
    if set(definitions) != {"Atomic", "DamageAtomic", "Time"}:
        raise ValueError("unexpected model-store kinds")
    if [definitions[name]["new_capacity"] for name in ("Atomic", "DamageAtomic", "Time")] != [15000, 160, 200]:
        raise ValueError("model-store capacities do not match the Bullworth contract")
    if records["collision_definitions"][0] != {"stock_capacity": 32768, "new_capacity": 327680}:
        raise ValueError("collision-buffer capacities do not match the Bullworth contract")

    constructors = {item["kind"]: item for item in records["constructors"]}
    for kind, definition in definitions.items():
        signature = constructors[kind]
        if signature["address"] != definition["constructor"] or len(signature["bytes"]) != 16:
            raise ValueError(f"constructor manifest mismatch for {kind}")

    crt_roles = {(item["kind"], item["role"]) for item in records["crt_routines"]}
    if crt_roles != {(kind, role) for kind in definitions for role in ("Constructor", "Destructor")}:
        raise ValueError("CRT constructor/destructor audit is incomplete")
    if any(len(item["bytes"]) != 16 for item in records["crt_routines"]):
        raise ValueError("CRT routine signatures must contain 16 expected bytes")

    patched_ranges: list[tuple[int, int, str]] = []
    for site in records["pointers"]:
        size = int(site["size"])
        offset = int(site["operand_offset"])
        expected = site["bytes"]
        if not 1 <= size <= len(expected) or offset + 4 > size:
            raise ValueError(f"invalid pointer instruction shape at 0x{site['address']:08X}")
        encoded = struct.unpack_from("<I", expected, offset)[0]
        wanted = int(definitions[site["kind"]]["base"]) + int(site["displacement"])
        if encoded != wanted:
            raise ValueError(f"model-store operand mismatch at 0x{site['address']:08X}")
        if site["action"] == "Patch":
            patched_ranges.append((int(site["address"]) + offset, int(site["address"]) + offset + 4, "model"))
        elif site["action"] != "ValidateOnly":
            raise ValueError(f"unknown action {site['action']}")

    for site in records["collision_pointers"]:
        size = int(site["size"])
        offset = int(site["operand_offset"])
        expected = site["bytes"]
        if not 1 <= size <= len(expected) or offset + 4 > size:
            raise ValueError(f"invalid collision instruction shape at 0x{site['address']:08X}")
        encoded = struct.unpack_from("<I", expected, offset)[0]
        old_base = encoded - int(site["displacement"])
        if old_base not in {0x00BC40D8, 0x00C8E0C8}:
            raise ValueError(f"collision-buffer operand mismatch at 0x{site['address']:08X}")
        patched_ranges.append((int(site["address"]) + offset, int(site["address"]) + offset + 4, "collision"))

    for site in records["growers"]:
        if len(site["bytes"]) != 5 or site["bytes"][0] != 0xE8:
            raise ValueError(f"invalid grower call at 0x{site['address']:08X}")
        patched_ranges.append((int(site["address"]) + 1, int(site["address"]) + 5, "grower"))
    for site in records["collision_nops"]:
        if len(site["bytes"]) != 5 or site["bytes"][0] != 0xE8:
            raise ValueError(f"invalid scratchpad call at 0x{site['address']:08X}")
        patched_ranges.append((int(site["address"]), int(site["address"]) + 5, "nop"))

    for index, left in enumerate(sorted(patched_ranges)):
        for right in sorted(patched_ranges)[index + 1 :]:
            if right[0] >= left[1]:
                break
            raise ValueError(f"overlapping patch writes: {left} and {right}")


def validate_executable(image: PeImage, records: dict[str, list[dict[str, object]]]) -> ExecutableIdentity:
    digest = hashlib.sha256(image.data).hexdigest()
    pe_tuple = (image.machine, image.magic, image.image_base, image.image_size, image.timestamp, image.checksum)
    matched = next(
        (identity for identity in EXECUTABLE_IDENTITIES if identity.sha256 == digest and identity.pe_tuple == pe_tuple),
        None,
    )
    if matched is None:
        raise ValueError(f"unsupported executable identity: sha256={digest}, PE={pe_tuple}")

    for group in ("constructors", "crt_routines", "pointers", "growers", "collision_pointers", "collision_nops"):
        for site in records[group]:
            size = int(site.get("size", len(site["bytes"])))
            expected_bytes = site["bytes"][:size]
            actual = image.read_va(int(site["address"]), size)
            if actual != expected_bytes:
                raise ValueError(f"{group} byte mismatch at 0x{site['address']:08X}")

    definitions = {item["kind"]: item for item in records["definitions"]}
    manifested: dict[int, set[int]] = {}
    for site in records["pointers"]:
        operand = int(site["address"]) + int(site["operand_offset"])
        encoded = struct.unpack_from("<I", site["bytes"], int(site["operand_offset"]))[0]
        manifested.setdefault(encoded, set()).add(operand)
    for definition in definitions.values():
        for displacement in (0, 4, 0x1C):
            value = int(definition["base"]) + displacement
            actual = image.executable_dword_occurrences(value)
            expected_sites = manifested.get(value, set())
            if actual != expected_sites:
                raise ValueError(
                    f"unmanifested executable references to 0x{value:08X}: "
                    f"actual={sorted(map(hex, actual))}, manifest={sorted(map(hex, expected_sites))}"
                )

    # Atomic's end address does not alias another store, so any executable
    # immediate referring to it would be an unhandled one-past-end bound. The
    # smaller stores end exactly where unrelated stock stores begin; their
    # bounds are count-driven and their aliased addresses cannot be rejected.
    atomic = definitions["Atomic"]
    atomic_end = int(atomic["base"]) + 4 + int(atomic["stock_capacity"]) * int(atomic["stride"])
    if image.executable_dword_occurrences(atomic_end):
        raise ValueError(f"unhandled Atomic one-past-end references to 0x{atomic_end:08X}")
    damage = definitions["DamageAtomic"]
    timed = definitions["Time"]
    if int(damage["base"]) + 4 + int(damage["stock_capacity"]) * int(damage["stride"]) != 0x00B1C934:
        raise ValueError("DamageAtomic one-past-end alias changed")
    if int(timed["base"]) + 4 + int(timed["stock_capacity"]) * int(timed["stride"]) != 0x00B1E128:
        raise ValueError("Time one-past-end alias changed")
    return matched


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--exe", type=Path, required=True, help="allowlisted GTA SA 1.0 US gta_sa.exe")
    args = parser.parse_args()

    records = parse_manifest(args.manifest)
    validate_manifest(records)
    identity = validate_executable(PeImage(args.exe), records)
    print(
        "native model-store manifest OK: "
        f"executable={identity.name}, "
        f"{len(records['pointers'])} model pointer instructions, "
        f"{len(records['growers'])} guarded grower calls, "
        f"{len(records['collision_pointers'])} collision pointers"
    )


if __name__ == "__main__":
    main()
