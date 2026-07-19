#!/usr/bin/env python3
"""Validate Neon's GTA SA FileID capture and stock-only relocation manifests."""

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
DEFAULT_RELOCATION_MANIFEST = REPOSITORY / "Client/game_sa/CFileIDRelocationSA.Manifest.inc"

CALL = re.compile(r"^NATIVE_FILE_ID_ANCHOR\((.*)\)$")
RELOCATION_CALL = re.compile(r"^NATIVE_FILE_ID_(POINTER|VALUE|MOVZX|UINT16|REDIRECT)\((.*?)\)(?:\s*//.*)?$")
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
STOCK_LAYOUT = {
    "dff": 0,
    "txd": 20_000,
    "col": 25_000,
    "ipl": 25_255,
    "dat": 25_511,
    "ifp": 25_575,
    "rrr": 25_755,
    "scm": 26_230,
    "loaded": 26_312,
    "requested": 26_314,
    "total": 26_316,
}
TARGET_LAYOUT = {
    "dff": 0,
    "txd": 32_000,
    "col": 40_000,
    "ipl": 40_512,
    "dat": 41_536,
    "ifp": 41_600,
    "rrr": 41_780,
    "scm": 42_255,
    "loaded": 42_337,
    "requested": 42_339,
    "total": 42_341,
}
EXPECTED_RELOCATION_COUNTS = {
    "ModelPointer": 712,
    "StreamingPointer": 308,
    "Value32": 222,
    "Movzx": 27,
    "Value16": 4,
    "RedirectNextOnCd": 1,
    "RedirectSave": 1,
    "RedirectLoad": 1,
}


@dataclass(frozen=True)
class Anchor:
    kind: str
    address: int
    operand_offset: int
    stock_value: int
    instruction_size: int
    expected: bytes


@dataclass(frozen=True)
class RelocationPatch:
    kind: str
    address: int
    expected: int | bytes
    replacement: int | None

    @property
    def size(self) -> int:
        if self.kind in {"ModelPointer", "StreamingPointer", "Value32"}:
            return 4
        if self.kind in {"Movzx", "Value16"}:
            return 2
        if self.kind in {"RedirectNextOnCd", "RedirectSave", "RedirectLoad"}:
            return 5
        raise ValueError(f"unknown FileID patch kind: {self.kind}")


def _arguments(text: str) -> list[str]:
    return [item.strip() for item in text.split(",")]


def parse_manifest(path: Path = DEFAULT_MANIFEST) -> list[Anchor]:
    anchors: list[Anchor] = []
    for line_number, original in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = original.strip()
        if not line or line.startswith("//"):
            continue
        match = CALL.fullmatch(line)
        if not match:
            raise ValueError(f"unparsed FileID manifest line {line_number}: {original}")
        args = _arguments(match.group(1))
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


def parse_relocation_manifest(path: Path = DEFAULT_RELOCATION_MANIFEST) -> list[RelocationPatch]:
    patches: list[RelocationPatch] = []
    for line_number, original in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = original.strip()
        if not line or line.startswith("//"):
            continue
        match = RELOCATION_CALL.fullmatch(line)
        if not match:
            raise ValueError(f"unparsed FileID relocation line {line_number}: {original}")
        macro, arguments = match.groups()
        args = _arguments(arguments)
        if macro == "POINTER" and len(args) == 4:
            patches.append(RelocationPatch(f"{args[0]}Pointer", int(args[1], 0), int(args[2], 0), int(args[3], 0)))
        elif macro == "VALUE" and len(args) == 3:
            patches.append(RelocationPatch("Value32", int(args[0], 0), int(args[1], 0), int(args[2], 0)))
        elif macro == "MOVZX" and len(args) == 1:
            patches.append(RelocationPatch("Movzx", int(args[0], 0), b"\x0f\xbf", 0xB70F))
        elif macro == "UINT16" and len(args) == 3:
            patches.append(RelocationPatch("Value16", int(args[0], 0), int(args[1], 0), int(args[2], 0)))
        elif macro == "REDIRECT" and len(args) == 7:
            patches.append(RelocationPatch(f"Redirect{args[0]}", int(args[1], 0), bytes(int(value, 0) for value in args[2:]), None))
        else:
            raise ValueError(f"bad FileID relocation macro on line {line_number}: {original}")
    return patches


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
    if streaming_bytes % 20 or streaming_bytes // 20 != STOCK_LAYOUT["total"]:
        raise ValueError("stock streaming endpoints do not describe 26,316 entries")


