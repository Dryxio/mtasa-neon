#!/usr/bin/env python3
"""Guards for the process-local format-3 verified-object fast path."""

from __future__ import annotations

import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
GAME_SA = REPOSITORY / "Client" / "game_sa"


class NativeWorldV3VerifiedRegistryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cache_header = (GAME_SA / "CNativeWorldCacheSA.h").read_text(encoding="utf-8")
        cls.cache = (GAME_SA / "CNativeWorldCacheSA.cpp").read_text(encoding="utf-8")
        cls.pack = (GAME_SA / "CNativeWorldPackSA.cpp").read_text(encoding="utf-8")

    def test_cache_hashes_the_handles_it_has_already_locked_with_bcrypt(self) -> None:
        hashing = self.cache[
            self.cache.index("bool HasExactHash(HANDLE file") : self.cache.index("bool HasExactHash(const SString& path")
        ]
        self.assertIn("BCryptOpenAlgorithmProvider", hashing)
        self.assertIn("BCryptHashData", hashing)
        self.assertIn("ReadFile(file", hashing)
        self.assertIn("request.cancellation", self.cache)

        validation = self.cache[
            self.cache.index("bool LockAndValidatePublishedFiles(") : self.cache.index("bool WriteAndFlushFile(")
        ]
        self.assertIn("handles.Get()[handleIndex++]", validation)
        self.assertNotIn("HasExactHash(paths.images", validation)

    def test_verified_object_retains_closed_identity_without_owning_gta_state(self) -> None:
        declaration = self.cache_header[
            self.cache_header.index("class CNativeWorldCacheVerifiedObjectSA") : self.cache_header.index("std::string GenerateNativeWorldContentId")
        ]
        self.assertIn("RevalidateClosedObject", declaration)
        self.assertIn("GetHandleCount", declaration)
        self.assertNotIn("CStreaming", declaration)
        self.assertNotIn("model", declaration.lower())

        revalidation = self.cache[
            self.cache.index("bool CNativeWorldCacheVerifiedObjectSA::RevalidateClosedObject") : self.cache.index(
                "struct CNativeWorldCacheCommittedLeaseSA::SImpl"
            )
        ]
        self.assertIn("HandleMatchesPath", revalidation)
        self.assertIn("dwVolumeSerialNumber", revalidation)
        self.assertIn("nFileIndexHigh", revalidation)
        self.assertIn("ValidateClosedPublishedDirectory", revalidation)

    def test_child_verification_is_single_flight_and_bounded_to_two_io_workers(self) -> None:
        self.assertIn("EVerifiedV3ChildState::Verifying", self.pack)
        self.assertIn("g_staticWorldV3RegistryChanged.wait_for", self.pack)
        self.assertIn("g_staticWorldV3ActiveAudits >= STATIC_WORLD_V3_MAX_ACTIVE_AUDITS", self.pack)
        self.assertIn("g_staticWorldV3VerifiedChildren.size() > 16", self.pack)

        publisher = self.pack[
            self.pack.index("SNativeWorldTransportPublishResult CNativeWorldPackManagerSA::PublishTransportOffer(") : self.pack.index(
                "bool CNativeWorldPackManagerSA::IsModelIdReserved"
            )
        ]
        child_return = publisher.index("return PublishStaticWorldV3TransportOffer")
        lifecycle_lock = publisher.index("std::lock_guard<std::mutex> lock(g_transportPublisherMutex)")
        self.assertLess(lifecycle_lock, child_return)

    def test_set_and_generation_borrow_catalogs_without_rehashing_payloads(self) -> None:
        acquire = self.pack[
            self.pack.index("bool AcquireStaticWorldV3SetChildren(") : self.pack.index("SNativeWorldTransportPublishResult PublishStaticWorldV3SetOffer")
        ]
        self.assertIn("WaitForVerifiedStaticWorldV3Children", acquire)
        self.assertIn("CreateNativeWorldCacheLeaseFromVerifiedObject", acquire)
        self.assertNotIn("AuditStaticWorldV3Directory", acquire)
        self.assertNotIn("GenerateSha256HexStringFromFile", acquire)

        verified_audit = self.pack[
            self.pack.index("const auto auditVerified") : self.pack.index("const std::uint64_t", self.pack.index("const auto auditVerified"))
        ]
        self.assertIn("AuditStaticWorldV3Directory", verified_audit)
        self.assertIn("false", verified_audit)

    def test_verification_telemetry_is_separate_from_generation_leases(self) -> None:
        for field in ("verificationHandles", "verifiedObjects", "verifiedObjectsHighWater"):
            self.assertIn(field, self.cache_header)
            self.assertIn(field, self.pack)


if __name__ == "__main__":
    unittest.main()
