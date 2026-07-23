#!/usr/bin/env python3
"""Deterministic model of the native-world authorization record.

The production envelope is protected by Windows DPAPI.  This module models the
canonical plaintext only so its field order, bounds, freshness, and one-shot
identity rules can be tested on non-Windows hosts.
"""

from __future__ import annotations

from dataclasses import dataclass
import posixpath
import struct

RECORD_MAGIC = b"MTANWAR1"
RECORD_FORMAT = 1
WIRE_VERSION = 1
STARTUP_MODE = 1
PACK_FORMAT = 1
POLICY_BULLWORTH = 1
STATIC_WORLD_WIRE_VERSION = 2
STATIC_WORLD_PACK_FORMAT = 2
POLICY_STATIC_WORLD_V1 = 2
STATIC_WORLD_V3_PACK_FORMAT = 3
STATIC_WORLD_V3_SET_WIRE_VERSION = 3
POLICY_STATIC_WORLD_V3_SET = 3
RECORD_LIFETIME_SECONDS = 900
CLOCK_ROLLBACK_TOLERANCE_SECONDS = 120
RESTART_MINIMUM_REMAINING_SECONDS = 60
TRANSPORT_BITSTREAM_VERSION = 0x35
AUTHORIZATION_BITSTREAM_VERSION = 0x36
STATIC_WORLD_TRANSPORT_BITSTREAM_VERSION = 0x37
STATIC_WORLD_AUTHORIZATION_BITSTREAM_VERSION = 0x38
STATIC_WORLD_V3_TRANSPORT_BITSTREAM_VERSION = 0x39
STATIC_WORLD_V3_LOD_TRANSPORT_BITSTREAM_VERSION = 0x3A
STATIC_WORLD_V3_SET_AUTHORIZATION_BITSTREAM_VERSION = 0x3B
LATEST_BITSTREAM_VERSION = STATIC_WORLD_V3_SET_AUTHORIZATION_BITSTREAM_VERSION


class RecordError(ValueError):
    pass


@dataclass(frozen=True)
class TransportDescriptor:
    manifest_path: str
    authorization_requested: bool
    format: int = PACK_FORMAT
    file_count: int = 3
    wire_version: int = WIRE_VERSION
    startup_mode: int = STARTUP_MODE
    policy: int = POLICY_BULLWORTH


def _canonical_manifest(value: str) -> bytes:
    try:
        encoded = value.encode("utf-8")
    except UnicodeEncodeError as exc:
        raise RecordError("manifest path must be UTF-8") from exc
    if (
        not 1 <= len(encoded) <= 255
        or value.startswith("/")
        or value.endswith("/")
        or "\\" in value
        or any(character in value for character in ':*?"<>|')
        or any(ord(character) < 0x20 or ord(character) == 0x7F for character in value)
        or any(component == ".." for component in value.split("/"))
        or posixpath.normpath(value) != value
    ):
        raise RecordError("manifest path is outside the closed relative-path contract")
    return encoded


