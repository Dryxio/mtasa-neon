#!/usr/bin/env python3
"""Cross-platform reference tooling for the native-world cache contract."""

from __future__ import annotations

import hashlib
import json
import os
import secrets
import shutil
from pathlib import Path
from typing import Any

from native_world_manifest import parse_runtime_manifest, validate_runtime_manifest


CONTENT_ID_DOMAIN = "mta-native-world-cache-content-v1"
CACHE_FORMAT_DIRECTORY = "v1"
CACHED_MANIFEST_FILE = "native-world.json"
CACHED_IDE_FILE = "world.ide"
CACHED_IMG_FILE = "world.img"


def canonical_manifest_bytes(manifest: dict[str, Any], policy_key: str) -> bytes:
    manifest = validate_runtime_manifest(manifest)
    if manifest["pack_id"] != policy_key:
        raise ValueError("manifest pack_id differs from compiled policy key")
    canonical = {
        "format": manifest["format"],
        "pack_id": manifest["pack_id"],
        "files": {
            "ide": {
                "name": CACHED_IDE_FILE,
                "bytes": manifest["files"]["ide"]["bytes"],
                "sha256": manifest["files"]["ide"]["sha256"],
            },
            "img": {
                "name": CACHED_IMG_FILE,
                "bytes": manifest["files"]["img"]["bytes"],
                "sha256": manifest["files"]["img"]["sha256"],
            },
        },
    }
    return (json.dumps(canonical, indent=2, ensure_ascii=True) + "\n").encode("ascii")


def content_id(manifest: dict[str, Any], policy_key: str) -> str:
    manifest = validate_runtime_manifest(manifest)
    if manifest["pack_id"] != policy_key:
        raise ValueError("manifest pack_id differs from compiled policy key")
    ide = manifest["files"]["ide"]
    img = manifest["files"]["img"]
    identity = (
        f"{CONTENT_ID_DOMAIN}\n"
        f"format={manifest['format']}\n"
        f"policy={policy_key}\n"
        f"ide.bytes={ide['bytes']}\n"
        f"ide.sha256={ide['sha256']}\n"
        f"img.bytes={img['bytes']}\n"
        f"img.sha256={img['sha256']}\n"
    ).encode("ascii")
    return hashlib.sha256(identity).hexdigest()


def _validate_file(path: Path, expected: dict[str, Any]) -> None:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"cache file is missing or not regular: {path.name}")
    if path.stat().st_size != expected["bytes"]:
        raise ValueError(f"cache file length mismatch: {path.name}")
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected["sha256"]:
        raise ValueError(f"cache file hash mismatch: {path.name}")


def validate_cache_object(directory: Path, manifest: dict[str, Any], policy_key: str) -> None:
    canonical = canonical_manifest_bytes(manifest, policy_key)
    manifest_path = directory / CACHED_MANIFEST_FILE
    if manifest_path.is_symlink() or manifest_path.read_bytes() != canonical:
        raise ValueError("cached manifest is not the canonical semantic manifest")
    cached_ide = dict(manifest["files"]["ide"], name=CACHED_IDE_FILE)
    cached_img = dict(manifest["files"]["img"], name=CACHED_IMG_FILE)
    _validate_file(directory / CACHED_IDE_FILE, cached_ide)
    _validate_file(directory / CACHED_IMG_FILE, cached_img)


def publish_local_seed(seed_directory: Path, cache_root: Path, policy_key: str) -> tuple[Path, str]:
    """Publish or repair a local seed using the runtime's semantic layout."""

    source_manifest_path = seed_directory / "native-world.json"
    source_bytes = source_manifest_path.read_bytes()
    manifest = parse_runtime_manifest(source_bytes.decode("ascii"))
    identity = content_id(manifest, policy_key)
    parent = cache_root / CACHE_FORMAT_DIRECTORY / policy_key
    final = parent / identity
    parent.mkdir(parents=True, exist_ok=True)

    if final.exists():
        try:
            validate_cache_object(final, manifest, policy_key)
            return final, "hit"
        except ValueError:
            invalid = parent / f".{identity}.invalid.{secrets.token_hex(16)}"
            final.rename(invalid)
            shutil.rmtree(invalid)

    quarantine = parent / f".{identity}.quarantine.{secrets.token_hex(16)}"
    quarantine.mkdir()
    try:
        manifest_path = quarantine / CACHED_MANIFEST_FILE
        manifest_path.write_bytes(canonical_manifest_bytes(manifest, policy_key))
        shutil.copyfile(seed_directory / manifest["files"]["ide"]["name"], quarantine / CACHED_IDE_FILE)
        shutil.copyfile(seed_directory / manifest["files"]["img"]["name"], quarantine / CACHED_IMG_FILE)
        validate_cache_object(quarantine, manifest, policy_key)
        for path in quarantine.iterdir():
            with path.open("rb") as file:
                os.fsync(file.fileno())
        quarantine.rename(final)
    finally:
        if quarantine.exists():
            shutil.rmtree(quarantine)
    validate_cache_object(final, manifest, policy_key)
    return final, "published"
