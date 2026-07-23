#!/usr/bin/env python3

from dataclasses import replace
import hashlib
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
sys.path.insert(0, str(ROOT))

from native_world_authorization import (  # noqa: E402
    AUTHORIZATION_BITSTREAM_VERSION,
    AuthorizationRecord,
    PACK_FORMAT,
    POLICY_BULLWORTH,
    RecordError,
    STARTUP_MODE,
    STATIC_WORLD_AUTHORIZATION_BITSTREAM_VERSION,
    STATIC_WORLD_PACK_FORMAT,
    STATIC_WORLD_TRANSPORT_BITSTREAM_VERSION,
    STATIC_WORLD_V3_PACK_FORMAT,
    STATIC_WORLD_V3_LOD_TRANSPORT_BITSTREAM_VERSION,
    STATIC_WORLD_V3_SET_AUTHORIZATION_BITSTREAM_VERSION,
    STATIC_WORLD_V3_SET_WIRE_VERSION,
    POLICY_STATIC_WORLD_V3_SET,
    STATIC_WORLD_V3_TRANSPORT_BITSTREAM_VERSION,
    STATIC_WORLD_WIRE_VERSION,
    POLICY_STATIC_WORLD_V1,
    TRANSPORT_BITSTREAM_VERSION,
    TransportDescriptor,
    WIRE_VERSION,
    decode_record,
    decode_descriptor,
    encode_record,
    encode_descriptor,
    durable_identity,
    freshness,
    publication_allowed,
    resolve_existing,
    semantic_identity,
    teardown_action,
    validate_descriptor_placement,
)


def sample_record() -> AuthorizationRecord:
    return AuthorizationRecord(
        content_id=bytes(range(32)),
        offer_id=bytes(range(32, 64)),
        server_id_digest=bytes(range(64, 96)),
        server_ipv4=bytes((127, 0, 0, 1)),
        server_port=22003,
        resource_name="native-world-transport-test",
        resource_net_id=17,
        resource_start_counter=3,
        bitstream_version=0x36,
        connection_generation=9,
        authorization_epoch=2,
        ticket_id=bytes(range(16)),
        issued_at=1_700_000_000,
        expires_at=1_700_000_900,
    )


