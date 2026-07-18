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
STATIC_WORLD_V1_POLICY = "static-world-v1"
LEAF = re.compile(r"^[a-z0-9_.-]+$")
IDENTIFIER = re.compile(r"^[a-z0-9_-]{1,15}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
MAX_IDE_BYTES = 1_048_576
MAX_IMG_BYTES = 131_072 * 2048
MAX_MANIFEST_BYTES = 4096


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
    else:
        raise ValueError("format must be 1 or 2")
    if not isinstance(root["pack_id"], str) or not IDENTIFIER.fullmatch(root["pack_id"]):
        raise ValueError("pack_id is invalid")
    if value["format"] == 1 and root["pack_id"] != "bullworth":
        raise ValueError("format 1 pack_id must be bullworth")
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

    return validate_runtime_manifest(json.loads(text, object_pairs_hook=object_pairs))


def build_runtime_manifest(
    report: dict[str, Any],
    ide_path: Path,
    img_path: Path,
    *,
    format_version: int = 1,
    policy: str | None = None,
    pack_id: str = "bullworth",
) -> dict[str, Any]:
    """Describe only payload identity; inventories are derived from bytes at runtime."""

    del report  # Round-trip validation must finish before this function is called.
    files = {
        "ide": {
            "name": ide_path.name,
            "bytes": ide_path.stat().st_size,
            "sha256": hashlib.sha256(ide_path.read_bytes()).hexdigest(),
        },
        "img": {
            "name": img_path.name,
            "bytes": img_path.stat().st_size,
            "sha256": hashlib.sha256(img_path.read_bytes()).hexdigest(),
        },
    }
    if format_version == 2:
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
    if format_version != 2 and policy is not None:
        raise ValueError("format 1 does not carry a policy field")
    return validate_runtime_manifest(manifest)


def dump_runtime_manifest(path: Path, manifest: dict[str, Any]) -> None:
    validate_runtime_manifest(manifest)
    path.write_text(json.dumps(manifest, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
