#!/usr/bin/env python3
"""Build and validate the immutable four-pack static-world-v3 set envelope."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any

from native_world_cache import content_id
from native_world_manifest import STATIC_WORLD_V3_POLICY, parse_runtime_manifest


FORMAT = 3
POLICY = "static-world-v3-set"
SET_ID_DOMAIN = "mta-native-world-static-world-v3-set-v1"
CANONICAL_PACK_ORDER = ("bullworth", "vice-city", "liberty-city", "carcer-city")
ROOT_KEYS = {"format", "policy", "set_id", "packs"}
PACK_KEYS = {"pack_id", "content_id"}
SHA256 = re.compile(r"^[0-9a-f]{64}$")
MAX_ENVELOPE_BYTES = 16 * 1024


def _object_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def calculate_set_id(packs: list[dict[str, str]]) -> str:
    if not isinstance(packs, list) or len(packs) != len(CANONICAL_PACK_ORDER):
        raise ValueError("static-world-v3-set must contain exactly four packs")
    identity = [SET_ID_DOMAIN, f"format={FORMAT}", f"policy={POLICY}"]
    for index, (expected_pack_id, pack) in enumerate(zip(CANONICAL_PACK_ORDER, packs, strict=True)):
        if not isinstance(pack, dict) or set(pack) != PACK_KEYS:
            raise ValueError(f"packs[{index}] has a non-exact schema")
        pack_id = pack.get("pack_id")
        content_id_value = pack.get("content_id")
        if pack_id != expected_pack_id or not isinstance(content_id_value, str) or not SHA256.fullmatch(content_id_value):
            raise ValueError(f"packs[{index}] is outside the canonical identity order")
        identity.extend(
            (
                f"pack[{index}].pack_id={pack_id}",
                f"pack[{index}].content_id={content_id_value}",
            )
        )
    return hashlib.sha256(("\n".join(identity) + "\n").encode("ascii")).hexdigest()


def validate_set_envelope(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != ROOT_KEYS:
        raise ValueError(f"static-world-v3-set root must contain exactly {sorted(ROOT_KEYS)}")
    if type(value["format"]) is not int or value["format"] != FORMAT or value["policy"] != POLICY:
        raise ValueError("static-world-v3-set format or policy is invalid")
    if not isinstance(value["set_id"], str) or not SHA256.fullmatch(value["set_id"]):
        raise ValueError("static-world-v3-set set_id is invalid")
    calculated = calculate_set_id(value["packs"])
    if value["set_id"] != calculated:
        raise ValueError("static-world-v3-set set_id differs from its ordered pack identities")
    return value


def parse_set_envelope(text: str) -> dict[str, Any]:
    try:
        encoded = text.encode("ascii")
    except UnicodeEncodeError as error:
        raise ValueError("static-world-v3-set must be ASCII") from error
    if not 0 < len(encoded) <= MAX_ENVELOPE_BYTES:
        raise ValueError("static-world-v3-set exceeds its byte policy")
    return validate_set_envelope(json.loads(text, object_pairs_hook=_object_pairs))


def canonical_set_envelope_bytes(value: dict[str, Any]) -> bytes:
    envelope = validate_set_envelope(value)
    canonical = {
        "format": envelope["format"],
        "policy": envelope["policy"],
        "set_id": envelope["set_id"],
        "packs": [
            {"pack_id": pack["pack_id"], "content_id": pack["content_id"]}
            for pack in envelope["packs"]
        ],
    }
    return (json.dumps(canonical, indent=2, ensure_ascii=True) + "\n").encode("ascii")


def build_set_envelope(pack_directories: list[Path]) -> dict[str, Any]:
    if len(pack_directories) != len(CANONICAL_PACK_ORDER):
        raise ValueError("exactly four canonical v3 pack directories are required")
    packs: list[dict[str, str]] = []
    for expected_pack_id, directory in zip(CANONICAL_PACK_ORDER, pack_directories, strict=True):
        manifest_path = directory / "native-world.json"
        manifest = parse_runtime_manifest(manifest_path.read_text(encoding="ascii"))
        if manifest["format"] != FORMAT or manifest["policy"] != STATIC_WORLD_V3_POLICY or manifest["pack_id"] != expected_pack_id:
            raise ValueError(f"{directory} is not the canonical {expected_pack_id} v3 pack")
        packs.append(
            {
                "pack_id": expected_pack_id,
                "content_id": content_id(manifest, STATIC_WORLD_V3_POLICY),
            }
        )
    envelope = {
        "format": FORMAT,
        "policy": POLICY,
        "set_id": calculate_set_id(packs),
        "packs": packs,
    }
    return validate_set_envelope(envelope)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--pack",
        type=Path,
        action="append",
        required=True,
        help="v3 pack directory, repeated in bullworth/vice-city/liberty-city/carcer-city order",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--verify", type=Path)
    args = parser.parse_args()

    envelope = build_set_envelope([path.resolve() for path in args.pack])
    encoded = canonical_set_envelope_bytes(envelope)
    if args.verify and args.verify.read_bytes() != encoded:
        raise SystemExit(f"static-world-v3-set differs from {args.verify}")
    args.output.write_bytes(encoded)
    print(f"static-world-v3-set OK: setId={envelope['set_id']} packs={len(envelope['packs'])}")


if __name__ == "__main__":
    main()
