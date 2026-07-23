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

from native_world_manifest import STATIC_WORLD_V1_POLICY, STATIC_WORLD_V3_POLICY, parse_runtime_manifest, validate_runtime_manifest


CONTENT_ID_DOMAIN_V1 = "mta-native-world-cache-content-v1"
CONTENT_ID_DOMAIN_V2 = "mta-native-world-cache-content-v2"
CONTENT_ID_DOMAIN_V3 = "mta-native-world-cache-content-v3"
CACHE_FORMAT_DIRECTORY_V1 = "v1"
CACHE_FORMAT_DIRECTORY_V2 = "v2"
CACHE_FORMAT_DIRECTORY_V3 = "v3"
CACHED_MANIFEST_FILE = "native-world.json"
CACHED_IDE_FILE = "world.ide"
CACHED_LOD_FILE = "world.lod"
CACHED_IMG_FILE = "world.img"


def canonical_manifest_bytes(manifest: dict[str, Any], policy_key: str) -> bytes:
    manifest = validate_runtime_manifest(manifest)
    if manifest["format"] == 1:
        if manifest["pack_id"] != policy_key:
            raise ValueError("manifest pack_id differs from compiled policy key")
    elif manifest["policy"] != policy_key:
        raise ValueError("manifest policy differs from compiled policy key")
    files: dict[str, Any] = {
        "ide": {
            "name": CACHED_IDE_FILE,
            "bytes": manifest["files"]["ide"]["bytes"],
            "sha256": manifest["files"]["ide"]["sha256"],
        }
    }
    if manifest["format"] == 3:
        files["lod"] = {
            "name": CACHED_LOD_FILE,
            "bytes": manifest["files"]["lod"]["bytes"],
            "sha256": manifest["files"]["lod"]["sha256"],
        }
        files["images"] = [
            {
                "name": image["name"],
                "bytes": image["bytes"],
                "sha256": image["sha256"],
            }
            for image in manifest["files"]["images"]
        ]
    else:
        files["img"] = {
            "name": CACHED_IMG_FILE,
            "bytes": manifest["files"]["img"]["bytes"],
            "sha256": manifest["files"]["img"]["sha256"],
        }
    if manifest["format"] in (2, 3):
        canonical = {
            "format": manifest["format"],
            "policy": manifest["policy"],
            "pack_id": manifest["pack_id"],
            "files": files,
        }
    else:
        canonical = {
            "format": manifest["format"],
            "pack_id": manifest["pack_id"],
            "files": files,
        }
    return (json.dumps(canonical, indent=2, ensure_ascii=True) + "\n").encode("ascii")


def content_id(manifest: dict[str, Any], policy_key: str) -> str:
    manifest = validate_runtime_manifest(manifest)
    if manifest["format"] == 1:
        if manifest["pack_id"] != policy_key:
            raise ValueError("manifest pack_id differs from compiled policy key")
        domain = CONTENT_ID_DOMAIN_V1
        identity_fields = f"policy={policy_key}\n"
    else:
        if manifest["policy"] != policy_key:
            raise ValueError("manifest policy differs from compiled policy key")
        domain = CONTENT_ID_DOMAIN_V2 if manifest["format"] == 2 else CONTENT_ID_DOMAIN_V3
        identity_fields = f"policy={manifest['policy']}\npack_id={manifest['pack_id']}\n"
    ide = manifest["files"]["ide"]
    identity = (
        f"{domain}\n"
        f"format={manifest['format']}\n"
        f"{identity_fields}"
        f"ide.bytes={ide['bytes']}\n"
        f"ide.sha256={ide['sha256']}\n"
    )
    if manifest["format"] == 3:
        lod = manifest["files"]["lod"]
        identity += f"lod.bytes={lod['bytes']}\nlod.sha256={lod['sha256']}\n"
        for image in manifest["files"]["images"]:
            identity += f"img.name={image['name']}\nimg.bytes={image['bytes']}\nimg.sha256={image['sha256']}\n"
    else:
        img = manifest["files"]["img"]
        identity += f"img.bytes={img['bytes']}\nimg.sha256={img['sha256']}\n"
    identity = identity.encode("ascii")
    return hashlib.sha256(identity).hexdigest()


