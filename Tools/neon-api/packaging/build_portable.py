#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import io
import json
import sys
import zipfile
from pathlib import Path, PurePosixPath


TOOL_DIRECTORY = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = TOOL_DIRECTORY.parents[1]
sys.path.insert(0, str(TOOL_DIRECTORY))

from neonlib.anchored import DirectoryAnchor  # noqa: E402
from neonlib.jsonio import parse_json_bytes  # noqa: E402
from neonlib.package_contract import (  # noqa: E402
    MINIMUM_PYTHON_VERSION,
    PACKAGE_VERSION,
    REQUIRED_PACKAGE_PATHS,
)


PACKAGE_ROOT = "neon-cli"
FIXED_ZIP_TIME = (2020, 1, 1, 0, 0, 0)
DEFAULT_OUTPUT = REPOSITORY_ROOT / "artifacts" / "neon-cli" / "Neon-CLI-Portable.zip"


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _source_files() -> list[tuple[str, Path, bool]]:
    files: list[tuple[str, Path, bool]] = [
        ("LICENSE", REPOSITORY_ROOT / "LICENSE", False),
        ("neon", REPOSITORY_ROOT / "neon", True),
        ("neon.cmd", REPOSITORY_ROOT / "neon.cmd", False),
        ("Tools/neon-api/README.md", TOOL_DIRECTORY / "README.md", False),
        ("Tools/neon-api/THIRD_PARTY_NOTICES.md", TOOL_DIRECTORY / "THIRD_PARTY_NOTICES.md", False),
        ("Tools/neon-api/neon-api.json", TOOL_DIRECTORY / "neon-api.json", False),
        ("Tools/neon-api/event-emission-evidence.json", TOOL_DIRECTORY / "event-emission-evidence.json", False),
        ("Tools/neon-api/neon.py", TOOL_DIRECTORY / "neon.py", True),
    ]
    for directory, pattern in (
        (TOOL_DIRECTORY / "neonlib", "*.py"),
        (TOOL_DIRECTORY / "schemas", "*.json"),
        (TOOL_DIRECTORY / "runtime-probe", "*"),
        (TOOL_DIRECTORY / "licenses", "*"),
    ):
        if directory.is_symlink() or not directory.is_dir():
            raise ValueError(f"required package source directory is missing or unsafe: {directory.relative_to(REPOSITORY_ROOT)}")
        for source in sorted(directory.glob(pattern), key=lambda path: path.name):
            if source.is_file() and not source.is_symlink():
                relative = source.relative_to(REPOSITORY_ROOT).as_posix()
                files.append((relative, source, source.suffix == ".py" and source.name == "neon.py"))
    unique: dict[str, tuple[str, Path, bool]] = {}
    for entry in files:
        if entry[0] in unique:
            raise ValueError(f"duplicate package path: {entry[0]}")
        unique[entry[0]] = entry
    if set(unique) != REQUIRED_PACKAGE_PATHS:
        missing = sorted(REQUIRED_PACKAGE_PATHS - set(unique))
        unexpected = sorted(set(unique) - REQUIRED_PACKAGE_PATHS)
        raise ValueError(f"package source inventory mismatch; missing={missing!r}; unexpected={unexpected!r}")
    return [unique[key] for key in sorted(unique)]


def _zip_info(path: str, executable: bool) -> zipfile.ZipInfo:
    pure = PurePosixPath(path)
    if pure.is_absolute() or ".." in pure.parts or "." in pure.parts:
        raise ValueError(f"unsafe package path: {path}")
    info = zipfile.ZipInfo(f"{PACKAGE_ROOT}/{pure.as_posix()}", FIXED_ZIP_TIME)
    info.create_system = 3
    mode = 0o100755 if executable else 0o100644
    info.external_attr = mode << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    return info


