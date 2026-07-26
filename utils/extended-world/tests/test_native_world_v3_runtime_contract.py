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
        self.assertIn("NativeWorldStaticWorldV3ServerSelectedSet", bitstream)
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
        self.assertIn("g_staticWorldV3Route ? g_staticWorldV3LargestEntryBlocks : Pack().largestImgEntryBlocks", function)
        self.assertIn("perChannelBlocks = (largestEntryBlocks + 1) & ~uint64_t{1}", function)
        self.assertIn("totalBlocks = perChannelBlocks * 2", function)
        self.assertIn("totalBlocks > std::numeric_limits<unsigned int>::max()", function)
        for largest, expected in ((1, 4), (2, 4), (3, 8), (65_535, 131_072)):
            per_channel = (largest + 1) & ~1
            self.assertEqual(expected, per_channel * 2)

    def test_v3_registrar_catalogues_all_packs_then_collapses_startup_ids(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        register = source[source.index("void RegisterStaticWorldV3Set()") : source.index("void RegisterPack()")]
        self.assertIn("catalogModels == 0 || catalogModels > 12000U", source)
        self.assertIn("std::vector<SStaticWorldV3RuntimePack>", source)
        self.assertIn('pack.identity.packId == "vice-city"', source)
        self.assertIn("ValidateStaticWorldV3LodProfile(error)", source)
        self.assertIn("HookInstallCall(LOAD_COL_BUFFER_CALL", register)
        self.assertIn("HookInstallCall(LOAD_IPL_BUFFER_CALL", register)
        self.assertIn("HookInstall(REMOVE_ALL_COLLISION_TAIL", register)
        self.assertIn("barrier=first-ModelInfo", register)
        self.assertIn("startupMapping=canonical-until-bounds", register)
        self.assertIn("g_staticWorldV3Generation.store(1", register)
        collapse = source[source.index("void __cdecl CompleteStaticWorldV3BoundingBootstrap") : source.index("void RegisterPack()")]
        self.assertIn("REMOVE_ALL_COLLISION", collapse)
        self.assertIn("canonicalPointersCleared", collapse)
        self.assertIn("clothesNamespaceOverlap=cleared-before-gameplay", collapse)

    def test_v3_lod_bootstrap_uses_reusable_banks_and_child_first_teardown(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        self.assertIn("STATIC_WORLD_V3_VICE_CITY_LOD_LINKS = 1081", source)
        self.assertIn("STATIC_WORLD_V3_LIBERTY_CITY_LOD_LINKS = 1957", source)
        self.assertIn("STATIC_WORLD_V3_VICE_CITY_LOD_CHILD_GROUPS = 9", source)
        self.assertIn("STATIC_WORLD_V3_LIBERTY_CITY_LOD_CHILD_GROUPS = 12", source)
        self.assertIn("STATIC_WORLD_V3_VICE_CITY_LOD_SCRATCH = 2162", source)
        self.assertIn("STATIC_WORLD_V3_LIBERTY_CITY_LOD_SCRATCH = 3914", source)
        self.assertIn("STATIC_WORLD_V3_VICE_CITY_MISSING_ANCHOR_COLS = 2", source)
        self.assertIn("STATIC_WORLD_V3_LIBERTY_CITY_MISSING_ANCHOR_COLS = 0", source)
        self.assertIn("IPL_ENTITY_SCRATCH_CAPACITY = 4096", source)
        self.assertIn("ValidateStaticWorldV3LodArrayGlobals(0", source)
        self.assertIn("ValidateStaticWorldV3LodArrayGlobals(32", source)
        self.assertIn("GET_NEW_IPL_ENTITY_INDEX_ARRAY = 0x404780", source)
        self.assertIn("IPL_ENTITY_INDEX_ARRAYS = 0x8E3F08", source)
        self.assertIn("allocate(IPL_ENTITY_SCRATCH_CAPACITY)", source)
        self.assertIn("SetupBigBuilding before CWorld::Add", source)
        self.assertIn("!entity->bDontCastShadowsOn", source)
        self.assertNotIn("!entity->bStreamingDontDelete", source)
        self.assertIn("entity->bStreamingDontDelete", source)
        self.assertIn("!modelInfo->bDoWeOwnTheColModel", source)
        self.assertNotIn("modelInfo->bDoWeOwnTheColModel == borrowedChildCol", source)
        self.assertIn("modelInfo->bIsColLoaded == borrowedChildCol", source)
        self.assertIn("SET_COL_MODEL = 0x4C4BC0", source)
        self.assertIn("!borrowedFrom->bIsColLoaded", source)
        self.assertIn("modelInfo->bIsColLoaded", source)
        self.assertIn("supplied placement COL did not materialize before IPL bootstrap", source)
        self.assertIn("entity->m_pLod = nullptr", source)
        self.assertIn("owner->relatedIpl = -1", source)
        self.assertIn("def->relatedIpl = pack.inventory.lodAnchorCount ? static_cast<int16_t>(bank) : -1", source)
        self.assertIn("if (boundingPass)", source)
        self.assertIn("instances[index].lodIndex = -1", source)
        self.assertIn("if (isAnchor)", source)
        self.assertIn("instance.lodIndex = static_cast<int>(link->anchorIndex)", source)
        self.assertIn("hidden owners after every spatial child IPL", source)
        self.assertIn("collisionTransfer=missing-anchor-only:%u", source)
        self.assertIn("missing-anchor collision transfer count drifted after admission", source)
        self.assertIn("DestroyStaticWorldV3LodAnchors", source)
        self.assertIn("generation fence reached a live or missing LOD anchor", source)

    def test_v3_runtime_coordinator_owns_two_banks_and_all_transition_fences(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        multiplayer = (REPOSITORY / "Client/multiplayer_sa/CMultiplayerSA.cpp").read_text(encoding="utf-8")
        streaming = (REPOSITORY / "Client/game_sa/CStreamingSA.cpp").read_text(encoding="utf-8")
        resource = (REPOSITORY / "test-resources/native-bw-test/server.lua").read_text(encoding="utf-8")
        self.assertIn("STATIC_WORLD_V3_MODEL_BANK_SIZE = 4096", source)
        self.assertIn("STATIC_WORLD_V3_MODEL_BANK_FIRST[] = {20000, 24096}", source)
        self.assertIn("BindStaticWorldV3PackToBank", source)
        self.assertIn("PatchStaticWorldV3ColRanges", source)
        self.assertIn("RebuildStaticWorldV3ArchiveChains", source)
        self.assertIn("FLUSH_STREAMING_CHANNELS", source)
        self.assertIn("COVER_INIT = 0x698710", source)
        retirement = source[source.index("void DeactivateStaticWorldV3Pack()") : source.index("void ActivateStaticWorldV3Pack(")]
        self.assertLess(
            retirement.index("reinterpret_cast<void(__cdecl*)()>(COVER_INIT)()"),
            retirement.index("g_streaming->RemoveModel(pGame->GetBaseIDforIPL() + slot)"),
        )
        self.assertIn("fence=cover-ipl-anchors-channels-col-dff", source)
        self.assertIn("transition=retired", source)
        self.assertIn("transition=active", source)
        self.assertIn("exclusion=one-city", source)
        self.assertIn("PrepareNativeWorldStreaming", multiplayer)
        self.assertGreaterEqual(streaming.count("PrepareStreamingAtPosition"), 2)
        for command in ("nativebw", "nativevc", "nativelc", "nativecc", "nativeback"):
            self.assertIn(f'addCommandHandler("{command}"', resource)

    def test_v3_registrar_pins_all_caches_and_commits_the_exact_ticket(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        startup = source[source.index("void CNativeWorldPackManagerSA::HandleStartupSelection") :
                         source.index("void CNativeWorldPackManagerSA::AttachAuthorizedStreaming")]
        self.assertIn("AcquireStaticWorldV3SetChildren(lockedManifest, selection.ticketId", startup)
        self.assertIn("g_staticWorldV3ChildLeases = std::move(childLeases)", startup)
        register = source[source.index("void RegisterStaticWorldV3Set()") : source.index("void RegisterPack()")]
        self.assertIn("lease.Commit(STATIC_WORLD_V3_FORMAT, STATIC_WORLD_V3_POLICY", register)
        self.assertIn("g_authorizedSelection.ticketId", register)
        self.assertIn("g_authorizedLease.Commit(STATIC_WORLD_V3_FORMAT, STATIC_WORLD_V3_SET_POLICY", register)

    def test_v3_registrar_uses_direct_multi_img_streaming_and_buffer_remap(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        prepare = source[source.index("bool PrepareStaticWorldV3Transaction") : source.index("void RegisterStaticWorldV3Set()")]
        self.assertIn("pack.manifest.images", prepare)
        self.assertIn("g_streaming->AddArchive", prepare)
        self.assertGreaterEqual(prepare.count("PredictNextPoolSlot("), 3)
        self.assertNotIn("GetFreeSlot()", prepare)
        self.assertIn("perArchive[archive->second]", prepare)
        self.assertIn("nextInImg", prepare)
        self.assertNotIn("LOAD_NAMED_CD_DIRECTORY", prepare)
        self.assertIn("LoadStaticWorldV3ColBuffer", source)
        self.assertIn("LoadStaticWorldV3IplBuffer", source)
        self.assertIn("data + offset + 30", source)
        self.assertIn("RemapStaticWorldV3Model(pack, instance.modelId)", source)

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
        self.assertIn("std::atomic_bool", pack)
        self.assertIn("g_nativeModelSlotsReserved{false}", pack)
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

    def test_v3_server_selected_set_has_an_append_only_closed_capability_gate(self) -> None:
        capability = "NativeWorldStaticWorldV3ServerSelectedSet"
        server_game = (REPOSITORY / "Server/mods/deathmatch/logic/CGame.cpp").read_text(encoding="utf-8")
        server_packet = (REPOSITORY / "Server/mods/deathmatch/logic/packets/CResourceStartPacket.cpp").read_text(encoding="utf-8")
        client_packet = (REPOSITORY / "Client/mods/deathmatch/logic/CPacketHandler.cpp").read_text(encoding="utf-8")
        core = (REPOSITORY / "Client/core/CCore.cpp").read_text(encoding="utf-8")
        store = (REPOSITORY / "Client/core/CNativeWorldAuthorizationStore.cpp").read_text(encoding="utf-8")
        for source in (server_game, server_packet, client_packet, core, store):
            self.assertIn(capability, source)

    def test_streamer_extra_sector_alignment_uses_floor_at_negative_boundaries(self) -> None:
        header = (REPOSITORY / "Client/mods/deathmatch/logic/CClientStreamSectorRow.h").read_text(encoding="utf-8")
        streamer = (REPOSITORY / "Client/mods/deathmatch/logic/CClientStreamer.cpp").read_text(encoding="utf-8")
        row = (REPOSITORY / "Client/mods/deathmatch/logic/CClientStreamSectorRow.cpp").read_text(encoding="utf-8")
        self.assertIn("std::floor(coordinate / sectorSize) * sectorSize", header)
        self.assertEqual(streamer.count("AlignStreamSectorCoordinate"), 2)
        self.assertEqual(row.count("AlignStreamSectorCoordinate"), 1)
        self.assertNotIn("if (vecPosition.fY < 0.0f)", streamer)
        self.assertNotIn("if (vecPosition.fX < 0.0f)", row)

    def test_mta_consumers_share_the_extended_world_coordinate_contract(self) -> None:
        limits = (REPOSITORY / "Shared/sdk/WorldLimits.h").read_text(encoding="utf-8")
        entity = (REPOSITORY / "Client/mods/deathmatch/logic/CClientEntity.cpp").read_text(encoding="utf-8")
        sync = (REPOSITORY / "Shared/sdk/net/SyncStructures.h").read_text(encoding="utf-8")
        self.assertIn("EXTENDED_WORLD_MIN_COORD = -10000.0f", limits)
        self.assertIn("EXTENDED_WORLD_MAX_ENTITY_COORD", limits)
        self.assertIn("!std::isfinite(vecPosition.fX)", entity)
        self.assertIn("vecPosition.fX < EXTENDED_WORLD_MIN_COORD", entity)
        self.assertIn("LOW_PRECISION_POSITION_BOUND = EXTENDED_WORLD_MAX_COORD", sync)


if __name__ == "__main__":
    unittest.main()
