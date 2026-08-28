"""Structural guards for first-set admission in an already running GTA process."""

from __future__ import annotations

import pathlib
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
GAME = (REPOSITORY / "Client/game_sa/CGameSA.cpp").read_text(encoding="utf-8")
MODEL_STORE = (REPOSITORY / "Client/game_sa/CNativeModelStoreSA.cpp").read_text(encoding="utf-8")
PACK = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
HARNESS = (REPOSITORY / "utils/native-world-hot-switch.py").read_text(encoding="utf-8")


def cpp_function(source: str, signature: str) -> str:
    if signature not in source:
        raise AssertionError(f"missing C++ function contract: {signature}")
    start = source.index(signature)
    opening = source.find("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated C++ function: {signature}")


class NativeWorldColdAdmissionTests(unittest.TestCase):
    def test_process_foundation_precedes_any_server_or_file_id_commit(self) -> None:
        capture = GAME.index("m_fileIDs.CaptureStockLayout")
        foundation = GAME.index("CNativeModelStoreSA::InstallProcessFoundation", capture)
        selection = GAME.index("BeginNativeWorldStartupSelection", foundation)
        relocation = GAME.index("m_fileIDs.InstallStockRelocation", selection)
        self.assertLess(capture, foundation)
        self.assertLess(foundation, selection)
        self.assertLess(selection, relocation)

        install = cpp_function(MODEL_STORE, "bool CNativeModelStoreSA::InstallProcessFoundation")
        self.assertIn("InstallValidated", install)
        for selected_content in ("server", "cache", "selection", "manifest", "pack"):
            self.assertNotIn(selected_content, install.lower())

    def test_stock_startup_arms_an_inert_hook_then_publishes_neutral(self) -> None:
        install = cpp_function(PACK, "void CNativeWorldPackManagerSA::InstallFromEnvironment")
        no_policy = install[: install.index("g_policy = selected;")]
        self.assertIn("CNativeModelStoreSA::IsInstalled()", no_policy)
        self.assertIn("EnsureStaticWorldV3LoaderHookSeal(true, false", no_policy)
        self.assertIn("g_state = EState::Hooked", no_policy)

        hook = cpp_function(PACK, "void __cdecl LoadCdDirectoryHook")
        stock = hook[hook.index("else\n            {") :]
        reserve = stock.index("ReserveStaticWorldV3LodArrays()")
        neutral = stock.index("g_state = EState::Neutral", reserve)
        baseline = stock.index('CaptureNativeWorldNeutralBaseline("cold-foundation-stock-cd-directory")', neutral)
        fence = stock.index("g_runtimeAdmissionFencePending = true", baseline)
        self.assertLess(reserve, neutral)
        self.assertLess(neutral, baseline)
        self.assertLess(baseline, fence)

    def test_connected_buffer_preparation_precedes_hooks_and_registrar(self) -> None:
        runtime = cpp_function(PACK, "bool CNativeWorldPackManagerSA::HandleRuntimeSelection")
        first_session = runtime.index("ValidateNativeWorldStartupSession")
        resize = runtime.index("SetStreamingBufferSize", first_session)
        second_session = runtime.index("ValidateNativeWorldStartupSession", resize)
        hooks = runtime.index("EnsureStaticWorldV3LoaderHookSeal", second_session)
        registrar = runtime.index("RegisterStaticWorldV3Set(false)", hooks)
        self.assertLess(first_session, resize)
        self.assertLess(resize, second_session)
        self.assertLess(second_session, hooks)
        self.assertLess(hooks, registrar)

        registrar_body = cpp_function(PACK, "void RegisterStaticWorldV3Set")
        self.assertIn("halfBufferBlocks < perChannelBlocks", registrar_body)
        self.assertNotIn("std::max(*reinterpret_cast<unsigned int*>(0x8E4CA8), requiredTotalBlocks)", registrar_body)

    def test_harness_rejects_bootstrap_restart_and_binds_launch_pid(self) -> None:
        run = HARNESS[HARNESS.index("def run(") : HARNESS.index("def arguments(")]
        self.assertIn("launch_gta_pid", run)
        self.assertIn("cold admission replaced the launch GTA process", run)
        self.assertIn("cold admission requested the retired mandatory authorization restart", run)
        bootstrap = run[: run.index('if args.restart_fallback:')]
        self.assertNotIn('driver.send("auth-restart")', bootstrap)
        self.assertIn("previous_log = driver.refresh_log()", bootstrap)
        self.assertIn("text.startswith(previous_log)", bootstrap)
        self.assertIn("len(previous_log) if append_only else 0", bootstrap)
        self.assertIn('"bootstrap", "log-bound", "ok"', bootstrap)
        self.assertIn("pendingAuthorizationRecords", HARNESS)
        self.assertIn(r"state=transaction-preflight-proved|\[NativeWorld\] registrar=active", run)


if __name__ == "__main__":
    unittest.main()
