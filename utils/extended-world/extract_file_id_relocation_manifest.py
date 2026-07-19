#!/usr/bin/env python3
"""Generate Neon's stock-only FileID relocation manifest from Fastman92."""

from __future__ import annotations

import argparse
import re
import struct
from dataclasses import dataclass
from pathlib import Path

from validate_native_model_store_patch import PeImage


FUNCTION = "void FileIDlimit::PatchFileIDlimits_GTA_SA_PC_1_0_HOODLUM()"
MODEL_MARKER = "// Patch pointers to CModelInfo::ms_modelInfoPtrs"
STREAMING_MARKER = "// Patch pointers to CStreaming::ms_aInfoForModel"
BASE_MARKER = "// Patch base IDs"
LIMIT_MARKER = "// Patch limits for different types of files."

STOCK_LAYOUT = {
    "DFF": 0,
    "TXD": 20_000,
    "COL": 25_000,
    "IPL": 25_255,
    "DAT": 25_511,
    "IFP": 25_575,
    "RRR": 25_755,
    "SCM": 26_230,
    "LOADED_START": 26_312,
    "REQUESTED_START": 26_314,
    "TOTAL": 26_316,
}

# DAT is GTA's path-node FileID partition. Neon deliberately keeps its stock
# 64 entries while reserving capacity only for static-world file types.
TARGET_LAYOUT = {
    "DFF": 0,
    "TXD": 32_000,
    "COL": 40_000,
    "IPL": 40_512,
    "DAT": 41_536,
    "IFP": 41_600,
    "RRR": 41_780,
    "SCM": 42_255,
    "LOADED_START": 42_337,
    "REQUESTED_START": 42_339,
    "TOTAL": 42_341,
}

MODEL_INFO_BASE = 0x00A9B0C8
STREAMING_INFO_BASE = 0x008E4CC0
STREAMING_INFO_SIZE = 20
STREAMING_FIELDS = {
    "m_nextIndex": 0,
    "m_prevIndex": 2,
    "m_nextModelOnCd": 4,
    "m_flags": 6,
    "m_image": 7,
    "iBlockOffset": 8,
    "iBlockCount": 12,
    "m_status": 16,
}

MODEL_IDS = {
    "MODEL_MALE01": 7,
    "MODEL_HUNTER": 155,
    "MODEL_SKIMMER": 190,
    "MODEL_JETPACK": 370,
    "MODEL_CAR_DOOR": 374,
    "MODEL_CAR_BUMPER": 375,
    "MODEL_CAR_PANEL": 376,
    "MODEL_CAR_BONNET": 377,
    "MODEL_CAR_BOOT": 378,
    "MODEL_CAR_WHEEL": 379,
    "MODEL_BODY_PART_A": 380,
    "MODEL_BODY_PART_B": 381,
    "MODEL_CLOTHES01_ID384": 384,
    "MODEL_SHANDL": 394,
    "MODEL_SHANDR": 395,
    "MODEL_FHANDL": 396,
    "MODEL_FHANDR": 397,
    "MODEL_FORKLIFT": 530,
}

PURE_BASE_EXPRESSIONS = {
    "GetBaseID(FILE_TYPE_TXD)": lambda layout: layout["TXD"],
    "GetBaseID(FILE_TYPE_TXD) * 4": lambda layout: layout["TXD"] * 4,
    "-GetBaseID(FILE_TYPE_TXD)": lambda layout: -layout["TXD"],
    "-GetBaseID(FILE_TYPE_TXD) * 3": lambda layout: -layout["TXD"] * 3,
    "GetBaseID(FILE_TYPE_COL)": lambda layout: layout["COL"],
    "-GetBaseID(FILE_TYPE_COL)": lambda layout: -layout["COL"],
    "GetBaseID(FILE_TYPE_IPL)": lambda layout: layout["IPL"],
    "-GetBaseID(FILE_TYPE_IPL)": lambda layout: -layout["IPL"],
    "GetBaseID(FILE_TYPE_DAT)": lambda layout: layout["DAT"],
    "-GetBaseID(FILE_TYPE_DAT)": lambda layout: -layout["DAT"],
    "GetBaseID(FILE_TYPE_IFP)": lambda layout: layout["IFP"],
    "-GetBaseID(FILE_TYPE_IFP)": lambda layout: -layout["IFP"],
    "GetBaseID(FILE_TYPE_RRR)": lambda layout: layout["RRR"],
    "-GetBaseID(FILE_TYPE_RRR)": lambda layout: -layout["RRR"],
    "GetBaseID(FILE_TYPE_SCM)": lambda layout: layout["SCM"],
    "-GetBaseID(FILE_TYPE_SCM)": lambda layout: -layout["SCM"],
}