def _allowed_value_pairs() -> set[tuple[int, int]]:
    pairs: set[tuple[int, int]] = set()
    for name in ("txd", "col", "ipl", "dat", "ifp", "rrr", "scm"):
        for multiplier in (1, -1):
            pairs.add(((STOCK_LAYOUT[name] * multiplier) & 0xFFFFFFFF, (TARGET_LAYOUT[name] * multiplier) & 0xFFFFFFFF))
    pairs.add(((STOCK_LAYOUT["txd"] * 4) & 0xFFFFFFFF, (TARGET_LAYOUT["txd"] * 4) & 0xFFFFFFFF))
    pairs.add(((STOCK_LAYOUT["txd"] * -3) & 0xFFFFFFFF, (TARGET_LAYOUT["txd"] * -3) & 0xFFFFFFFF))
    return pairs


def validate_relocation_manifest(patches: list[RelocationPatch]) -> None:
    counts = {kind: 0 for kind in EXPECTED_RELOCATION_COUNTS}
    ranges: list[tuple[int, int, str]] = []
    allowed_streaming_fields = {0, 2, 4, 6, 7, 8, 12, 16}
    allowed_values = _allowed_value_pairs()

    for patch in patches:
        if patch.kind not in counts:
            raise ValueError(f"unexpected FileID relocation kind: {patch.kind}")
        counts[patch.kind] += 1
        if not 0x00400000 <= patch.address < 0x01000000:
            raise ValueError(f"FileID relocation address outside the normal executable at 0x{patch.address:08X}")
        ranges.append((patch.address, patch.address + patch.size, patch.kind))

        if patch.kind == "ModelPointer":
            assert isinstance(patch.expected, int) and patch.replacement is not None
            stock_offset = patch.expected - EXPECTED_STOCK_LAYOUT["ModelInfoBegin"]
            if stock_offset < 0 or stock_offset % 4 or stock_offset > STOCK_LAYOUT["txd"] * 4:
                raise ValueError(f"invalid stock model-pointer operand at 0x{patch.address:08X}")
            if patch.replacement % 4 or patch.replacement > TARGET_LAYOUT["txd"] * 4:
                raise ValueError(f"invalid target model-pointer displacement at 0x{patch.address:08X}")
        elif patch.kind == "StreamingPointer":
            assert isinstance(patch.expected, int) and patch.replacement is not None
            stock_offset = patch.expected - EXPECTED_STOCK_LAYOUT["StreamingBegin"]
            if stock_offset < 0 or stock_offset > (STOCK_LAYOUT["total"] + 1) * 20:
                raise ValueError(f"invalid stock streaming-pointer operand at 0x{patch.address:08X}")
            if patch.replacement < 0 or patch.replacement > (TARGET_LAYOUT["total"] + 1) * 20:
                raise ValueError(f"invalid target streaming-pointer displacement at 0x{patch.address:08X}")
            if stock_offset % 20 not in allowed_streaming_fields or patch.replacement % 20 not in allowed_streaming_fields:
                raise ValueError(f"invalid CStreamingInfo field offset at 0x{patch.address:08X}")
        elif patch.kind == "Value32":
            assert isinstance(patch.expected, int) and patch.replacement is not None
            if (patch.expected, patch.replacement) not in allowed_values:
                raise ValueError(f"unexpected FileID base rewrite at 0x{patch.address:08X}")
            if patch.expected == patch.replacement:
                raise ValueError(f"no-op FileID base rewrite at 0x{patch.address:08X}")
        elif patch.kind == "Movzx":
            if patch.expected != b"\x0f\xbf" or patch.replacement != 0xB70F:
                raise ValueError(f"invalid signed-to-unsigned rewrite at 0x{patch.address:08X}")
        elif patch.kind == "Value16":
            assert isinstance(patch.expected, int) and patch.replacement is not None
            expected_sentinels = {
                STOCK_LAYOUT["requested"] - 1: TARGET_LAYOUT["requested"] - 1,
                STOCK_LAYOUT["loaded"]: TARGET_LAYOUT["loaded"],
                STOCK_LAYOUT["total"] - 1: TARGET_LAYOUT["total"] - 1,
                STOCK_LAYOUT["requested"]: TARGET_LAYOUT["requested"],
            }
            if expected_sentinels.get(patch.expected) != patch.replacement:
                raise ValueError(f"invalid relocated sentinel index at 0x{patch.address:08X}")
        elif patch.kind.startswith("Redirect"):
            if not isinstance(patch.expected, bytes) or len(patch.expected) != 5 or patch.replacement is not None:
                raise ValueError(f"invalid save compatibility hook at 0x{patch.address:08X}")

    if counts != EXPECTED_RELOCATION_COUNTS:
        raise ValueError(f"FileID relocation coverage changed: {counts}")
    ordered = sorted(ranges)
    for left, right in zip(ordered, ordered[1:]):
        if right[0] < left[1]:
            raise ValueError(f"overlapping FileID relocation writes at 0x{right[0]:08X}: {left[2]} and {right[2]}")

    if TARGET_LAYOUT["txd"] - TARGET_LAYOUT["dff"] != 32_000:
        raise ValueError("target DFF partition is not 32,000 entries")
    if TARGET_LAYOUT["col"] - TARGET_LAYOUT["txd"] != 8_000:
        raise ValueError("target TXD partition is not 8,000 entries")
    if TARGET_LAYOUT["ipl"] - TARGET_LAYOUT["col"] != 512:
        raise ValueError("target COL partition is not 512 entries")
    if TARGET_LAYOUT["dat"] - TARGET_LAYOUT["ipl"] != 1_024:
        raise ValueError("target IPL partition is not 1,024 entries")
    for left, right in (("dat", "ifp"), ("ifp", "rrr"), ("rrr", "scm"), ("scm", "loaded")):
        if TARGET_LAYOUT[right] - TARGET_LAYOUT[left] != STOCK_LAYOUT[right] - STOCK_LAYOUT[left]:
            raise ValueError(f"excluded {left.upper()} partition changed size")
    if TARGET_LAYOUT["total"] > 0xFFFF or TARGET_LAYOUT["txd"] - 1 > 0x7FFF:
        raise ValueError("target FileID widths violate the uint16/int16 contract")


