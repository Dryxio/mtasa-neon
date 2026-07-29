"""Structural guards for read-only ownership of relocated ModelInfo tails."""

from __future__ import annotations

import pathlib
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
HEADER = REPOSITORY / "Client/game_sa/CNativeModelStoreSA.h"
SOURCE = REPOSITORY / "Client/game_sa/CNativeModelStoreSA.cpp"


class NativeModelStoreTailOwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_public_contract_names_all_relocated_store_kinds(self) -> None:
        contract = self.header[
            self.header.index("enum class ENativeModelStoreKindSA") :
            self.header.index("class CNativeModelStoreSA")
        ]
        for kind in ("Atomic", "DamageAtomic", "Time"):
            self.assertIn(kind, contract)
        for field in ("kind{}", "index{}", "model{}"):
            self.assertIn(field, contract)
        for field in ("before{}", "after{}", "entries;"):
            self.assertIn(field, contract)

    def test_capture_enumerates_each_appended_tail(self) -> None:
        capture = self.source[
            self.source.index("bool CNativeModelStoreSA::CaptureOwnedTail") :
            self.source.index("bool CNativeModelStoreSA::ValidateOwnedTail")
        ]
        self.assertIn("index = GetCount(snapshot.before, internalKind)", capture)
        self.assertIn("index < GetCount(snapshot.after, internalKind)", capture)
        self.assertIn("GetStoreObject(internalKind, index)", capture)
        self.assertIn("return ValidateOwnedTail(snapshot, error)", capture)

    def test_validation_proves_private_pointer_arithmetic_and_exact_counts(self) -> None:
        validation = self.source[self.source.index("bool CNativeModelStoreSA::ValidateOwnedTail") :]
        self.assertIn("state.store->count != afterCount", validation)
        self.assertIn("entry.index != expectedIndex", validation)
        self.assertIn("entry.model != GetStoreObject(kind, entry.index)", validation)
        self.assertIn("next.atomic != snapshot.after.atomic", validation)
        pointer = self.source[
            self.source.index("CBaseModelInfoSAInterface* GetStoreObject") :
            self.source.index("void DebugLog")
        ]
        self.assertIn("store->objects", pointer)
        self.assertIn("index) * definition.stride", pointer)

    def test_ownership_api_is_read_only_with_respect_to_engine_state(self) -> None:
        ownership = self.source[self.source.index("bool CNativeModelStoreSA::CaptureOwnedTail") :]
        for forbidden in ("Shutdown", "VirtualFree", "store->count =", "Destructor", "DeallocateModel"):
            self.assertNotIn(forbidden, ownership)


if __name__ == "__main__":
    unittest.main()
