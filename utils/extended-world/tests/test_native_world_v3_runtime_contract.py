#!/usr/bin/env python3
"""Source-level guards for the publish-only static-world-v3 runtime boundary."""

from __future__ import annotations

import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]


class NativeWorldV3RuntimeContractTest(unittest.TestCase):
    def test_v3_returns_before_legacy_registrar_policy_state(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        publication = source[source.index("SNativeWorldTransportPublishResult CNativeWorldPackManagerSA::PublishTransportOffer") :]
        route = publication.index("offer.format == STATIC_WORLD_V3_FORMAT")
        legacy_policy = publication.index("FindNativeWorldPackPolicy(offer.format)")
        self.assertLess(route, legacy_policy)
        v3 = source[source.index("PublishStaticWorldV3TransportOffer") : source.index("}  // namespace", source.index("PublishStaticWorldV3TransportOffer"))]
        self.assertNotIn("g_policy =", v3)
        self.assertNotIn("g_pack =", v3)
        self.assertNotIn("AcquireExistingNativeWorldCacheLease", v3)
        self.assertIn("static-world-v3-transport-envelope-v1", source)

    def test_v3_spatial_ownership_and_hash_guards_are_derived(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        self.assertIn("colModelIds", source)
        self.assertIn("iplModelIds", source)
        self.assertIn("generated model is shared by multiple spatial IPLs", source)
        self.assertIn("COL model is not placed by its paired spatial IPL", source)
        self.assertIn("StaticWorldV3UppercaseKey", source)
        self.assertIn("generated model names collide in GTA uppercase key space", source)
        self.assertIn("generated TXD names collide in GTA uppercase key space", source)
        self.assertIn("ValidateStaticWorldV3Cols(const SStaticWorldV3Ide& ide, SStaticWorldV3Inventory& inventory", source)
        self.assertIn('memcmp(data.data() + offset, "COLL", 4) == 0', source)
        self.assertIn('memcmp(prefix, "COLL", 4) == 0', source)
        self.assertNotIn("COL archive contains a non-COL3 record", source)

    def test_v3_child_lod_transport_and_set_startup_are_independently_gated(self) -> None:
        bitstream = (REPOSITORY / "Shared/sdk/net/bitstream.h").read_text(encoding="utf-8")
        server = (REPOSITORY / "Server/mods/deathmatch/logic/CResource.cpp").read_text(encoding="utf-8")
        packet = (REPOSITORY / "Server/mods/deathmatch/logic/packets/CResourceStartPacket.cpp").read_text(encoding="utf-8")
        authorization = (REPOSITORY / "Client/sdk/core/CNativeWorldAuthorization.h").read_text(encoding="utf-8")
        meta = (REPOSITORY / "test-resources/native-world-v3-transport-test/meta.xml").read_text(encoding="utf-8")
        self.assertIn("NativeWorldStaticWorldV3LodTransport", bitstream)
        self.assertIn("NativeWorldStaticWorldV3StartupAuthorization", bitstream)
        self.assertIn("staticWorldV3PublishOnly", server)
        self.assertIn("!startupAttribute", server)
        self.assertIn("NativeWorldStaticWorldV3LodTransport", packet)
        self.assertIn("NATIVE_WORLD_STATIC_V3_SET_AUTHORIZATION_VERSION", authorization)
        self.assertNotIn("startup=", meta)

    def test_v3_payload_and_disk_accounting_use_separate_u64_budgets(self) -> None:
        cache = (REPOSITORY / "Client/game_sa/CNativeWorldCacheSA.cpp").read_text(encoding="utf-8")
        client = (REPOSITORY / "Client/mods/deathmatch/logic/CResource.cpp").read_text(encoding="utf-8")
        self.assertIn("std::uint64_t payloadBytes", cache)
        self.assertIn("const std::uint64_t requestedDiskBytes", cache)
        self.assertIn("MAX_V3_TOTAL_BYTES - image.bytes", cache)
        self.assertIn("V3_MAXIMUM_MANIFEST_BYTES + V3_MAXIMUM_TOTAL_BYTES", client)
        self.assertIn("v3PayloadBytes = 0", client)
        self.assertGreaterEqual(client.count("V3_MAXIMUM_TOTAL_BYTES - v3PayloadBytes"), 2)

    def test_v3_cache_has_transactional_object_bank_without_widening_byte_cap(self) -> None:
        cache = (REPOSITORY / "Client/game_sa/CNativeWorldCacheSA.cpp").read_text(encoding="utf-8")
        self.assertIn("V3_MAX_OBJECTS = 8", cache)
        self.assertIn("V3_MAX_CACHE_BYTES = 32ULL * 1024ULL * 1024ULL * 1024ULL", cache)
        self.assertIn("maximumObjects = isV3 ? V3_MAX_OBJECTS : LEGACY_MAX_OBJECTS", cache)
        self.assertNotIn("4ULL * (MAX_V3_TOTAL_BYTES", cache)

    def test_v3_cache_objects_can_be_leased_for_a_later_aggregate_transaction(self) -> None:
        cache = (REPOSITORY / "Client/game_sa/CNativeWorldCacheSA.cpp").read_text(encoding="utf-8")
        acquire = cache[cache.index("bool AcquireExistingNativeWorldCacheLease") :]
        self.assertIn("const bool isV3 = request.format == 3", acquire)
        self.assertNotIn("request.format == 3 ||", acquire)
        self.assertIn("(!isV3 && request.img.name != CACHED_IMG_FILE)", acquire)

    def test_v3_set_cache_keeps_closed_lease_and_verified_cleanup_boundaries(self) -> None:
        cache = (REPOSITORY / "Client/game_sa/CNativeWorldCacheSA.cpp").read_text(encoding="utf-8")
        set_publication = cache[cache.index("bool PublishNativeWorldV3Set") : cache.index("bool AcquireExistingNativeWorldV3SetLease")]
        set_lease = cache[cache.index("bool AcquireExistingNativeWorldV3SetLease") : cache.index("bool PrepareAndLockNativeWorldCache")]
        self.assertIn("RemoveVerifiedNativeWorldV3SetDirectory", set_publication)
        self.assertNotIn("DeleteFileW(SharedUtil::FromUTF8(quarantineManifest)", set_publication)
        self.assertIn("IsPrivateCacheSibling(name)", set_publication)
        for ancestor in ("dataRoot", "root", "format", "policy"):
            self.assertIn(f"LockDirectory({ancestor}, handles, error)", set_lease)

    def test_native_world_authorization_teardown_has_manager_owned_retry(self) -> None:
        resource = (REPOSITORY / "Client/mods/deathmatch/logic/CResource.cpp").read_text(encoding="utf-8")
        manager = (REPOSITORY / "Client/mods/deathmatch/logic/CResourceManager.cpp").read_text(encoding="utf-8")
        core = (REPOSITORY / "Client/core/CCore.cpp").read_text(encoding="utf-8")
        self.assertIn("RetireNativeWorldAuthorizationRevocation", resource)
        self.assertIn("PulseNativeWorldAuthorizationRevocations(false)", manager)
        self.assertIn("RevokeDetachedNativeWorldStartupAuthorization", manager)
        self.assertIn("NativeWorldAuthorizationStore::Revoke(authorization, contentId)", core)

    def test_server_cannot_start_native_world_transport_without_client_files(self) -> None:
        resource = (REPOSITORY / "Server/mods/deathmatch/logic/CResource.cpp").read_text(encoding="utf-8")
        packet = (REPOSITORY / "Server/mods/deathmatch/logic/packets/CResourceStartPacket.cpp").read_text(encoding="utf-8")
        self.assertIn("m_nativeWorldPackTransport.present && !StartOptions.bClientFiles", resource)
        self.assertIn("nativeWorldPack.present && !m_pResource->IsClientFilesOn()", packet)

    def test_native_pack_streaming_floor_covers_both_channel_halves(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        function = source[source.index("unsigned int CNativeWorldPackManagerSA::GetRequiredStreamingBufferSizeBlocks") :]
        function = function[: function.index("void CNativeWorldPackManagerSA::LogStreamingBufferClamp")]
        self.assertIn("static_cast<uint64_t>(Pack().largestImgEntryBlocks) + 1", function)
        self.assertIn("totalBlocks = perChannelBlocks * 2", function)
        self.assertIn("totalBlocks > std::numeric_limits<unsigned int>::max()", function)
        for largest, expected in ((1, 4), (2, 4), (3, 8), (65_535, 131_072)):
            per_channel = (largest + 1) & ~1
            self.assertEqual(expected, per_channel * 2)

    def test_native_physical_model_slots_are_hidden_from_mta_model_apis(self) -> None:
        game_api = (REPOSITORY / "Client/sdk/game/CGame.h").read_text(encoding="utf-8")
        game_sa = (REPOSITORY / "Client/game_sa/CGameSA.cpp").read_text(encoding="utf-8")
        pack = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        manager = (REPOSITORY / "Client/mods/deathmatch/logic/CClientModelManager.cpp").read_text(encoding="utf-8")
        self.assertIn("NATIVE_WORLD_MODEL_ARENA_FIRST = 20000", game_api)
        self.assertIn("NATIVE_WORLD_MODEL_ARENA_LAST = 29999", game_api)
        self.assertIn("IsNativeWorldModelIdReserved(uint32_t modelId) const", game_api)
        self.assertIn("CNativeWorldPackManagerSA::IsModelIdReserved(modelId)", game_sa)
        self.assertIn("modelId >= NATIVE_WORLD_MODEL_ARENA_FIRST", pack)
        self.assertIn("modelId <= NATIVE_WORLD_MODEL_ARENA_LAST", pack)
        self.assertIn("std::atomic_bool                    g_nativeModelSlotsReserved", pack)
        self.assertIn("g_nativeModelSlotsReserved.load(std::memory_order_acquire)", pack)
        self.assertNotIn("if (!g_pack || g_state", pack[pack.index("bool CNativeWorldPackManagerSA::IsModelIdReserved") :])
        self.assertGreaterEqual(manager.count("IsNativeWorldModelIdReserved"), 3)
        allocator = manager[
            manager.index("int CClientModelManager::GetFirstFreeModelID")
            : manager.index("int CClientModelManager::GetFreeTxdModelID")
        ]
        resolver = manager[
            manager.index("bool CClientModelManager::ResolveModelID")
            : manager.index("const SServerModelDefinition*", manager.index("bool CClientModelManager::ResolveModelID"))
        ]
        self.assertIn("continue;", allocator)
        self.assertIn("return false;", resolver)


if __name__ == "__main__":
    unittest.main()
