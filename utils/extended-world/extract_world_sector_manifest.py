#!/usr/bin/env python3
"""Extract selected Fastman92 map-limit CCodeMover calls into deterministic manifests."""

from __future__ import annotations

import argparse
import ast
import json
import re
from pathlib import Path


TARGETS = {
    "world": {
        "function": "void MapLimits::PatchWorldSectors_GTA_SA_PC_1_0_HOODLUM()",
        "call_count": 397,
        "opcodes": {0x00, 0x01, 0x02, 0x03, 0x05},
        "prefix": "WORLD_SECTOR",
        "entry_type": "SWorldSectorManifestEntry",
        "array_name": "WORLD_SECTOR_MANIFEST",
    },
    "water": {
        "function": "void MapLimits::PatchWaterMapSize_GTA_SA_PC_1_0_HOODLUM()",
        "call_count": 14,
        "opcodes": {0x00, 0x01, 0x03, 0x05},
        "prefix": "WATER_MAP",
        "entry_type": "SWaterMapManifestEntry",
        "array_name": "WATER_MAP_MANIFEST",
    },
}
CALL_PREFIXES = ("CCodeMover::FixOnAddress(", "CCodeMover::FixOnAddressRel(")
SUPPORTED_OPCODES = {0x00, 0x01, 0x02, 0x03, 0x05}

# Fastman92 changes this global because GTA also uses its vanilla 60.0 value as
# the half-width of the 120x120 world-sector grid. The same address is a shared
# numeric constant, however, and unrelated systems use it for their own 60.0
# values. In particular, the ANPK loader converts animation seconds to 60 Hz
# keyframe ticks through this address. Every world-sector instruction is already
# redirected to the dedicated fNumberOfWorldSectorsPerDimensionHalf variable,
# so changing the shared data value is both redundant and unsafe.
EXCLUDED_SHARED_DATA_PATCHES = {0x00858B34}


def extract_braced_block(source: str, marker: str) -> str:
    start = source.index(marker)
    opening_brace = source.index("{", start + len(marker))
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace + 1 : index]
    raise ValueError(f"Unterminated function: {marker}")


def split_arguments(arguments: str) -> list[str]:
    result: list[str] = []
    start = 0
    depth = 0
    in_string = False
    escaped = False
    for index, character in enumerate(arguments):
        if in_string:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_string = False
        elif character == '"':
            in_string = True
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        elif character == "," and depth == 0:
            result.append(arguments[start:index].strip())
            start = index + 1
    result.append(arguments[start:].strip())
    return result


def extract_calls(block: str) -> list[dict[str, object]]:
    calls: list[dict[str, object]] = []
    cursor = 0
    while True:
        matches = [(block.find(prefix, cursor), prefix) for prefix in CALL_PREFIXES]
        matches = [(position, prefix) for position, prefix in matches if position >= 0]
        if not matches:
            break
        start, prefix = min(matches)
        arguments_start = start + len(prefix)
        depth = 1
        in_string = False
        escaped = False
        index = arguments_start
        while depth:
            character = block[index]
            if in_string:
                if escaped:
                    escaped = False
                elif character == "\\":
                    escaped = True
                elif character == '"':
                    in_string = False
            elif character == '"':
                in_string = True
            elif character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
            index += 1

        arguments = split_arguments(block[arguments_start : index - 1])
        if len(arguments) != 4:
            raise ValueError(f"Unexpected argument count at offset {start}: {arguments}")

        string_tokens = re.findall(r'"(?:\\.|[^"\\])*"', arguments[2])
        bytecode = b"".join(ast.literal_eval("b" + token) for token in string_tokens)
        calls.append(
            {
                "kind": "relative" if "FixOnAddressRel" in prefix else "absolute",
                "address": int(arguments[0], 0),
                "original_size": int(arguments[1], 0),
                "bytecode": bytecode.hex(),
                "continue_at": int(arguments[3], 0),
            }
        )
        cursor = index
    return calls


def read_c_string(bytecode: bytes, cursor: int) -> int:
    return bytecode.index(0, cursor) + 1


def inspect_bytecode(bytecode: bytes) -> dict[int, int]:
    counts: dict[int, int] = {}
    cursor = 0
    while cursor < len(bytecode):
        opcode = bytecode[cursor]
        counts[opcode] = counts.get(opcode, 0) + 1
        cursor += 1
        if opcode == 0x00:
            if cursor != len(bytecode):
                raise ValueError("Data follows the CCodeMover terminator")
            break
        if opcode == 0x01:  # Literal bytes
            size = bytecode[cursor]
            cursor += 1 + size
        elif opcode == 0x02:  # Bytes copied from a GTA address
            cursor += 5
        elif opcode == 0x03:  # Relative 32-bit GTA address
            cursor += 4
        elif opcode == 0x05:  # Named variable, truncated to the requested size
            cursor = read_c_string(bytecode, cursor + 1)
        else:
            raise ValueError(f"Unsupported WorldSectors opcode: 0x{opcode:02X}")
    if cursor != len(bytecode):
        raise ValueError("CCodeMover bytecode is truncated")
    return counts


