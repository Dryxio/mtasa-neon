# Neon update manifest

`manifest.py` creates and verifies the signed update metadata published with a
public GitHub release. It never generates keys. The release workflow provides
the base64-encoded PKCS#8 PEM private key through the protected
`NEON_UPDATE_SIGNING_KEY_B64` secret and removes the decoded temporary file
before the job exits.

The current trust anchor is:

- key ID: `neon-release-2026-01`
- algorithm: Ed25519
- raw public key, unpadded base64url:
  `oc55WO1PYJFiyipKMTAoDR-nVPLgUZ7bgyDfvl3WhsI`

The manifest signature covers this UTF-8 payload with LF line endings and a
final LF:

```text
mta-neon-update-v1
build=<decimal build>
display_version=<YYYY.MM.DD.build>
technical_version=<MTA sortable version>
release_tag=<GitHub release tag>
asset_name=<installer filename>
asset_url=<tag-specific GitHub release URL>
asset_size=<decimal bytes>
asset_sha256=<lowercase hexadecimal SHA-256>
key_id=<signing key identifier>
```

Consumers must validate the strict XML schema, reconstruct this payload in the
same order, verify the Ed25519 signature, and only then trust its fields. The
installer itself must match both the signed byte size and SHA-256 before it is
executed.

To verify a downloaded manifest and installer without access to the private
key:

```sh
python3 utils/neon-updater/manifest.py verify \
  --public-key-base64url oc55WO1PYJFiyipKMTAoDR-nVPLgUZ7bgyDfvl3WhsI \
  --repository Dryxio/mtasa-neon \
  --manifest MTA-Neon-Update.xml \
  --asset MTA-Neon-Setup.exe
```
