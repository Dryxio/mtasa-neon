import json
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))
import release_catalog  # noqa: E402


CATALOG_PATH = (
    Path(__file__).resolve().parents[2]
    / "Tools"
    / "server-browser-prototype"
    / "src"
    / "releaseNotes.json"
)


class ReleaseCatalogTests(unittest.TestCase):
    def test_repository_catalog_is_valid_and_prepares_build_182(self) -> None:
        releases = release_catalog.load_catalog(CATALOG_PATH)
        self.assertEqual(releases[0].build, 182)
        self.assertEqual(releases[0].display_version, "2026.08.23.182")

    def test_next_build_ignores_ci_and_legacy_tags(self) -> None:
        tags = [
            "neon-build-855.1-deadbeef",
            "neon-2026.08.07.174",
            "neon-2026.08.09.179",
            "unrelated-999",
        ]
        self.assertEqual(release_catalog.next_public_build(tags), 180)

    def test_next_build_uses_highest_public_identity_not_list_order(self) -> None:
        tags = ["neon-2026.08.21.180", "neon-2026.08.09.179", "neon-2026.08.20.181"]
        self.assertEqual(release_catalog.next_public_build(tags), 182)

    def test_rejects_duplicate_or_unsorted_builds(self) -> None:
        document = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        document["releases"][1]["build"] = document["releases"][0]["build"]
        with tempfile.TemporaryDirectory(prefix="neon-release-catalog-") as temporary_directory:
            path = Path(temporary_directory) / "releases.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaises(release_catalog.ReleaseCatalogError):
                release_catalog.load_catalog(path)

    def test_github_markdown_contains_complete_sections(self) -> None:
        release = release_catalog.release_for_build(release_catalog.load_catalog(CATALOG_PATH), 180)
        markdown = release_catalog.render_markdown(release)
        self.assertIn("## What’s new", markdown)
        self.assertIn("#### For players", markdown)
        self.assertIn("#### For creators", markdown)
        self.assertIn("#### Servers and fixes", markdown)
        self.assertNotIn("placeholder", markdown.lower())


if __name__ == "__main__":
    unittest.main()
