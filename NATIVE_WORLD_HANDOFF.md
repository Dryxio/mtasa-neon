# Native world streaming handoff

Last updated: 2026-07-16

This file is the operational handoff for the native extended-world project
started from [Dryxio/mtasa-neon issue #1](https://github.com/Dryxio/mtasa-neon/issues/1).
It is intended to let a replacement orchestrator resume without reconstructing
the long development conversation.

The final goal is to let servers distribute multiple custom cities and have GTA
SA stream them through its native IDE/IMG/IPL system, as if they had been part
of the installed game data. Once a startup plan is active, a player should be
able to fly between San Andreas and the added cities without a custom Lua
streamer or a visible loading transition.

## Read first

Before changing anything, read these files completely:

1. `AGENTS.md` for the canonical tree, VM workflow, build targets, launch
   commands, verification expectations, and Git rules.
2. `LIMIT_PATCHING.md` for the executable-patching methodology and the limits
   already lifted for the extended world.
3. `utils/extended-world/NATIVE_BW_PACK.md` for the authoritative native-pack
   format, executable allowlist, audits, cache semantics, diagnostics, current
   Bullworth policy, and runtime failure boundaries.
4. `test-resources/native-world-transport-test/README.md` and `meta.xml` for the
   current server-transport declaration and manual test contract.

Treat this handoff as a status and navigation document. When a technical detail
disagrees with `NATIVE_BW_PACK.md` or the current code, investigate the current
code and update both documents rather than silently trusting this snapshot.

## Non-negotiable collaboration rules

- The user performs every in-game action and decides whether behavior is
  correct. Agents must not test gameplay themselves. They may build, start or
  stop the server, launch the client when asked, inspect logs/processes/cache
  files/crash dumps, and prepare exact test instructions.
- Keep the established orchestrated workflow. For major checkpoints, use a
  research/exploration/planning agent and an independent implementation/review
  agent where their work is useful. Keep file ownership clear and prefer
  read-only parallel work when edits would overlap.
- Define explicit checkpoints. Tell the user when a build is ready, what to do
  in game, what result to report, and when the client must be closed.
- Do not commit a gameplay-affecting checkpoint until the user has supplied the
  requested in-game feedback and the result is understood.
- Do not skip safety reviews around executable writes, downloaded native data,
  cache publication, activation authorization, worker cancellation, native
  object lifetimes, or aggregate pool allocation.
- Preserve unrelated work in the dirty tree. Never stage or commit files merely
  because they appear in `git status`.
- Source edits, reviews, commits, and pushes happen only in the canonical macOS
  tree. The VM-local tree is a disposable build/runtime mirror. Do not push
  unless the user explicitly asks.

## Repository snapshot

At the time of this handoff:

```text
Canonical tree  /Users/salimtrouve/Documents/GitHub/mtasa-neon
Branch          master
HEAD            7c38a9278 feat(streaming): publish native world packs from resources
origin/master   7c38a9278
VM              Windows 11
VM build tree   C:\dev\mtasa-vm-custom
```

Always re-run `git status --short` and `git log -5 --oneline --decorate`; the
user and other agents work in this repository concurrently. On 2026-07-16 the
tree contained unrelated, uncommitted story/camera/input/audio/tagging changes,
including changes below `Client/core`, `Client/game_sa`, Client Deathmatch Lua
definitions, `README.md`, `STORY_RUNTIME.md`, native script-camera/tagging test
resources, and untracked `.claude`, `Tools`, `game-resources`, `out.dff`, and a
native mission-audio test resource. None belongs to the native-world checkpoint
unless a later diff proves otherwise. In particular,
`Client/mods/deathmatch/logic/CResource.cpp` is shared with the transport work
but currently has unrelated lifecycle edits in the working tree; inspect and
stage its hunks explicitly.

## Implemented native-world checkpoints

Read the commit bodies as design records. The core sequence is:

| Commit | Checkpoint |
| --- | --- |
| `5edd8e7f9` | Opt-in native Bullworth prototype, native IDE/IMG/COL/binary IPL registration, required model-store/collision limits, native spatial streaming, and reconnect-safe streaming-buffer floor. |
| `8bbdd4a31` | Generic static-world pack manager separated from the immutable compiled Bullworth policy. |
| `1304f98d8` | Minimal versioned runtime manifest and derivation of the native allocation plan from IDE/IMG bytes. |
| `d65e8eee0` | Closed structural and semantic validation for RenderWare DFF/TXD, COL, IMG, IDE, and binary IPL payloads. |
| `5d43f18e5` | Immutable ProgramData cache with semantic content IDs, locked quarantine, atomic publication, reparse/path protections, guarded revalidation, and activation leases. |
| `7c38a9278` | Version-gated server resource transport, bounded HTTP streaming, asynchronous full audit, quotas, safe cancellation, atomic publish-only cache insertion, and legacy-client omission. |

Relevant earlier extended-world foundations include the enlarged world sectors,
coordinate/network ranges, water bounds, renderer capacity, radar composition,
and the earlier resident-IMG city prototype. Do not confuse that resident/custom
streamer with the native-world target. The old `ug-bw` resource must remain
stopped during native Bullworth tests because it owns the same models and
placements through a different lifecycle.

### Source map

- `Client/game_sa/CNativeWorldPackSA.*` and `CGameSA.*` contain the native pack
  policy/manager bridge, closed payload audit, preflight and registration path.
- `Client/game_sa/CNativeWorldCacheSA.*` contains semantic identity, guarded
  cache lookup/publication, quotas, remnant handling and activation leases.
- Client Deathmatch `CPacketHandler.cpp`, `CResource.*`,
  `CResourceFileDownloadManager.cpp`, and `CResourceManager.*` parse the
  versioned offer, enforce download identity, run/retire the asynchronous audit,
  and report its result.
- Server Deathmatch `CResource.*`, `CResourceFile.*`,
  `packets/CResourceStartPacket.cpp`, and `CHTTPD.cpp` validate metadata,
  version-gate the group and stream the files.
- `Shared/sdk/net/bitstream.h` carries the protocol capability and
  `Shared/httpd/Types.h` carries the bounded file-response state.
- `utils/extended-world` contains the generator, validators and focused tests;
  `test-resources/native-world-transport-test` is the metadata-only live
  transport harness.

## Current architecture

### Native startup path

`CNativeWorldPackManagerSA` performs exact preflight, allocation planning,
native commit, postconditions, IPL bootstrap, and process-lifetime management.
`CNativeBullworthPackSA` is the only compiled trusted policy. It supports only
the two exact GTA SA 1.0 US identities documented in `NATIVE_BW_PACK.md`.

With `MTA_NATIVE_BW_MODEL_STORES=1`, the current prototype reads the local
installation copy of `native-world.json` as its startup selector. It can then
activate the matching immutable ProgramData cache object. Once that object
exists, the installation IDE and IMG may be removed, but the small local
selector manifest is still required. Successful registration is process-global
and intentionally survives resource stops and reconnects; changing packs
requires a clean client restart.

### Server transport path

A resource declares exactly one format-1 `<native_world>` descriptor and
exactly three tagged automatic-download files: `native-world.json`, one IDE,
and one IMG. The version-gated ResourceStart packet advertises the descriptor
and tagged-file metadata to capable clients; normal resource HTTP carries the
file bodies. Legacy clients receive neither the descriptor nor the tagged-file
metadata and therefore do not request those bodies.

The built-in HTTP server streams file bodies through a 64 KiB buffer. After the
normal download size and checksum checks, a cancellable worker performs the
complete closed Bullworth audit, copies/hashes into a random same-volume locked
quarantine, audits the copy, atomically renames the directory, and revalidates
the final immutable object. A cache hit follows the same guarded validation
path.

Transport deliberately does not authorize or activate the object. Successful
diagnostics contain:

```text
[NativeWorldTransport] state=audit-started ... activation=no lease=no
[NativeWorldTransport] state=cached ... disposition=published|hit ...
    audit=closed-bullworth publish=atomic activation=no lease=no
    restart-required=yes
```

The separation is essential:

```text
downloaded bytes -> checksum -> closed semantic audit -> immutable cache
immutable cache  != trusted server authorization
trusted authorization + exact cached content + clean startup -> activation
```

### Cache policy

The cache is rooted at:

```text
C:\ProgramData\MTA San Andreas All\1.7\native-world-cache\v1
```

The validated Bullworth object used during the transport checkpoint had content
ID:

```text
6a090231416e0298eb78e671eba91d4c58ed1f9c16dfae94d162a81a52464824
```

The transport limits are a 4 KiB manifest, 1 MiB IDE, 256 MiB IMG, at most four
content objects total for the policy, at most 1 GiB counted data, and the
requested bytes plus a 64 MiB free-space margin. Unsafe siblings, reparse
points, unverifiable remnants, quota exhaustion, and immutable conflicts are
refused.

`netc.dll` is external and its in-tree ABI does not expose the underlying write
callback. A callback-local hard cap is therefore not currently possible. The
client instead bounds the declaration before queuing, polls visible progress,
rejects missing or divergent Content-Length and received-byte overflow, then
requires the exact final disk length and checksum. Treat an exact callback cap
as a possible future net-module/ABI improvement, not as implemented behavior.

## Validation already completed

Agents performed builds, static checks, log inspection, cache inspection, and
hash comparisons. The user performed all gameplay checks.

Confirmed checkpoints include:

- `Game SA` and `Client Deathmatch` built as `Release|Win32` for the latest
  transport checkpoint.
- `python3 -m unittest discover -s utils/extended-world/tests -p 'test_*.py'`
  reported 38 passing tests and two optional environment-dependent skips.
- The final independent transport/cache review reported no remaining
  actionable P0-P2 issue. The external net-module callback boundary below was
  recorded as an accepted residual rather than hidden.
- Native Bullworth loaded through GTA's IDE/IMG/COL and seven spatial binary
  IPLs with textures and collision.
- Travel into, around, away from, and back into Bullworth worked through native
  position-driven streaming.
- Repeated disconnect/reconnect and clean process restart were exercised during
  the native runtime series. Resource restart and respawn remain prescribed
  regression cases unless a later checkpoint records fresh evidence.
- A fresh resource transport downloaded the three files and completed the full
  closed audit.
- After the previous cache object was moved aside, transport reported
  `disposition=published`; the three resulting hashes exactly matched the
  known-good object and no quarantine sibling remained.
- A following reconnect reported `disposition=hit`, retained one object and
  unchanged hashes, and did not crash.
- Rollback with the native environment switch disabled preserved normal San
  Andreas behavior.

Historical regressions worth retaining in later test matrices are the
`gta_sa.exe+0x00331AB5` stale TXD reference crash after extended exploration,
the `gta_sa.exe+0x00004B85` reconnect/streaming-update crash, and the severe
one-frame-per-several-seconds stall after a Bullworth teleport. Their observed
test cases stopped reproducing after the corresponding fixes, but future
multi-pack or activation changes could reopen the same lifetime/capacity class
of bugs.

The tracked `test-resources/native-world-transport-test` contains metadata and
instructions only. Its large audited Bullworth payload is intentionally copied
only into the VM runtime resource and is never Git-indexed. Do not commit
generated city assets.

## Known boundaries

- Bullworth is still the only compiled policy; the transport is not arbitrary
  IDE support.
- A server can supply and cache bytes but cannot yet authorize or activate them.
- Startup still depends on the local selector manifest and environment flag.
- There is no authenticated/session-bound activation ticket.
- There is no aggregate multi-pack allocation or transactional registration of
  several cities.
- Native registration cannot currently be safely hot-unloaded. Treat active
  startup packs as process-lifetime state.
- Current generated Bullworth IPL placements use `lod_index = -1`; native
  spatial streaming and collision are validated, but GTA UG-equivalent
  long-distance LOD behavior is not.
- Radar tiles, path nodes, zones/population, audio/environment data, interiors,
  and similar city subsystems are separate from static IDE/IMG/IPL streaming.
  Missing Bullworth radar tiles are expected at this checkpoint.
- General multi-city capacities still require aggregate audits for model and
  streaming infos, TXDs, COL/IPL stores, archive/stream slots, buildings and
  pointer nodes, request lists, streaming memory/channels, LODs, and any
  optional city subsystem.

## Immediate next checkpoint: authorized startup activation

The next phase removes the last preinstalled selector dependency and binds one
exact audited cache object to a server-authorized startup. Do not begin by
simply loading the most recently downloaded cache entry: content identity is
not authorization.

Start with a read-only design/research checkpoint:

1. Trace the precise MTA connection, server identity, resource-start, game
   startup, reconnect, and shutdown sequence.
2. Inventory what authenticated or stable server identity MTA already exposes.
   Do not claim cryptographic authentication if the existing protocol cannot
   provide it; document the actual trust boundary.
3. Design a closed, versioned activation record bound at minimum to the server
   identity, exact policy key and content ID, protocol/format version, freshness
   or expiry, and intended one-shot/replay behavior.
4. Define atomic persistence, crash recovery, tamper refusal, cancellation, and
   cleanup behavior.
5. Define the two-launch flow: first connection downloads/audits/publishes and
   requests a restart; the clean next startup validates the record and cache
   before the irreversible native commit, then reconnects to the intended
   server.
6. Define fail-soft behavior before IDE commit and fatal behavior after an
   irreversible partial native commit.
7. Produce an implementation plan and independent security/lifecycle review
   before editing activation code.

Implement progressively:

- **Checkpoint A — inert authorization record:** receive, validate, persist,
  inspect, expire, and clear the record, but always log `activation=no`.
- **Checkpoint B — startup selection:** consume the record at clean startup,
  locate and fully revalidate the exact immutable object, and acquire the
  pending activation lease without committing GTA state.
- **Checkpoint C — native activation:** feed the selected object into the
  existing preflight/commit path, remove the environment/local-selector
  requirement for this route, and keep rollback/fatal boundaries intact.
- **Checkpoint D — restart/reconnect UX:** initially use an explicit user-driven
  restart; automate or polish it only after the trust and lifecycle path is
  stable.

Each checkpoint needs negative tests for wrong server, wrong content ID,
missing/corrupt cache, expired/replayed/tampered record, disconnect during
publication, resource stop, crash between write and consume, and a modified or
unsupported GTA executable. Ask the user for gameplay validation only after the
relevant builds, logs, and non-game checks pass.

## Remaining global roadmap

After authorized activation:

1. Replace Bullworth-specific compiled payload assumptions with a constrained,
   versioned generic static-world pack policy.
2. Prove that a second city, preferably Carcer, uses the same pipeline without
   city-specific C++.
3. Build a deterministic aggregate startup plan for multiple packs, including
   conflict detection and all combined pool/store/archive/streaming limits.
4. Transactionally register San Andreas plus Bullworth plus Carcer in one
   process through native GTA streaming.
5. Tune streaming memory, buffers, request lists, spatial IPL behavior and
   LOD/prefetch behavior for seamless repeated flights and stable FPS.
6. Add optional pack components and validators for radar, paths, zones,
   population, water, CULL/occlusion, audio, timecycle, interiors, and other
   city systems.
7. Add server/admin/client status APIs for cached, restart-required, active,
   refused, and planned packs, with explicit process-lifetime semantics.
8. Provide reproducible pack conversion, canonical-manifest generation,
   conflict reporting, budget estimation, and FLA/GTA UG configuration
   comparison tools.
9. Harden with fuzzing, hostile-server cases, cache/ticket recovery, telemetry,
   long reconnect/restart cycles, and several-hour multi-city endurance tests.
10. Freeze protocol/pack versions, document deployment and rollback, add CI
    fixtures, and retire or isolate the old custom-streaming workarounds.

Practical milestones are: Bullworth without preinstallation; Carcer through the
same generic path; Bullworth and Carcer simultaneously; seamless native flights;
city radar/path/environment support; then production hardening.

## VM and verification quick reference

Follow `AGENTS.md` for full commands and current paths. The essentials are:

```text
Canonical source        /Users/salimtrouve/Documents/GitHub/mtasa-neon
VM shared view          C:\Mac\Home\Documents\GitHub\mtasa-neon
VM-local build source   C:\dev\mtasa-vm-custom
Solution                C:\dev\mtasa-vm-custom\Build\MTASA.sln
Client configuration    Release|Win32
Server configuration    Release|x64
Client log              C:\dev\mtasa-vm-custom\Bin\MTA\logs\logfile.txt
Server endpoint         127.0.0.1:22003 UDP
HTTP endpoint           127.0.0.1:22005 TCP
```

- Edit only the canonical tree, then synchronize to the VM-local tree while
  preserving `Build` and `Bin` and excluding `.git`.
- Do not build from the Parallels shared folder.
- Run `./utils/clang-format.ps1` after C++ changes. If the VM PowerShell version
  cannot run it, use the pinned formatter already available under the VM build
  tree, but inspect the resulting diff.
- Build only the affected projects during iteration, then the smallest complete
  client/server set appropriate to the checkpoint.
- Run focused Python tests and `git diff --check` before requesting gameplay
  validation.
- GUI launches through `prlctl exec` require `--current-user`; otherwise a
  command can report success without opening a visible client.
- Never replace the current custom `netc.dll` with the older MTA 1.6 module.

## Suggested opening message for a replacement orchestrator

```text
Read AGENTS.md, LIMIT_PATCHING.md, NATIVE_WORLD_HANDOFF.md,
utils/extended-world/NATIVE_BW_PACK.md, and the recent native-world commit
bodies completely. Recheck HEAD and the dirty tree before touching files.

Continue with the authorized startup activation research checkpoint described
in NATIVE_WORLD_HANDOFF.md. Orchestrate research/planning and independent
implementation/review agents, define explicit checkpoints, preserve unrelated
changes, and use the canonical macOS-to-VM workflow. Never perform in-game
tests yourself: prepare the build and exact test instructions, then wait for me
to test and report feedback.
```