def _manifest(entries: list[tuple[str, bytes, bool]], catalogue: dict) -> bytes:
    document = {
        "schemaVersion": "1.0.0",
        "packageVersion": PACKAGE_VERSION,
        "minimumPython": MINIMUM_PYTHON_VERSION,
        "catalogueVersion": catalogue["catalogueVersion"],
        "engineVersion": catalogue["engine"]["version"],
        "catalogueSha256": next(_sha256(payload) for path, payload, _ in entries if path == "Tools/neon-api/neon-api.json"),
        "files": [
            {"path": path, "sha256": _sha256(payload), "size": len(payload), "executable": executable}
            for path, payload, executable in entries
        ],
    }
    return (json.dumps(document, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def build(output: Path) -> dict:
    entries: list[tuple[str, bytes, bool]] = []
    with DirectoryAnchor(REPOSITORY_ROOT) as source_anchor:
        for relative, _source, executable in _source_files():
            entries.append((relative, source_anchor.read(relative, 32 * 1024 * 1024), executable))
    catalogue_payload = next(payload for path, payload, _ in entries if path == "Tools/neon-api/neon-api.json")
    catalogue = parse_json_bytes(catalogue_payload)
    manifest_payload = _manifest(entries, catalogue)
    entries.append(("NEON_CLI_MANIFEST.json", manifest_payload, False))
    entries.sort(key=lambda item: item[0])

    archive_buffer = io.BytesIO()
    with zipfile.ZipFile(archive_buffer, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for relative, payload, executable in entries:
            archive.writestr(_zip_info(relative, executable), payload, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    archive_payload = archive_buffer.getvalue()
    digest = _sha256(archive_payload)

    requested = (output if output.is_absolute() else Path.cwd() / output).absolute()
    if not requested.parent.exists():
        try:
            relative_parent = requested.parent.relative_to(REPOSITORY_ROOT.absolute())
        except ValueError as exc:
            raise ValueError("custom output parent must already exist") from exc
        with DirectoryAnchor(REPOSITORY_ROOT, writable=True) as repository_anchor:
            repository_anchor.ensure_directory(relative_parent.as_posix())
    sidecar = requested.with_suffix(requested.suffix + ".sha256")
    with DirectoryAnchor(requested.parent, writable=True) as output_anchor:
        for name, label in ((requested.name, "output"), (sidecar.name, "checksum output")):
            if output_anchor.entry_kind(name) not in {"missing", "file"}:
                raise ValueError(f"{label} must be a regular file path, not a link or special file")
        output_identity = output_anchor.replace(requested.name, archive_payload)
        sidecar_identity = output_anchor.replace(sidecar.name, f"{digest}  {requested.name}\n".encode("ascii"))
        output_owned = output_anchor.entry_identity(requested.name) == output_identity
        sidecar_owned = output_anchor.entry_identity(sidecar.name) == sidecar_identity
        if not output_owned or not sidecar_owned:
            if output_owned:
                output_anchor.unlink_if_identity(requested.name, output_identity)
            if sidecar_owned:
                output_anchor.unlink_if_identity(sidecar.name, sidecar_identity)
            raise OSError("package outputs were replaced concurrently")
        if not output_anchor.current():
            output_anchor.unlink_if_identity(requested.name, output_identity)
            output_anchor.unlink_if_identity(sidecar.name, sidecar_identity)
            raise OSError("output directory identity changed during packaging")
    return {
        "schemaVersion": "1.0.0",
        "command": "package.portable",
        "status": "pass",
        "summary": {"errors": 0, "warnings": 0, "files": len(entries)},
        "diagnostics": [],
        "output": str(requested),
        "sha256": digest,
        "sidecar": str(sidecar),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the deterministic standalone Neon CLI ZIP")
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT))
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        result = build(Path(args.output))
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        result = {
            "schemaVersion": "1.0.0",
            "command": "package.portable",
            "status": "fail",
            "summary": {"errors": 1, "warnings": 0, "files": 0},
            "diagnostics": [{"code": "PACKAGE_BUILD_FAILED", "severity": "error", "message": str(exc), "path": "."}],
        }
    if args.json:
        sys.stdout.write(json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n")
    elif result["status"] == "pass":
        print(f"Created {result['output']}\nSHA-256 {result['sha256']}")
    else:
        print(f"neon package: FAIL: {result['diagnostics'][0]['message']}", file=sys.stderr)
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
