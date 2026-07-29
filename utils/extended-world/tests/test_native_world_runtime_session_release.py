"""Structural guards for releasing a detached native-world session to Neutral."""

from __future__ import annotations

import pathlib
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
CORE = (REPOSITORY / "Client/core/CCore.cpp").read_text(encoding="utf-8")
CORE_HEADER = (REPOSITORY / "Client/core/CCore.h").read_text(encoding="utf-8")
MOD_MANAGER = (REPOSITORY / "Client/core/CModManager.cpp").read_text(encoding="utf-8")
GAME_SDK = (REPOSITORY / "Client/sdk/game/CGame.h").read_text(encoding="utf-8")
GAME = (REPOSITORY / "Client/game_sa/CGameSA.cpp").read_text(encoding="utf-8")
PACK = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")


def function_text(source: str, signature: str) -> str:
    """Return one C++ function using its balanced outer braces."""

    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated C++ function: {signature}")


class NativeWorldRuntimeSessionReleaseTests(unittest.TestCase):
    def test_release_runs_after_client_and_dll_teardown(self) -> None:
        stop = function_text(MOD_MANAGER, "void CModManager::TryStop()")
        ordered = (
            "m_client->ClientShutdown()",
            "m_client = {}",
            "FreeLibrary(m_library)",
            "m_library = {}",
            "OnModUnload()",
            "GetGame()->Reset()",
            "GetNetwork()->Reset()",
            "TryReleaseDetachedNativeWorldSessionAfterModUnload",
        )
        positions = [stop.index(token) for token in ordered]
        self.assertEqual(positions, sorted(positions))

    def test_game_abi_exposes_detached_query_and_closed_release(self) -> None:
        self.assertIn("IsNativeWorldContentDetached() const", GAME_SDK)
        self.assertIn("ReleaseDetachedNativeWorldSession", GAME_SDK)
        self.assertIn("const SNativeWorldStartupSelection&", GAME_SDK)
        self.assertIn("std::string& error", GAME_SDK)

        detached = function_text(GAME, "bool CGameSA::IsNativeWorldContentDetached() const")
        release = function_text(GAME, "bool CGameSA::ReleaseDetachedNativeWorldSession(")
        self.assertIn("CNativeWorldPackManagerSA::IsRuntimeContentDetached", detached)
        self.assertIn("CNativeWorldPackManagerSA::ReleaseDetachedRuntimeSession", release)

    def test_release_is_a_detached_to_neutral_transition(self) -> None:
        self.assertIn("Neutral", PACK[PACK.index("enum class EState") : PACK.index("struct SStaticWorldV3StreamingBinding")])
        release = function_text(PACK, "bool CNativeWorldPackManagerSA::ReleaseDetachedRuntimeSession(")
        detached_gate = release.index("EState::Detached")
        route_boundary = release.index("g_authorizedRoute = false")
        neutral_publication = release.index("g_state = EState::Neutral")
        self.assertLess(detached_gate, route_boundary)
        self.assertLess(route_boundary, neutral_publication)
        self.assertNotIn("g_state = EState::Off", release)

    def test_every_release_preflight_precedes_route_clear(self) -> None:
        release = function_text(PACK, "bool CNativeWorldPackManagerSA::ReleaseDetachedRuntimeSession(")
        boundary = release.index("g_authorizedRoute = false")
        for proof in (
            "g_state != EState::Detached",
            "g_staticWorldV3CommittedGeneration",
            "g_staticWorldV3Transitioning",
            "expectedSelection",
            "g_authorizedSelection",
            "ReadNativeWorldLifecycleTelemetry",
            "contentNeutral",
            "ioQuiescent",
            "pendingHandles",
            "processHandles",
            "committedGroups",
            "g_nativeModelSlotsReserved",
        ):
            self.assertLess(release.index(proof), boundary, proof)

        # A recoverable false result is valid only before route ownership is
        # discarded. All later invariant failures must be fail-stop.
        self.assertNotIn("return false", release[boundary:])
        self.assertIn("Fatal", release[boundary:])

    def test_neutral_final_proof_follows_baseline_and_route_publication(self) -> None:
        release = function_text(PACK, "bool CNativeWorldPackManagerSA::ReleaseDetachedRuntimeSession(")
        boundary = release.index("g_authorizedRoute = false")
        final_sample = release.index("finalState")
        self.assertLess(boundary, final_sample)
        self.assertLess(release.index("g_nativeWorldNeutralBaseline", boundary), final_sample)
        for proof in ("contentNeutral", "sessionNeutral", "admissionBaselineMatches", "ioQuiescent"):
            self.assertIn(proof, release[final_sample:])
        self.assertIn("EState::Neutral", release[boundary:final_sample])

    def test_transport_treats_neutral_as_idle_but_not_committed(self) -> None:
        publisher = function_text(
            PACK, "SNativeWorldTransportPublishResult CNativeWorldPackManagerSA::PublishTransportOffer("
        )
        first_format_branch = publisher.index("offer.format == STATIC_WORLD_V3_FORMAT")
        idle_gate = publisher[:first_format_branch]
        self.assertIn("EState::Off", idle_gate)
        self.assertIn("EState::Neutral", idle_gate)

        committed = function_text(PACK, "bool HasCommittedActivation(EState state)")
        self.assertNotIn("EState::Neutral", committed)

    def test_core_releases_endpoint_only_after_game_success(self) -> None:
        release = function_text(CORE, "bool CCore::TryReleaseDetachedNativeWorldSessionAfterModUnload()")
        active_gate = release.index("ENativeWorldStartupPhase::Active")
        disconnected_gate = release.index("IsConnected")
        detached_gate = release.index("IsNativeWorldContentDetached")
        game_release = release.index("m_pGame->ReleaseDetachedNativeWorldSession")
        epoch = release.index("m_nativeWorldAuthorizationEpoch", game_release)
        selection = release.index("m_nativeWorldStartupSelection = {}", game_release)
        phase = release.index("m_nativeWorldStartupPhase = ENativeWorldStartupPhase::Off", game_release)

        self.assertLess(active_gate, game_release)
        self.assertLess(disconnected_gate, game_release)
        self.assertLess(detached_gate, game_release)
        self.assertLess(game_release, epoch)
        self.assertLess(epoch, selection)
        self.assertLess(selection, phase)
        self.assertNotIn("NativeWorldAuthorizationStore::Clear", release)
        self.assertNotIn("NativeWorldAuthorizationStore::Revoke", release)

        self.assertIn("TryReleaseDetachedNativeWorldSessionAfterModUnload", CORE_HEADER)


if __name__ == "__main__":
    unittest.main()