def write_cpp_manifest(output: Path, calls: list[dict[str, object]], target: dict[str, object]) -> None:
    # Fastman92 also lists compact-executable mirrors at 0x0156xxxx. MTA uses
    # the normal HOODLUM image, where those addresses are not mapped.
    calls = [call for call in calls if int(call["address"]) < 0x01000000]
    lines = [
        "// Generated by utils/extended-world/extract_world_sector_manifest.py.",
        f"// Source: Fastman92 Limit Adjuster, {str(target['function']).removeprefix('void ')}.",
        "// Fastman92 source is licensed under the MIT License.",
        "",
    ]
    for index, call in enumerate(calls):
        bytecode = bytes.fromhex(str(call["bytecode"]))
        encoded = ", ".join(f"0x{value:02X}" for value in bytecode)
        lines.append(f"static constexpr std::uint8_t {target['prefix']}_BYTECODE_{index}[] = {{{encoded}}};")

    lines.extend(("", f"static constexpr {target['entry_type']} {target['array_name']}[] = {{"))
    for index, call in enumerate(calls):
        lines.append(
            "    {"
            f"0x{int(call['address']):08X}, {int(call['original_size'])}, "
            f"{target['prefix']}_BYTECODE_{index}, sizeof({target['prefix']}_BYTECODE_{index}), "
            f"0x{int(call['continue_at']):08X}"
            "},"
        )
    lines.extend(("};", ""))
    output.write_text("\n".join(lines), encoding="utf-8")


def parse_address(expression: str) -> int:
    if not re.fullmatch(r"[0-9A-Fa-fxX+() ]+", expression):
        raise ValueError(f"Unsupported patch address: {expression}")
    return int(eval(expression, {"__builtins__": {}}, {}))


def write_direct_patch_manifest(output: Path, function: str) -> None:
    method_kinds = {
        "PatchPointer": "POINTER",
        "PatchUINT32": "UINT32",
        "PatchFloat": "FLOAT",
        "RedirectCode": "REDIRECT",
    }
    entries: list[tuple[int, str, str]] = []
    values: dict[str, int] = {}
    pattern = re.compile(r"^\s*CPatch::(PatchPointer|PatchUINT32|PatchFloat|RedirectCode)\((.+?)\);", re.MULTILINE)
    for match in pattern.finditer(function):
        arguments = split_arguments(match.group(2))
        if len(arguments) != 2:
            raise ValueError(f"Unexpected CPatch arguments: {arguments}")
        address = parse_address(arguments[0])
        if address >= 0x01000000:
            continue
        value = " ".join(arguments[1].split())
        value_id = values.setdefault(value, len(values))
        if address in EXCLUDED_SHARED_DATA_PATCHES:
            continue
        entries.append((address, method_kinds[match.group(1)], f"VALUE_{value_id}"))

    # CRenderer::SetupScanLists also calls GTA's out-of-line CWorld::GetSector
    # helper. It is absent from Fastman92's manifest and would otherwise keep
    # clamping to the old 120x120 grid after every generated-manifest refresh.
    neon_get_sector_value = "&GetExtendedWorldSector"
    neon_get_sector_value_id = values.setdefault(neon_get_sector_value, len(values))
    entries.append((0x00407260, "REDIRECT", f"VALUE_{neon_get_sector_value_id}"))

    lines = [
        "// Generated by utils/extended-world/extract_world_sector_manifest.py.",
        "// Source: Fastman92 Limit Adjuster WorldSectors patch (MIT License).",
        "// Shared GTA constant 0x00858B34 is intentionally preserved at its vanilla 60.0 value.",
        "",
        "enum class EWorldSectorPatchValue : std::uint8_t",
        "{",
    ]
    for value, value_id in values.items():
        lines.append(f"    VALUE_{value_id},  // {value}")
    lines.extend(("};", "", "static constexpr SWorldSectorDirectPatchEntry WORLD_SECTOR_DIRECT_PATCHES[] = {"))
    for address, kind, value_id in entries:
        if address == 0x00407260:
            lines.extend(
                (
                    "    // CRenderer::SetupScanLists calls this out-of-line helper, which otherwise",
                    "    // clamps indices to 119 and indexes GTA's old 120x120 sector grid.",
                )
            )
        lines.append(f"    {{0x{address:08X}, EWorldSectorDirectPatchKind::{kind}, EWorldSectorPatchValue::{value_id}}},")
    lines.extend(("};", ""))
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("map_limits", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--format", choices=("json", "cpp", "direct-cpp"), default="json")
    parser.add_argument("--target", choices=tuple(TARGETS), default="world")
    arguments = parser.parse_args()

    target = TARGETS[arguments.target]
    source = arguments.map_limits.read_text(encoding="utf-8-sig")
    function = extract_braced_block(source, str(target["function"]))
    calls = extract_calls(function)
    if len(calls) != target["call_count"]:
        raise ValueError(f"Expected {target['call_count']} CCodeMover calls, found {len(calls)}")

    opcode_counts: dict[int, int] = {}
    for call in calls:
        for opcode, count in inspect_bytecode(bytes.fromhex(str(call["bytecode"]))).items():
            opcode_counts[opcode] = opcode_counts.get(opcode, 0) + count
    if set(opcode_counts) != target["opcodes"] or not set(opcode_counts).issubset(SUPPORTED_OPCODES):
        raise ValueError(f"Unexpected opcode set: {sorted(opcode_counts)}")

    if arguments.format == "cpp":
        write_cpp_manifest(arguments.output, calls, target)
    elif arguments.format == "direct-cpp":
        if arguments.target != "world":
            raise ValueError("direct-cpp generation is currently specific to the WorldSectors target")
        write_direct_patch_manifest(arguments.output, function)
    else:
        manifest = {
            "source_function": target["function"],
            "opcode_counts": {f"0x{opcode:02X}": count for opcode, count in sorted(opcode_counts.items())},
            "calls": calls,
        }
        arguments.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