def encode_descriptor(descriptor: TransportDescriptor, client_bitstream_version: int) -> bytes:
    """Model the complete N/A descriptor header before its three F chunks."""
    manifest = _canonical_manifest(descriptor.manifest_path)
    is_v3_set = descriptor.format == STATIC_WORLD_V3_PACK_FORMAT and descriptor.authorization_requested
    supported_file_count = (
        descriptor.file_count == 1 if is_v3_set else 4 <= descriptor.file_count <= 35
    ) if descriptor.format == STATIC_WORLD_V3_PACK_FORMAT else descriptor.file_count == 3
    if descriptor.format not in (PACK_FORMAT, STATIC_WORLD_PACK_FORMAT, STATIC_WORLD_V3_PACK_FORMAT) or not supported_file_count:
        raise RecordError("unsupported transport descriptor")
    transport_capability = (
        TRANSPORT_BITSTREAM_VERSION
        if descriptor.format == PACK_FORMAT
        else STATIC_WORLD_TRANSPORT_BITSTREAM_VERSION if descriptor.format == STATIC_WORLD_PACK_FORMAT
        else STATIC_WORLD_V3_LOD_TRANSPORT_BITSTREAM_VERSION
    )
    authorization_capability = (
        AUTHORIZATION_BITSTREAM_VERSION
        if descriptor.format == PACK_FORMAT
        else STATIC_WORLD_AUTHORIZATION_BITSTREAM_VERSION if descriptor.format == STATIC_WORLD_PACK_FORMAT
        else STATIC_WORLD_V3_SET_AUTHORIZATION_BITSTREAM_VERSION
    )
    if client_bitstream_version < transport_capability:
        return b""
    authorized = descriptor.authorization_requested and client_bitstream_version >= authorization_capability
    fields = bytearray(b"A" if authorized else b"N")
    fields += bytes((descriptor.format, descriptor.file_count, len(manifest)))
    fields += manifest
    if authorized:
        expected = (
            (WIRE_VERSION, STARTUP_MODE, POLICY_BULLWORTH)
            if descriptor.format == PACK_FORMAT
            else (STATIC_WORLD_WIRE_VERSION, STARTUP_MODE, POLICY_STATIC_WORLD_V1)
            if descriptor.format == STATIC_WORLD_PACK_FORMAT
            else (STATIC_WORLD_V3_SET_WIRE_VERSION, STARTUP_MODE, POLICY_STATIC_WORLD_V3_SET)
        )
        if (descriptor.wire_version, descriptor.startup_mode, descriptor.policy) != expected:
            raise RecordError("unsupported startup authorization")
        fields += bytes((descriptor.wire_version, descriptor.startup_mode, descriptor.policy))
    return bytes(fields)


def decode_descriptor(data: bytes, client_bitstream_version: int) -> TransportDescriptor:
    if not isinstance(data, bytes) or len(data) < 4:
        raise RecordError("truncated transport descriptor")
    tag, format_value, file_count, manifest_length = data[:4]
    is_v3_set = format_value == STATIC_WORLD_V3_PACK_FORMAT and tag == ord("A")
    supported_file_count = (
        file_count == 1 if is_v3_set else 4 <= file_count <= 35
    ) if format_value == STATIC_WORLD_V3_PACK_FORMAT else file_count == 3
    if (
        tag not in (ord("N"), ord("A"))
        or format_value not in (PACK_FORMAT, STATIC_WORLD_PACK_FORMAT, STATIC_WORLD_V3_PACK_FORMAT)
        or not supported_file_count
        or not manifest_length
    ):
        raise RecordError("malformed transport descriptor")
    transport_capability = (
        TRANSPORT_BITSTREAM_VERSION
        if format_value == PACK_FORMAT
        else STATIC_WORLD_TRANSPORT_BITSTREAM_VERSION if format_value == STATIC_WORLD_PACK_FORMAT
        else STATIC_WORLD_V3_LOD_TRANSPORT_BITSTREAM_VERSION
    )
    authorization_capability = (
        AUTHORIZATION_BITSTREAM_VERSION
        if format_value == PACK_FORMAT
        else STATIC_WORLD_AUTHORIZATION_BITSTREAM_VERSION if format_value == STATIC_WORLD_PACK_FORMAT
        else STATIC_WORLD_V3_SET_AUTHORIZATION_BITSTREAM_VERSION
    )
    if client_bitstream_version < transport_capability:
        raise RecordError("transport descriptor exceeds negotiated capability")
    expected = 4 + manifest_length + (3 if tag == ord("A") else 0)
    if len(data) != expected:
        raise RecordError("truncated transport descriptor or trailing fields")
    try:
        manifest_path = data[4 : 4 + manifest_length].decode("utf-8")
    except UnicodeDecodeError as exc:
        raise RecordError("manifest path must be UTF-8") from exc
    _canonical_manifest(manifest_path)
    if tag == ord("A"):
        if client_bitstream_version < authorization_capability:
            raise RecordError("authorization descriptor exceeds negotiated capability")
        wire_version, startup_mode, policy = data[-3:]
        expected = (
            (WIRE_VERSION, STARTUP_MODE, POLICY_BULLWORTH)
            if format_value == PACK_FORMAT
            else (STATIC_WORLD_WIRE_VERSION, STARTUP_MODE, POLICY_STATIC_WORLD_V1)
            if format_value == STATIC_WORLD_PACK_FORMAT
            else (STATIC_WORLD_V3_SET_WIRE_VERSION, STARTUP_MODE, POLICY_STATIC_WORLD_V3_SET)
        )
        if (wire_version, startup_mode, policy) != expected:
            raise RecordError("unsupported startup authorization")
        return TransportDescriptor(manifest_path=manifest_path, authorization_requested=True, format=format_value, file_count=file_count,
                                   wire_version=wire_version, startup_mode=startup_mode, policy=policy)
    return TransportDescriptor(manifest_path=manifest_path, authorization_requested=False, format=format_value, file_count=file_count)