def _cache_parent(cache_root: Path, manifest: dict[str, Any], policy_key: str) -> Path:
    if manifest["format"] == 1:
        return cache_root / CACHE_FORMAT_DIRECTORY_V1 / policy_key
    expected_policy = STATIC_WORLD_V1_POLICY if manifest["format"] == 2 else STATIC_WORLD_V3_POLICY
    if policy_key != expected_policy:
        raise ValueError(f"format {manifest['format']} cache policy is unsupported")
    format_directory = CACHE_FORMAT_DIRECTORY_V2 if manifest["format"] == 2 else CACHE_FORMAT_DIRECTORY_V3
    return cache_root / format_directory / policy_key


def _validate_file(path: Path, expected: dict[str, Any]) -> None:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"cache file is missing or not regular: {path.name}")
    if path.stat().st_size != expected["bytes"]:
        raise ValueError(f"cache file length mismatch: {path.name}")
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected["sha256"]:
        raise ValueError(f"cache file hash mismatch: {path.name}")


def validate_cache_object(directory: Path, manifest: dict[str, Any], policy_key: str) -> None:
    canonical = canonical_manifest_bytes(manifest, policy_key)
    expected_files = {CACHED_MANIFEST_FILE, CACHED_IDE_FILE}
    if manifest["format"] == 3:
        expected_files.add(CACHED_LOD_FILE)
        expected_files.update(image["name"] for image in manifest["files"]["images"])
    else:
        expected_files.add(CACHED_IMG_FILE)
    if directory.is_symlink() or not directory.is_dir() or {path.name for path in directory.iterdir()} != expected_files:
        raise ValueError("cache object is not the exact closed file directory")
    manifest_path = directory / CACHED_MANIFEST_FILE
    if manifest_path.is_symlink() or manifest_path.read_bytes() != canonical:
        raise ValueError("cached manifest is not the canonical semantic manifest")
    cached_ide = dict(manifest["files"]["ide"], name=CACHED_IDE_FILE)
    _validate_file(directory / CACHED_IDE_FILE, cached_ide)
    if manifest["format"] == 3:
        cached_lod = dict(manifest["files"]["lod"], name=CACHED_LOD_FILE)
        _validate_file(directory / CACHED_LOD_FILE, cached_lod)
        for image in manifest["files"]["images"]:
            _validate_file(directory / image["name"], image)
    else:
        cached_img = dict(manifest["files"]["img"], name=CACHED_IMG_FILE)
        _validate_file(directory / CACHED_IMG_FILE, cached_img)


def open_existing_cache(cache_root: Path, manifest: dict[str, Any], policy_key: str, expected_content_id: str) -> Path:
    """Reference model for the non-repairing Checkpoint-B lookup."""
    identity = content_id(manifest, policy_key)
    if identity != expected_content_id:
        raise ValueError("authorized content ID differs from the semantic manifest")
    directory = _cache_parent(cache_root, manifest, policy_key) / identity
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError("exact cache object is missing or unsafe")
    validate_cache_object(directory, manifest, policy_key)
    return directory


def publish_local_seed(seed_directory: Path, cache_root: Path, policy_key: str) -> tuple[Path, str]:
    """Publish or repair a local seed using the runtime's semantic layout."""

    source_manifest_path = seed_directory / "native-world.json"
    source_bytes = source_manifest_path.read_bytes()
    manifest = parse_runtime_manifest(source_bytes.decode("ascii"))
    identity = content_id(manifest, policy_key)
    parent = _cache_parent(cache_root, manifest, policy_key)
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
        if manifest["format"] == 3:
            shutil.copyfile(seed_directory / manifest["files"]["lod"]["name"], quarantine / CACHED_LOD_FILE)
            for image in manifest["files"]["images"]:
                shutil.copyfile(seed_directory / image["name"], quarantine / image["name"])
        else:
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
