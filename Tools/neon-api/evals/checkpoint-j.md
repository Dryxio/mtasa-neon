# Checkpoint J portable distribution evaluation

- Date: 2026-08-28
- Base commit: `7685d87f0`
- Final verdict: PASS
- Auditor: fresh independent `gpt-5.6-sol` agent
- Audit mode: read-only hostile filesystem/package review, Python 3.10
  contract tests, macOS closed harnesses, and extracted ZIP execution in a
  Windows 11 VM

Checkpoint J makes the existing MTA+Neon CLI usable outside a source checkout.
It adds compatible launchers, safe one-command workspace initialization, an
exact deterministic portable package, package self-verification, and bounded
portable harness behavior. It does not compile or install MTA, silently run
checks after edits, add MCP, publish a release, or modify the updater.

## Final gates

```text
Independent Python 3.10 ContractSchemaTests + PortableDistributionTests
PASS: 32 tests

Local PortableDistributionTests
PASS: 16 tests

Clean detached-worktree ./neon harness --json
PASS: 217 tests, 0 errors, 2 expected platform skips

Portable self-test
PASS: 7 attempted tests

Portable self-test with missing manifest
EXPECTED FAIL: exit 1, 4 attempted tests, PACKAGE_MANIFEST_MISSING

Two independent portable builds
PASS: byte-identical, 57 payload files
SHA-256: be82ce3e3910afbfc556af87302804aab31d255ba9d220d47f744ee3ae944288

Windows 11 extracted ZIP proof
PASS: Python 3.14.0, portable self-test/harness, path with spaces,
old unusable Python first on PATH, failing command exit 1, tooling junction
rejected, init/check/generate/verify/search all pass

Windows handle regression
PASS: junction rejection and retained handle behavior

python3 -m py_compile Tools/neon-api/neon.py Tools/neon-api/neonlib/*.py
PASS

git diff --check -- neon neon.cmd Tools/neon-api
PASS

./neon check --json
PASS
```

The decisive maintainer harness ran from a clean detached worktree at the
checkpoint commit, so unrelated dirty C++ registrations could not contaminate
its runtime inventory.

## Distribution and onboarding contract

- `neon` and `neon.cmd` select only Python 3.10 or newer. The Windows launcher
  tries `py -3`, compatible PATH commands, and standard per-user/system Python
  locations while preserving the CLI exit code.
- `neon init` discovers MTA resources, creates a project, installs the full
  pinned MTA+Neon catalogue, writes a short agent guide, generates context, and
  verifies it. Existing projects, contexts, guides, catalogues, symlinks, and
  Windows reparse points are never silently overwritten.
- Init mutations are rooted in retained directory handles. POSIX artifacts are
  prepared under private unpredictable names and published with an atomic
  no-replace rename; Windows uses handle-relative exclusive creation.
- Rollback is conditional on the exact inode/file ID Neon created. Concurrent
  user replacements are preserved, including replacements arriving between
  validation and deletion. POSIX quarantine cleanup never opens a FIFO and
  restores raced symlinks or special files to their public name.
- Context subdirectory identities are checked before every descendant write
  and again before success. Workspace swaps can affect reads only; staging is
  outside the workspace namespace and every project mutation remains anchored.
- The ZIP manifest binds an exact allowlisted inventory, every payload size and
  SHA-256, package/Python/catalogue/engine versions, and the catalogue digest.
  Unknown, missing, duplicated, oversized, linked, or changed files fail
  closed. The adjacent ZIP checksum detects corruption but is explicitly not a
  publisher signature.
- Repository `neon harness` retains the complete maintainer suite. Portable
  `neon harness` and `neon self-test` verify the package and execute a bounded
  isolated discovery/check/generate/verify workflow without repository tests.
- The generated agent guide makes check/generate/context verification explicit;
  the CLI does not pretend those checks run automatically after every edit.

## Defects found and closed during independent review

The independent audit found and drove deterministic regression coverage for:

- missing manifests incorrectly passing in repository mode;
- incomplete or metadata-forged package inventories;
- symlink and parent-swap output writes in the package builder;
- Windows batch launchers masking failing command exit codes;
- Windows junction escapes through `.neon-tooling`;
- init leaving partial files or directories after write/fsync/open failures;
- rollback deleting a concurrently replaced guide, project, catalogue, or
  context file;
- check-to-unlink races and rename-time symlink/FIFO replacements;
- workspace-root and generated context subdirectory swaps writing through a
  different namespace;
- package verification reopening a different file after validation; and
- portable self-test claiming seven tests when the workflow was short-circuited.

All P0/P1/P2 findings were fixed. The final independent verdict reported no
remaining blocker and made no source edits.

## Frozen implementation hashes

```text
neon                                            b1cd4ca5f56b80204aa8d808f6a2d32344abe7fb71b3d91a452d48c7c381504a
neon.cmd                                        21d465fb8ee61b68d5cd98f18cc7ca35170224a8d18c387ab2a75d556d398583
README.md                                        8b963ebb941a7aae0177753f7bfae5de85ee2d2a91379ecbd2489501f50574f4
neon.py                                          f217e7e2ada3fbc1b9321c69726c80e710ac3cfdee5dbd78919134a6b3bdd83f
neonlib/anchored.py                              c91cafcb95163cc0cf050cf054c64a9e4670832f544473dd5716863ec4cdd966
neonlib/initialize.py                            803550c9b7bfff50e42992294a4338325216e2cd0fcfc47f9fe5b061b7eea7c9
neonlib/jsonio.py                                c8745f699647b47984494ec9c53087364b42845438f6a6c202e472ab94a8b501
neonlib/package_contract.py                      ed4a37711faaca4765ff8a3e31844c2cb64ff598928ac712220e40fa13f38fbe
neonlib/portable.py                              e4629ff271493120640c258af0da7ef85de162f5993b6b287951c55bcfe966b4
neonlib/proof.py                                 46deab09cdec24a6c7bf883d295a4bdaefd643c70c1c5181ce51c1659efb237b
neonlib/winfs.py                                 af4860e497e6731740ac6c6e237b0f78c4ecfbafa262170e5da53e6cb5c80cdf
packaging/build_portable.py                      d24c4861d894f2bdba34b28981b54a89ef8fb9b013efbf3fcbf50bef43e0196b
schemas/neon-init-result.schema.json             139a62e3554121c228a6ae8b9d26627a483c103ff95c8e68c88dc8412daa4b29
schemas/neon-package-manifest.schema.json        dc56bb91da90471b21be2b3ee0b987ea2b7e1d9545a0cd8ef25d61a300339d10
tests/fixtures/run-windows-portable-proof.ps1    dc6ccfd1ad132b382d2cd174f00cb691e6b8392e6fd786c014815a1e38a51421
tests/test_neon_api.py                           53effafc43ce257b89545b99b43f59b1080de7d609713f67146cc0409f46372b
```

No GitHub release, upload, push, deployment, updater publication, or C++ build
was performed. No C++ source changed in this checkpoint; Windows validation
used the extracted standalone ZIP directly.