class NativeWorldAuthorizationCodecTests(unittest.TestCase):
    def test_round_trip_and_golden_digest(self) -> None:
        encoded = encode_record(sample_record())
        self.assertEqual(decode_record(encoded), sample_record())
        self.assertEqual(
            hashlib.sha256(encoded).hexdigest(),
            "912446bb0380cdbd0f908f340c1b78ebb2d2243a9a8e9eec6c08d31613266c14",
        )

    def test_rejects_every_truncation_and_trailing_bytes(self) -> None:
        encoded = encode_record(sample_record())
        for length in range(len(encoded)):
            with self.subTest(length=length), self.assertRaises(RecordError):
                decode_record(encoded[:length])
        with self.assertRaises(RecordError):
            decode_record(encoded + b"\0")

    def test_fixed_versions_bounds_and_lifetime(self) -> None:
        record = sample_record()
        for changed in (
            replace(record, policy=2),
            replace(record, startup_mode=0),
            replace(record, resource_name="bad/name"),
            replace(record, resource_name="é"),
            replace(record, resource_net_id=0xFFFF),
            replace(record, connection_generation=0),
            replace(record, authorization_epoch=0),
            replace(record, expires_at=record.expires_at + 1),
        ):
            with self.subTest(changed=changed), self.assertRaises(RecordError):
                encode_record(changed)
        self.assertEqual(decode_record(encode_record(replace(record, resource_net_id=0))).resource_net_id, 0)

    def test_format_2_record_round_trip_is_distinct_and_closed(self) -> None:
        record = replace(
            sample_record(),
            wire_version=STATIC_WORLD_WIRE_VERSION,
            pack_format=STATIC_WORLD_PACK_FORMAT,
            policy=POLICY_STATIC_WORLD_V1,
            bitstream_version=STATIC_WORLD_AUTHORIZATION_BITSTREAM_VERSION,
        )
        self.assertEqual(decode_record(encode_record(record)), record)
        self.assertEqual(hashlib.sha256(encode_record(record)).hexdigest(), "1755486d481c3dd85e5e13922327837e8264624eeb1b3617fded0aab84919926")
        with self.assertRaises(RecordError):
            resolve_existing(sample_record(), record)
        for changed in (
            replace(record, wire_version=1),
            replace(record, policy=1),
            replace(record, pack_format=1),
            replace(record, bitstream_version=STATIC_WORLD_TRANSPORT_BITSTREAM_VERSION),
        ):
            with self.subTest(changed=changed), self.assertRaises(RecordError):
                encode_record(changed)

    def test_only_the_three_closed_startup_tuples_are_encodable(self) -> None:
        accepted = {
            (PACK_FORMAT, WIRE_VERSION, STARTUP_MODE, POLICY_BULLWORTH),
            (STATIC_WORLD_PACK_FORMAT, STATIC_WORLD_WIRE_VERSION, STARTUP_MODE, POLICY_STATIC_WORLD_V1),
            (STATIC_WORLD_V3_PACK_FORMAT, STATIC_WORLD_V3_SET_WIRE_VERSION, STARTUP_MODE, POLICY_STATIC_WORLD_V3_SET),
        }
        for pack_format in (PACK_FORMAT, STATIC_WORLD_PACK_FORMAT, 3):
            for wire_version in (WIRE_VERSION, STATIC_WORLD_WIRE_VERSION, 3):
                for startup_mode in (0, STARTUP_MODE, 2):
                    for policy in (POLICY_BULLWORTH, POLICY_STATIC_WORLD_V1, 3):
                        startup_tuple = (pack_format, wire_version, startup_mode, policy)
                        record = replace(
                            sample_record(),
                            pack_format=pack_format,
                            wire_version=wire_version,
                            startup_mode=startup_mode,
                            policy=policy,
                            bitstream_version=STATIC_WORLD_V3_SET_AUTHORIZATION_BITSTREAM_VERSION,
                        )
                        with self.subTest(startup_tuple=startup_tuple):
                            if startup_tuple in accepted:
                                self.assertEqual(decode_record(encode_record(record)), record)
                            else:
                                with self.assertRaises(RecordError):
                                    encode_record(record)

    def test_freshness_boundaries_are_exact(self) -> None:
        record = sample_record()
        self.assertEqual(freshness(record, record.issued_at - 120), "fresh")
        self.assertEqual(freshness(record, record.issued_at - 121), "clock-refused")
        self.assertEqual(freshness(record, record.expires_at), "fresh")
        self.assertEqual(freshness(record, record.expires_at + 1), "expired")

    def test_ticket_and_time_do_not_refresh_semantic_identity(self) -> None:
        record = sample_record()
        repeated = replace(record, ticket_id=b"x" * 16, issued_at=record.issued_at + 1, expires_at=record.expires_at + 1)
        self.assertEqual(semantic_identity(record), semantic_identity(repeated))
        self.assertNotEqual(semantic_identity(record), semantic_identity(replace(record, connection_generation=10)))


