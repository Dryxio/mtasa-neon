#!/usr/bin/env python3
"""Validate Neon's read-only GTA SA FileID runtime anchors off-game."""

from __future__ import annotations

import argparse
import re
import struct
from dataclasses import dataclass
from pathlib import Path

from validate_native_model_store_patch import PeImage


TOOLS = Path(__file__).resolve().parent
REPOSITORY = TOOLS.parents[1]
DEFAULT_MANIFEST = REPOSITORY / "Client/game_sa/CFileIDRuntimeSA.Manifest.inc"

CALL = re.compile(r"^NATIVE_FILE_ID_ANCHOR\((.*)\)$")
EXPECTED_STOCK_LAYOUT = {
    "TxdBase": 20_000,
    "ColBase": 25_000,
    "IplBase": 25_255,
    "DatBase": 25_511,
    "IfpBase": 25_575,
    "RrrBase": 25_755,
    "ScmBase": 26_230,
    "StreamingBegin": 0x008E4CC0,
    "StreamingEnd": 0x009654B0,
    "ModelInfoBegin": 0x00A9B0C8,
}


@dataclass(frozen=True)
class Anchor:
    kind: str
    address: int
    operand_offset: int
    stock_value: int
    instruction_size: int
    expected: bytes


def parse_manifest(path: Path = DEFAULT_MANIFEST) -> list[Anchor]:
    anchors: list[Anchor] = []
    for line_number, original in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = original.strip()
        if not line or line.startswith("//"):
            continue
        match = CALL.fullmatch(line)
        if not match:
            raise ValueError(f"unparsed FileID manifest line {line_number}: {original}")
        args = [item.strip() for item in match.group(1).split(",")]
        if len(args) != 15:
            raise ValueError(f"bad FileID anchor on line {line_number}")
        anchors.append(
            Anchor(
                kind=args[0],
                address=int(args[1], 0),
                operand_offset=int(args[2], 0),
                stock_value=int(args[3], 0),
                instruction_size=int(args[4], 0),
                expected=bytes(int(value, 0) for value in args[5:]),
            )
        )
    return anchors


def validate_manifest(anchors: list[Anchor]) -> None:
    by_kind = {anchor.kind: anchor for anchor in anchors}
    if len(by_kind) != len(anchors):
        raise ValueError("duplicate FileID anchor kind")
    if set(by_kind) != set(EXPECTED_STOCK_LAYOUT):
        raise ValueError(f"FileID anchor coverage changed: {sorted(by_kind)}")

    ranges: list[tuple[int, int, str]] = []
    for kind, expected_stock in EXPECTED_STOCK_LAYOUT.items():
        anchor = by_kind[kind]
        if anchor.stock_value != expected_stock:
            raise ValueError(f"unexpected stock operand for {kind}")
        if not 1 <= anchor.instruction_size <= len(anchor.expected):
            raise ValueError(f"invalid instruction size for {kind}")
        if anchor.operand_offset + 4 > anchor.instruction_size:
            raise ValueError(f"operand outside instruction for {kind}")
        encoded = struct.unpack_from("<I", anchor.expected, anchor.operand_offset)[0]
        if encoded != anchor.stock_value:
            raise ValueError(f"manifest bytes do not encode the stock operand for {kind}")
        ranges.append((anchor.address, anchor.address + anchor.instruction_size, kind))

    for left, right in zip(sorted(ranges), sorted(ranges)[1:]):
        if right[0] < left[1]:
            raise ValueError(f"overlapping FileID anchors: {left[2]} and {right[2]}")

    streaming_bytes = EXPECTED_STOCK_LAYOUT["StreamingEnd"] - EXPECTED_STOCK_LAYOUT["StreamingBegin"]
    if streaming_bytes % 20 or streaming_bytes // 20 != 26_316:
        raise ValueError("stock streaming endpoints do not describe 26,316 entries")


def validate_executable(image: PeImage, anchors: list[Anchor]) -> None:
    if image.machine != 0x14C or image.magic != 0x10B or image.image_base != 0x00400000:
        raise ValueError("FileID anchors require a 32-bit PE32 image based at 0x00400000")
    for anchor in anchors:
        actual = image.read_va(anchor.address, anchor.instruction_size)
        expected = anchor.expected[: anchor.instruction_size]
        if actual != expected:
            raise ValueError(f"FileID anchor byte mismatch for {anchor.kind} at 0x{anchor.address:08X}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--exe", type=Path, required=True, help="stock-compatible GTA SA 1.0 US gta_sa.exe")
    args = parser.parse_args()

    anchors = parse_manifest(args.manifest)
    validate_manifest(anchors)
    validate_executable(PeImage(args.exe), anchors)
    print("native FileID runtime manifest OK: 10 read-only anchors, stock total=26316, native writes=no")


if __name__ == "__main__":
    main()
