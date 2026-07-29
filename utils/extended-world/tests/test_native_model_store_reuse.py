"""Structural guards for same-process native model-store foundation reuse."""

from __future__ import annotations

import pathlib
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
SOURCE = (REPOSITORY / "Client/game_sa/CNativeModelStoreSA.cpp").read_text(encoding="utf-8")


class NativeModelStoreReuseTests(unittest.TestCase):
    def test_installed_path_is_validation_only(self) -> None:
        validator = SOURCE[SOURCE.index("bool ValidateInstalledFoundation") : SOURCE.index("void ReleaseUncommittedAllocations")]
        self.assertIn("ValidateExecutable", validator)
        self.assertIn("ValidateInstalledAllocations", validator)
        self.assertIn("ValidateInstalledPatchSet", validator)
        for mutation in ("CommitPatchSet", "AllocateStoresAndCollisionBuffer", "MemPut<", "MemCpy(", "VirtualAlloc"):
            self.assertNotIn(mutation, validator)

        install = SOURCE[SOURCE.index("bool InstallValidated") : SOURCE.index("}  // namespace")]
        self.assertIn("if (g_installed)", install)
        self.assertLess(install.index("ValidateInstalledFoundation"), install.index("AllocateStoresAndCollisionBuffer"))

    def test_every_patch_family_has_an_exact_installed_postcondition(self) -> None:
        validator = SOURCE[SOURCE.index("bool ValidateInstalledPatchSet") : SOURCE.index("bool ValidateInstalledFoundation")]
        for family in ("POINTER_SITES", "GROWER_SITES", "COLLISION_POINTER_SITES", "COLLISION_NOP_SITES"):
            self.assertIn(family, validator)
        self.assertIn("site.action == EPatchAction::Patch", validator)
        self.assertIn("GrowerWrapper(site.kind)", validator)
        self.assertIn("reinterpret_cast<DWORD>(g_collisionBuffer)", validator)
        self.assertIn("constexpr BYTE NOPS[5]", validator)

    def test_reuse_revalidates_allocations_capacities_and_usage(self) -> None:
        validator = SOURCE[SOURCE.index("bool ValidateInstalledAllocations") : SOURCE.index("bool ValidateInstalledPatchSet")]
        for proof in (
            "ValidateCommittedAllocation",
            "state.occupiedAtInstall > definition.originalCapacity",
            "state.store->count < state.occupiedAtInstall",
            "state.store->count > definition.newCapacity",
            "state.highWater < state.store->count",
            "state.highWater > definition.newCapacity",
            "vtable != definition.vtable",
            "COLLISION_BUFFER_DEFINITIONS[0].newCapacity",
        ):
            self.assertIn(proof, validator)

    def test_public_preflight_switches_to_installed_contract(self) -> None:
        public = SOURCE[SOURCE.index("bool CNativeModelStoreSA::ValidateExecutableAndPatchManifestReadOnly") : SOURCE.index("void CNativeModelStoreSA::LogDiagnostics")]
        self.assertIn("if (g_installed)", public)
        self.assertIn("return ValidateInstalledFoundation(gameVersion, error)", public)
        prepared = SOURCE[SOURCE.index("bool CNativeModelStoreSA::InstallForAuthorizedStartup") : SOURCE.index("bool CNativeModelStoreSA::ValidateExecutable")]
        self.assertIn('reused ? "no" : "yes"', prepared)
        self.assertIn('reused ? "revalidated" : "installed"', prepared)


if __name__ == "__main__":
    unittest.main()