SENTINEL_POINTERS = (
    (0x5B8B12 + 6, "LOADED_START", 0, "m_nextIndex"),
    (0x5B8B1C + 6, "REQUESTED_START", -1, "m_nextIndex"),
    (0x5B8B26 + 6, "REQUESTED_START", 0, "m_nextIndex"),
    (0x5B8B30 + 6, "TOTAL", -1, "m_nextIndex"),
    (0x5B8B3A + 3, "LOADED_START", 0, "m_nextIndex"),
    (0x5B8B43 + 3, "LOADED_START", 0, "m_prevIndex"),
    (0x5B8B4A + 3, "REQUESTED_START", -1, "m_nextIndex"),
    (0x5B8B51 + 3, "REQUESTED_START", -1, "m_prevIndex"),
    (0x5B8B5A + 3, "REQUESTED_START", 0, "m_nextIndex"),
    (0x5B8B63 + 3, "REQUESTED_START", 0, "m_prevIndex"),
    (0x5B8B6A + 3, "TOTAL", -1, "m_nextIndex"),
    (0x5B8B71 + 3, "TOTAL", -1, "m_prevIndex"),
)

SENTINEL_VALUES = (
    (0x5B8B3A + 7, "REQUESTED_START", -1),
    (0x5B8B51 + 7, "LOADED_START", 0),
    (0x5B8B5A + 7, "TOTAL", -1),
    (0x5B8B71 + 7, "REQUESTED_START", 0),
)

# FLA supplies the save/load replacements. Neon's additional NextOnCd hook
# preserves GTA's -1 control-flow sentinel after the linked ID itself becomes
# unsigned: movzx turns 0xFFFF into 65535, so the stock 32-bit cmp -1 no longer
# terminates the IMG chain without this comparison rewrite.
REDIRECTS = (("NextOnCd", 0x40CD10), ("Save", 0x5D29A0), ("Load", 0x5D29E0))


@dataclass(frozen=True)
class PointerPatch:
    kind: str
    address: int
    expected: int
    target_offset: int
    source: str


@dataclass(frozen=True)
class ValuePatch:
    address: int
    expected: int
    replacement: int
    source: str


