#!/usr/bin/env python3
"""Create authenticated Neon resource assets without exposing the content key to client Lua."""

from __future__ import annotations

import argparse
import os
import secrets
import struct
import sys
from pathlib import Path, PurePosixPath

try:
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
except ImportError as exc:  # pragma: no cover - exercised by the CLI environment
    raise SystemExit("Install dependencies with: python -m pip install -r requirements.txt") from exc


MAGIC = b"NEONAST1"
FORMAT_VERSION = 1
HEADER = struct.Struct("<8sBBHQ16s12s")
TAG_SIZE = 16
MAX_PLAINTEXT_SIZE = 64 * 1024 * 1024
TYPE_IDS = {"dff": 1, "txd": 2, "col": 3}
ID_TYPES = {value: key for key, value in TYPE_IDS.items()}


def canonical_relative_path(value: str) -> str:
    if not value or "\\" in value or value.startswith("/") or value.endswith("/") or any(char in value for char in ':*?"<>|'):
        raise ValueError("asset path must be a canonical forward-slash relative path")
    path = PurePosixPath(value)
    if any(part in ("", ".", "..") for part in path.parts) or str(path) != value:
        raise ValueError("asset path must not contain empty, dot, or parent components")
    if len(value.encode("utf-8")) > 0xFFFF:
        raise ValueError("asset path is too long")
    return value


def read_hex(path: Path, expected_bytes: int, label: str) -> bytes:
    try:
        value = bytes.fromhex(path.read_text(encoding="ascii").strip())
    except (OSError, UnicodeError, ValueError) as exc:
        raise ValueError(f"invalid {label} file: {exc}") from exc
    if len(value) != expected_bytes:
        raise ValueError(f"{label} must contain exactly {expected_bytes * 2} hexadecimal characters")
    return value


def build_aad(header: bytes, resource: str, relative_path: str) -> bytes:
    resource_bytes = resource.encode("utf-8")
    path_bytes = relative_path.encode("utf-8")
    if not resource_bytes or len(resource_bytes) > 0xFFFF:
        raise ValueError("resource name must contain 1..65535 UTF-8 bytes")
    return header + struct.pack("<H", len(resource_bytes)) + resource_bytes + struct.pack("<H", len(path_bytes)) + path_bytes


def pack_asset(plaintext: bytes, key: bytes, package_id: bytes, resource: str, relative_path: str, asset_type: str, nonce: bytes | None = None) -> bytes:
    relative_path = canonical_relative_path(relative_path)
    if not plaintext or len(plaintext) > MAX_PLAINTEXT_SIZE:
        raise ValueError(f"plaintext size must be between 1 and {MAX_PLAINTEXT_SIZE} bytes")
    if len(key) != 32 or len(package_id) != 16:
        raise ValueError("invalid content key or package id length")
    nonce = nonce or secrets.token_bytes(12)
    if len(nonce) != 12:
        raise ValueError("nonce must be exactly 12 bytes")
    header = HEADER.pack(MAGIC, FORMAT_VERSION, TYPE_IDS[asset_type], 0, len(plaintext), package_id, nonce)
    return header + AESGCM(key).encrypt(nonce, plaintext, build_aad(header, resource, relative_path))


def unpack_asset(container: bytes, key: bytes, package_id: bytes, resource: str, relative_path: str) -> tuple[str, bytes]:
    relative_path = canonical_relative_path(relative_path)
    if len(container) < HEADER.size + TAG_SIZE:
        raise ValueError("container is truncated")
    magic, version, type_id, reserved, plaintext_size, embedded_package, nonce = HEADER.unpack_from(container)
    if magic != MAGIC or version != FORMAT_VERSION or type_id not in ID_TYPES or reserved != 0:
        raise ValueError("invalid or unsupported container header")
    if plaintext_size < 1 or plaintext_size > MAX_PLAINTEXT_SIZE or len(container) != HEADER.size + plaintext_size + TAG_SIZE:
        raise ValueError("invalid container length")
    if embedded_package != package_id:
        raise ValueError("package id mismatch")
    header = container[: HEADER.size]
    plaintext = AESGCM(key).decrypt(nonce, container[HEADER.size :], build_aad(header, resource, relative_path))
    if len(plaintext) != plaintext_size:
        raise ValueError("plaintext length mismatch")
    return ID_TYPES[type_id], plaintext


def atomic_write(path: Path, data: bytes, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{secrets.token_hex(8)}.tmp")
    try:
        temporary.write_bytes(data)
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def command_keygen(args: argparse.Namespace) -> None:
    key_path = Path(args.key_file)
    if key_path.exists() and not args.force:
        raise ValueError(f"refusing to overwrite existing key file: {key_path}")
    key = secrets.token_bytes(32)
    package_id = secrets.token_bytes(16)
    atomic_write(key_path, key.hex().encode("ascii") + b"\n", 0o600)
    print(f"package={package_id.hex()}")
    print(f"keyfile={key_path}")


def command_pack(args: argparse.Namespace) -> None:
    source = Path(args.input)
    plaintext = source.read_bytes()
    key = read_hex(Path(args.key_file), 32, "content key")
    package_id = bytes.fromhex(args.package)
    if len(package_id) != 16:
        raise ValueError("package id must contain exactly 32 hexadecimal characters")
    output = pack_asset(plaintext, key, package_id, args.resource, args.path, args.type)
    atomic_write(Path(args.output), output)
    print(f"packed {source} -> {args.output} ({args.type}, {len(plaintext)} plaintext bytes)")


def command_verify(args: argparse.Namespace) -> None:
    key = read_hex(Path(args.key_file), 32, "content key")
    package_id = bytes.fromhex(args.package)
    if len(package_id) != 16:
        raise ValueError("package id must contain exactly 32 hexadecimal characters")
    asset_type, plaintext = unpack_asset(Path(args.input).read_bytes(), key, package_id, args.resource, args.path)
    if args.output:
        atomic_write(Path(args.output), plaintext)
    print(f"verified {args.input} ({asset_type}, {len(plaintext)} plaintext bytes)")


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    keygen = subparsers.add_parser("keygen", help="generate a server-only content key and package id")
    keygen.add_argument("--key-file", required=True)
    keygen.add_argument("--force", action="store_true")
    keygen.set_defaults(handler=command_keygen)

    for name, handler in (("pack", command_pack), ("verify", command_verify)):
        command = subparsers.add_parser(name)
        command.add_argument("--key-file", required=True)
        command.add_argument("--package", required=True)
        command.add_argument("--resource", required=True)
        command.add_argument("--path", required=True, help="canonical destination path inside the resource")
        command.add_argument("--input", required=True)
        if name == "pack":
            command.add_argument("--type", choices=sorted(TYPE_IDS), required=True)
            command.add_argument("--output", required=True)
        else:
            command.add_argument("--output", help="optional plaintext output for offline verification only")
        command.set_defaults(handler=handler)
    return parser


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    try:
        args.handler(args)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
