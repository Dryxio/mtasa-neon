#!/usr/bin/env python3

from dataclasses import replace
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[1]
sys.path.insert(0, str(ROOT))

from native_world_authorization import (  # noqa: E402
    AuthorizationRecord,
    RecordError,
    StartupLedger,
    TypedCacheLease,
    parse_closed_startup_uri,
)


def record(ticket: bytes = b"t" * 16) -> AuthorizationRecord:
    return AuthorizationRecord(
        content_id=b"c" * 32,
        offer_id=b"o" * 32,
        server_id_digest=b"s" * 32,
        server_ipv4=bytes((127, 0, 0, 1)),
        server_port=22003,
        resource_name="native-world-transport-test",
        resource_net_id=7,
        resource_start_counter=4,
        bitstream_version=0x36,
        connection_generation=2,
        authorization_epoch=3,
        ticket_id=ticket,
        issued_at=10_000,
        expires_at=10_900,
    )


class ClosedStartupUriTests(unittest.TestCase):
    def test_accepts_only_exact_canonical_numeric_target(self) -> None:
        self.assertEqual((bytes((127, 0, 0, 1)), 22003), parse_closed_startup_uri("mtasa://127.0.0.1:22003"))
        for uri in (
            None,
            "MTASA://127.0.0.1:22003",
            "mtasa://localhost:22003",
            "mtasa://127.0.0.1",
            "mtasa://127.0.0.1:0",
            "mtasa://127.0.0.1:65536",
            "mtasa://127.00.0.1:22003",
            "mtasa://127.0.0.1:022003",
            "mtasa://user@127.0.0.1:22003",
            "mtasa://127.0.0.1:22003/",
            "mtasa://127.0.0.1:22003?password=x",
            "mtasa://[::1]:22003",
        ):
            with self.subTest(uri=uri):
                self.assertIsNone(parse_closed_startup_uri(uri))


class StartupTransactionTests(unittest.TestCase):
    def test_wrong_targets_leave_pending_untouched(self) -> None:
        original = record()
        for uri in (None, "mtasa://127.0.0.2:22003", "mtasa://127.0.0.1:22004"):
            ledger = StartupLedger(original, {})
            self.assertEqual("unmatched", ledger.begin(uri, 10_000))
            self.assertEqual(original, ledger.pending)
            self.assertFalse(ledger.spent)

    def test_claim_and_replay_are_one_shot(self) -> None:
        original = record()
        ledger = StartupLedger(original, {})
        self.assertEqual("selected", ledger.begin("mtasa://127.0.0.1:22003", 10_900))
        self.assertEqual("claimed", ledger.finish(10_900, claim=True))
        self.assertIsNone(ledger.pending)
        self.assertEqual(original, ledger.spent[original.ticket_id])
        ledger.pending = original
        with self.assertRaises(RecordError):
            ledger.begin("mtasa://127.0.0.1:22003", 10_900)

    def test_crash_boundary_and_terminal_refusal(self) -> None:
        original = record()
        ledger = StartupLedger(original, {})
        self.assertEqual("selected", ledger.begin("mtasa://127.0.0.1:22003", 10_000))
        ledger.crash()
        self.assertEqual(original, ledger.pending)
        self.assertEqual("selected", ledger.begin("mtasa://127.0.0.1:22003", 10_000))
        self.assertEqual("terminal-refused", ledger.finish(10_901, claim=True))
        self.assertIsNone(ledger.pending)

    def test_legacy_selector_ambiguity_burns_without_claim(self) -> None:
        original = record()
        ledger = StartupLedger(original, {})
        self.assertEqual("ambiguous", ledger.begin(None, 10_000, legacy_selector=True))
        self.assertEqual("terminal-refused", ledger.finish(10_000, claim=False))

    def test_cancellation_immediately_before_claim_terminally_refuses(self) -> None:
        ledger = StartupLedger(record(), {})
        self.assertEqual("selected", ledger.begin("mtasa://127.0.0.1:22003", 10_000))
        ledger.cancel()
        self.assertEqual("terminal-refused", ledger.finish(10_000, claim=True))

    def test_distinct_fresh_ticket_can_follow_older_spent_ticket(self) -> None:
        old = record(b"a" * 16)
        fresh = replace(record(b"b" * 16), issued_at=10_100, expires_at=11_000)
        ledger = StartupLedger(fresh, {old.ticket_id: old})
        self.assertEqual("selected", ledger.begin("mtasa://127.0.0.1:22003", 10_100))

    def test_typed_lease_rejects_mismatch_and_double_commit(self) -> None:
        lease = TypedCacheLease("bullworth", "c" * 64, "t" * 32)
        with self.assertRaises(RecordError):
            lease.commit("bullworth", "d" * 64, "t" * 32)
        self.assertTrue(lease.active)
        lease.commit("bullworth", "c" * 64, "t" * 32)
        with self.assertRaises(RecordError):
            lease.commit("bullworth", "c" * 64, "t" * 32)

    def test_cpp_c_prepares_stores_but_defers_hook_and_pack_commit(self) -> None:
        pack = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        start = pack.index("void CNativeWorldPackManagerSA::HandleStartupSelection")
        end = pack.index("void CNativeWorldPackManagerSA::AttachAuthorizedStreaming", start)
        body = pack[start:end]
        self.assertIn("InstallForAuthorizedStartup", body)
        self.assertIn("g_authorizedLease = std::move(lease)", body)
        for forbidden in ("HookInstallCall", "LOAD_OBJECT_TYPES", "CommitRegistrationLease"):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, body)

    def test_cpp_c_verifies_session_and_lease_before_installing_hook(self) -> None:
        pack = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        start = pack.index("bool CNativeWorldPackManagerSA::VerifyAuthorizedStartupBeforeStartGame")
        end = pack.index("void CNativeWorldPackManagerSA::CancelAuthorizedActivation", start)
        body = pack[start:end]
        self.assertLess(body.index("ValidateNativeWorldStartupSession"), body.index("RevalidateClosedObject"))
        self.assertLess(body.index("RevalidateClosedObject"), body.index("HookInstallCall"))

    def test_cpp_c_promotes_the_typed_lease_after_native_postconditions(self) -> None:
        pack = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        register_start = pack.index("void RegisterPack()")
        register_end = pack.index("void __cdecl LoadCdDirectoryHook", register_start)
        register = pack[register_start:register_end]
        self.assertLess(register.index("LOAD_OBJECT_TYPES"), register.index("CommitRegistrationLease"))
        self.assertLess(register.index("ValidatePostconditions"), register.index("CommitRegistrationLease"))
        self.assertIn("g_authorizedLease.Commit(g_policy->key, g_authorizedSelection.contentId, g_authorizedSelection.ticketId", pack)

    def test_cpp_c_pins_connect_and_checks_every_server_connected_packet(self) -> None:
        connect = (REPOSITORY / "Client/core/CConnectManager.cpp").read_text(encoding="utf-8")
        packets = (REPOSITORY / "Client/mods/deathmatch/logic/CPacketHandler.cpp").read_text(encoding="utf-8")
        self.assertIn("ValidateNativeWorldStartupEndpoint", connect)
        self.assertIn("FailNativeWorldStartupBeforeActive", connect)
        packet_start = packets.index("void CPacketHandler::Packet_ServerConnected")
        packet_end = packets.index("void CPacketHandler::Packet_ServerJoined", packet_start)
        packet = packets[packet_start:packet_end]
        self.assertLess(packet.index("VerifyNativeWorldStartupBeforeStartGame"), packet.index("g_pGame->StartGame()"))


if __name__ == "__main__":
    unittest.main()
