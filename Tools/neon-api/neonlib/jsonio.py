from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any


MAX_JSON_BYTES = 16 * 1024 * 1024


class JsonDocumentError(ValueError):
    pass


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise JsonDocumentError(f"duplicate object key: {key}")
        result[key] = value
    return result


def _reject_nonfinite(value: str) -> None:
    raise JsonDocumentError(f"non-finite JSON number is forbidden: {value}")


def load_json(path: Path, *, max_bytes: int = MAX_JSON_BYTES) -> Any:
    try:
        size = path.stat().st_size
    except OSError as exc:
        raise JsonDocumentError(str(exc)) from exc
    if size > max_bytes:
        raise JsonDocumentError(f"JSON document exceeds {max_bytes} bytes")
    try:
        payload = path.read_bytes()
    except OSError as exc:
        raise JsonDocumentError(str(exc)) from exc
    return parse_json_bytes(payload, max_bytes=max_bytes)


def parse_json_bytes(payload: bytes, *, max_bytes: int = MAX_JSON_BYTES) -> Any:
    if len(payload) > max_bytes:
        raise JsonDocumentError(f"JSON document exceeds {max_bytes} bytes")
    try:
        text = payload.decode("utf-8")
    except UnicodeError as exc:
        raise JsonDocumentError(str(exc)) from exc
    try:
        return json.loads(text, object_pairs_hook=_reject_duplicate_keys, parse_constant=_reject_nonfinite)
    except JsonDocumentError:
        raise
    except json.JSONDecodeError as exc:
        raise JsonDocumentError(f"line {exc.lineno}, column {exc.colno}: {exc.msg}") from exc


def canonical_json(document: Any) -> str:
    return json.dumps(document, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n"


def pretty_json(document: Any) -> str:
    return json.dumps(document, ensure_ascii=False, sort_keys=True, indent=2) + "\n"


def write_json(path: Path, document: Any, *, pretty: bool = True) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(pretty_json(document) if pretty else canonical_json(document), encoding="utf-8", newline="\n")


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()