def validate_descriptor_placement(chunk_types: tuple[str, ...], file_count: int = 3) -> None:
    """Model the client's closed placement rule for one uninterrupted N/A + F group."""
    if any(chunk not in ("N", "A", "F", "E") for chunk in chunk_types):
        raise RecordError("unknown resource-start chunk type")
    descriptors = [index for index, chunk in enumerate(chunk_types) if chunk in ("N", "A")]
    if not descriptors:
        return
    if len(descriptors) != 1 or descriptors[0] != 0 or not (file_count == 1 or 3 <= file_count <= 35) or chunk_types[1 : 1 + file_count] != ("F",) * file_count:
        raise RecordError("interrupted, duplicate, or misplaced native-world descriptor")


def publication_allowed(*, connected: bool, cancelled: bool, captured_generation: int, current_generation: int,
                        captured_epoch: int, current_epoch: int, resource_still_matches: bool) -> bool:
    return (
        connected
        and not cancelled
        and captured_generation != 0
        and captured_generation == current_generation
        and captured_epoch != 0
        and captured_epoch == current_epoch
        and resource_still_matches
    )


def teardown_action(reason: str, publication_may_exist: bool) -> str:
    """Only an explicit resource stop consumes the pending one-shot ticket."""
    if reason == "resource-stop" and publication_may_exist:
        return "revoke"
    return "preserve"


@dataclass(frozen=True)
class AuthorizationRecord:
    content_id: bytes
    offer_id: bytes
    server_id_digest: bytes
    server_ipv4: bytes
    server_port: int
    resource_name: str
    resource_net_id: int
    resource_start_counter: int
    bitstream_version: int
    connection_generation: int
    authorization_epoch: int
    ticket_id: bytes
    issued_at: int
    expires_at: int
    wire_version: int = WIRE_VERSION
    startup_mode: int = STARTUP_MODE
    pack_format: int = PACK_FORMAT
    policy: int = POLICY_BULLWORTH


def _require_uint(value: int, bits: int, field: str) -> None:
    if not isinstance(value, int) or not 0 <= value < 1 << bits:
        raise RecordError(f"{field} is outside uint{bits}")


def _require_bytes(value: bytes, size: int, field: str) -> None:
    if not isinstance(value, bytes) or len(value) != size:
        raise RecordError(f"{field} must be exactly {size} bytes")