class NativeWorldAuthorizationWireAndLifecycleTests(unittest.TestCase):
    def test_capability_matrix_keeps_legacy_n_byte_for_byte_closed(self) -> None:
        inert = TransportDescriptor("native/native-world.json", authorization_requested=False)
        requested = replace(inert, authorization_requested=True)
        legacy_n = encode_descriptor(inert, TRANSPORT_BITSTREAM_VERSION)
        self.assertEqual(legacy_n, b"N\x01\x03\x18native/native-world.json")
        self.assertEqual(encode_descriptor(requested, TRANSPORT_BITSTREAM_VERSION - 1), b"")
        self.assertEqual(encode_descriptor(requested, TRANSPORT_BITSTREAM_VERSION), legacy_n)
        self.assertEqual(decode_descriptor(legacy_n, AUTHORIZATION_BITSTREAM_VERSION), inert)
        authorized_a = encode_descriptor(requested, AUTHORIZATION_BITSTREAM_VERSION)
        self.assertEqual(authorized_a[:1], b"A")
        self.assertEqual(encode_descriptor(requested, STATIC_WORLD_AUTHORIZATION_BITSTREAM_VERSION), authorized_a)
        self.assertEqual(decode_descriptor(authorized_a, AUTHORIZATION_BITSTREAM_VERSION), requested)

    def test_authorization_descriptor_rejects_every_truncation_trailing_and_unknown_value(self) -> None:
        encoded = encode_descriptor(TransportDescriptor("native/native-world.json", True), AUTHORIZATION_BITSTREAM_VERSION)
        for length in range(len(encoded)):
            with self.subTest(length=length), self.assertRaises(RecordError):
                decode_descriptor(encoded[:length], AUTHORIZATION_BITSTREAM_VERSION)
        for changed in (
            encoded + b"\0",
            b"X" + encoded[1:],
            encoded[:-3] + bytes((2, 1, 1)),
            encoded[:-3] + bytes((1, 2, 1)),
            encoded[:-3] + bytes((1, 1, 2)),
        ):
            with self.subTest(changed=changed), self.assertRaises(RecordError):
                decode_descriptor(changed, AUTHORIZATION_BITSTREAM_VERSION)
        with self.assertRaises(RecordError):
            decode_descriptor(encoded, TRANSPORT_BITSTREAM_VERSION)

    def test_format_2_authorization_is_append_only_and_downgrades_to_publish_only(self) -> None:
        inert = TransportDescriptor("native/native-world.json", False, format=STATIC_WORLD_PACK_FORMAT)
        requested = replace(
            inert,
            authorization_requested=True,
            wire_version=STATIC_WORLD_WIRE_VERSION,
            policy=POLICY_STATIC_WORLD_V1,
        )
        self.assertEqual(encode_descriptor(requested, STATIC_WORLD_TRANSPORT_BITSTREAM_VERSION), encode_descriptor(inert, STATIC_WORLD_TRANSPORT_BITSTREAM_VERSION))
        authorized = encode_descriptor(requested, STATIC_WORLD_AUTHORIZATION_BITSTREAM_VERSION)
        self.assertEqual(authorized[:4], b"A\x02\x03\x18")
        self.assertEqual(authorized[-3:], bytes((STATIC_WORLD_WIRE_VERSION, 1, POLICY_STATIC_WORLD_V1)))
        self.assertEqual(decode_descriptor(authorized, STATIC_WORLD_AUTHORIZATION_BITSTREAM_VERSION), requested)
        with self.assertRaises(RecordError):
            decode_descriptor(authorized, STATIC_WORLD_TRANSPORT_BITSTREAM_VERSION)
        for changed in (
            replace(requested, wire_version=1),
            replace(requested, policy=1),
            replace(requested, format=1),
        ):
            with self.subTest(changed=changed), self.assertRaises(RecordError):
                encode_descriptor(changed, STATIC_WORLD_AUTHORIZATION_BITSTREAM_VERSION)

    def test_format_3_child_and_authorized_set_are_distinct_closed_shapes(self) -> None:
        descriptor = TransportDescriptor(
            "native/native-world.json",
            authorization_requested=False,
            format=STATIC_WORLD_V3_PACK_FORMAT,
            file_count=6,
        )
        self.assertEqual(encode_descriptor(descriptor, STATIC_WORLD_V3_LOD_TRANSPORT_BITSTREAM_VERSION)[:4], b"N\x03\x06\x18")
        self.assertEqual(
            decode_descriptor(encode_descriptor(descriptor, STATIC_WORLD_V3_LOD_TRANSPORT_BITSTREAM_VERSION), STATIC_WORLD_V3_LOD_TRANSPORT_BITSTREAM_VERSION),
            descriptor,
        )
        self.assertEqual(encode_descriptor(descriptor, STATIC_WORLD_V3_LOD_TRANSPORT_BITSTREAM_VERSION - 1), b"")
        authorized_set = TransportDescriptor(
            "native/static-world-v3-set.json",
            authorization_requested=True,
            format=STATIC_WORLD_V3_PACK_FORMAT,
            file_count=1,
            wire_version=STATIC_WORLD_V3_SET_WIRE_VERSION,
            policy=POLICY_STATIC_WORLD_V3_SET,
        )
        encoded_set = encode_descriptor(authorized_set, STATIC_WORLD_V3_SET_AUTHORIZATION_BITSTREAM_VERSION)
        self.assertEqual(encoded_set[:4], b"A\x03\x01\x1f")
        self.assertEqual(decode_descriptor(encoded_set, STATIC_WORLD_V3_SET_AUTHORIZATION_BITSTREAM_VERSION), authorized_set)
        with self.assertRaises(RecordError):
            decode_descriptor(encoded_set, STATIC_WORLD_V3_LOD_TRANSPORT_BITSTREAM_VERSION)
        validate_descriptor_placement(("N",) + ("F",) * 6 + ("E",), file_count=6)

    def test_descriptor_group_is_unique_first_and_uninterrupted(self) -> None:
        validate_descriptor_placement(("A", "F", "F", "F", "E"))
        validate_descriptor_placement(("F", "E"))
        for sequence in (("X",), ("F", "A", "F", "F", "F"), ("A", "F", "X", "F"), ("A", "F", "E", "F"), ("A", "F", "F"),
                         ("A", "F", "F", "F", "N")):
            with self.subTest(sequence=sequence), self.assertRaises(RecordError):
                validate_descriptor_placement(sequence)

    def test_manifest_model_matches_closed_cpp_path_examples(self) -> None:
        for path in ("native/./world.json", "native/world.json/", "native/a:b.json", "native/*", "../native/world.json"):
            with self.subTest(path=path), self.assertRaises(RecordError):
                encode_descriptor(TransportDescriptor(path, True), AUTHORIZATION_BITSTREAM_VERSION)

    def test_publication_requires_exact_live_snapshot(self) -> None:
        base = dict(connected=True, cancelled=False, captured_generation=7, current_generation=7,
                    captured_epoch=3, current_epoch=3, resource_still_matches=True)
        self.assertTrue(publication_allowed(**base))
        for field, value in (("connected", False), ("cancelled", True), ("current_generation", 8),
                             ("current_epoch", 4), ("resource_still_matches", False)):
            with self.subTest(field=field):
                self.assertFalse(publication_allowed(**(base | {field: value})))

    def test_only_explicit_resource_stop_revokes(self) -> None:
        self.assertEqual(teardown_action("resource-stop", True), "revoke")
        for reason in ("disconnect", "mod-unload", "process-exit", "worker-cancel"):
            self.assertEqual(teardown_action(reason, True), "preserve")
        self.assertEqual(teardown_action("resource-stop", False), "preserve")

    def test_reconnect_attaches_without_refreshing_launch_provenance(self) -> None:
        original = sample_record()
        reconnect = replace(original, connection_generation=10, authorization_epoch=3, ticket_id=b"z" * 16,
                            issued_at=original.issued_at + 1, expires_at=original.expires_at + 1)
        self.assertNotEqual(semantic_identity(original), semantic_identity(reconnect))
        self.assertEqual(durable_identity(original), durable_identity(reconnect))
        disposition, retained = resolve_existing(original, reconnect)
        self.assertEqual(disposition, "attached")
        self.assertIs(retained, original)
        self.assertEqual(
            (retained.ticket_id, retained.issued_at, retained.expires_at, retained.connection_generation, retained.authorization_epoch),
            (original.ticket_id, original.issued_at, original.expires_at, original.connection_generation, original.authorization_epoch),
        )
        self.assertEqual(resolve_existing(original, original), ("idempotent", original))
        with self.assertRaises(RecordError):
            resolve_existing(original, replace(reconnect, content_id=b"different-content".ljust(32, b"!")))
        self.assertEqual(teardown_action("disconnect", True), "preserve")
        self.assertEqual(teardown_action("resource-stop", True), "revoke")


