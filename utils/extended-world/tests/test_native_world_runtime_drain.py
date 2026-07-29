"""Structural guards for the one-way native-world drain checkpoint."""

from __future__ import annotations

import pathlib
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
PACK = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
GAME = (REPOSITORY / "Client/sdk/game/CGame.h").read_text(encoding="utf-8")
COMMANDS = (REPOSITORY / "Client/core/CCommandFuncs.cpp").read_text(encoding="utf-8")


class NativeWorldRuntimeDrainTests(unittest.TestCase):
    def test_draining_state_blocks_position_driven_reentry(self) -> None:
        self.assertIn("Draining", PACK)
        prepare = PACK[PACK.index("void CNativeWorldPackManagerSA::PrepareStreamingAtPosition") : PACK.index("bool CNativeWorldPackManagerSA::BeginRuntimeDrain")]
        self.assertIn("g_state != EState::Active", prepare)
        drain = PACK[PACK.index("bool CNativeWorldPackManagerSA::BeginRuntimeDrain") : PACK.index("void CNativeWorldPackManagerSA::LogLifecycleTelemetry")]
        self.assertLess(drain.index("g_state = EState::Draining"), drain.index("DeactivateStaticWorldV3Pack()"))
        self.assertIn("g_staticWorldV3Transitioning", prepare)
        self.assertIn("g_staticWorldV3Transitioning", drain)

    def test_drain_proves_streaming_and_generation_ownership(self) -> None:
        drain = PACK[PACK.index("bool CNativeWorldPackManagerSA::BeginRuntimeDrain") : PACK.index("void CNativeWorldPackManagerSA::LogLifecycleTelemetry")]
        for proof in (
            "LoadAllRequestedModels(false",
            "FLUSH_STREAMING_CHANNELS",
            "ValidateOwnedTail",
            "InvalidateStaticWorldV3CollisionProducers",
            "IsStaticWorldV3DrainQuiescent",
        ):
            self.assertIn(proof, drain)
        predicate = PACK[PACK.index("bool IsStaticWorldV3DrainQuiescent") : PACK.index("void ActivateStaticWorldV3Pack")]
        for proof in (
            "sample.loadedList.nativeArenaNodes != 0",
            "sample.requestedList.nativeArenaNodes != 0",
            "sample.arenaModelPointers != 0",
            "sample.arenaStreamingEntries != 0",
            "!sample.ioQuiescent",
            "cacheOwnershipComplete",
            "StaticWorldV3EntityPoolsAreDetached",
            "def->rect.left <= def->rect.right",
            "pack.logicalToPhysical.empty()",
            "pack.lodAnchors.empty()",
        ):
            self.assertIn(proof, predicate)
        self.assertIn("teardown=not-started", drain)
        self.assertIn("generationOwnership=preserved", drain)

    def test_drain_surface_is_local_and_append_only(self) -> None:
        self.assertIn("virtual bool BeginNativeWorldDrain() = 0", GAME)
        self.assertIn("virtual bool IsNativeWorldDrainQuiescent() const = 0", GAME)
        command = COMMANDS[COMMANDS.index("void CCommandFuncs::NativeWorldDrain") : COMMANDS.index("// this fails randomly")]
        self.assertIn("game->BeginNativeWorldDrain()", command)
        self.assertIn("game->IsNativeWorldDrainQuiescent()", command)
        self.assertIn('[status|begin|teardown]', command)

    def test_draining_is_a_committed_non_releasable_state(self) -> None:
        helper = PACK[PACK.index("bool HasCommittedActivation") : PACK.index("const char* LodStateName")]
        self.assertIn("EState::Active", helper)
        self.assertIn("EState::Draining", helper)
        verify = PACK[PACK.index("bool CNativeWorldPackManagerSA::VerifyAuthorizedStartupBeforeStartGame") : PACK.index("void CNativeWorldPackManagerSA::InstallFromEnvironment")]
        self.assertIn("HasCommittedActivation(g_state)", verify)
        publish = PACK[PACK.index("SNativeWorldTransportPublishResult CNativeWorldPackManagerSA::PublishTransportOffer") :]
        self.assertIn("result.existingActivationActive = HasCommittedActivation(g_state)", publish)

    def test_collision_and_entity_producers_are_fenced(self) -> None:
        collision = PACK[PACK.index("void InvalidateStaticWorldV3CollisionProducers") : PACK.index("bool IsStaticWorldV3DrainQuiescent")]
        for proof in ("def->rect = CRect()", "def->required = false", "def->refCount != 0", "def->procedural"):
            self.assertIn(proof, collision)
        pools = PACK[PACK.index("bool StaticWorldV3EntityPoolsAreDetached") : PACK.index("void InvalidateStaticWorldV3CollisionProducers")]
        for address in ("0xB74498", "0xB7449C", "0xB744A0"):
            self.assertIn(address, pools)
        self.assertIn("GetEntityIplIndex", pools)

    def test_streaming_buffer_floor_survives_draining(self) -> None:
        required = PACK[PACK.index("unsigned int CNativeWorldPackManagerSA::GetRequiredStreamingBufferSizeBlocks") :]
        self.assertIn("g_state != EState::Active && g_state != EState::Draining", required)


if __name__ == "__main__":
    unittest.main()
