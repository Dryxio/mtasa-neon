import base64
import tempfile
import unittest
from pathlib import Path

import manifest


REPOSITORY = "Dryxio/mtasa-neon"
PUBLIC_KEY = "oc55WO1PYJFiyipKMTAoDR-nVPLgUZ7bgyDfvl3WhsI"
ZERO_SIGNATURE = base64.b64encode(bytes(64)).decode("ascii")


def valid_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<neon_update schema="1">
  <build>170</build>
  <display_version>2026.08.07.170</display_version>
  <technical_version>1.7.0-5.00170</technical_version>
  <release_tag>neon-2026.08.07.170</release_tag>
  <asset>
    <name>MTA-Neon-Setup.exe</name>
    <url>https://github.com/{REPOSITORY}/releases/download/neon-2026.08.07.170/MTA-Neon-Setup.exe</url>
    <size>16777216</size>
    <sha256>{"0" * 64}</sha256>
  </asset>
  <signature key_id="neon-release-2026-01" algorithm="ed25519" encoding="base64">{ZERO_SIGNATURE}</signature>
</neon_update>
"""


class ManifestTests(unittest.TestCase):
    def parse(self, content: str) -> manifest.ManifestFields:
        with tempfile.TemporaryDirectory(prefix="neon-manifest-test-") as temporary_directory:
            path = Path(temporary_directory) / "MTA-Neon-Update.xml"
            path.write_bytes(content.encode("utf-8"))
            fields, _ = manifest.parse_manifest(path, REPOSITORY)
            return fields

    def assert_rejected(self, content: str) -> None:
        with self.assertRaises(manifest.ManifestError):
            self.parse(content)

    def test_valid_envelope_and_canonical_payload(self) -> None:
        fields = self.parse(valid_xml())
        expected = (
            "mta-neon-update-v1\n"
            "build=170\n"
            "display_version=2026.08.07.170\n"
            "technical_version=1.7.0-5.00170\n"
            "release_tag=neon-2026.08.07.170\n"
            "asset_name=MTA-Neon-Setup.exe\n"
            f"asset_url=https://github.com/{REPOSITORY}/releases/download/neon-2026.08.07.170/MTA-Neon-Setup.exe\n"
            "asset_size=16777216\n"
            f"asset_sha256={'0' * 64}\n"
            "key_id=neon-release-2026-01\n"
        ).encode("utf-8")
        self.assertEqual(fields.canonical_payload(), expected)

    def test_rejects_noncanonical_build_decimal(self) -> None:
        self.assert_rejected(valid_xml().replace("<build>170</build>", "<build>00170</build>"))

    def test_rejects_noncanonical_size_decimal(self) -> None:
        self.assert_rejected(valid_xml().replace("<size>16777216</size>", "<size>016777216</size>"))

    def test_rejects_invalid_calendar_date(self) -> None:
        self.assert_rejected(valid_xml().replace("2026.08.07", "2026.02.30"))

    def test_rejects_leaf_attributes(self) -> None:
        self.assert_rejected(valid_xml().replace("<build>", '<build unexpected="1">'))

    def test_rejects_leaf_children(self) -> None:
        self.assert_rejected(valid_xml().replace("<build>170</build>", "<build>170<unexpected></unexpected></build>"))

    def test_rejects_installer_below_size_bound(self) -> None:
        self.assert_rejected(valid_xml().replace("<size>16777216</size>", "<size>16777215</size>"))

    def test_rejects_untagged_asset_url(self) -> None:
        self.assert_rejected(
            valid_xml().replace(
                "/releases/download/neon-2026.08.07.170/MTA-Neon-Setup.exe",
                "/releases/latest/download/MTA-Neon-Setup.exe",
            )
        )

    def test_openssl_verifies_rfc8032_ed25519_vector(self) -> None:
        public_key = bytes.fromhex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c")
        signature = bytes.fromhex(
            "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
            "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"
        )
        manifest.verify_signature(public_key, b"\x72", signature)

    def test_pinned_public_key_has_expected_raw_length(self) -> None:
        self.assertEqual(len(manifest.decode_public_key_base64url(PUBLIC_KEY)), 32)


if __name__ == "__main__":
    unittest.main()
