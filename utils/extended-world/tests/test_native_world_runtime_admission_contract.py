"""Structural guards for the Neutral-to-Active runtime admission contract.

The runtime path crosses Core, the durable authorization store, Client
Deathmatch resource ownership, and the Game SA registrar.  These guards keep
the result dispositions and irreversible boundaries visible together; testing
only the registrar's happy path would miss stale pending tickets or a claim
performed after cancellation.
"""

from __future__ import annotations

import pathlib
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
CORE = (REPOSITORY / "Client/core/CCore.cpp").read_text(encoding="utf-8")
STORE = (REPOSITORY / "Client/core/CNativeWorldAuthorizationStore.cpp").read_text(encoding="utf-8")
AUTHORIZATION = (REPOSITORY / "Client/sdk/core/CNativeWorldAuthorization.h").read_text(encoding="utf-8")
RESOURCE = (REPOSITORY / "Client/mods/deathmatch/logic/CResource.cpp").read_text(encoding="utf-8")
RESOURCE_MANAGER = (REPOSITORY / "Client/mods/deathmatch/logic/CResourceManager.cpp").read_text(encoding="utf-8")
PACK = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")


def cpp_function(source: str, signature: str) -> str:
    """Return one balanced C++ function, failing with a useful contract name."""

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


class NativeWorldRuntimeAdmissionContractTests(unittest.TestCase):
    def test_core_and_resource_preserve_all_result_dispositions(self) -> None:
        """Fallback, deferred, active, terminal refusal, and retry ownership stay distinct."""

        self.assertIn("bool runtimeAdmissionAttempted{};", AUTHORIZATION)
        self.assertIn("bool               runtimeAdmissionDeferred{};", AUTHORIZATION)
        activate = cpp_function(CORE, "CCore::TryActivatePublishedNativeWorldRuntime")
        verify = cpp_function(RESOURCE, "CResource::VerifyNativeWorldTransportReady")

        # Not eligible: return the untouched Persist disposition.  CResource
        # must replace it only after Core says a runtime attempt actually ran.
        self.assertIn("SNativeWorldAuthorizationRecordResult result = persisted;", activate)
        self.assertIn("result.runtimeAdmissionAttempted = false;", activate)
        self.assertIn("GetNativeWorldRuntimeAdmissionReadiness()", activate)
        self.assertIn("ENativeWorldRuntimeAdmissionReadiness::Ineligible", activate)
        self.assertIn("ENativeWorldRuntimeAdmissionReadiness::WaitingForIo", activate)
        self.assertIn("result.runtimeAdmissionDeferred = true;", activate)
        self.assertIn("if (runtimeResult.runtimeAdmissionAttempted)", verify)
        self.assertIn("authorizationResult = runtimeResult;", verify)

        # Active: Core publishes claimed+attempted and the resource deliberately
        # owns no durable pending record.
        active = activate.index("result = DescribeNativeWorldStartupProcess();")
        self.assertLess(active, activate.index("result.claimed = true;", active))
        self.assertLess(active, activate.index("result.runtimeAdmissionAttempted = true;", active))
        self.assertIn("authorizationRecordPublished = !authorizationResult.claimed", verify)
        self.assertIn("state=runtime-active", verify)

        # Refused with a proved terminalization owns no pending record, whereas
        # any unproved terminalization is conservatively retained for stop-time
        # retry.  The latter includes failures before a rename ever occurred.
        self.assertIn(
            "result.publicationAmbiguous = !m_nativeWorldRuntimeTerminalResult.success || "
            "m_nativeWorldRuntimeTerminalResult.publicationAmbiguous;",
            activate,
        )
        self.assertIn("if (authorizationResult.publicationAmbiguous)", verify)
        self.assertIn("authorizationPublicationAmbiguous = true", verify)
        self.assertIn("authorizationContentId = result.contentId", verify)
        self.assertIn("state=refused", verify)

    def test_runtime_claim_joins_the_finish_startup_cancellation_boundary(self) -> None:
        """Runtime claim cannot bypass the cancellation sample at commit."""

        claim = cpp_function(STORE, "NativeWorldAuthorizationStore::ClaimPublishedRuntime")
        finish = cpp_function(STORE, "NativeWorldAuthorizationStore::FinishStartup")
        self.assertIn("std::make_unique<SStartupTransaction>()", claim)
        self.assertIn("beginScope.Publish(std::move(startup));", claim)
        self.assertIn('return FinishStartup(ticketId, true, "");', claim)
        self.assertNotIn("TerminalizeStartup(", claim)

        cancellation = finish.index("g_startupCancelled.load(std::memory_order_acquire)")
        terminalize = finish.index("TerminalizeStartup(")
        self.assertLess(cancellation, terminalize)
        self.assertIn("cancelledAtCommit", finish[terminalize:])

    def test_runtime_read_only_plan_precedes_the_claim(self) -> None:
        """All predictable capacity failures remain on the pre-claim side."""

        selection = cpp_function(PACK, "CNativeWorldPackManagerSA::HandleStartupSelection")
        v3 = selection[selection.index("if (selection.packFormat == NATIVE_WORLD_STATIC_V3_SET_FORMAT)") :]
        planner = v3.index("runtimeAdmission && !PreflightStaticWorldV3TransactionReadOnly(error)")
        claim = v3.index('finish(true, "")')
        self.assertLess(planner, claim)

        preflight = cpp_function(PACK, "bool PreflightStaticWorldV3TransactionReadOnly")
        for proof in (
            "ValidateStaticWorldV3RuntimeBaseline",
            "PlanArchiveAllocations",
            "PlanStaticWorldV3PoolAllocations",
            "StreamingInfoIsFree",
        ):
            self.assertIn(proof, preflight)
        for mutation in (
            "RegisterStaticWorldV3Set(",
            "FinishNativeWorldStartupSelection",
            "LOAD_OBJECT_TYPES",
            "HookInstall",
            "AddArchive(",
        ):
            self.assertNotIn(mutation, preflight)

    def test_session_is_revalidated_immediately_before_modelinfo_barrier(self) -> None:
        registrar = cpp_function(PACK, "void RegisterStaticWorldV3Set")
        validation = registrar.index("g_pCore->ValidateNativeWorldStartupSession(error)")
        barrier = registrar.index("bool crossedBarrier = false;")
        first_model_write = registrar.index("crossedBarrier = true;", barrier)
        self.assertLess(validation, barrier)
        self.assertLess(barrier, first_model_write)
        failure = registrar[validation:barrier]
        self.assertIn("RollbackStaticWorldV3PoolsAndArchives(journal)", failure)
        self.assertIn("TerminateNativeWorldStartup", failure)
        self.assertIn("return;", failure)

    def test_every_postclaim_registrar_refusal_is_fail_stop(self) -> None:
        runtime = cpp_function(PACK, "CNativeWorldPackManagerSA::HandleRuntimeSelection")
        registrar = runtime.index("RegisterStaticWorldV3Set(false);")
        refusal = runtime.index("g_state != EState::Active", registrar)
        terminate = runtime.index("TerminateNativeWorldStartup(error)", refusal)
        stop = runtime.index("return false;", terminate)
        bootstrap = runtime.index("BootstrapStaticWorldV3SpatialBoundsAtRuntime()", stop)
        self.assertLess(registrar, refusal)
        self.assertLess(refusal, terminate)
        self.assertLess(terminate, stop)
        self.assertLess(stop, bootstrap)

    def test_runtime_eligibility_is_exactly_core_off_and_game_ready(self) -> None:
        activate = cpp_function(CORE, "CCore::TryActivatePublishedNativeWorldRuntime")
        runtime = cpp_function(PACK, "CNativeWorldPackManagerSA::HandleRuntimeSelection")
        self.assertIn("m_nativeWorldStartupPhase != ENativeWorldStartupPhase::Off", activate)
        self.assertIn("ENativeWorldRuntimeAdmissionReadiness::Ready", activate)
        self.assertIn("selection.packFormat != NATIVE_WORLD_STATIC_V3_SET_FORMAT", runtime)
        self.assertIn("g_state != EState::Neutral", runtime)
        self.assertIn("ValidateRetainedStaticWorldV3LodFoundation(error)", runtime)

    def test_io_busy_admission_is_owned_and_retried_by_the_resource(self) -> None:
        """Only stock I/O can defer; no CResource pointer crosses into Core."""

        activate = cpp_function(CORE, "CCore::TryActivatePublishedNativeWorldRuntime")
        readiness = cpp_function(PACK, "CNativeWorldPackManagerSA::GetRuntimeAdmissionReadiness")
        for invariant in (
            "sample.contentNeutral",
            "sample.sessionNeutral",
            "sample.admissionBaselineMatches",
            "sample.modelStoresInstalled",
        ):
            self.assertIn(invariant, readiness)
        self.assertIn("sample.ioQuiescent", readiness)
        self.assertIn("ENativeWorldRuntimeAdmissionReadiness::WaitingForIo", readiness)
        self.assertIn("ENativeWorldRuntimeAdmissionReadiness::Ineligible", readiness)

        pulse = cpp_function(RESOURCE, "CResource::PulseNativeWorldRuntimeAdmission")
        self.assertIn("authorizationRuntimeDeferred", pulse)
        self.assertIn("authorizationSnapshot", pulse)
        self.assertIn("authorizationPublication", pulse)
        self.assertIn("authorizationPersistedResult", pulse)
        self.assertIn("g_pNet->IsConnected()", pulse)
        self.assertIn("TryActivatePublishedNativeWorldRuntime", pulse)
        self.assertIn("runtimeAdmissionDeferred", pulse)
        self.assertIn("authorizationPersistedResult.expiresAt", pulse)
        self.assertIn("RevokeNativeWorldStartupAuthorization", pulse)
        self.assertIn("state=runtime-expired", pulse)
        self.assertIn("runtime-foundation-ineligible", pulse)
        self.assertIn("state=terminalization-ambiguous", pulse)
        self.assertIn("restart-required=unknown", pulse)
        self.assertNotIn("CResource*", activate)

        manager_pulse = cpp_function(RESOURCE_MANAGER, "CResourceManager::PulseNativeWorldTransportPublications")
        self.assertIn("resource->PulseNativeWorldRuntimeAdmission();", manager_pulse)
        self.assertLess(
            manager_pulse.index("resource->PulseNativeWorldRuntimeAdmission();"),
            manager_pulse.index("resource->IsActive()"),
        )


if __name__ == "__main__":
    unittest.main()
