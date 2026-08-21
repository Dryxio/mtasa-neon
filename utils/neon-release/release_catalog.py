#!/usr/bin/env python3
"""Validate Neon release notes and render the public GitHub changelog."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from datetime import date
from pathlib import Path
from typing import Iterable


PUBLIC_TAG_PATTERN = re.compile(r"^neon-(\d{4}\.\d{2}\.\d{2})\.([1-9]\d{0,4})$")
ALLOWED_TONES = {"players", "creators", "servers"}
REQUIRED_RELEASE_KEYS = {"build", "date", "title", "summary", "highlights", "sections"}
REQUIRED_SECTION_KEYS = {"label", "tone", "items"}


class ReleaseCatalogError(ValueError):
    pass


@dataclass(frozen=True)
class Release:
    build: int
    release_date: str
    title: str
    summary: str
    highlights: tuple[str, ...]
    sections: tuple[dict[str, object], ...]

    @property
    def display_version(self) -> str:
        return f"{self.release_date.replace('-', '.')}.{self.build}"


def _require_text(value: object, field: str) -> str:
    if not isinstance(value, str) or not value.strip() or value != value.strip() or "\n" in value or "\r" in value:
        raise ReleaseCatalogError(f"{field} must be one line of non-empty text without surrounding whitespace")
    return value


def _require_text_list(value: object, field: str) -> tuple[str, ...]:
    if not isinstance(value, list) or not value:
        raise ReleaseCatalogError(f"{field} must be a non-empty array")
    return tuple(_require_text(item, f"{field}[{index}]") for index, item in enumerate(value))


def load_catalog(path: Path) -> tuple[Release, ...]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ReleaseCatalogError(f"could not read {path}: {error}") from error

    if not isinstance(document, dict) or set(document) != {"schema", "releases"}:
        raise ReleaseCatalogError("catalog root must contain only schema and releases")
    if document["schema"] != 1:
        raise ReleaseCatalogError("unsupported release catalog schema")
    if not isinstance(document["releases"], list) or not document["releases"]:
        raise ReleaseCatalogError("catalog must contain at least one release")

    releases: list[Release] = []
    seen_builds: set[int] = set()
    previous_build = 100000
    for index, raw_release in enumerate(document["releases"]):
        prefix = f"releases[{index}]"
        if not isinstance(raw_release, dict) or set(raw_release) != REQUIRED_RELEASE_KEYS:
            raise ReleaseCatalogError(f"{prefix} has unexpected or missing fields")

        build = raw_release["build"]
        if isinstance(build, bool) or not isinstance(build, int) or not 1 <= build <= 99999:
            raise ReleaseCatalogError(f"{prefix}.build must be an integer between 1 and 99999")
        if build in seen_builds:
            raise ReleaseCatalogError(f"duplicate release build {build}")
        if build >= previous_build:
            raise ReleaseCatalogError("releases must be ordered by descending build number")
        seen_builds.add(build)
        previous_build = build

        release_date = _require_text(raw_release["date"], f"{prefix}.date")
        try:
            parsed_date = date.fromisoformat(release_date)
        except ValueError as error:
            raise ReleaseCatalogError(f"{prefix}.date must be a real ISO calendar date") from error
        if parsed_date.isoformat() != release_date:
            raise ReleaseCatalogError(f"{prefix}.date must use canonical YYYY-MM-DD form")

        highlights = _require_text_list(raw_release["highlights"], f"{prefix}.highlights")
        if len(highlights) != 3:
            raise ReleaseCatalogError(f"{prefix}.highlights must contain exactly three popup items")

        raw_sections = raw_release["sections"]
        if not isinstance(raw_sections, list) or not raw_sections:
            raise ReleaseCatalogError(f"{prefix}.sections must be a non-empty array")
        sections: list[dict[str, object]] = []
        for section_index, raw_section in enumerate(raw_sections):
            section_prefix = f"{prefix}.sections[{section_index}]"
            if not isinstance(raw_section, dict) or set(raw_section) != REQUIRED_SECTION_KEYS:
                raise ReleaseCatalogError(f"{section_prefix} has unexpected or missing fields")
            tone = _require_text(raw_section["tone"], f"{section_prefix}.tone")
            if tone not in ALLOWED_TONES:
                raise ReleaseCatalogError(f"{section_prefix}.tone is not supported")
            sections.append(
                {
                    "label": _require_text(raw_section["label"], f"{section_prefix}.label"),
                    "tone": tone,
                    "items": _require_text_list(raw_section["items"], f"{section_prefix}.items"),
                }
            )

        releases.append(
            Release(
                build=build,
                release_date=release_date,
                title=_require_text(raw_release["title"], f"{prefix}.title"),
                summary=_require_text(raw_release["summary"], f"{prefix}.summary"),
                highlights=highlights,
                sections=tuple(sections),
            )
        )

    return tuple(releases)


def next_public_build(tags: Iterable[str]) -> int:
    builds: list[int] = []
    for raw_tag in tags:
        tag = raw_tag.strip()
        match = PUBLIC_TAG_PATTERN.fullmatch(tag)
        if not match:
            continue
        try:
            date.fromisoformat(match.group(1).replace(".", "-"))
        except ValueError:
            continue
        builds.append(int(match.group(2)))

    if not builds:
        raise ReleaseCatalogError("no sequential neon-YYYY.MM.DD.build release tag was found")
    next_build = max(builds) + 1
    if next_build > 99999:
        raise ReleaseCatalogError("public Neon build number exceeds the five-digit manifest limit")
    return next_build


def release_for_build(releases: tuple[Release, ...], build: int) -> Release:
    for release in releases:
        if release.build == build:
            return release
    raise ReleaseCatalogError(f"release catalog has no entry for build {build}")


def render_markdown(release: Release) -> str:
    lines = ["## What’s new", "", f"### {release.title}", "", release.summary, ""]
    for section in release.sections:
        lines.extend((f"#### {section['label']}", ""))
        lines.extend(f"- {item}" for item in section["items"])
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate = subparsers.add_parser("validate", help="validate the release catalogue")
    validate.add_argument("--catalog", type=Path, required=True)
    validate.add_argument("--expected-build", type=int)
    validate.add_argument("--expected-date")

    next_build = subparsers.add_parser("next-build", help="calculate the next public build from release tags on stdin")
    next_build.add_argument("--catalog", type=Path, required=True)

    render = subparsers.add_parser("render", help="render one release as Markdown")
    render.add_argument("--catalog", type=Path, required=True)
    render.add_argument("--build", type=int, required=True)
    render.add_argument("--output", type=Path, required=True)

    identity = subparsers.add_parser("identity", help="print one validated release identity field")
    identity.add_argument("--catalog", type=Path, required=True)
    identity.add_argument("--build", type=int, required=True)
    identity.add_argument(
        "--field",
        choices=("display-version", "technical-version", "tag", "title"),
        required=True,
    )
    return parser.parse_args()


def main() -> int:
    arguments = _parse_args()
    try:
        releases = load_catalog(arguments.catalog)
        if arguments.command == "validate":
            if arguments.expected_build is not None and releases[0].build != arguments.expected_build:
                raise ReleaseCatalogError(
                    f"latest catalog build is {releases[0].build}, expected {arguments.expected_build}"
                )
            if arguments.expected_date is not None and releases[0].release_date != arguments.expected_date:
                raise ReleaseCatalogError(
                    f"latest catalog date is {releases[0].release_date}, expected {arguments.expected_date}"
                )
        elif arguments.command == "next-build":
            build = next_public_build(sys.stdin)
            if releases[0].build != build:
                raise ReleaseCatalogError(f"latest catalog build is {releases[0].build}, next public build is {build}")
            print(build)
        elif arguments.command == "render":
            release = release_for_build(releases, arguments.build)
            with arguments.output.open("w", encoding="utf-8", newline="\n") as output:
                output.write(render_markdown(release))
        elif arguments.command == "identity":
            release = release_for_build(releases, arguments.build)
            fields = {
                "display-version": release.display_version,
                "technical-version": f"1.7.0-5.{release.build:05d}",
                "tag": f"neon-{release.display_version}",
                "title": f"MTA:SA Neon — {release.display_version}",
            }
            print(fields[arguments.field])
    except (OSError, ReleaseCatalogError) as error:
        print(f"release catalog error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
