#!/usr/bin/env python3
"""Static tests for the native FileID capture and stock-only relocation."""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from collections import Counter
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1]
REPOSITORY = TOOLS.parents[1]
sys.path.insert(0, str(TOOLS))

from validate_native_file_id_runtime import (  # noqa: E402
    EXPECTED_RELOCATION_COUNTS,
    EXPECTED_STOCK_LAYOUT,
    STOCK_LAYOUT,
    TARGET_LAYOUT,
    PeImage,
    parse_manifest,
    parse_relocation_manifest,
    validate_executable,
    validate_manifest,
    validate_relocation_executable,
    validate_relocation_manifest,
)


class NativeFileIDRuntimeTest(unittest.TestCase):
    def test_manifests_cover_capture_and_relocation(self) -> None:
        anchors = parse_manifest()
        relocation = parse_relocation_manifest()
        validate_manifest(anchors)
        validate_relocation_manifest(relocation)
        self.assertEqual(10, len(anchors))
        self.assertEqual(EXPECTED_STOCK_LAYOUT, {anchor.kind: anchor.stock_value for anchor in anchors})
        self.assertEqual(EXPECTED_RELOCATION_COUNTS, Counter(patch.kind for patch in relocation))
        self.assertEqual(1_276, len(relocation))
        next_on_cd = [patch for patch in relocation if patch.kind == "RedirectNextOnCd"]
        self.assertEqual(1, len(next_on_cd))
        self.assertEqual(0x0040CD10, next_on_cd[0].address)
        self.assertEqual(b"\x83\xfe\xff\x0f\x84", next_on_cd[0].expected)

    def test_target_layout_reserves_only_static_world_partitions(self) -> None:
        self.assertEqual((31_999, 32_000), (TARGET_LAYOUT["txd"] - 1, TARGET_LAYOUT["txd"]))
        self.assertEqual((39_999, 40_000), (TARGET_LAYOUT["col"] - 1, TARGET_LAYOUT["col"]))
        self.assertEqual((40_511, 40_512), (TARGET_LAYOUT["ipl"] - 1, TARGET_LAYOUT["ipl"]))
        self.assertEqual(42_341, TARGET_LAYOUT["total"])
        self.assertLessEqual(TARGET_LAYOUT["total"], 0xFFFF)
        self.assertLessEqual(TARGET_LAYOUT["txd"] - 1, 0x7FFF)
        for left, right in (("dat", "ifp"), ("ifp", "rrr"), ("rrr", "scm"), ("scm", "loaded")):
            self.assertEqual(STOCK_LAYOUT[right] - STOCK_LAYOUT[left], TARGET_LAYOUT[right] - TARGET_LAYOUT[left])

    def test_mta_consumers_have_no_legacy_static_file_id_captures(self) -> None:
        roots = (
            REPOSITORY / "Client/core",
            REPOSITORY / "Client/game_sa",
            REPOSITORY / "Client/mods/deathmatch",
            REPOSITORY / "Client/multiplayer_sa",
        )
        implementation = REPOSITORY / "Client/game_sa/CFileIDRuntimeSA.cpp"
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
                if source == implementation:
                    continue
                text = source.read_text(encoding="utf-8", errors="replace")
                for token in forbidden:
                    if token in text:
                        offenders.append(f"{source.relative_to(REPOSITORY)}: {token}")
        self.assertEqual([], offenders)

    def test_install_is_preflighted_and_process_lifetime(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CFileIDRuntimeSA.cpp").read_text(encoding="utf-8")
        self.assertIn("ValidateRelocationManifest", source)
        self.assertIn("VirtualQuery", source)
        self.assertIn("VirtualAlloc", source)
        self.assertIn("VirtualProtect", source)
        self.assertIn("FlushInstructionCache", source)
        self.assertIn("nativeWrites=no", source)
        self.assertIn("nativeWrites=yes", source)
        self.assertIn("datExpansion=no pathsExpansion=no", source)
        self.assertIn("std::array<BYTE, STOCK_SAVED_FILE_COUNT>", source)
        self.assertIn("STOCK_SAVED_FILE_COUNT = 26316", source)
        self.assertIn("CompareNextModelOnCdUnsigned", source)
        self.assertIn("info.flg |= savedFlags", source)
        prepare = source.index("std::vector<SPreparedWrite> writes")
        commit = source.index("m_installStarted = true", prepare)
        native_write = source.index("WriteMemory(write.address", commit)
        self.assertLess(prepare, commit)
        self.assertLess(commit, native_write)

    def test_game_startup_orders_shared_instruction_preflights(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CGameSA.cpp").read_text(encoding="utf-8")
        capture = source.index("m_fileIDs.CaptureStockLayout")
        stores = source.index("CNativeModelStoreSA::InstallFromEnvironment", capture)
        relocation = source.index("m_fileIDs.InstallStockRelocation", stores)
        publish = source.index("CModelInfoSAInterface::ms_modelInfoPtrs", relocation)
        self.assertLess(capture, stores)
        self.assertLess(stores, relocation)
        self.assertLess(relocation, publish)

    def test_manifest_is_reproducible_when_local_references_are_available(self) -> None:
        fla_root = Path(
            os.environ.get(
                "FLA_SOURCE_ROOT",
                "/Users/salimtrouve/Documents/GitHub/mta-misc/fastman/source code/fastman92 limit adjuster/"
                "fastman92 limit adjuster/Source files",
            )
        )
        file_id_limit = fla_root / "Modules/FileIDlimit.cpp"
        int32_header = (
            fla_root
            / "Modules/FileIDlimit/GTA SA 1.0 US HOODLUM WIN_X86/GTASA_int32_base_movsx_patches.h"
        )
        executable = Path(
            os.environ.get(
                "GTA_SA_REFERENCE_EXE",
                "/Users/salimtrouve/Documents/GitHub/gta-reversed-dryxio/gta_sa_compact1.0.exe",
            )
        )
        if not all(path.is_file() for path in (file_id_limit, int32_header, executable)):
            self.skipTest("FLA source or raw HOODLUM reference executable is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            generated = Path(temporary) / "CFileIDRelocationSA.Manifest.inc"
            subprocess.run(
                [
                    sys.executable,
                    str(TOOLS / "extract_file_id_relocation_manifest.py"),
                    str(file_id_limit),
                    str(int32_header),
                    str(executable),
                    str(generated),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            committed = REPOSITORY / "Client/game_sa/CFileIDRelocationSA.Manifest.inc"
            self.assertEqual(committed.read_bytes(), generated.read_bytes())

    def test_local_stock_executable_when_available(self) -> None:
        candidates = (
            Path(os.environ["GTA_SA_EXE"]) if "GTA_SA_EXE" in os.environ else None,
            Path("/Users/salimtrouve/Documents/GitHub/gta-reversed-dryxio/gta_sa_compact1.0.exe"),
            Path("/Users/salimtrouve/Documents/GTA-SanAndreas/gta_sa.exe"),
        )
        candidate = next((path for path in candidates if path is not None and path.is_file()), None)
        if candidate is None:
            self.skipTest("stock GTA SA 1.0 US HOODLUM executable is unavailable")
        anchors = parse_manifest()
        relocation = parse_relocation_manifest()
        validate_manifest(anchors)
        validate_relocation_manifest(relocation)
        image = PeImage(candidate)
        validate_executable(image, anchors)
        validate_relocation_executable(image, relocation)


if __name__ == "__main__":
    unittest.main()
