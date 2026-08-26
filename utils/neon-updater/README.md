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

## Publishing a public Neon update

A push or pull request only builds test artifacts. Public updates require an
explicit `workflow_dispatch` of `.github/workflows/build.yaml` from `master`
with `publish_release=true`; do not create the tag or GitHub release manually.

Public Neon build numbers are intentionally independent from
`GITHUB_RUN_NUMBER`. The workflow reads existing `neon-YYYY.MM.DD.build`
releases, calculates the next sequential build, and requires the newest entry
in `Tools/server-browser-prototype/src/releaseNotes.json` to match that build
and the current UTC release date. The resolved identity is injected into the
client, server, installer, signed manifest, in-client What's New screen and
GitHub release notes. Any disagreement fails before public binaries compile.

For example, when the newest public release is build 179, the next approved
dispatch produces build 180 even if GitHub Actions has executed hundreds of
unpublished CI runs in between.

Before dispatching a release:

1. Add the complete player-facing entry to `releaseNotes.json` and make it the
   first item.
2. If the release changes `Services/neon-identity`, deploy that service
   separately: run `npm ci`, `npm test`, `npm run build` and `npm run migrate`,
   then restart it and verify `/healthz`, JWKS publication and the changed
   authenticated flow. The GitHub release workflow does not build or deploy the
   service, and its ignored `dist/` directory must not be assumed current after
   a source update.
3. For server upgrades, keep the owner's existing `mtaserver.conf` and
   `neon-identity.keys`, but install the candidate's new
   `mtaserver.conf.template`. The server uses that template to add missing
   settings without replacing existing values; updating only executables and
   libraries keeps old behavior but cannot add newly documented fields.
4. Push the reviewed changes to `master` and wait for its ordinary Build run to
   pass.
5. Dispatch Build on `master` with `prepare_release=true` and
   `publish_release=false`. Download and test the candidate artifacts; this
   resolves the next public identity without creating a tag or release.
6. Without changing the reviewed source, dispatch Build again with
   `publish_release=true`. It rebuilds the same public identity and publishes
   only after every required job passes.
7. Verify the created tag, all six release assets, the signed manifest and an
   update from the previous public client before announcing the release.