class NativeWorldAuthorizationSourceContractTests(unittest.TestCase):
    def test_protocol_is_separate_and_legacy_descriptor_is_unchanged(self) -> None:
        bitstream = (REPO / "Shared/sdk/net/bitstream.h").read_text()
        writer = (REPO / "Server/mods/deathmatch/logic/packets/CResourceStartPacket.cpp").read_text()
        reader = (REPO / "Client/mods/deathmatch/logic/CPacketHandler.cpp").read_text()
        self.assertIn("NativeWorldStartupAuthorization", bitstream)
        self.assertIn("writeStartupAuthorization ? 'A' : 'N'", writer)
        self.assertIn("case 'A'", reader)
        self.assertIn("case 'N'", reader)

    def test_static_world_v2_startup_is_append_only_gated(self) -> None:
        bitstream = (REPO / "Shared/sdk/net/bitstream.h").read_text()
        server_resource = (REPO / "Server/mods/deathmatch/logic/CResource.cpp").read_text()
        writer = (REPO / "Server/mods/deathmatch/logic/packets/CResourceStartPacket.cpp").read_text()
        reader = (REPO / "Client/mods/deathmatch/logic/CPacketHandler.cpp").read_text()
        client_resource = (REPO / "Client/mods/deathmatch/logic/CResource.cpp").read_text()

        self.assertLess(bitstream.index("NativeWorldStartupAuthorization,"), bitstream.index("NativeWorldStaticWorldV2Transport,"))
        self.assertLess(bitstream.index("NativeWorldStaticWorldV2Transport,"), bitstream.index("NativeWorldStaticWorldV2StartupAuthorization,"))
        self.assertIn('formatAttribute->GetValue() == "2"', server_resource)
        self.assertIn('policyAttribute->GetValue() == "static-world-v1"', server_resource)
        self.assertIn("staticWorldV2Authorized", server_resource)
        self.assertIn("NativeWorldStaticWorldV2Transport", writer)
        self.assertIn("if (!isNativeWorldFile(resourceFile))", writer)
        self.assertIn("NativeWorldStaticWorldV2StartupAuthorization", writer)
        self.assertIn("NativeWorldStaticWorldV2Transport", reader)
        self.assertIn("NativeWorldStaticWorldV2StartupAuthorization", reader)
        self.assertIn("IsClosedNativeWorldStartupAuthorization", client_resource)
        self.assertIn("result.auditProfile.c_str()", client_resource)

    def test_static_world_v3_child_transport_and_set_startup_are_separately_gated(self) -> None:
        bitstream = (REPO / "Shared/sdk/net/bitstream.h").read_text()
        server_header = (REPO / "Server/mods/deathmatch/logic/CResource.h").read_text()
        server_resource = (REPO / "Server/mods/deathmatch/logic/CResource.cpp").read_text()
        writer = (REPO / "Server/mods/deathmatch/logic/packets/CResourceStartPacket.cpp").read_text()
        reader = (REPO / "Client/mods/deathmatch/logic/CPacketHandler.cpp").read_text()
        client_header = (REPO / "Client/mods/deathmatch/logic/CResource.h").read_text()
        game_interface = (REPO / "Client/sdk/game/CGame.h").read_text()
        authorization = (REPO / "Client/sdk/core/CNativeWorldAuthorization.h").read_text()

        self.assertLess(bitstream.index("NativeWorldStaticWorldV2StartupAuthorization,"), bitstream.index("NativeWorldStaticWorldV3Transport,"))
        self.assertIn("std::vector<CResourceFile*>", server_header)
        self.assertIn('formatAttribute->GetValue() == "3"', server_resource)
        self.assertIn('policyAttribute->GetValue() == "static-world-v3"', server_resource)
        self.assertIn("!startupAttribute", server_resource)
        self.assertIn("NativeWorldStaticWorldV3LodTransport", writer)
        self.assertIn("NativeWorldStaticWorldV3LodTransport", reader)
        self.assertIn("fileCount >= 4 && fileCount <= 35", reader)
        self.assertIn("std::vector<CDownloadableResource*>", client_header)
        self.assertIn("std::vector<SNativeWorldTransportFile>", game_interface)
        self.assertIn("unsigned int declaredBytes", game_interface)
        self.assertIn("NATIVE_WORLD_STATIC_V3_SET_AUTHORIZATION_VERSION", authorization)

    def test_store_is_dpapi_atomic_unicode_and_inert(self) -> None:
        store = (REPO / "Client/core/CNativeWorldAuthorizationStore.cpp").read_text()
        for token in (
            "GetSystemLocalAppDataPath",
            "CryptProtectData",
            "CryptUnprotectData",
            "CRYPTPROTECT_UI_FORBIDDEN",
            "FILE_FLAG_OPEN_REPARSE_POINT",
            "FlushFileBuffers",
            "MOVEFILE_WRITE_THROUGH",
            "CreateFileW",
            "GetFinalPathNameByHandleW",
            "SetOwnerToCurrentUser",
            "SetSecurityInfo",
            "WRITE_OWNER",
            "activation=no lease=no",
        ):
            self.assertIn(token, store)
        write_start = store.index("bool WriteAndFlush")
        write_end = store.index("SNativeWorldAuthorizationRecordResult MakeResult", write_start)
        write = store[write_start:write_end]
        self.assertLess(write.index("SetOwnerToCurrentUser"), write.index("HandleMatchesPath"))
        self.assertLess(write.index("HandleMatchesPath"), write.index("WriteFile"))
        for forbidden in (
            "InstallFromEnvironment",
            "PrepareAndLockNativeWorldCache",
            "CommitNativeWorldCacheLease",
            "LoadCdDirectoryHook",
            "CNativeModelStore",
        ):
            self.assertNotIn(forbidden, store)

    def test_no_lua_authorization_surface(self) -> None:
        matches = list((REPO / "Client/mods/deathmatch/logic/luadefs").rglob("*NativeWorldAuthorization*"))
        self.assertEqual(matches, [])


if __name__ == "__main__":
    unittest.main()
