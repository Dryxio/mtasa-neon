# Checkpoint I black-box evaluation

- Date: 2026-08-28
- Base commit: `977acff28ef2`
- Final verdict: PASS
- Auditors: three fresh independent `gpt-5.6-sol` agents
- Audit mode: read-only hostile source/schema review, closed macOS harness,
  and real MTA client/server execution in an isolated Windows 11 VM

Checkpoint I adds an opt-in, authenticated Windows runtime proof. A proof binds
the pinned project, catalogue, profile, session, server process, expected client
roles, build observations, and a fresh signed report emitted by a bundled MTA
resource. It does not add arbitrary process launch, shell access, automatic
gameplay claims, anti-cheat, hostile-process attestation, or an MCP server.

## Final gates

```text
macOS targeted contract/runtime/supervisor/proof suite
PASS: 71 tests, 2 expected Windows-only skips

macOS post-audit contract/supervisor regression suite
PASS: 48 tests, 2 expected Windows-only skips

macOS dirty-tree ./neon harness --json (diagnostic only)
201 tests; exactly one expected unrelated failure because concurrent C++ work
adds 103 uncommitted event registrations outside this checkpoint

macOS clean detached-worktree ./neon harness --json
PASS: 201 tests, 0 errors, 2 expected Windows-only skips

Windows 11 real MTA pair proof (server + one GTA client, port 22333)
PASS: server, client and in-game authenticated labels

Windows 11 real MTA multiclient proof (server + two GTA clients, port 22323)
PASS: server, client, in-game and multiplayer authenticated labels

./neon check --json
PASS

./neon catalogue verify --source-ref HEAD --json
PASS: no source, semantic, registration, event, or runtime-inventory drift

python3 -m py_compile Tools/neon-api/neon.py Tools/neon-api/neonlib/*.py
PASS

luac -p Tools/neon-api/runtime-probe/*.lua
PASS

git diff --check -- Tools/neon-api
PASS
```

The decisive full harness was executed from a clean detached worktree at the
checkpoint commit, so concurrent source work could not contaminate its runtime
registration inventory.

## Trust, permission, and evidence boundaries

- `client.launch` is opt-in, Windows-only, fixed when the session is created,
  and requires the explicit `resource.lifecycle` capability plus exact server
  and client roots.
- The approved client image is opened and hashed before its process is created
  suspended. It is assigned to a kill-on-close Job Object before its primary
  thread is resumed. This is lifecycle containment, not hostile image or
  process attestation.
- Every expected client role must have been launched by the session and remain
  alive. The guardian is pinged immediately before a proof, and the signed probe
  report must be no more than ten seconds old.
- The probe secret is generated per session, inherited through protected local
  files, and never serialized into the public session document. The MTA server
  derives client versions itself and generates report nonces server-side.
- Probe installation and report access traverse the server resource tree with
  directory handles. POSIX symlinks and Windows reparse points are rejected at
  every component, including during mid-operation swaps.
- The report is written atomically, signed with HMAC-SHA256, and bound to the
  session, project, catalogue, profile, topology, server/client observations,
  client roles, resource hash, and build inputs.
- Launching processes grants zero evidence labels. Only a fresh verified report
  can grant `server`, `client`, `in-game`, and, for the multiclient profile,
  `multiplayer`.
- Resource mutations now distinguish `not-submitted`, `command-submitted`, and
  `outcome-unknown`. The schema binds `MUTATION_OUTCOME_UNKNOWN` to the latter
  and rejects contradictory diagnostic/scope documents.
- Windows ACLs are inherited from the selected server tree. The documented
  operating boundary is an isolated developer-owned MTA server, not a shared
  hostile machine.

## Defects found and closed during independent review

The three independent audits found and drove regression coverage for:

- intermediate and mid-operation symlink/junction escapes during probe install;
- accepting proof before every expected client role was launched;
- trusting client-supplied version, build, or nonce observations;
- proving after the MTA guardian/server died or from a stale report;
- non-atomic report writes and reports surviving player/resource teardown;
- a client executable hash-to-launch race and child-process lifecycle escape;
- contradictory session, topology, capability, port, proof, and mutation
  schemas; and
- failed resource commands incorrectly claiming `command-submitted`, followed
  by a schema that did not initially bind unknown-outcome diagnostics to their
  required scope.

All material findings were fixed and covered by the closed harness. The final
auditor reported no remaining P0/P1 blocker and made no source edits.

## Frozen implementation hashes

```text
README.md                                      2735623f80d11569c33891e32eb966be47b2a515731c389bbe6ee4de627071db
neon-api.json                                 1f670026058a35bb8e16ff51ce41439905f3b4415965aa38da7d51e91b9c2521
neon.py                                       675381c3da0cc49e229f555e907e36e585ff3cc882f9d2e44fb3159d6d6a45a6
neonlib/mutation.py                           d81a85d3d75ed7be8694f78062fddf967e01eb6dd5417f7003cbd70cf2e1f367
neonlib/proof.py                              9cd573f365c08c2315d11690b3de6a7333817c8f3912402ccee1c5b1ba0b1377
neonlib/runtime.py                            e48a475650267c2b1385d77d79075617f01121a82070ae16b002a6bdb676552b
neonlib/schema.py                             9d367b2fd9f14871775e619a36922808317c4cf03c34dd5f88ea7d44f6c1b01e
neonlib/supervisor.py                         dc6fd8636cba0f97a50505a5e0640cd6217ee4a8fc4f6c501aa5de0b342901ce
neonlib/winfs.py                              3666645226e8951cd1d9ff8ba6f482e45d8faacab19be80a2cf2136cf43e6bf0
runtime-probe/meta.xml                        511929631bcfbf2ce18074bd62d10a718b62ccfebbb734bb939ba9288ebea48c
runtime-probe/server.lua                      0f4145e9f4b19ad6866094d0025c07b4eb8b95cb482f7d4d4d808a27396e212f
runtime-probe/client.lua                      ef91156df941128f6ba03ece9754c0962220dccff817dd1c1bffc63f345c9c87
schemas/neon-mutation-result.schema.json      9791f285de1598ab5cb362da31022e8f982e06d60363969d242faeac60570cf8
schemas/neon-probe-config.schema.json         74e189a094e6bbf7febfa76254ccfd38c59b9ba3be854e41f6e51bc47da902d7
schemas/neon-probe-install-result.schema.json b6c7518bd5fa9c9fc1ed7cda2e03513d7d9a5174452fc0bcd8463a95d3c066fc
schemas/neon-probe-report.schema.json         784fffd75ab8da60f0e3be57ef8161970d1c78d721ea8a4989066e449687df2c
schemas/neon-proof-result.schema.json         77923be1dd1f7df2f1b2491019739a1c10bd9a9133f24f6c2a0c780c0e197951
schemas/neon-supervisor-session.schema.json   050416599f5c84df9d2d188bcb3ae2ae480eb628ff5e44caee3f22954aa6f6c3
tests/test_neon_api.py                        aec3ef9814c15c98a69fdff4d31fa7b3b073d6f0a7410b26755cf7330954170e
tests/fixtures/run-windows-runtime-proof.ps1  4730aba8f05fa2c61246e0f41fc9a7151498f0873546775d7c5a3a51d4501c65
```

No GitHub release, upload, push, deployment, or updater publication was
performed.
