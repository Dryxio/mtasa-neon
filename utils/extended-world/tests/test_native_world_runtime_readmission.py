"""Structural guards for reusable native-world runtime foundations.

These tests intentionally describe the checkpoint-6 contract before its C++
implementation.  A retained executable hook or scratch allocation is safe to
reuse only when its exact process-owned identity can be proved; merely seeing a
patched opcode or a non-null pointer is not sufficient.
"""

from __future__ import annotations

import pathlib
import re
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
PACK = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
PACK_HEADER = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.h").read_text(encoding="utf-8")
CORE = (REPOSITORY / "Client/core/CCore.cpp").read_text(encoding="utf-8")
CONNECT = (REPOSITORY / "Client/core/CConnectManager.cpp").read_text(encoding="utf-8")
MOD_MANAGER = (REPOSITORY / "Client/core/CModManager.cpp").read_text(encoding="utf-8")


def cpp_function(source: str, signature: str) -> str:
    """Return one C++ function body while producing a useful test failure."""

    if signature not in source:
        raise AssertionError(f"missing C++ function contract: {signature}")
    start = source.index(signature)
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing opening brace after: {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated C++ function: {signature}")


class NativeWorldRuntimeReadmissionTests(unittest.TestCase):
    def test_spatial_pool_telemetry_includes_dynamic_ptr_nodes(self) -> None:
        telemetry = cpp_function(PACK, "SNativeWorldLifecycleTelemetry ReadNativeWorldLifecycleTelemetry")
        self.assertIn('"quadTreeNode", "ptrNodeSingleLink"', PACK)
        self.assertIn("CPtrNodeSingleLinkPoolSA::GetPoolInstance", telemetry)
        self.assertIn("ptrNodePool->GetCapacity()", telemetry)
        self.assertIn("ptrNodePool->GetUsedSize()", telemetry)
        self.assertNotIn("ReadNativePoolTelemetry(0x00B74484)", telemetry)

    def test_loader_hook_seal_has_exact_three_way_classification(self) -> None:
        """Original, our exact rel32 target, and foreign bytes are distinct."""

        for token in (
            "enum class ENativeWorldHookSealState",
            "Original",
            "InstalledExact",
            "Foreign",
            "ClassifyNativeWorldHookSeal",
            "EnsureStaticWorldV3LoaderHookSeal",
        ):
            self.assertTrue(token in PACK, f"missing loader-hook seal token: {token}")

        classifier = cpp_function(PACK, "ENativeWorldHookSealState ClassifyNativeWorldHookSeal")
        for proof in ("expectedOriginal", "expectedOpcode", "expectedTarget", "displacement"):
            self.assertIn(proof, classifier)
        self.assertRegex(classifier, r"address\s*\+\s*5")
        self.assertIn("InstalledExact", classifier)
        self.assertIn("Foreign", classifier)

        seal = cpp_function(PACK, "bool EnsureStaticWorldV3LoaderHookSeal")
        for address, target in (
            ("LOAD_CD_DIRECTORY_CALL", "LoadCdDirectoryHook"),
            ("LOAD_COL_BUFFER_CALL", "LoadStaticWorldV3ColBuffer"),
            ("LOAD_IPL_BUFFER_CALL", "LoadStaticWorldV3IplBuffer"),
            ("SET_IPL_DEF_CALL", "CacheStaticWorldV3IplDef"),
            ("REMOVE_ALL_COLLISION_TAIL", "CompleteStaticWorldV3BoundingBootstrap"),
        ):
            self.assertIn(address, seal)
            self.assertIn(target, seal)
        self.assertIn("ENativeWorldHookSealState::Foreign", seal)
        self.assertIn("ENativeWorldHookSealState::InstalledExact", seal)

    def test_exactly_installed_hooks_are_reused_without_reinstallation(self) -> None:
        """All writes to the retained sites must live behind one seal."""

        seal = cpp_function(PACK, "bool EnsureStaticWorldV3LoaderHookSeal")
        self.assertIn("ENativeWorldHookSealState::Original", seal)
        self.assertLess(seal.index("ENativeWorldHookSealState::Original"), seal.index("HookInstall"))

        install_patterns = (
            r"HookInstallCall\(LOAD_CD_DIRECTORY_CALL",
            r"HookInstallCall\(LOAD_COL_BUFFER_CALL",
            r"HookInstallCall\(LOAD_IPL_BUFFER_CALL",
            r"HookInstallCall\(SET_IPL_DEF_CALL",
            r"HookInstall\(REMOVE_ALL_COLLISION_TAIL",
        )
        for pattern in install_patterns:
            self.assertEqual(len(re.findall(pattern, PACK)), len(re.findall(pattern, seal)), pattern)

        registrar = cpp_function(PACK, "void RegisterStaticWorldV3Set")
        startup = cpp_function(PACK, "bool CNativeWorldPackManagerSA::VerifyAuthorizedStartupBeforeStartGame")
        self.assertIn("EnsureStaticWorldV3LoaderHookSeal", registrar)
        self.assertIn("EnsureStaticWorldV3LoaderHookSeal", startup)
        self.assertNotIn("HookInstall", registrar)
        self.assertNotIn("HookInstall", startup)

    def test_complete_hook_seal_is_classified_before_the_first_write(self) -> None:
        """A foreign later site must not leave an earlier site half-installed."""

        seal = cpp_function(PACK, "bool EnsureStaticWorldV3LoaderHookSeal")
        loop = "for (SHook& hook : hooks)"
        first_classification = seal.index(loop)
        installation_pass = seal.index(loop, first_classification + len(loop))
        postcondition_pass = seal.index(loop, installation_pass + len(loop))
        first_write = min(index for index in (seal.find("HookInstallCall"), seal.find("HookInstall(")) if index >= 0)

        self.assertLess(first_classification, installation_pass)
        self.assertLess(installation_pass, first_write)
        self.assertLess(first_write, postcondition_pass)
        classification_pass = seal[first_classification:installation_pass]
        self.assertIn("ClassifyNativeWorldHookSeal", classification_pass)
        self.assertIn("ENativeWorldHookSealState::Foreign", classification_pass)
        self.assertNotIn("HookInstall", classification_pass)

    def test_retained_lod_scratch_is_identity_checked_and_reused(self) -> None:
        """The first two 4096-entry arrays survive teardown and are not replaced."""

        self.assertTrue("g_staticWorldV3LodArrayPointers" in PACK, "retained LOD scratch pointers are not journaled")
        reserve = cpp_function(PACK, "void ReserveStaticWorldV3LodArrays")
        retained = cpp_function(PACK, "bool ValidateRetainedStaticWorldV3LodFoundation")
        self.assertIn("g_staticWorldV3LodArraysReserved", reserve)
        self.assertIn("ValidateRetainedStaticWorldV3LodFoundation", reserve)
        self.assertIn("ValidateStaticWorldV3LodArrayGlobals(32", retained)
        self.assertIn("g_staticWorldV3LodArrayPointers[bank]", retained)
        self.assertIn("IPL_ENTITY_SCRATCH_CAPACITY", retained)
        self.assertIn("EStaticWorldV3LodState::Reserved", retained)
        self.assertIn("g_staticWorldV3ActivePack != -1", retained)
        self.assertIn("g_staticWorldV3BankOwners", retained)
        self.assertRegex(reserve, r"g_staticWorldV3LodArraysReserved[\s\S]*?return;")
        self.assertLess(reserve.index("g_staticWorldV3LodArraysReserved"), reserve.index("GET_NEW_IPL_ENTITY_INDEX_ARRAY"))

        runtime = cpp_function(PACK, "CNativeWorldPackManagerSA::HandleRuntimeSelection")
        self.assertLess(runtime.index("ValidateRetainedStaticWorldV3LodFoundation"), runtime.index("HandleStartupSelection"))
        preflight = cpp_function(PACK, "bool PreflightStaticWorldV3TransactionReadOnly")
        self.assertIn("ValidateRetainedStaticWorldV3LodFoundation", preflight)

        teardown = cpp_function(PACK, "bool CNativeWorldPackManagerSA::TeardownRuntimeContent")
        self.assertNotIn("g_staticWorldV3LodArrayPointers = {}", teardown)
        self.assertNotIn("g_staticWorldV3LodArraysReserved = false", teardown)

    def test_generation_epoch_is_monotonic_across_readmission(self) -> None:
        registrar = cpp_function(PACK, "void RegisterStaticWorldV3Set")
        advance = cpp_function(PACK, "unsigned int AdvanceStaticWorldV3Generation")
        self.assertFalse("g_staticWorldV3Generation.store(1" in PACK, "generation epoch is reset to one")
        self.assertFalse("committedGeneration->generation = 1" in PACK, "committed journal generation is reset to one")
        self.assertFalse("generation=1" in PACK, "generation telemetry still hard-codes the first epoch")
        self.assertIn("compare_exchange_weak", advance)
        self.assertIn("std::numeric_limits<unsigned int>::max()", advance)
        self.assertIn("AdvanceStaticWorldV3Generation()", registrar)
        self.assertIn("committedGeneration->generation = generation", registrar)

    def test_neutral_admission_has_a_direct_runtime_registrar_entry(self) -> None:
        """LoadCdDirectory is never called again after GTA's first startup."""

        self.assertTrue("HandleRuntimeSelection" in PACK_HEADER, "no direct runtime selection API is exposed")
        runtime = cpp_function(PACK, "CNativeWorldPackManagerSA::HandleRuntimeSelection")
        self.assertIn("EState::Neutral", runtime)
        self.assertIn("RegisterStaticWorldV3Set(false);", runtime)
        self.assertIn("BootstrapStaticWorldV3SpatialBoundsAtRuntime();", runtime)
        self.assertIn("EnsureStaticWorldV3LoaderHookSeal", runtime)
        self.assertNotIn("LoadCdDirectoryHook()", runtime)
        self.assertNotIn("HookInstall", runtime)

    def test_retained_hooks_delegate_to_stock_without_live_content(self) -> None:
        """Detached and Neutral are deliberately outside the content route."""

        route = cpp_function(PACK, "bool StaticWorldV3LoaderHooksOwnContent")
        for allowed in ("EState::Active", "EState::Draining"):
            self.assertIn(allowed, route)
        self.assertNotIn("EState::Registering", route)
        self.assertNotIn("EState::Detached", route)
        self.assertNotIn("EState::Neutral", route)
        self.assertIn("g_staticWorldV3CommittedGeneration", route)

        col = cpp_function(PACK, "bool __cdecl LoadStaticWorldV3ColBuffer")
        ipl = cpp_function(PACK, "bool __cdecl LoadStaticWorldV3IplBuffer")
        for loader, owner_lookup in (
            (col, "g_staticWorldV3ColOwners.find"),
            (ipl, "g_staticWorldV3IplOwners.find"),
        ):
            self.assertIn("StaticWorldV3LoaderHooksOwnContent", loader)
            self.assertLess(loader.index("StaticWorldV3LoaderHooksOwnContent"), loader.index(owner_lookup))
            self.assertLess(loader.index("return original"), loader.index(owner_lookup))

        bootstrap = cpp_function(PACK, "void CompleteStaticWorldV3BoundingBootstrapInternal")
        self.assertIn("StaticWorldV3LoaderHooksOwnContent", bootstrap)
        self.assertLess(bootstrap.index("REMOVE_ALL_COLLISION"), bootstrap.index("StaticWorldV3LoaderHooksOwnContent"))
        self.assertLess(bootstrap.index("StaticWorldV3LoaderHooksOwnContent"), bootstrap.index("g_staticWorldV3BootstrapBoundsComplete"))

    def test_runtime_spatial_replay_is_generation_owned(self) -> None:
        replay = cpp_function(PACK, "void BootstrapStaticWorldV3SpatialBoundsAtRuntime")
        self.assertIn("g_staticWorldV3CommittedGeneration->cols", replay)
        self.assertIn("pack.iplSlots", replay)
        self.assertIn("NativeWorldRuntimeColBounds", replay)
        self.assertIn("NativeWorldRuntimeIplBounds", replay)
        self.assertIn("CountItemOccurrences", replay)
        for expansion in (
            "def->rect.left -= 120.0f",
            "def->rect.right += 120.0f",
            "def->rect.top -= 120.0f",
            "def->rect.bottom += 120.0f",
        ):
            self.assertIn(expansion, replay)
        self.assertIn("CompleteStaticWorldV3BoundingBootstrapInternal(false);", replay)
        self.assertNotIn("BoundingBoxesPostProcess", replay)
        self.assertIn("foreignColRects", replay)
        self.assertIn("foreignColSpatial.push_back", replay)
        self.assertIn("foreign.rect, actual->rect, foreign.treeOccurrences, installedOccurrences", replay)
        self.assertIn("actual->rect.left < foreign.rect.left", replay)
        self.assertIn("actual->rect.right > foreign.rect.right", replay)
        self.assertIn("actual->rect.top < foreign.rect.top", replay)
        self.assertIn("actual->rect.bottom > foreign.rect.bottom", replay)
        self.assertIn("retained duplicate quadtree ownership before bounding replay", replay)
        self.assertIn("CountItemOccurrences(actual) != foreign.treeOccurrences", replay)
        self.assertIn("g_staticWorldV3ColOwners.find", replay)
        self.assertIn("unreferenced COL", replay)
        self.assertIn("foreignColSpatial", replay)
        self.assertIn("deleteTreeItem", replay)

    def test_runtime_spatial_replay_uses_retail_request_lifecycle(self) -> None:
        """Priority is consumed while reading; only KEEP survives IPL removal."""

        replay = cpp_function(PACK, "void BootstrapStaticWorldV3SpatialBoundsAtRuntime")
        self.assertIn("RequestModel(fileId, 0);", replay)
        self.assertIn('LoadAllRequestedModels(false, "NativeWorldRuntimeColBounds")', replay)
        self.assertIn("info->flg != 0", replay)
        self.assertNotIn("reinterpret_cast<void(__cdecl*)()>(0x410E60)();", replay)

        self.assertIn("RequestModel(fileId, 0x18);", replay)
        self.assertIn('LoadAllRequestedModels(true, "NativeWorldRuntimeIplBounds")', replay)
        self.assertIn("info->flg != 0x08", replay)
        self.assertNotIn("info->flg != 0x18", replay)
        self.assertNotIn("RequestModel(fileId, 0x16)", replay)

    def test_runtime_ipl_bounds_skip_the_released_fixed_cinfo_snapshot(self) -> None:
        """The live IplDef is updated, but CINFO's startup-only copy is gone."""

        cache = cpp_function(PACK, "void __cdecl CacheStaticWorldV3IplDef")
        self.assertIn("COL_ACCEL_IPL_CACHE", cache)
        self.assertIn("COL_ACCEL_IPL_CACHE_CAPACITY", cache)
        self.assertIn("!cacheAvailable", cache)
        self.assertIn("g_staticWorldV3IplOwners.find", cache)
        self.assertIn("return;", cache)
        self.assertIn("SET_IPL_DEF", cache)

    def test_runtime_teardown_restores_admission_pool_cursors(self) -> None:
        teardown = cpp_function(PACK, "bool CNativeWorldPackManagerSA::TeardownRuntimeContent")
        for pool in ("txd", "col", "ipl"):
            self.assertIn(
                f"{pool}Pool->m_nFirstFree = g_staticWorldV3CommittedGeneration->{pool}FirstFreeBefore;",
                teardown,
            )
            self.assertNotIn(f"{pool}Pool->m_nFirstFree = live{pool.capitalize()}FirstFree;", teardown)

    def test_refused_hot_switch_arms_only_an_exact_numeric_restart(self) -> None:
        arm = cpp_function(CORE, "bool CCore::ArmVerifiedNativeWorldRestart")
        fallback = cpp_function(CORE, "bool CCore::PrepareNativeWorldHotSwitchRestart")
        connect = cpp_function(CONNECT, "bool CConnectManager::Connect")

        self.assertIn('GetRegistryValue("", "OnQuitCommand")', arm)
        self.assertIn('SetRegistryValue("", "OnQuitCommand", expected, true)', arm)
        self.assertIn("observed == expected", arm)
        self.assertIn("another post-exit action is already scheduled", arm)
        self.assertIn('SString expected("restart\\t\\t%s\\t\\t"', arm)
        self.assertIn("ENativeWorldStartupPhase::Active", fallback)
        self.assertIn("targetAddress.s_addr", fallback)
        self.assertIn("targetPort == 0", fallback)
        self.assertIn("NativeWorldRestartProcessIsExclusive", fallback)
        self.assertIn("m_nativeWorldRestartFallbackArmed = true", fallback)
        self.assertNotIn("password", fallback.lower())
        self.assertGreaterEqual(connect.count("PrepareNativeWorldHotSwitchRestart(targetAddress, usPort"), 2)
        self.assertGreaterEqual(connect.count("CCore::GetSingleton().Quit();"), 2)

    def test_local_connect_validation_precedes_native_world_drain(self) -> None:
        connect = cpp_function(CONNECT, "bool CConnectManager::Connect")
        drain = connect.index("PrepareNativeWorldContentForUnload")
        self.assertLess(connect.index("CheckNickProvided"), drain)
        self.assertLess(connect.index("CheckDiskSpace"), drain)
        self.assertEqual(connect.count("CheckNickProvided"), 1)
        self.assertEqual(connect.count("CheckDiskSpace"), 1)

    def test_mod_replacement_cannot_bypass_native_world_teardown(self) -> None:
        request = cpp_function(MOD_MANAGER, "void CModManager::RequestLoad")
        start = cpp_function(MOD_MANAGER, "bool CModManager::Start")
        load = cpp_function(MOD_MANAGER, "bool CModManager::Load")
        self.assertIn("HasActiveNativeWorldSession", request)
        self.assertIn('PrepareNativeWorldContentForUnload("client-deathmatch-replacement"', request)
        self.assertLess(request.index("PrepareNativeWorldContentForUnload"), request.index("m_state = State::PendingStart"))
        self.assertIn("m_state != State::PendingStart", load)
        self.assertIn("HasActiveNativeWorldSession", start)
        self.assertIn('PrepareNativeWorldContentForUnload("client-deathmatch-start-replacement"', start)
        self.assertLess(start.index("PrepareNativeWorldContentForUnload"), start.index("Stop();"))


if __name__ == "__main__":
    unittest.main()