def extract_braced_block(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index("{", start + len(marker))
    depth = 0
    for index in range(opening, len(source)):
        depth += (source[index] == "{") - (source[index] == "}")
        if depth == 0:
            return source[opening + 1 : index]
    raise ValueError(f"unterminated function: {marker}")


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//.*", "", source)


def split_arguments(arguments: str) -> list[str]:
    result: list[str] = []
    start = 0
    depth = 0
    for index, character in enumerate(arguments):
        if character in "([":
            depth += 1
        elif character in ")]":
            depth -= 1
        elif character == "," and depth == 0:
            result.append(arguments[start:index].strip())
            start = index + 1
    result.append(arguments[start:].strip())
    return result


def parse_address(expression: str) -> int:
    if not re.fullmatch(r"[0-9A-Fa-fxX+() ]+", expression):
        raise ValueError(f"unsupported patch address: {expression}")
    return int(eval(expression, {"__builtins__": {}}, {}))


def dword(image: PeImage, address: int) -> int:
    return struct.unpack("<I", image.read_va(address, 4))[0]


def word(image: PeImage, address: int) -> int:
    return struct.unpack("<H", image.read_va(address, 2))[0]


def streaming_index(expression: str, layout: dict[str, int]) -> int:
    expression = expression.strip()
    expression = re.sub(
        r"GetBaseID\(FILE_TYPE_([A-Z_]+)\)",
        lambda match: str(layout[match.group(1)]),
        expression,
    )
    expression = expression.replace("GetCountOfAllFileIDs()", str(layout["TOTAL"]))
    if not re.fullmatch(r"[0-9+\-() ]+", expression):
        raise ValueError(f"unsupported streaming index: {expression}")
    return int(eval(expression, {"__builtins__": {}}, {}))


def streaming_offset(expression: str, layout: dict[str, int]) -> int:
    compact = " ".join(expression.split())
    prefix = "CStreaming__ms_aInfoForModel.gta_sa"
    if compact.startswith("&"):
        compact = compact[1:]
    if not compact.startswith(prefix):
        raise ValueError(f"unsupported streaming pointer: {expression}")
    tail = compact[len(prefix) :]
    index = 0
    if tail.startswith("["):
        closing = tail.index("]")
        index = streaming_index(tail[1:closing], layout)
        tail = tail[closing + 1 :]
    elif tail.startswith(" + "):
        index = streaming_index(tail[3:], layout)
        tail = ""
    field = 0
    if tail:
        match = re.fullmatch(r"(?:->|\.)([A-Za-z0-9_]+)", tail)
        if not match or match.group(1) not in STREAMING_FIELDS:
            raise ValueError(f"unsupported streaming field: {expression}")
        field = STREAMING_FIELDS[match.group(1)]
    return index * STREAMING_INFO_SIZE + field


def model_offset(expression: str, layout: dict[str, int]) -> int:
    compact = " ".join(expression.split())
    prefix = "CModelInfo__ms_modelInfoPtrs.gta_sa"
    if not compact.startswith(prefix):
        raise ValueError(f"unsupported model pointer: {expression}")
    suffix = compact[len(prefix) :].strip()
    if not suffix:
        index = 0
    else:
        if not suffix.startswith("+"):
            raise ValueError(f"unsupported model pointer suffix: {expression}")
        suffix = suffix[1:].strip()
        suffix = suffix.replace("GetBaseID(FILE_TYPE_TXD)", str(layout["TXD"]))
        for name, value in MODEL_IDS.items():
            suffix = re.sub(rf"\b{name}\b", str(value), suffix)
        if not re.fullmatch(r"[0-9+\-() ]+", suffix):
            raise ValueError(f"unsupported model index: {expression}")
        index = int(eval(suffix, {"__builtins__": {}}, {}))
    return index * 4


def pointer_calls(section: str) -> list[tuple[int, str]]:
    section = strip_comments(section)
    calls: list[tuple[int, str]] = []
    for match in re.finditer(r"CPatch::PatchPointer\((.*?)\);", section, flags=re.DOTALL):
        arguments = split_arguments(match.group(1))
        if len(arguments) != 2:
            raise ValueError(f"unexpected PatchPointer arguments: {arguments}")
        address = parse_address(arguments[0])
        if address < 0x01000000:
            calls.append((address, arguments[1]))
    return calls


def build_pointer_patches(function: str, image: PeImage) -> list[PointerPatch]:
    model_start = function.index(MODEL_MARKER)
    streaming_start = function.index(STREAMING_MARKER)
    base_start = function.index(BASE_MARKER)
    patches: list[PointerPatch] = []
    for address, expression in pointer_calls(function[model_start:streaming_start]):
        stock_offset = model_offset(expression, STOCK_LAYOUT)
        target_offset = model_offset(expression, TARGET_LAYOUT)
        expected = MODEL_INFO_BASE + stock_offset
        patches.append(PointerPatch("Model", address, expected, target_offset, expression))

    for address, expression in pointer_calls(function[streaming_start:base_start]):
        stock_offset = streaming_offset(expression, STOCK_LAYOUT)
        target_offset = streaming_offset(expression, TARGET_LAYOUT)
        expected = STREAMING_INFO_BASE + stock_offset
        patches.append(PointerPatch("Streaming", address, expected, target_offset, expression))

    for address, layout_key, delta, field_name in SENTINEL_POINTERS:
        stock_offset = (STOCK_LAYOUT[layout_key] + delta) * STREAMING_INFO_SIZE + STREAMING_FIELDS[field_name]
        target_offset = (TARGET_LAYOUT[layout_key] + delta) * STREAMING_INFO_SIZE + STREAMING_FIELDS[field_name]
        expected = STREAMING_INFO_BASE + stock_offset
        if dword(image, address) != expected:
            raise ValueError(f"sentinel pointer mismatch at 0x{address:08X}")
        patches.append(PointerPatch("Streaming", address, expected, target_offset, f"sentinel {layout_key}{delta:+d}.{field_name}"))

    by_address: dict[int, PointerPatch] = {}
    for patch in patches:
        previous = by_address.setdefault(patch.address, patch)
        if previous != patch:
            raise ValueError(f"conflicting pointer patch at 0x{patch.address:08X}")
    return sorted(by_address.values(), key=lambda patch: patch.address)


def build_value_patches(function: str, image: PeImage) -> list[ValuePatch]:
    section = strip_comments(function[function.index(BASE_MARKER) : function.index(LIMIT_MARKER)])
    patches: list[ValuePatch] = []
    for match in re.finditer(r"CPatch::PatchUINT32\((.*?)\);", section, flags=re.DOTALL):
        arguments = split_arguments(match.group(1))
        if len(arguments) != 2:
            raise ValueError(f"unexpected PatchUINT32 arguments: {arguments}")
        expression = " ".join(arguments[1].split())
        resolver = PURE_BASE_EXPRESSIONS.get(expression)
        if resolver is None:
            continue
        address = parse_address(arguments[0])
        if address >= 0x01000000:
            continue
        stock_value = resolver(STOCK_LAYOUT) & 0xFFFFFFFF
        replacement = resolver(TARGET_LAYOUT) & 0xFFFFFFFF
        patches.append(ValuePatch(address, stock_value, replacement, expression))
    return sorted(patches, key=lambda patch: patch.address)


def build_movzx_patches(int32_header: Path, image: PeImage) -> list[tuple[int, bytes]]:
    patches: list[tuple[int, bytes]] = []
    pattern = re.compile(r"FixOnAddress(?:Rel)?\((0x[0-9A-Fa-f]+).*?//.*?:\s*(.*)$")
    for line in int32_header.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.search(line)
        if not match or int(match.group(1), 16) >= 0x01000000 or "movsx" not in match.group(2):
            continue
        address = int(match.group(1), 16)
        expected = image.read_va(address, 2)
        if expected != b"\x0f\xbf":
            raise ValueError(f"expected movsx opcode at 0x{address:08X}, got {expected.hex()}")
        patches.append((address, expected))
    return patches


def write_manifest(
    output: Path,
    pointers: list[PointerPatch],
    values: list[ValuePatch],
    movzx: list[tuple[int, bytes]],
    image: PeImage,
) -> None:
    lines = [
        "// Generated by utils/extended-world/extract_file_id_relocation_manifest.py.",
        "// Sources: Fastman92 FileIDlimit.cpp and GTASA_int32_base_movsx_patches.h (MIT License).",
        "// Target: 32,000 DFF / 8,000 TXD / 512 COL / 1,024 IPL; stock DAT/IFP/RRR/SCM.",
        "",
    ]
    for patch in pointers:
        lines.append(
            f"NATIVE_FILE_ID_POINTER({patch.kind}, 0x{patch.address:08X}, 0x{patch.expected:08X}, "
            f"0x{patch.target_offset:08X})  // {patch.source}"
        )
    lines.append("")
    for patch in values:
        lines.append(
            f"NATIVE_FILE_ID_VALUE(0x{patch.address:08X}, 0x{patch.expected:08X}, 0x{patch.replacement:08X})  // {patch.source}"
        )
    lines.append("")
    for address, _ in movzx:
        lines.append(f"NATIVE_FILE_ID_MOVZX(0x{address:08X})")
    lines.append("")
    for address, layout_key, delta in SENTINEL_VALUES:
        expected = word(image, address)
        stock = (STOCK_LAYOUT[layout_key] + delta) & 0xFFFF
        replacement = (TARGET_LAYOUT[layout_key] + delta) & 0xFFFF
        if expected != stock:
            raise ValueError(f"sentinel value mismatch at 0x{address:08X}")
        lines.append(
            f"NATIVE_FILE_ID_UINT16(0x{address:08X}, 0x{expected:04X}, 0x{replacement:04X})  // {layout_key}{delta:+d}"
        )
    lines.append("")
    for kind, address in REDIRECTS:
        expected = image.read_va(address, 5)
        encoded = ", ".join(f"0x{byte:02X}" for byte in expected)
        lines.append(f"NATIVE_FILE_ID_REDIRECT({kind}, 0x{address:08X}, {encoded})")
    lines.append("")
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file_id_limit", type=Path)
    parser.add_argument("int32_header", type=Path)
    parser.add_argument("exe", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()

    source = arguments.file_id_limit.read_text(encoding="utf-8", errors="replace")
    function = extract_braced_block(source, FUNCTION)
    image = PeImage(arguments.exe)
    pointers = build_pointer_patches(function, image)
    values = build_value_patches(function, image)
    movzx = build_movzx_patches(arguments.int32_header, image)
    write_manifest(arguments.output, pointers, values, movzx, image)
    print(
        f"generated {arguments.output}: pointers={len(pointers)} values={len(values)} "
        f"movzx={len(movzx)} sentinels={len(SENTINEL_VALUES)} redirects={len(REDIRECTS)}"
    )


if __name__ == "__main__":
    main()
