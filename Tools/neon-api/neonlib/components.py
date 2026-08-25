from __future__ import annotations

import hashlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .jsonio import JsonDocumentError, canonical_json, load_json
from .schema import SchemaStore, schema_major


@dataclass(frozen=True)
class ManifestIssue:
    code: str
    message: str


def manifest_semantic_issues(manifest: dict[str, Any]) -> list[ManifestIssue]:
    issues: list[ManifestIssue] = []
    if schema_major(manifest.get("schemaVersion", "")) != 1:
        issues.append(ManifestIssue("COMPONENT_SCHEMA_VERSION_UNSUPPORTED", f"unsupported component schema {manifest.get('schemaVersion')!r}"))
    kind = manifest.get("kind")
    if kind == "module" and "module" not in manifest:
        issues.append(ManifestIssue("MODULE_ABI_MISSING", "a module manifest must declare its module ABI contract"))
    if kind == "resource" and "module" in manifest:
        issues.append(ManifestIssue("RESOURCE_MODULE_CONTRACT_FORBIDDEN", "a resource manifest cannot declare a native module ABI"))

    identities = (
        ("exports", (lambda item: item.get("name")) if kind == "module" else (lambda item: (item.get("name"), item.get("side"))), "export"),
        ("events", lambda item: item.get("name"), "event"),
        ("elements", lambda item: item.get("name"), "element"),
        ("dependencies", lambda item: (item.get("kind"), item.get("name")), "dependency"),
        ("acl", lambda item: item.get("right"), "ACL right"),
    )
    for field, identity, label in identities:
        seen: set[Any] = set()
        for item in manifest.get(field, []):
            key = identity(item)
            if key in seen:
                issues.append(ManifestIssue("COMPONENT_DUPLICATE_IDENTITY", f"duplicate {label} identity {key!r}"))
            seen.add(key)
    for dependency in manifest.get("dependencies", []):
        if dependency.get("kind") == kind and dependency.get("name") == manifest.get("name"):
            issues.append(ManifestIssue("COMPONENT_SELF_DEPENDENCY", f"{kind} {manifest.get('name')} cannot depend on itself"))
    for export in manifest.get("exports", []):
        parameter_names: set[str] = set()
        optional_seen = False
        for parameter in export.get("parameters", []):
            if parameter.get("name") in parameter_names:
                issues.append(ManifestIssue("COMPONENT_DUPLICATE_PARAMETER", f"duplicate parameter {parameter.get('name')} in export {export.get('name')}"))
            parameter_names.add(parameter.get("name"))
            optional_seen = optional_seen or parameter.get("optional", False)
            if optional_seen and not parameter.get("optional", False):
                issues.append(ManifestIssue("COMPONENT_PARAMETER_ORDER_INVALID", f"required parameter {parameter.get('name')} follows an optional parameter in export {export.get('name')}"))
            if "default" in parameter and not parameter.get("optional", False):
                issues.append(ManifestIssue("COMPONENT_PARAMETER_DEFAULT_INVALID", f"required parameter {parameter.get('name')} has a default in export {export.get('name')}"))
        return_names: set[str] = set()
        for returned in export.get("returns", []):
            name = returned.get("name")
            if name is not None and name in return_names:
                issues.append(ManifestIssue("COMPONENT_DUPLICATE_RETURN", f"duplicate named return {name} in export {export.get('name')}"))
            if name is not None:
                return_names.add(name)
    for event in manifest.get("events", []):
        names: set[str] = set()
        for parameter in event.get("parameters", []):
            if parameter.get("name") in names:
                issues.append(ManifestIssue("COMPONENT_DUPLICATE_PARAMETER", f"duplicate parameter {parameter.get('name')} in event {event.get('name')}"))
            names.add(parameter.get("name"))
    return issues


def load_manifest(path: Path, schema_store: SchemaStore) -> tuple[dict[str, Any] | None, list[ManifestIssue]]:
    try:
        manifest = load_json(path)
    except JsonDocumentError as exc:
        suffix = " JSON-compatible YAML 1.2 is required" if path.suffix.lower() in (".yaml", ".yml") else ""
        return None, [ManifestIssue("COMPONENT_MANIFEST_INVALID", f"{exc};{suffix}" if suffix else str(exc))]
    if not isinstance(manifest, dict):
        return None, [ManifestIssue("COMPONENT_MANIFEST_INVALID", "component manifest must be an object")]
    schema_issues = schema_store.validate("neon-component", manifest)
    if schema_issues:
        return None, [ManifestIssue("COMPONENT_SCHEMA_INVALID", f"{issue.pointer}: {issue.message}") for issue in schema_issues]
    semantic = manifest_semantic_issues(manifest)
    return (None, semantic) if semantic else (manifest, [])


def manifest_sha256(manifest: dict[str, Any]) -> str:
    return hashlib.sha256(canonical_json(manifest).encode("utf-8")).hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def manifest_symbols(manifest: dict[str, Any], manifest_path: str, state: str = "documented-only") -> list[dict[str, Any]]:
    owner_kind = manifest["kind"]
    owner = manifest["name"]
    provenance = {"manifest": manifest_path, "sha256": manifest_sha256(manifest)}
    symbols: list[dict[str, Any]] = []
    for export in manifest["exports"]:
        if owner_kind == "resource":
            symbol_id = f"resource:{owner}:{export['side']}-export:{export['name']}"
            kind = "export"
        else:
            symbol_id = f"module:{owner}:function:{export['name']}"
            kind = "function"
        symbols.append({"id": symbol_id, "kind": kind, "name": export["name"], "owner": owner, "ownerKind": owner_kind,
                        "side": export["side"], "state": state, "signatureKnown": True, "parameters": export["parameters"], "returns": export["returns"],
                        "description": export["description"], "http": export["http"], "restricted": export["restricted"],
                        "provenance": provenance})
    for event in manifest["events"]:
        symbols.append({"id": f"{owner_kind}:{owner}:event:{event['name']}", "kind": "event", "name": event["name"],
                        "owner": owner, "ownerKind": owner_kind, "side": event["side"], "state": state,
                        "directions": event["directions"], "signatureKnown": True, "parameters": event["parameters"],
                        "allowRemoteTrigger": event["allowRemoteTrigger"], "description": event["description"], "provenance": provenance})
    for element in manifest["elements"]:
        symbols.append({"id": f"{owner_kind}:{owner}:element:{element['name']}", "kind": "element", "name": element["name"],
                        "owner": owner, "ownerKind": owner_kind, "side": element["side"], "state": state,
                        "lifetime": element["lifetime"], "description": element["description"], "provenance": provenance})
    return sorted(symbols, key=lambda item: item["id"])
