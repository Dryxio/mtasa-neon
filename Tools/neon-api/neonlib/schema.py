from __future__ import annotations

import re
import json
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any

from .jsonio import JsonDocumentError, load_json


SEMVER_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$")
ID_RE = re.compile(r"^[a-z][a-z0-9]*(?:[._:-][A-Za-z0-9][A-Za-z0-9._:-]*)*$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


@dataclass(frozen=True)
class SchemaIssue:
    pointer: str
    message: str


class SchemaStore:
    def __init__(self, schema_directory: Path):
        self.schema_directory = schema_directory
        self._cache: dict[str, dict[str, Any]] = {}

    def load(self, name: str) -> dict[str, Any]:
        if name not in self._cache:
            path = self.schema_directory / f"{name}.schema.json"
            try:
                document = load_json(path)
            except JsonDocumentError as exc:
                raise ValueError(f"cannot load schema {name}: {exc}") from exc
            if not isinstance(document, dict):
                raise ValueError(f"schema {name} is not an object")
            self._cache[name] = document
        return self._cache[name]

    def validate(self, name: str, instance: Any) -> list[SchemaIssue]:
        root = self.load(name)
        issues: list[SchemaIssue] = []
        self._validate_node(instance, root, root, "", issues)
        return sorted(issues, key=lambda issue: (issue.pointer, issue.message))

    def _validate_node(
        self,
        instance: Any,
        schema: dict[str, Any],
        root: dict[str, Any],
        pointer: str,
        issues: list[SchemaIssue],
    ) -> None:
        if "$ref" in schema:
            reference = schema["$ref"]
            if not isinstance(reference, str) or not reference.startswith("#/"):
                issues.append(SchemaIssue(pointer or "/", f"unsupported schema reference {reference!r}"))
                return
            target: Any = root
            for part in reference[2:].split("/"):
                key = part.replace("~1", "/").replace("~0", "~")
                if not isinstance(target, dict) or key not in target:
                    issues.append(SchemaIssue(pointer or "/", f"unresolved schema reference {reference}"))
                    return
                target = target[key]
            self._validate_node(instance, target, root, pointer, issues)
            return

        if "allOf" in schema:
            for child in schema["allOf"]:
                self._validate_node(instance, child, root, pointer, issues)
        if "anyOf" in schema:
            candidates = [self._candidate_issues(instance, child, root, pointer) for child in schema["anyOf"]]
            if not any(not candidate for candidate in candidates):
                issues.append(SchemaIssue(pointer or "/", "does not match any allowed schema"))
                return
        if "oneOf" in schema:
            matches = sum(not self._candidate_issues(instance, child, root, pointer) for child in schema["oneOf"])
            if matches != 1:
                issues.append(SchemaIssue(pointer or "/", f"must match exactly one schema (matched {matches})"))
                return

        if "const" in schema and instance != schema["const"]:
            issues.append(SchemaIssue(pointer or "/", f"must equal {schema['const']!r}"))
        if "enum" in schema and instance not in schema["enum"]:
            issues.append(SchemaIssue(pointer or "/", f"must be one of {schema['enum']!r}"))

        expected = schema.get("type")
        if expected and not self._is_type(instance, expected):
            issues.append(SchemaIssue(pointer or "/", f"must be {expected}"))
            return

        if isinstance(instance, dict):
            required = schema.get("required", [])
            for key in required:
                if key not in instance:
                    issues.append(SchemaIssue(self._join(pointer, key), "is required"))
            properties = schema.get("properties", {})
            if schema.get("additionalProperties") is False:
                for key in instance:
                    if key not in properties:
                        issues.append(SchemaIssue(self._join(pointer, key), "unknown property"))
            for key, child in properties.items():
                if key in instance:
                    self._validate_node(instance[key], child, root, self._join(pointer, key), issues)
            minimum = schema.get("minProperties")
            if minimum is not None and len(instance) < minimum:
                issues.append(SchemaIssue(pointer or "/", f"must contain at least {minimum} properties"))

        if isinstance(instance, list):
            if "minItems" in schema and len(instance) < schema["minItems"]:
                issues.append(SchemaIssue(pointer or "/", f"must contain at least {schema['minItems']} items"))
            if "maxItems" in schema and len(instance) > schema["maxItems"]:
                issues.append(SchemaIssue(pointer or "/", f"must contain at most {schema['maxItems']} items"))
            if schema.get("uniqueItems"):
                seen: set[str] = set()
                for index, item in enumerate(instance):
                    marker = json.dumps(item, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
                    if marker in seen:
                        issues.append(SchemaIssue(self._join(pointer, str(index)), "duplicate item"))
                    seen.add(marker)
            child = schema.get("items")
            if child:
                for index, item in enumerate(instance):
                    self._validate_node(item, child, root, self._join(pointer, str(index)), issues)

        if isinstance(instance, str):
            if "minLength" in schema and len(instance) < schema["minLength"]:
                issues.append(SchemaIssue(pointer or "/", f"must contain at least {schema['minLength']} characters"))
            if "maxLength" in schema and len(instance) > schema["maxLength"]:
                issues.append(SchemaIssue(pointer or "/", f"must contain at most {schema['maxLength']} characters"))
            if "pattern" in schema and re.fullmatch(schema["pattern"], instance) is None:
                issues.append(SchemaIssue(pointer or "/", "does not match the required pattern"))
            if "format" in schema and not self._valid_format(instance, schema["format"]):
                issues.append(SchemaIssue(pointer or "/", f"must use {schema['format']} format"))

        if isinstance(instance, (int, float)) and not isinstance(instance, bool):
            if "minimum" in schema and instance < schema["minimum"]:
                issues.append(SchemaIssue(pointer or "/", f"must be at least {schema['minimum']}"))
            if "maximum" in schema and instance > schema["maximum"]:
                issues.append(SchemaIssue(pointer or "/", f"must be at most {schema['maximum']}"))

    def _candidate_issues(self, instance: Any, schema: dict[str, Any], root: dict[str, Any], pointer: str) -> list[SchemaIssue]:
        result: list[SchemaIssue] = []
        self._validate_node(instance, schema, root, pointer, result)
        return result

    @staticmethod
    def _is_type(value: Any, expected: str) -> bool:
        return {
            "object": isinstance(value, dict),
            "array": isinstance(value, list),
            "string": isinstance(value, str),
            "integer": isinstance(value, int) and not isinstance(value, bool),
            "number": isinstance(value, (int, float)) and not isinstance(value, bool),
            "boolean": isinstance(value, bool),
            "null": value is None,
        }.get(expected, False)

    @staticmethod
    def _join(pointer: str, part: str) -> str:
        escaped = part.replace("~", "~0").replace("/", "~1")
        return f"{pointer}/{escaped}"

    @staticmethod
    def _valid_format(value: str, name: str) -> bool:
        if name == "semver":
            return SEMVER_RE.fullmatch(value) is not None
        if name == "stable-id":
            return ID_RE.fullmatch(value) is not None
        if name == "sha256":
            return SHA256_RE.fullmatch(value) is not None
        if name == "relative-path":
            path = PurePosixPath(value.replace("\\", "/"))
            return bool(value) and not path.is_absolute() and ".." not in path.parts and "." not in path.parts
        if name == "media-type":
            return re.fullmatch(r"[a-z0-9!#$&^_.+-]+/[a-z0-9!#$&^_.+-]+", value) is not None
        return True


def schema_major(version: str) -> int | None:
    match = SEMVER_RE.fullmatch(version)
    return int(match.group(1)) if match else None
