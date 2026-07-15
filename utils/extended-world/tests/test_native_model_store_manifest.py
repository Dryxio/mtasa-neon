#!/usr/bin/env python3
"""Static tests for the native model-store executable patch manifest."""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

from validate_native_model_store_patch import (  # noqa: E402
    EXECUTABLE_IDENTITIES,
    PeImage,
    parse_manifest,
    validate_executable,
    validate_manifest,
)


class NativeModelStoreManifestTest(unittest.TestCase):
    def test_manifest_is_complete_and_internally_consistent(self) -> None:
        records = parse_manifest()
        validate_manifest(records)
        self.assertEqual(57, len(records["pointers"]))
        self.assertEqual(51, sum(item["action"] == "Patch" for item in records["pointers"]))
        self.assertEqual(6, len(records["crt_routines"]))
        self.assertEqual(12, len(records["collision_pointers"]))

    def test_executable_allowlist_is_exact(self) -> None:
        actual = {
            identity.name: (identity.sha256, identity.pe_tuple)
            for identity in EXECUTABLE_IDENTITIES
        }
        self.assertEqual(
            {
                "hoodlum-raw": (
                    "72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b",
                    (0x14C, 0x10B, 0x00400000, 0x008B1000, 0x427101CA, 0x00DC5BEA),
                ),
                "mta-programdata": (
                    "77485627b4ef17f92819318050d501e171c7ab84ceffe5091b973b9e29f9cc98",
                    (0x14C, 0x10B, 0x00400000, 0x01177000, 0x437101CA, 0x00DC29E6),
                ),
            },
            actual,
        )

    def test_local_stock_executable_when_available(self) -> None:
        candidate = Path(os.environ.get("GTA_SA_EXE", "/Users/salimtrouve/Documents/GTA-SanAndreas/gta_sa.exe"))
        if not candidate.is_file():
            self.skipTest("stock GTA SA 1.0 US HOODLUM executable is unavailable")
        records = parse_manifest()
        validate_manifest(records)
        identity = validate_executable(PeImage(candidate), records)
        self.assertEqual("hoodlum-raw", identity.name)

    def test_local_mta_runtime_executable_when_available(self) -> None:
        configured = os.environ.get("GTA_SA_MTA_EXE")
        if not configured:
            self.skipTest("GTA_SA_MTA_EXE does not point to the MTA ProgramData runtime executable")
        candidate = Path(configured)
        if not candidate.is_file():
            self.fail(f"GTA_SA_MTA_EXE is not a file: {candidate}")
        records = parse_manifest()
        validate_manifest(records)
        identity = validate_executable(PeImage(candidate), records)
        self.assertEqual("mta-programdata", identity.name)


if __name__ == "__main__":
    unittest.main()