def validate_executable(image: PeImage, anchors: list[Anchor]) -> None:
    if image.machine != 0x14C or image.magic != 0x10B or image.image_base != 0x00400000:
        raise ValueError("FileID anchors require a 32-bit PE32 image based at 0x00400000")
    for anchor in anchors:
        actual = image.read_va(anchor.address, anchor.instruction_size)
        expected = anchor.expected[: anchor.instruction_size]
        if actual != expected:
            raise ValueError(f"FileID anchor byte mismatch for {anchor.kind} at 0x{anchor.address:08X}")


def validate_relocation_executable(image: PeImage, patches: list[RelocationPatch]) -> None:
    """Check only operands that remain stable in the packed stock executable.

    Most pointer/base operands are validated after HOODLUM has unpacked itself,
    immediately before installation. The linked-list opcodes, initializer
    sentinels, and function prologues are also stable in the on-disk image and
    give the generator an independent off-game byte gate.
    """

    for patch in patches:
        if patch.kind == "Movzx":
            expected = b"\x0f\xbf"
        elif patch.kind == "Value16":
            assert isinstance(patch.expected, int)
            expected = struct.pack("<H", patch.expected)
        elif patch.kind.startswith("Redirect"):
            assert isinstance(patch.expected, bytes)
            expected = patch.expected
        else:
            continue
        actual = image.read_va(patch.address, len(expected))
        if actual != expected:
            raise ValueError(f"FileID relocation byte mismatch for {patch.kind} at 0x{patch.address:08X}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--relocation-manifest", type=Path, default=DEFAULT_RELOCATION_MANIFEST)
    parser.add_argument("--exe", type=Path, required=True, help="stock-compatible GTA SA 1.0 US gta_sa.exe")
    args = parser.parse_args()

    anchors = parse_manifest(args.manifest)
    relocation = parse_relocation_manifest(args.relocation_manifest)
    validate_manifest(anchors)
    validate_relocation_manifest(relocation)
    image = PeImage(args.exe)
    validate_executable(image, anchors)
    validate_relocation_executable(image, relocation)
    print(
        "native FileID manifests OK: 10 capture anchors, 1,276 relocation writes, "
        "target total=42341, DAT/path expansion=no"
    )


if __name__ == "__main__":
    main()