def _validate(record: AuthorizationRecord) -> bytes:
    for value, size, field in (
        (record.content_id, 32, "content_id"),
        (record.offer_id, 32, "offer_id"),
        (record.server_id_digest, 32, "server_id_digest"),
        (record.server_ipv4, 4, "server_ipv4"),
        (record.ticket_id, 16, "ticket_id"),
    ):
        _require_bytes(value, size, field)
    for value, bits, field in (
        (record.server_port, 16, "server_port"),
        (record.resource_net_id, 16, "resource_net_id"),
        (record.resource_start_counter, 32, "resource_start_counter"),
        (record.bitstream_version, 16, "bitstream_version"),
        (record.connection_generation, 64, "connection_generation"),
        (record.authorization_epoch, 64, "authorization_epoch"),
        (record.issued_at, 64, "issued_at"),
        (record.expires_at, 64, "expires_at"),
    ):
        _require_uint(value, bits, field)
    closed_bullworth = (
        record.pack_format,
        record.wire_version,
        record.startup_mode,
        record.policy,
    ) == (PACK_FORMAT, WIRE_VERSION, STARTUP_MODE, POLICY_BULLWORTH)
    closed_static_world = (
        record.pack_format,
        record.wire_version,
        record.startup_mode,
        record.policy,
    ) == (STATIC_WORLD_PACK_FORMAT, STATIC_WORLD_WIRE_VERSION, STARTUP_MODE, POLICY_STATIC_WORLD_V1)
    closed_static_world_v3_set = (
        record.pack_format,
        record.wire_version,
        record.startup_mode,
        record.policy,
    ) == (STATIC_WORLD_V3_PACK_FORMAT, STATIC_WORLD_V3_SET_WIRE_VERSION, STARTUP_MODE, POLICY_STATIC_WORLD_V3_SET)
    if not closed_bullworth and not closed_static_world and not closed_static_world_v3_set:
        raise RecordError("unsupported closed authorization version or policy")
    if not record.server_port or record.resource_net_id == 0xFFFF or not record.resource_start_counter:
        raise RecordError("invalid endpoint or resource identity")
    if not record.connection_generation or not record.authorization_epoch:
        raise RecordError("zero generation or authorization epoch")
    if (
        record.content_id == bytes(32)
        or record.offer_id == bytes(32)
        or record.ticket_id == bytes(16)
        or record.server_id_digest == bytes(32)
        or record.server_ipv4 == bytes(4)
    ):
        raise RecordError("empty server identity or endpoint")
    minimum_bitstream_version = (
        AUTHORIZATION_BITSTREAM_VERSION
        if closed_bullworth
        else STATIC_WORLD_AUTHORIZATION_BITSTREAM_VERSION if closed_static_world
        else STATIC_WORLD_V3_SET_AUTHORIZATION_BITSTREAM_VERSION
    )
    if not minimum_bitstream_version <= record.bitstream_version <= LATEST_BITSTREAM_VERSION:
        raise RecordError("bitstream version is outside the startup capability window")
    try:
        resource = record.resource_name.encode("ascii")
    except UnicodeEncodeError as exc:
        raise RecordError("resource_name must use the closed ASCII alphabet") from exc
    if not 1 <= len(resource) <= 64 or any(
        not (character in b"_- ." or chr(character).isalnum()) for character in resource
    ):
        raise RecordError("non-canonical resource_name")
    if b" " in resource:
        raise RecordError("non-canonical resource_name")
    if record.expires_at < record.issued_at or record.expires_at - record.issued_at != RECORD_LIFETIME_SECONDS:
        raise RecordError("invalid fixed lifetime")
    return resource


def encode_record(record: AuthorizationRecord) -> bytes:
    resource = _validate(record)
    fields = bytearray(RECORD_MAGIC)
    fields += struct.pack(
        "<HBBBB",
        RECORD_FORMAT,
        record.wire_version,
        record.startup_mode,
        record.pack_format,
        record.policy,
    )
    fields += record.content_id
    fields += record.offer_id
    fields += record.server_id_digest
    fields += record.server_ipv4
    fields += struct.pack("<HB", record.server_port, len(resource))
    fields += resource
    fields += struct.pack(
        "<HIHQQ",
        record.resource_net_id,
        record.resource_start_counter,
        record.bitstream_version,
        record.connection_generation,
        record.authorization_epoch,
    )
    fields += record.ticket_id
    fields += struct.pack("<QQ", record.issued_at, record.expires_at)
    return bytes(fields)


