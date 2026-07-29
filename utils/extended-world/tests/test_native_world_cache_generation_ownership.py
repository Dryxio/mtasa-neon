#!/usr/bin/env python3
"""Source-level guards for generation-owned native-world cache leases."""

from __future__ import annotations

import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
GAME_SA = REPOSITORY / "Client" / "game_sa"


class NativeWorldCacheGenerationOwnershipTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (GAME_SA / "CNativeWorldCacheSA.h").read_text(encoding="utf-8")
        cls.source = (GAME_SA / "CNativeWorldCacheSA.cpp").read_text(encoding="utf-8")

    def test_committed_group_is_move_only_and_explicitly_releasable(self) -> None:
        declaration = self.header[
            self.header.index("class CNativeWorldCacheCommittedLeaseSA") : self.header.index("class CNativeWorldCacheLeaseSA")
        ]
        self.assertIn("CNativeWorldCacheCommittedLeaseSA(CNativeWorldCacheCommittedLeaseSA&&) noexcept", declaration)
        self.assertIn("operator=(CNativeWorldCacheCommittedLeaseSA&&) noexcept", declaration)
        self.assertIn("CNativeWorldCacheCommittedLeaseSA(const CNativeWorldCacheCommittedLeaseSA&) = delete", declaration)
        self.assertIn("operator=(const CNativeWorldCacheCommittedLeaseSA&) = delete", declaration)
        self.assertIn("void   Release()", declaration)

        release = self.source[
            self.source.index("void CNativeWorldCacheCommittedLeaseSA::Release()") : self.source.index("struct CNativeWorldCacheLeaseSA::SImpl")
        ]
        self.assertIn("m_impl.reset()", release)

    def test_commit_transfers_one_exact_handle_group_without_flattening(self) -> None:
        commit = self.source[
            self.source.index("bool CNativeWorldCacheLeaseSA::Commit(") : self.source.index("void CNativeWorldCacheLeaseSA::Release()")
        ]
        self.assertIn("CNativeWorldCacheCommittedLeaseSA& committedLease", commit)
        self.assertIn("committed->handles.swap(m_impl->handles)", commit)
        self.assertIn("committed->Register()", commit)
        self.assertNotIn("m_impl->handles.swap(committed->handles)", commit)
        self.assertIn("g_legacyCommittedGroups.emplace_back(std::move(committedLease))", commit)
        self.assertNotIn("g_processLocks", self.source)
        self.assertIn("std::vector<CNativeWorldCacheCommittedLeaseSA> g_legacyCommittedGroups", self.source)

    def test_group_destruction_closes_once_and_updates_exact_telemetry(self) -> None:
        implementation = self.source[
            self.source.index("struct CNativeWorldCacheCommittedLeaseSA::SImpl") : self.source.index("struct CNativeWorldCacheLeaseSA::SImpl")
        ]
        self.assertIn("handle && handle != INVALID_HANDLE_VALUE", implementation)
        self.assertEqual(1, implementation.count("CloseHandle(handle)"))
        self.assertIn("++telemetry.groups", implementation)
        self.assertIn("telemetry.handles += handles.size()", implementation)
        self.assertIn("--telemetry.groups", implementation)
        self.assertIn("telemetry.handles -= handleCount", implementation)

        telemetry = self.source[self.source.index("SNativeWorldCacheLeaseTelemetrySA GetNativeWorldCacheLeaseTelemetry()") :]
        self.assertIn("telemetry.handles", telemetry)
        self.assertIn("telemetry.groups", telemetry)
        self.assertIn("telemetry.groupsHighWater", telemetry)
        self.assertIn("size_t       committedGroups", self.header)

    def test_counter_state_survives_cross_translation_unit_owner_teardown(self) -> None:
        state = self.source[
            self.source.index("SCommittedLeaseTelemetryState& CommittedLeaseTelemetryState()") : self.source.index("class CScopedHandles")
        ]
        self.assertIn("static SCommittedLeaseTelemetryState* state = new SCommittedLeaseTelemetryState", state)

    def test_legacy_pending_rollback_remains_separate(self) -> None:
        release = self.source[
            self.source.index("void ReleaseNativeWorldCacheLease()") : self.source.index("SNativeWorldCacheLeaseTelemetrySA")
        ]
        self.assertIn("for (HANDLE handle : g_pendingLocks)", release)
        self.assertIn("CloseHandle(handle)", release)
        self.assertIn("g_pendingLocks.clear()", release)
        self.assertIn("g_cachePrepared = false", release)
        self.assertNotIn("g_legacyCommittedGroups", release)


if __name__ == "__main__":
    unittest.main()
