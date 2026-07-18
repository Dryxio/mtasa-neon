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
    POLICY_STATIC_WORLD_V1,
    RecordError,
    STATIC_WORLD_AUTHORIZATION_BITSTREAM_VERSION,
    STATIC_WORLD_PACK_FORMAT,
    STATIC_WORLD_WIRE_VERSION,
    StartupLedger,
    TypedCacheLease,
    parse_closed_startup_uri,
    restart_uri,
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

    def test_restart_uri_is_fresh_canonical_and_contains_no_record_identity(self) -> None:
        authorization = record()
        uri = restart_uri(authorization, authorization.expires_at - 60)
        self.assertEqual(uri, "mtasa://127.0.0.1:22003")
        self.assertEqual(parse_closed_startup_uri(uri), (authorization.server_ipv4, authorization.server_port))
        for secret in (authorization.ticket_id.hex(), authorization.content_id.hex(), authorization.resource_name):
            self.assertNotIn(secret, uri)
        for now in (authorization.issued_at - 121, authorization.expires_at - 59, authorization.expires_at + 1):
            with self.subTest(now=now), self.assertRaises(RecordError):
                restart_uri(authorization, now)


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
        lease = TypedCacheLease(1, "bullworth", "c" * 64, "t" * 32)
        with self.assertRaises(RecordError):
            lease.commit(1, "bullworth", "d" * 64, "t" * 32)
        self.assertTrue(lease.active)
        with self.assertRaises(RecordError):
            lease.commit(2, "bullworth", "c" * 64, "t" * 32)
        lease.commit(1, "bullworth", "c" * 64, "t" * 32)
        with self.assertRaises(RecordError):
            lease.commit(1, "bullworth", "c" * 64, "t" * 32)

    def test_format_2_record_uses_the_same_one_shot_startup_transaction(self) -> None:
        generic = replace(
            record(),
            wire_version=STATIC_WORLD_WIRE_VERSION,
            pack_format=STATIC_WORLD_PACK_FORMAT,
            policy=POLICY_STATIC_WORLD_V1,
            bitstream_version=STATIC_WORLD_AUTHORIZATION_BITSTREAM_VERSION,
        )
        ledger = StartupLedger(generic, {})
        self.assertEqual("selected", ledger.begin("mtasa://127.0.0.1:22003", generic.issued_at))
        self.assertEqual("claimed", ledger.finish(generic.issued_at, claim=True))

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
        self.assertIn("g_authorizedLease.Commit(g_policy->format, g_policy->key, g_authorizedSelection.contentId", pack)

    def test_cpp_c_pins_connect_and_checks_every_server_connected_packet(self) -> None:
        connect = (REPOSITORY / "Client/core/CConnectManager.cpp").read_text(encoding="utf-8")
        commands = (REPOSITORY / "Client/core/CCommandFuncs.cpp").read_text(encoding="utf-8")
        core = (REPOSITORY / "Client/core/CCore.cpp").read_text(encoding="utf-8")
        packets = (REPOSITORY / "Client/mods/deathmatch/logic/CPacketHandler.cpp").read_text(encoding="utf-8")
        self.assertIn("ValidateNativeWorldStartupEndpoint", connect)
        self.assertIn("FailNativeWorldStartupBeforeActive", connect)
        connect_start = connect.index("bool CConnectManager::Connect")
        reconnect_start = connect.index("bool CConnectManager::Reconnect", connect_start)
        connect_body = connect[connect_start:reconnect_start]
        self.assertLess(connect_body.index("ValidateConnectionTarget"), connect_body.index("CModManager::GetSingleton().Unload"))
        self.assertLess(connect_body.index("ValidateConnectionTarget"), connect_body.index("AdvanceNetworkConnectionGeneration"))
        self.assertLess(connect_body.index("ValidateConnectionTarget"), connect_body.index("pNet->Reset"))
        self.assertGreaterEqual(connect_body.count("ValidateNativeWorldStartupEndpoint"), 1)
        command_start = commands.index("void CCommandFuncs::Connect")
        command_end = commands.index("void CCommandFuncs::ReloadNews", command_start)
        command_body = commands[command_start:command_end]
        self.assertLess(command_body.index("ValidateConnectionTarget"), command_body.index("CModManager::GetSingleton().Unload"))
        endpoint_start = core.index("bool CCore::ValidateNativeWorldStartupEndpoint")
        endpoint_end = core.index("void CCore::HandleNativeWorldConnectionTargetRefusal", endpoint_start)
        self.assertNotIn("ENativeWorldStartupPhase::Candidate", core[endpoint_start:endpoint_end])
        packet_start = packets.index("void CPacketHandler::Packet_ServerConnected")
        packet_end = packets.index("void CPacketHandler::Packet_ServerJoined", packet_start)
        packet = packets[packet_start:packet_end]
        self.assertLess(packet.index("VerifyNativeWorldStartupBeforeStartGame"), packet.index("g_pGame->StartGame()"))

    def test_active_native_world_refuses_cross_server_request_without_unloading(self) -> None:
        core = (REPOSITORY / "Client/core/CCore.cpp").read_text(encoding="utf-8")
        connect = (REPOSITORY / "Client/core/CConnectManager.cpp").read_text(encoding="utf-8")
        handler_start = core.index("void CCore::HandleNativeWorldConnectionTargetRefusal")
        handler_end = core.index("bool CCore::ValidateNativeWorldStartupSession", handler_start)
        handler = core[handler_start:handler_end]
        self.assertIn("ENativeWorldStartupPhase::Active", handler)
        self.assertIn("existing-native-world=preserved", handler)
        self.assertIn("next-server-restart-required=yes", handler)
        self.assertIn("TerminateNativeWorldStartup(reason)", handler)
        self.assertIn("Close and restart Multi Theft Auto", handler)

        reconnect_start = connect.index("bool CConnectManager::Reconnect")
        reconnect_end = connect.index("bool CConnectManager::Event_OnCancelClick", reconnect_start)
        reconnect = connect[reconnect_start:reconnect_end]
        self.assertLess(reconnect.index("ValidateConnectionTarget"), reconnect.index("m_strHost ="))
        self.assertLess(reconnect.index("ValidateConnectionTarget"), reconnect.index("m_bReconnect = true"))
        self.assertLess(reconnect.index("ValidateConnectionTarget"), reconnect.index('CVARS_GET("password"'))
        self.assertLess(reconnect.index("IsNativeWorldStartupCredentialSuppressed"), reconnect.index("m_strPassword ="))

        commands = (REPOSITORY / "Client/core/CCommandFuncs.cpp").read_text(encoding="utf-8")
        command_start = commands.index("void CCommandFuncs::Reconnect")
        command_end = commands.index("void CCommandFuncs::Bind", command_start)
        command = commands[command_start:command_end]
        self.assertLess(command.index("ValidateConnectionTarget"), command.index('CVARS_GET("password"'))

    def test_active_native_world_guards_browser_and_local_server_before_credentials_or_start(self) -> None:
        browser = (REPOSITORY / "Client/core/ServerBrowser/CServerBrowser.cpp").read_text(encoding="utf-8")
        server_info = (REPOSITORY / "Client/core/ServerBrowser/CServerInfo.cpp").read_text(encoding="utf-8")
        menu = (REPOSITORY / "Client/core/CMainMenu.cpp").read_text(encoding="utf-8")
        client_game = (REPOSITORY / "Client/mods/deathmatch/logic/CClientGame.cpp").read_text(encoding="utf-8")

        address_start = browser.index("bool CServerBrowser::OnConnectClick")
        address_end = browser.index("void CServerBrowser::NotifyServerExists", address_start)
        address = browser[address_start:address_end]
        self.assertLess(address.index("ValidateConnectionTarget"), address.index("GetServerPassword"))

        selected_start = browser.index("bool CServerBrowser::ConnectToSelectedServer")
        selected_end = browser.index("bool CServerBrowser::OnRefreshClick", selected_start)
        selected = browser[selected_start:selected_end]
        self.assertLess(selected.index("ValidateConnectionTarget"), selected.index("GetServerPassword"))

        info_start = server_info.index("void CServerInfo::Connect")
        info_end = server_info.index("void CServerInfo::ResetServerGUI", info_start)
        info = server_info[info_start:info_end]
        self.assertLess(info.index("ValidateConnectionTarget"), info.index("m_strPassword"))
        self.assertLess(info.index("ValidateConnectionTarget"), info.index("GetServerPassword"))

        for function, next_function in (
            ("bool CMainMenu::OnHostGameButtonClick", "bool CMainMenu::OnEditorButtonClick"),
            ("bool CMainMenu::OnEditorButtonClick", "bool CMainMenu::OnSettingsButtonClick"),
        ):
            start = menu.index(function)
            end = menu.index(next_function, start)
            body = menu[start:end]
            self.assertLess(body.index("ValidateConnectionTarget"), body.index("RequestLoad"))

        local_start = client_game.index("bool CClientGame::StartLocalGame")
        local_end = client_game.index("bool CClientGame::OnCancelLocalGameClick", local_start)
        local = client_game[local_start:local_end]
        self.assertLess(local.index("ValidateNativeWorldStartupEndpoint"), local.index("m_Server.Start"))
        self.assertLess(local.index("ValidateNativeWorldStartupEndpoint"), local.index("m_Server.SetPassword"))

    def test_cpp_d_schedules_only_the_closed_passwordless_restart_then_quits(self) -> None:
        core = (REPOSITORY / "Client/core/CCore.cpp").read_text(encoding="utf-8")
        command = (REPOSITORY / "Client/core/CCommandFuncs.cpp").read_text(encoding="utf-8")
        start = core.index("SNativeWorldAuthorizationRecordResult CCore::PrepareNativeWorldStartupRestart")
        end = core.index("bool CCore::IsNativeWorldStartupCredentialSuppressed", start)
        body = core[start:end]
        self.assertIn("InspectFreshRestartTarget", body)
        self.assertIn('SetRegistryValue("", "OnQuitCommand", expected, true)', body)
        self.assertNotIn('SetOnQuitCommand("restart", "", uri)', body)
        self.assertIn("const SString observed", body)
        self.assertIn("writeAppearsUnchanged", body)
        self.assertIn("writeAppearsPartial", body)
        self.assertIn("restart-scheduling-ambiguous", body)
        self.assertIn('SetRegistryValue("", "OnQuitCommand", existing, true)', body)
        self.assertIn("credential=suppressed", body)
        for forbidden in ("contentId", "resourceName", "serverIdDigest", "savedPassword"):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, body)
        command_start = command.index("void CCommandFuncs::NativeWorldAuthorization")
        command_end = command.index("void CCommandFuncs::Clear", command_start)
        command_body = command[command_start:command_end]
        self.assertIn('operation == "restart" && result.success', command_body)
        self.assertLess(command_body.index('operation == "restart" && result.success'), command_body.index("g_pCore->Quit()"))

    def test_cpp_d_status_clear_and_credentials_are_process_scoped(self) -> None:
        core = (REPOSITORY / "Client/core/CCore.cpp").read_text(encoding="utf-8")
        connect = (REPOSITORY / "Client/core/CConnectManager.cpp").read_text(encoding="utf-8")
        self.assertIn("return DescribeNativeWorldStartupProcess();", core)
        self.assertIn("action=clear-refused", core)
        self.assertIn("action=restart-refused", core)
        self.assertIn('state = "active"', core)
        self.assertIn('activation = "yes"', core)
        self.assertIn('lease = "process"', core)
        credential_start = core.index("bool CCore::IsNativeWorldStartupCredentialSuppressed")
        credential_end = core.index("SNativeWorldAuthorizationRecordResult CCore::DescribeNativeWorldStartupProcess", credential_start)
        credential = core[credential_start:credential_end]
        self.assertIn("m_nativeWorldStartupPhase != ENativeWorldStartupPhase::Off", credential)
        self.assertIn("IsNativeWorldStartupCredentialSuppressed", connect)
        self.assertIn('m_strPassword = bSuppressNativeWorldCredential ? "" : szPassword', connect)
        self.assertLess(connect.index('m_strPassword = bSuppressNativeWorldCredential ? "" : szPassword'), connect.index("m_strLastPassword = m_strPassword"))
        self.assertIn("!bSuppressNativeWorldCredential && m_strPassword.empty()", connect)


if __name__ == "__main__":
    unittest.main()
