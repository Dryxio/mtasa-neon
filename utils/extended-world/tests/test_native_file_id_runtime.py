#!/usr/bin/env python3
"""Static tests for the read-only native FileID runtime abstraction."""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1]
REPOSITORY = TOOLS.parents[1]
sys.path.insert(0, str(TOOLS))

from validate_native_file_id_runtime import (  # noqa: E402
    EXPECTED_STOCK_LAYOUT,
    PeImage,
    parse_manifest,
    validate_executable,
    validate_manifest,
)


class NativeFileIDRuntimeTest(unittest.TestCase):
    def test_manifest_covers_the_complete_runtime_layout(self) -> None:
        anchors = parse_manifest()
        validate_manifest(anchors)
        self.assertEqual(10, len(anchors))
        self.assertEqual(EXPECTED_STOCK_LAYOUT, {anchor.kind: anchor.stock_value for anchor in anchors})

    def test_mta_has_no_legacy_static_file_id_captures(self) -> None:
        roots = (
            REPOSITORY / "Client/core",
            REPOSITORY / "Client/game_sa",
            REPOSITORY / "Client/mods/deathmatch",
            REPOSITORY / "Client/multiplayer_sa",
        )
        forbidden = (
            "ARRAY_ModelInfo",
            "CStreaming__ms_aInfoForModel",
            "*(char**)(0x5B8B08 + 6)",
            "0xA9B0C8",
            "0A9B0C8h",
        )
        offenders: list[str] = []
        for root in roots:
            for source in (*root.rglob("*.cpp"), *root.rglob("*.h")):
                text = source.read_text(encoding="utf-8", errors="replace")
                for token in forbidden:
                    if token in text:
                        offenders.append(f"{source.relative_to(REPOSITORY)}: {token}")
        self.assertEqual([], offenders)

    def test_runtime_capture_is_read_only(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CFileIDRuntimeSA.cpp").read_text(encoding="utf-8")
        self.assertIn("std::memcmp", source)
        self.assertIn("VirtualQuery", source)
        self.assertNotIn("MemPut", source)
        self.assertNotIn("MemCpy", source)
        self.assertNotIn("VirtualProtect", source)
        self.assertIn("nativeWrites=no", source)

    def test_local_stock_executable_when_available(self) -> None:
        candidate = Path(os.environ.get("GTA_SA_EXE", "/Users/salimtrouve/Documents/GTA-SanAndreas/gta_sa.exe"))
        if not candidate.is_file():
            self.skipTest("stock GTA SA 1.0 US HOODLUM executable is unavailable")
        anchors = parse_manifest()
        validate_manifest(anchors)
        validate_executable(PeImage(candidate), anchors)


if __name__ == "__main__":
    unittest.main()
