#!/usr/bin/env python3
"""Create and strictly validate minimal static-world runtime manifests."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from typing import Any


FORMAT_1_ROOT_KEYS = {"format", "pack_id", "files"}
FORMAT_2_ROOT_KEYS = {"format", "policy", "pack_id", "files"}
FORMAT_3_ROOT_KEYS = {"format", "policy", "pack_id", "files"}
STATIC_WORLD_V1_POLICY = "static-world-v1"
STATIC_WORLD_V3_POLICY = "static-world-v3"
LEAF = re.compile(r"^[a-z0-9_.-]+$")
IDENTIFIER = re.compile(r"^[a-z0-9_-]{1,15}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
MAX_IDE_BYTES = 1_048_576
MAX_IMG_BYTES = 131_072 * 2048
MAX_V3_IDE_BYTES = 8 * 1024 * 1024
MAX_V3_LOD_BYTES = 256 * 1024
MAX_V3_IMG_BYTES = 256 * 1024 * 1024
MAX_V3_IMAGES = 32
MAX_V3_TOTAL_BYTES = 8 * 1024 * 1024 * 1024
MAX_MANIFEST_BYTES = 64 * 1024


def _exact(value: Any, keys: set[str], context: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != keys:
        raise ValueError(f"{context} must contain exactly {sorted(keys)}")
    return value


def _file(value: Any, context: str, maximum_bytes: int) -> dict[str, Any]:
    item = _exact(value, {"name", "bytes", "sha256"}, context)
    if (
        not isinstance(item["name"], str)
        or item["name"] in {".", ".."}
        or not 0 < len(item["name"]) <= 63
        or not LEAF.fullmatch(item["name"])
    ):
        raise ValueError(f"{context}.name must be a safe lowercase leaf filename")
    if type(item["bytes"]) is not int or not 0 < item["bytes"] <= maximum_bytes:
        raise ValueError(f"{context}.bytes exceeds trusted policy")
    if not isinstance(item["sha256"], str) or not SHA256.fullmatch(item["sha256"]):
        raise ValueError(f"{context}.sha256 is invalid")
    return item


def validate_runtime_manifest(value: Any) -> dict[str, Any]:
    """Apply the same minimal closed schema enforced by the C++ runtime."""

    if not isinstance(value, dict) or type(value.get("format")) is not int:
        raise ValueError("format is invalid")
    if value["format"] == 1:
        root = _exact(value, FORMAT_1_ROOT_KEYS, "root")
    elif value["format"] == 2:
        root = _exact(value, FORMAT_2_ROOT_KEYS, "root")
        if root["policy"] != STATIC_WORLD_V1_POLICY:
            raise ValueError(f"policy must be {STATIC_WORLD_V1_POLICY}")
    elif value["format"] == 3:
        root = _exact(value, FORMAT_3_ROOT_KEYS, "root")
        if root["policy"] != STATIC_WORLD_V3_POLICY:
            raise ValueError(f"policy must be {STATIC_WORLD_V3_POLICY}")
    else:
        raise ValueError("format must be 1, 2, or 3")
    if not isinstance(root["pack_id"], str) or not IDENTIFIER.fullmatch(root["pack_id"]):
        raise ValueError("pack_id is invalid")
    if value["format"] == 1 and root["pack_id"] != "bullworth":
        raise ValueError("format 1 pack_id must be bullworth")
    if value["format"] == 3:
        files = _exact(root["files"], {"ide", "lod", "images"}, "files")
        ide = _file(files["ide"], "files.ide", MAX_V3_IDE_BYTES)
        lod = _file(files["lod"], "files.lod", MAX_V3_LOD_BYTES)
        if ide["name"] != "world.ide" or lod["name"] != "world.lod":
            raise ValueError("format 3 IDE/LOD filenames are not canonical")
        images = files["images"]
        if not isinstance(images, list) or not 1 <= len(images) <= MAX_V3_IMAGES:
            raise ValueError("files.images must be a bounded non-empty array")
        names: set[str] = {ide["name"], lod["name"]}
        total_bytes = ide["bytes"] + lod["bytes"]
        for index, image_value in enumerate(images):
            image = _file(image_value, f"files.images[{index}]", MAX_V3_IMG_BYTES)
            if image["name"] != f"w{index:03d}.img":
                raise ValueError("files.images must use contiguous canonical wNNN.img names")
            if image["bytes"] % 2048:
                raise ValueError(f"files.images[{index}].bytes must be sector aligned")
            if image["name"] in names:
                raise ValueError("files contains duplicate filenames")
            names.add(image["name"])
            total_bytes += image["bytes"]
        if total_bytes > MAX_V3_TOTAL_BYTES:
            raise ValueError("format 3 payload exceeds the compiled total-byte policy")
    else:
        files = _exact(root["files"], {"ide", "img"}, "files")
        _file(files["ide"], "files.ide", MAX_IDE_BYTES)
        img = _file(files["img"], "files.img", MAX_IMG_BYTES)
        if img["bytes"] % 2048:
            raise ValueError("files.img.bytes must be sector aligned")
    return root


def parse_runtime_manifest(text: str) -> dict[str, Any]:
    """Parse JSON while rejecting duplicate keys, trailing data, and non-ASCII."""

    encoded = text.encode("ascii")
    if not 0 < len(encoded) <= MAX_MANIFEST_BYTES:
        raise ValueError("manifest byte length exceeds trusted policy")
    if re.search(r'\\(?!["\\/])', text):
        raise ValueError("manifest uses an unsupported JSON string escape")

    def object_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    result = validate_runtime_manifest(json.loads(text, object_pairs_hook=object_pairs))
    if result["format"] in (1, 2) and len(encoded) > 4096:
        raise ValueError("legacy manifest exceeds its closed 4096-byte policy")
    return result


def build_runtime_manifest(
    report: dict[str, Any],
    ide_path: Path,
    img_path: Path | None = None,
    *,
    img_paths: list[Path] | None = None,
    format_version: int = 1,
    policy: str | None = None,
    pack_id: str = "bullworth",
) -> dict[str, Any]:
    """Describe only payload identity; inventories are derived from bytes at runtime."""

    del report  # Round-trip validation must finish before this function is called.
    ide = {
        "name": ide_path.name,
        "bytes": ide_path.stat().st_size,
        "sha256": _sha256(ide_path),
    }
    if format_version == 3:
        if img_path is not None or not img_paths:
            raise ValueError("format 3 requires img_paths and no single img_path")
        lod_path = ide_path.with_name("world.lod")
        if not lod_path.is_file():
            raise ValueError("format 3 requires a world.lod sidecar beside world.ide")
        files = {
            "ide": ide,
            "lod": {
                "name": lod_path.name,
                "bytes": lod_path.stat().st_size,
                "sha256": _sha256(lod_path),
            },
            "images": [
                {
                    "name": path.name,
                    "bytes": path.stat().st_size,
                    "sha256": _sha256(path),
                }
                for path in img_paths
            ],
        }
    else:
        if img_path is None or img_paths is not None:
            raise ValueError("formats 1 and 2 require exactly one img_path")
        files = {
            "ide": ide,
            "img": {
                "name": img_path.name,
                "bytes": img_path.stat().st_size,
                "sha256": _sha256(img_path),
            },
        }
    if format_version in (2, 3):
        manifest = {
            "format": format_version,
            "policy": policy,
            "pack_id": pack_id,
            "files": files,
        }
    else:
        manifest = {
            "format": format_version,
            "pack_id": pack_id,
            "files": files,
        }
    if format_version == 1 and policy is not None:
        raise ValueError("format 1 does not carry a policy field")
    return validate_runtime_manifest(manifest)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def dump_runtime_manifest(path: Path, manifest: dict[str, Any]) -> None:
    validate_runtime_manifest(manifest)
    path.write_text(json.dumps(manifest, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
