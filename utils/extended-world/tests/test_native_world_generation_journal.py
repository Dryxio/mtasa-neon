"""Structural guards for generation-owned native-world mutations."""

from __future__ import annotations

import pathlib
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
SOURCE = REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp"


class NativeWorldGenerationJournalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_committed_journal_names_every_mutation_family(self) -> None:
        journal = self.source[
            self.source.index("struct SStaticWorldV3CommittedGeneration") :
            self.source.index("const SBinaryIplInstance& GetStaticWorldV3Instance")
        ]
        for field in ("modelStoreTail;", "models;", "txds;", "cols;", "ipls;", "archives;", "bindings;", "cacheLeases;"):
            self.assertIn(field, journal)
        for baseline in (
            "txdFirstFreeBefore",
            "colFirstFreeBefore",
            "iplFirstFreeBefore",
            "txdFindCacheBefore",
        ):
            self.assertIn(baseline, journal)

    def test_model_tail_ownership_preserves_file_id_pointer_and_exact_store_tail(self) -> None:
        registration = self.source[
            self.source.index("void RegisterStaticWorldV3Set") :
            self.source.index("void RebuildStaticWorldV3ArchiveChains")
        ]
        self.assertIn("journal.models.push_back({physicalId, model, model->usTextureDictionary})", registration)
        self.assertIn("CNativeModelStoreSA::CaptureOwnedTail", registration)
        self.assertIn("unboundStoreModels.emplace(entry.model)", registration)
        self.assertIn("ownedPhysicalIds.emplace(model.physicalId)", registration)
        self.assertIn("ms_modelInfoPtrs[model.physicalId] != model.model", registration)
        self.assertIn("unboundStoreModels.erase(model.model) != 1", registration)
        self.assertIn("journal.models.size() != modelStoreTail.entries.size()", registration)

    def test_cache_leases_are_owned_by_the_published_generation(self) -> None:
        registration = self.source[
            self.source.index("void RegisterStaticWorldV3Set") :
            self.source.index("void RebuildStaticWorldV3ArchiveChains")
        ]
        self.assertIn("committedGeneration->cacheLeases.reserve", registration)
        self.assertIn("committedGeneration->cacheLeases.back(), error", registration)
        self.assertIn("cacheLeaseGroups=%u cacheHandles=%u", registration)

    def test_publication_happens_only_after_cache_commit(self) -> None:
        registration = self.source[
            self.source.index("void RegisterStaticWorldV3Set") :
            self.source.index("void RebuildStaticWorldV3ArchiveChains")
        ]
        cache_commit = registration.index("g_authorizedLease.Commit")
        journal_publish = registration.index("g_staticWorldV3CommittedGeneration = std::move(committedGeneration)")
        active_state = registration.index("g_state = EState::Active")
        self.assertLess(cache_commit, journal_publish)
        self.assertLess(journal_publish, active_state)
        self.assertIn("already owns a committed generation journal", self.source)

    def test_full_pool_snapshots_remain_transaction_local(self) -> None:
        publication = self.source[
            self.source.index("auto committedGeneration = std::make_unique<SStaticWorldV3CommittedGeneration>") :
            self.source.index("g_state = EState::Active")
        ]
        for rollback_only in ("txdObjects", "txdFlags", "colObjects", "colFlags", "iplObjects", "iplFlags"):
            self.assertNotIn(rollback_only, publication)


if __name__ == "__main__":
    unittest.main()