def decode_record(data: bytes) -> AuthorizationRecord:
    if not isinstance(data, bytes):
        raise RecordError("record must be bytes")
    fixed_prefix = len(RECORD_MAGIC) + struct.calcsize("<HBBBB") + 32 * 3 + 4 + struct.calcsize("<HB")
    if len(data) < fixed_prefix:
        raise RecordError("truncated record")
    offset = 0
    if data[: len(RECORD_MAGIC)] != RECORD_MAGIC:
        raise RecordError("bad magic")
    offset += len(RECORD_MAGIC)
    record_format, wire_version, startup_mode, pack_format, policy = struct.unpack_from("<HBBBB", data, offset)
    offset += struct.calcsize("<HBBBB")
    if record_format != RECORD_FORMAT:
        raise RecordError("unknown record format")

    def take(size: int) -> bytes:
        nonlocal offset
        if len(data) - offset < size:
            raise RecordError("truncated record")
        value = data[offset : offset + size]
        offset += size
        return value

    content_id = take(32)
    offer_id = take(32)
    server_id_digest = take(32)
    server_ipv4 = take(4)
    if len(data) - offset < struct.calcsize("<HB"):
        raise RecordError("truncated endpoint")
    server_port, resource_length = struct.unpack_from("<HB", data, offset)
    offset += struct.calcsize("<HB")
    if not 1 <= resource_length <= 64:
        raise RecordError("invalid resource length")
    try:
        resource_name = take(resource_length).decode("ascii")
    except UnicodeDecodeError as exc:
        raise RecordError("invalid resource encoding") from exc
    tail_format = "<HIHQQ"
    if len(data) - offset < struct.calcsize(tail_format) + 16 + 16:
        raise RecordError("truncated record tail")
    resource_net_id, resource_start_counter, bitstream_version, connection_generation, authorization_epoch = struct.unpack_from(
        tail_format, data, offset
    )
    offset += struct.calcsize(tail_format)
    ticket_id = take(16)
    issued_at, expires_at = struct.unpack_from("<QQ", data, offset)
    offset += struct.calcsize("<QQ")
    if offset != len(data):
        raise RecordError("unexpected trailing bytes")
    record = AuthorizationRecord(
        content_id=content_id,
        offer_id=offer_id,
        server_id_digest=server_id_digest,
        server_ipv4=server_ipv4,
        server_port=server_port,
        resource_name=resource_name,
        resource_net_id=resource_net_id,
        resource_start_counter=resource_start_counter,
        bitstream_version=bitstream_version,
        connection_generation=connection_generation,
        authorization_epoch=authorization_epoch,
        ticket_id=ticket_id,
        issued_at=issued_at,
        expires_at=expires_at,
        wire_version=wire_version,
        startup_mode=startup_mode,
        pack_format=pack_format,
        policy=policy,
    )
    _validate(record)
    return record


def freshness(record: AuthorizationRecord, now: int) -> str:
    _validate(record)
    _require_uint(now, 64, "now")
    if now > record.expires_at:
        return "expired"
    if now + CLOCK_ROLLBACK_TOLERANCE_SECONDS < record.issued_at:
        return "clock-refused"
    return "fresh"


def semantic_identity(record: AuthorizationRecord) -> tuple[object, ...]:
    _validate(record)
    return (
        record.wire_version,
        record.startup_mode,
        record.pack_format,
        record.policy,
        record.content_id,
        record.offer_id,
        record.server_id_digest,
        record.server_ipv4,
        record.server_port,
        record.resource_name,
        record.resource_net_id,
        record.resource_start_counter,
        record.bitstream_version,
        record.connection_generation,
        record.authorization_epoch,
    )


def durable_identity(record: AuthorizationRecord) -> tuple[object, ...]:
    """Identity retained across a reconnect; launch provenance is excluded."""
    return semantic_identity(record)[:-2]


def resolve_existing(existing: AuthorizationRecord, requested: AuthorizationRecord) -> tuple[str, AuthorizationRecord]:
    """Model no-refresh Persist resolution for an already-fresh pending record."""
    if semantic_identity(existing) == semantic_identity(requested):
        return "idempotent", existing
    if durable_identity(existing) == durable_identity(requested):
        return "attached", existing
    raise RecordError("a different unexpired authorization is already pending")


