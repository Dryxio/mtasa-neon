"""Structural guards for committed native-world content teardown."""

from __future__ import annotations

import pathlib
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
PACK = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
STREAMING = (REPOSITORY / "Client/game_sa/CStreamingSA.cpp").read_text(encoding="utf-8")
MODEL_STORE = (REPOSITORY / "Client/game_sa/CNativeModelStoreSA.cpp").read_text(encoding="utf-8")


class NativeWorldRuntimeTeardownTests(unittest.TestCase):
    def test_preflight_precedes_irreversible_state(self) -> None:
        teardown = PACK[PACK.index("bool CNativeWorldPackManagerSA::TeardownRuntimeContent") :]
        boundary = teardown.index("g_state = EState::TearingDown")
        for proof in (
            "ValidateStaticWorldV3PoolOwnership",
            "ValidateStaticWorldV3ModelOwnership",
            "NormalizeStaticWorldV3StreamingLifecycle",
            "ValidateArchiveAllocationOwnershipBatch",
            "RestoreStaticWorldV3StreamingBindings",
        ):
            self.assertLess(teardown.index(proof), boundary)
        self.assertGreater(teardown.index("REMOVE_IPL_SLOT"), boundary)
        self.assertGreater(teardown.index("ShutdownAndRewindOwnedTail"), boundary)

    def test_native_slot_destructors_and_child_first_lod_order_are_used(self) -> None:
        teardown = PACK[PACK.index("bool CNativeWorldPackManagerSA::TeardownRuntimeContent") :]
        for token in ("REMOVE_IPL_SLOT", "REMOVE_COL_SLOT", "REMOVE_TXD_SLOT", "CountItemOccurrences"):
            self.assertIn(token, teardown)
        self.assertIn("left->hiddenLodOwner < right->hiddenLodOwner", teardown)
        self.assertLess(teardown.index("removeIplSlot"), teardown.index("ShutdownAndRewindOwnedTail"))
        self.assertLess(teardown.index("ShutdownAndRewindOwnedTail"), teardown.index("removeColSlot"))

    def test_col_identity_uses_pool_generation_and_catalogs_not_opaque_bytes(self) -> None:
        col_def = PACK[PACK.index("struct SColDef") : PACK.index("struct SIdePlan")]
        self.assertIn("reserved[18]", col_def)
        self.assertNotIn("name[18]", col_def)

        validation = PACK[
            PACK.index("bool ValidateStaticWorldV3PoolOwnership") : PACK.index("bool ValidateStaticWorldV3ModelOwnership")
        ]
        for proof in ("installedFlag", "catalogColOwners", "g_staticWorldV3ColOwners", "packIndex"):
            self.assertIn(proof, validation)
        self.assertNotIn("FixedNameEquals(colPool", validation)
        self.assertNotIn("installedName", PACK[PACK.index("struct SCol") : PACK.index("struct SIpl")])

    def test_renderware_current_txd_globals_cannot_outlive_owned_dictionaries(self) -> None:
        helper = PACK[
            PACK.index("bool NormalizeStaticWorldV3RenderWareTxdGlobals(std::string& error)\n    {") : PACK.index(
                "bool ValidateStaticWorldV3PoolOwnership"
            )
        ]
        for token in (
            "loadedTxdDictionaries",
            "RwTexDictionaryGetCurrent",
            "RwTexDictionarySetCurrent",
            "stored-current-TXD-retains-generation-ownership",
            "no-live-neutral-current-TXD",
        ):
            self.assertIn(token, helper)

        deactivate = PACK[PACK.index("void DeactivateStaticWorldV3Pack") : PACK.index("std::unordered_set<int> GetStaticWorldV3OwnedIplSlots")]
        self.assertLess(deactivate.index("NormalizeStaticWorldV3RenderWareTxdGlobals"), deactivate.index("RemoveModel"))

        drain = PACK[PACK.index("bool CNativeWorldPackManagerSA::BeginRuntimeDrain") :]
        self.assertLess(drain.index("NormalizeStaticWorldV3RenderWareTxdGlobals"), drain.index("DeactivateStaticWorldV3Pack"))

        teardown = PACK[PACK.index("bool CNativeWorldPackManagerSA::TeardownRuntimeContent") :]
        boundary = teardown.index("g_state = EState::TearingDown")
        self.assertLess(teardown.index("NormalizeStaticWorldV3RenderWareTxdGlobals"), teardown.index("RemoveModel"))
        self.assertGreater(teardown.index("RenderWare global retained a destroyed generation TXD"), boundary)

    def test_file_ids_are_restored_without_predecessor_guessing(self) -> None:
        restore = PACK[PACK.index("bool RestoreStaticWorldV3StreamingBindings") : PACK.index("void ActivateStaticWorldV3Pack")]
        self.assertIn("nextInImg = 0xFFFF", restore)
        self.assertIn("= binding.original", restore)
        self.assertNotIn("SetStreamingInfo", restore)
        validation = PACK[PACK.index("bool ValidateStaticWorldV3StreamingOwnership") : PACK.index("bool RestoreStaticWorldV3StreamingBindings")]
        self.assertIn("pGame->GetCountOfAllFileIDs()", validation)
        self.assertIn("ownedArchives.count(info->archiveId)", validation)

    def test_archives_have_exact_capture_seal_and_reverse_close(self) -> None:
        for token in (
            "CaptureArchiveAllocationOwnership",
            "SealArchiveAllocationOwnership",
            "ValidateArchiveAllocationOwnershipBatch",
            "RemoveArchiveAllocationsCheckedReverse",
            "GetFileInformationByHandle",
            "CloseHandle",
        ):
            self.assertIn(token, STREAMING)
        registration = PACK[PACK.index("bool PrepareStaticWorldV3Transaction") : PACK.index("void RegisterStaticWorldV3Set")]
        self.assertLess(registration.index("CaptureArchiveAllocationOwnership"), registration.index("AddArchive"))
        self.assertLess(registration.index("AddArchive"), registration.index("SealArchiveAllocationOwnership"))

    def test_model_store_shutdown_is_reverse_inline_and_fail_stop(self) -> None:
        teardown = MODEL_STORE[MODEL_STORE.index("bool CNativeModelStoreSA::ShutdownAndRewindOwnedTail") :]
        self.assertIn("snapshot.entries.rbegin()", teardown)
        self.assertIn("VFTBL->Shutdown", teardown)
        self.assertIn("store->count = GetCount(snapshot.before", teardown)
        self.assertIn("FailOwnedTailTeardown", teardown)
        self.assertNotIn("delete entry", teardown)

    def test_detached_retains_process_foundations_but_clears_content(self) -> None:
        teardown = PACK[PACK.index("bool CNativeWorldPackManagerSA::TeardownRuntimeContent") :]
        for token in (
            "g_staticWorldV3PermanentBindings.clear()",
            "g_staticWorldV3Packs.clear()",
            "g_staticWorldV3CommittedGeneration.reset()",
            "g_nativeModelSlotsReserved.store(false",
            "g_state = EState::Detached",
            "bufferFoundation=retained",
        ):
            self.assertIn(token, teardown)
        required = PACK[PACK.index("unsigned int CNativeWorldPackManagerSA::GetRequiredStreamingBufferSizeBlocks") :]
        self.assertIn("g_staticWorldV3LargestEntryBlocks", required)

    def test_detached_promotes_deferred_stock_counts_before_final_proof(self) -> None:
        teardown = PACK[PACK.index("bool CNativeWorldPackManagerSA::TeardownRuntimeContent") :]
        final_sample = teardown.index("const SNativeWorldLifecycleTelemetry finalState")
        self.assertIn("const SNativeWorldLifecycleTelemetry neutralFoundationBefore", teardown)
        self.assertIn("detached.modelPointers != neutralFoundationBefore.modelPointers", teardown)
        self.assertIn("detached.streamingEntries != neutralFoundationBefore.streamingEntries", teardown)
        for token in (
            "g_nativeWorldNeutralBaseline.modelPointers = detached.modelPointers",
            "g_nativeWorldNeutralBaseline.streamingEntries = detached.streamingEntries",
        ):
            self.assertIn(token, teardown)
            self.assertLess(teardown.index(token), final_sample)
        self.assertIn('LogNativeWorldLifecycleTelemetry("teardown-detached-postcondition", finalState)', teardown)


if __name__ == "__main__":
    unittest.main()
