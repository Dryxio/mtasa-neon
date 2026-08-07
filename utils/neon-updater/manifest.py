#!/usr/bin/env python3
"""Create and verify signed MTA Neon update manifests.

The Ed25519 signature covers the UTF-8 bytes of this canonical payload, with
LF line endings and the final LF included::

    mta-neon-update-v1
    build=<decimal build>
    display_version=<YYYY.MM.DD.build>
    technical_version=<MTA sortable version>
    release_tag=<GitHub release tag>
    asset_name=<installer filename>
    asset_url=<tag-specific GitHub release URL>
    asset_size=<decimal bytes>
    asset_sha256=<lowercase hexadecimal SHA-256>
    key_id=<signing key identifier>

All values are validated before either signing or verification. The XML is a
transport envelope; consumers must reconstruct and verify the canonical
payload before trusting any field.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import datetime
import hashlib
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


SCHEMA_VERSION = "1"
CANONICAL_PREFIX = "mta-neon-update-v1"
SIGNATURE_ALGORITHM = "ed25519"
SIGNATURE_ENCODING = "base64"
ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")

REPOSITORY_RE = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
TAG_RE = re.compile(r"neon-(\d{4})\.(\d{2})\.(\d{2})\.([1-9]\d{0,4})")
DISPLAY_VERSION_RE = re.compile(r"\d{4}\.\d{2}\.\d{2}\.[1-9]\d{0,4}")
TECHNICAL_VERSION_RE = re.compile(r"1\.7\.0-5\.\d{5}")
ASSET_NAME_RE = re.compile(r"MTA-Neon-Setup\.exe")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
KEY_ID_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}")
DECIMAL_RE = re.compile(r"[1-9]\d*")

MIN_INSTALLER_SIZE = 16 * 1024 * 1024
MAX_INSTALLER_SIZE = 512 * 1024 * 1024


class ManifestError(ValueError):
    pass


@dataclass(frozen=True)
class ManifestFields:
    build: int
    display_version: str
    technical_version: str
    release_tag: str
    asset_name: str
    asset_url: str
    asset_size: int
    asset_sha256: str
    key_id: str

    def validate(self, repository: str) -> None:
        if not REPOSITORY_RE.fullmatch(repository):
            raise ManifestError("repository must have the form owner/name")
        if not 1 <= self.build <= 99999:
            raise ManifestError("build must be between 1 and 99999")
        if not DISPLAY_VERSION_RE.fullmatch(self.display_version):
            raise ManifestError("display_version must have the form YYYY.MM.DD.build")
        if not TECHNICAL_VERSION_RE.fullmatch(self.technical_version):
            raise ManifestError("technical_version must have the form 1.7.0-5.00000")
        if not TAG_RE.fullmatch(self.release_tag):
            raise ManifestError("release_tag must have the form neon-YYYY.MM.DD.build")
        if not ASSET_NAME_RE.fullmatch(self.asset_name):
            raise ManifestError("unexpected installer asset name")
        if not MIN_INSTALLER_SIZE <= self.asset_size <= MAX_INSTALLER_SIZE:
            raise ManifestError("installer size is outside the 16 MiB to 512 MiB safety bound")
        if not SHA256_RE.fullmatch(self.asset_sha256):
            raise ManifestError("asset_sha256 must be lowercase hexadecimal SHA-256")
        if not KEY_ID_RE.fullmatch(self.key_id):
            raise ManifestError("invalid signing key identifier")

        tag_match = TAG_RE.fullmatch(self.release_tag)
        assert tag_match is not None
        try:
            datetime.date(int(tag_match.group(1)), int(tag_match.group(2)), int(tag_match.group(3)))
        except ValueError as exc:
            raise ManifestError("release_tag contains an invalid calendar date") from exc
        tag_display_version = ".".join(tag_match.groups())
        if tag_display_version != self.display_version:
            raise ManifestError("release_tag and display_version do not identify the same release")
        if int(tag_match.group(4)) != self.build:
            raise ManifestError("release_tag build does not match build")
        if int(self.technical_version.rsplit(".", 1)[1]) != self.build:
            raise ManifestError("technical_version build does not match build")

        expected_url = f"https://github.com/{repository}/releases/download/{self.release_tag}/{self.asset_name}"
        if self.asset_url != expected_url:
            raise ManifestError("asset_url is not the exact tag-specific GitHub release URL")

    def canonical_payload(self) -> bytes:
        values = (
            ("build", str(self.build)),
            ("display_version", self.display_version),
            ("technical_version", self.technical_version),
            ("release_tag", self.release_tag),
            ("asset_name", self.asset_name),
            ("asset_url", self.asset_url),
            ("asset_size", str(self.asset_size)),
            ("asset_sha256", self.asset_sha256),
            ("key_id", self.key_id),
        )
        return (CANONICAL_PREFIX + "\n" + "".join(f"{name}={value}\n" for name, value in values)).encode("utf-8")


def run_openssl(arguments: list[str], *, input_data: bytes | None = None) -> bytes:
    try:
        result = subprocess.run(
            ["openssl", *arguments],
            input=input_data,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except FileNotFoundError as exc:
        raise ManifestError("openssl is required to sign or verify update manifests") from exc

    if result.returncode != 0:
        diagnostic = result.stderr.decode("utf-8", errors="replace").strip()
        raise ManifestError(f"openssl failed: {diagnostic or 'unknown error'}")
    return result.stdout


def decode_public_key_base64url(value: str) -> bytes:
    if not re.fullmatch(r"[A-Za-z0-9_-]{43}", value):
        raise ManifestError("Ed25519 public key must be unpadded base64url encoding of 32 bytes")
    try:
        decoded = base64.urlsafe_b64decode(value + "=")
    except (ValueError, binascii.Error) as exc:
        raise ManifestError("invalid base64url Ed25519 public key") from exc
    if len(decoded) != 32:
        raise ManifestError("Ed25519 public key must contain exactly 32 bytes")
    return decoded


def derive_public_key(private_key: Path) -> bytes:
    public_der = run_openssl(["pkey", "-in", os.fspath(private_key), "-pubout", "-outform", "DER"])
    if not public_der.startswith(ED25519_SPKI_PREFIX) or len(public_der) != len(ED25519_SPKI_PREFIX) + 32:
        raise ManifestError("signing key is not an Ed25519 private key")
    return public_der[len(ED25519_SPKI_PREFIX) :]


def sign_payload(private_key: Path, payload: bytes) -> bytes:
    with tempfile.NamedTemporaryFile(prefix="neon-update-payload-", delete=False) as payload_file:
        payload_file.write(payload)
        payload_path = Path(payload_file.name)
    try:
        return run_openssl(["pkeyutl", "-sign", "-rawin", "-inkey", os.fspath(private_key), "-in", os.fspath(payload_path)])
    finally:
        payload_path.unlink(missing_ok=True)


def verify_signature(public_key: bytes, payload: bytes, signature: bytes) -> None:
    public_der = ED25519_SPKI_PREFIX + public_key
    with tempfile.TemporaryDirectory(prefix="neon-update-verify-") as temporary_directory:
        temporary_root = Path(temporary_directory)
        public_path = temporary_root / "public.der"
        payload_path = temporary_root / "payload"
        signature_path = temporary_root / "signature"
        public_path.write_bytes(public_der)
        payload_path.write_bytes(payload)
        signature_path.write_bytes(signature)
        run_openssl(
            [
                "pkeyutl",
                "-verify",
                "-rawin",
                "-pubin",
                "-keyform",
                "DER",
                "-inkey",
                os.fspath(public_path),
                "-in",
                os.fspath(payload_path),
                "-sigfile",
                os.fspath(signature_path),
            ]
        )


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def child_text(parent: ET.Element, path: str) -> str:
    child = parent.find(path)
    if child is None or child.text is None or not child.text:
        raise ManifestError(f"manifest is missing {path}")
    if child.attrib or len(child):
        raise ManifestError(f"manifest field {path} must be a plain leaf element")
    if child.text != child.text.strip():
        raise ManifestError(f"manifest field {path} contains surrounding whitespace")
    return child.text


def element_text(element: ET.Element, context: str) -> str:
    if element.text is None or not element.text:
        raise ManifestError(f"manifest is missing {context}")
    if len(element):
        raise ManifestError(f"manifest field {context} must not contain child elements")
    if element.text != element.text.strip():
        raise ManifestError(f"manifest field {context} contains surrounding whitespace")
    return element.text


def parse_manifest(path: Path, repository: str) -> tuple[ManifestFields, bytes]:
    if not path.is_file():
        raise ManifestError(f"manifest does not exist: {path}")
    if path.stat().st_size > 64 * 1024:
        raise ManifestError("manifest exceeds the 64 KiB safety bound")
    try:
        root = ET.fromstring(path.read_bytes())
    except ET.ParseError as exc:
        raise ManifestError(f"invalid manifest XML: {exc}") from exc
    if root.tag != "neon_update" or root.attrib != {"schema": SCHEMA_VERSION}:
        raise ManifestError("unexpected manifest root or schema")

    expected_children = ["build", "display_version", "technical_version", "release_tag", "asset", "signature"]
    if [child.tag for child in root] != expected_children:
        raise ManifestError("manifest fields are missing, duplicated, or out of order")
    asset = root.find("asset")
    signature_node = root.find("signature")
    assert asset is not None and signature_node is not None
    if asset.attrib or [child.tag for child in asset] != ["name", "url", "size", "sha256"]:
        raise ManifestError("unexpected asset fields")
    if signature_node.attrib.get("algorithm") != SIGNATURE_ALGORITHM or signature_node.attrib.get("encoding") != SIGNATURE_ENCODING:
        raise ManifestError("unsupported signature algorithm or encoding")
    if set(signature_node.attrib) != {"key_id", "algorithm", "encoding"}:
        raise ManifestError("unexpected signature attributes")

    build_text = child_text(root, "build")
    asset_size_text = child_text(asset, "size")
    if not DECIMAL_RE.fullmatch(build_text) or not DECIMAL_RE.fullmatch(asset_size_text):
        raise ManifestError("build and asset size must be canonical positive decimal integers")
    build = int(build_text, 10)
    asset_size = int(asset_size_text, 10)
    fields = ManifestFields(
        build=build,
        display_version=child_text(root, "display_version"),
        technical_version=child_text(root, "technical_version"),
        release_tag=child_text(root, "release_tag"),
        asset_name=child_text(asset, "name"),
        asset_url=child_text(asset, "url"),
        asset_size=asset_size,
        asset_sha256=child_text(asset, "sha256"),
        key_id=signature_node.attrib.get("key_id", ""),
    )
    fields.validate(repository)

    signature_text = element_text(signature_node, "signature")
    try:
        signature = base64.b64decode(signature_text, validate=True)
    except (ValueError, binascii.Error) as exc:
        raise ManifestError("signature is not strict standard base64") from exc
    if len(signature) != 64:
        raise ManifestError("Ed25519 signature must contain exactly 64 bytes")
    return fields, signature


def create_manifest(args: argparse.Namespace) -> None:
    asset_path = args.asset.resolve()
    private_key = args.private_key.resolve()
    if not asset_path.is_file():
        raise ManifestError(f"installer asset does not exist: {asset_path}")
    if not private_key.is_file():
        raise ManifestError(f"private key does not exist: {private_key}")

    asset_name = asset_path.name
    fields = ManifestFields(
        build=args.build,
        display_version=args.display_version,
        technical_version=args.technical_version,
        release_tag=args.release_tag,
        asset_name=asset_name,
        asset_url=f"https://github.com/{args.repository}/releases/download/{args.release_tag}/{asset_name}",
        asset_size=asset_path.stat().st_size,
        asset_sha256=file_sha256(asset_path),
        key_id=args.key_id,
    )
    fields.validate(args.repository)

    expected_public_key = decode_public_key_base64url(args.expected_public_key_base64url)
    actual_public_key = derive_public_key(private_key)
    if actual_public_key != expected_public_key:
        raise ManifestError("private signing key does not match the pinned Neon update public key")

    payload = fields.canonical_payload()
    signature = sign_payload(private_key, payload)
    if len(signature) != 64:
        raise ManifestError("openssl produced an invalid Ed25519 signature length")
    verify_signature(expected_public_key, payload, signature)

    root = ET.Element("neon_update", {"schema": SCHEMA_VERSION})
    ET.SubElement(root, "build").text = str(fields.build)
    ET.SubElement(root, "display_version").text = fields.display_version
    ET.SubElement(root, "technical_version").text = fields.technical_version
    ET.SubElement(root, "release_tag").text = fields.release_tag
    asset = ET.SubElement(root, "asset")
    ET.SubElement(asset, "name").text = fields.asset_name
    ET.SubElement(asset, "url").text = fields.asset_url
    ET.SubElement(asset, "size").text = str(fields.asset_size)
    ET.SubElement(asset, "sha256").text = fields.asset_sha256
    signature_node = ET.SubElement(
        root,
        "signature",
        {"key_id": fields.key_id, "algorithm": SIGNATURE_ALGORITHM, "encoding": SIGNATURE_ENCODING},
    )
    signature_node.text = base64.b64encode(signature).decode("ascii")
    ET.indent(root, space="  ")
    output = b'<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding="utf-8", short_empty_elements=False) + b"\n"

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary_output = args.output.with_name(args.output.name + ".tmp")
    temporary_output.write_bytes(output)
    os.replace(temporary_output, args.output)


def verify_manifest(args: argparse.Namespace) -> None:
    fields, signature = parse_manifest(args.manifest, args.repository)
    public_key = decode_public_key_base64url(args.public_key_base64url)
    verify_signature(public_key, fields.canonical_payload(), signature)
    if args.asset is not None:
        asset_path = args.asset.resolve()
        if not asset_path.is_file():
            raise ManifestError(f"installer asset does not exist: {asset_path}")
        if asset_path.name != fields.asset_name:
            raise ManifestError("installer asset name does not match manifest")
        if asset_path.stat().st_size != fields.asset_size:
            raise ManifestError("installer asset size does not match manifest")
        if file_sha256(asset_path) != fields.asset_sha256:
            raise ManifestError("installer asset SHA-256 does not match manifest")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create", help="create and sign a manifest")
    create.add_argument("--private-key", type=Path, required=True, help="base64-decoded Ed25519 PKCS#8 PEM private key")
    create.add_argument("--expected-public-key-base64url", required=True, help="pinned raw 32-byte Ed25519 public key")
    create.add_argument("--repository", required=True, help="GitHub owner/repository")
    create.add_argument("--build", type=int, required=True)
    create.add_argument("--display-version", required=True)
    create.add_argument("--technical-version", required=True)
    create.add_argument("--release-tag", required=True)
    create.add_argument("--key-id", required=True)
    create.add_argument("--asset", type=Path, required=True)
    create.add_argument("--output", type=Path, required=True)
    create.set_defaults(handler=create_manifest)

    verify = subparsers.add_parser("verify", help="verify a manifest and optional installer asset")
    verify.add_argument("--public-key-base64url", required=True, help="pinned raw 32-byte Ed25519 public key")
    verify.add_argument("--repository", required=True, help="GitHub owner/repository")
    verify.add_argument("--manifest", type=Path, required=True)
    verify.add_argument("--asset", type=Path)
    verify.set_defaults(handler=verify_manifest)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        args.handler(args)
    except ManifestError as exc:
        print(f"manifest error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