def parse_closed_startup_uri(uri: str | None) -> tuple[bytes, int] | None:
    """Accept only the production launch-2 numeric endpoint grammar."""
    if not isinstance(uri, str) or not uri.startswith("mtasa://"):
        return None
    endpoint = uri[len("mtasa://") :]
    if endpoint.count(":") != 1:
        return None
    host, port_text = endpoint.split(":")
    octets = host.split(".")
    if len(octets) != 4 or any(not part.isascii() or not part.isdigit() for part in octets):
        return None
    if any((len(part) > 1 and part.startswith("0")) or not 0 <= int(part) <= 255 for part in octets):
        return None
    if bytes(map(int, octets)) == bytes(4):
        return None
    if not port_text.isascii() or not port_text.isdigit() or port_text.startswith("0"):
        return None
    port = int(port_text)
    if not 1 <= port <= 65535:
        return None
    return bytes(map(int, octets)), port


def restart_uri(record: AuthorizationRecord, now: int) -> str:
    """Produce the only launch-2 target permitted for a fresh record."""
    if freshness(record, now) != "fresh":
        raise RecordError("restart requires a fresh native-world authorization")
    if record.expires_at - now < RESTART_MINIMUM_REMAINING_SECONDS:
        raise RecordError("restart authorization has insufficient time remaining")
    host = ".".join(str(octet) for octet in record.server_ipv4)
    uri = f"mtasa://{host}:{record.server_port}"
    if parse_closed_startup_uri(uri) != (record.server_ipv4, record.server_port):
        raise RecordError("restart endpoint is not canonical numeric IPv4")
    return uri


@dataclass
class StartupLedger:
    pending: AuthorizationRecord | None
    spent: dict[bytes, AuthorizationRecord]
    selected: AuthorizationRecord | None = None
    cancelled: bool = False

    def begin(self, uri: str | None, now: int, *, legacy_selector: bool = False) -> str:
        if self.selected is not None:
            raise RecordError("startup transaction already active")
        if self.pending is None:
            return "absent"
        _validate(self.pending)
        if self.pending.ticket_id in self.spent:
            raise RecordError("pending ticket is already spent")
        state = freshness(self.pending, now)
        if state != "fresh":
            return state
        endpoint = parse_closed_startup_uri(uri)
        if legacy_selector:
            self.selected = self.pending
            self.cancelled = False
            return "ambiguous"
        if endpoint != (self.pending.server_ipv4, self.pending.server_port):
            return "unmatched"
        self.selected = self.pending
        self.cancelled = False
        return "selected"

    def finish(self, now: int, *, claim: bool) -> str:
        if self.selected is None or self.pending != self.selected:
            raise RecordError("selected pending transaction changed")
        selected = self.selected
        self.selected = None
        if selected.ticket_id in self.spent:
            raise RecordError("ticket was already spent")
        if claim and (self.cancelled or freshness(selected, now) != "fresh"):
            claim = False
        self.spent[selected.ticket_id] = selected
        self.pending = None
        return "claimed" if claim else "terminal-refused"

    def crash(self) -> None:
        """A pre-claim crash releases the in-memory lock and preserves pending."""
        self.selected = None

    def cancel(self) -> None:
        if self.selected is not None:
            self.cancelled = True


@dataclass
class TypedCacheLease:
    format: int
    policy: str
    content_id: str
    ticket_id: str
    active: bool = True
    committed: bool = False

    def commit(self, format_value: int, policy: str, content_id_value: str, ticket_id: str) -> None:
        if not self.active:
            raise RecordError("lease already completed")
        if (format_value, policy, content_id_value, ticket_id) != (self.format, self.policy, self.content_id, self.ticket_id):
            raise RecordError("lease token mismatch")
        self.active = False
        self.committed = True

    def release(self) -> None:
        self.active = False
