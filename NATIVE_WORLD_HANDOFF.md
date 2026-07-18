# Native world streaming handoff

Last updated: 2026-07-18

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

- By default the user performs every in-game action and decides whether
  behavior is correct. Agents may run gameplay automation only when the user
  explicitly authorizes it for the current work, as happened for Checkpoint C;
  otherwise they stop at a ready build and provide exact test instructions.
- Keep the established orchestrated workflow. For major checkpoints, use a
  research/exploration/planning agent and an independent implementation/review
  agent where their work is useful. Keep file ownership clear and prefer
  read-only parallel work when edits would overlap.
- The primary orchestrator owns the implementation and final decisions. Work in
  short review loops: implement one coherent slice, obtain independent focused
  feedback, verify each finding against the actual code, apply only justified
  corrections, and request a second review of security- or lifecycle-sensitive
  fixes before formatting, tests, and VM builds. Sub-agents are
  counter-reviewers, not automatic decision makers.
- Define explicit checkpoints. Tell the user when a build is ready, what to do
  in game, what result to report, and when the client must be closed.
- Do not commit a gameplay-affecting checkpoint until the user has supplied the
  requested in-game feedback, or explicitly authorized an automated live gate,
  and the resulting evidence is understood.
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
Checkpoint C    163605d59 feat(native-world): activate authorized startup packs
origin/master   7895f0e1e feat(story): match SWEET1 all-wheels gates
VM              Windows 11
VM build tree   C:\dev\mtasa-vm-custom
```

Always re-run `git status --short` and `git log -5 --oneline --decorate`; the
user and other agents work in this repository concurrently. Checkpoint A is
commit `b9ce96d3c`; the VM-helper marker repair is `3c8d608e5`. Both are now in
the history reachable from `origin/master`. Untracked `.claude`, `Tools`,
`game-resources`, `out.dff`, and `test-resources/synchronized-video-screen`
remain unrelated and must not be staged.
Re-establish ownership from the current diff before every later commit rather
than relying on this list.

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

Checkpoint A, the inert authorization record, is implemented in `b9ce96d3c`
and passed both non-game validation and the user-only live gate recorded below.
Checkpoint B, record-driven startup selection without native mutation, is
implemented in `5971c8a67`. Checkpoint C, record-driven native activation, is
implemented and passed the explicitly authorized automated live gate recorded
below. Checkpoint D, explicit safe restart/reconnect UX, is implemented and
passed the explicitly authorized automated live gate recorded below. The next
unfinished global roadmap item is the constrained generic static-world policy.

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
  and report its result. The resource captures the authorization offer on the
  main thread, carries an immutable snapshot through the worker, and asks Core
  to persist only after the exact audit/publication succeeds and the connection
  is still current.
- `Client/core/CNativeWorldAuthorizationStore.*`, Core connection management,
  and `Client/sdk/core/CNativeWorldAuthorization.h` own the connection epoch,
  opaque server/session identity capture, DPAPI-protected authorization store,
  inspection, clear/revoke operations, safe restart scheduling, process-aware
  diagnostics, and prepared-process credential suppression.
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

The legacy `MTA_NATIVE_BW_MODEL_STORES=1` prototype still reads the local
installation copy of `native-world.json` as its developer-only startup
selector. The record-driven path no longer needs that local selector: a clean
launch for the exact canonical numeric URI selects the pending authorization,
locks and fully reaudits the exact ProgramData object, performs the read-only
executable preflight, claims the ticket, and prepares the model stores. The
second session must reproduce the raw server-ID digest, canonical numeric
endpoint, and bitstream version immediately before `StartGame`; only then is
the hook installed and the typed lease committed after native postconditions.
A valid record and the legacy switch together remain terminally ambiguous and
suppress both routes. Successful native registration is process-global and
intentionally survives resource stops and exact-server reconnects; changing
packs requires a clean client restart.

### Server transport and authorization-offer path

A resource declares exactly one format-1 `<native_world>` descriptor and
exactly three tagged automatic-download files: `native-world.json`, one IDE,
and one IMG. The inert legacy descriptor remains valid. The only authorization
opt-in is exactly `startup="true" policy="bullworth"`; partial, unknown, or
contradictory authorization metadata is rejected.

The version-gated ResourceStart packet now has two closed wire forms. Clients
through protocol capability `0x35` receive the original `N` group byte-for-byte.
Clients advertising the appended authorization capability `0x36` receive the
distinct complete `A` group only for an opted-in resource. `A` contains the
common descriptor and file metadata, then the fixed startup/policy values;
truncation, duplicates, bad placement, unknown groups, and unknown values are
fatal. Older clients receive no native-world group and do not request the
tagged bodies.

The built-in HTTP server streams file bodies through a 64 KiB buffer. After the
normal download size and checksum checks, a cancellable worker performs the
complete closed Bullworth audit, copies/hashes into a random same-volume locked
quarantine, audits the copy, atomically renames the directory, and revalidates
the final immutable object. A cache hit follows the same guarded validation
path.

Transport alone still does not authorize or activate the object. An opted-in
offer may publish an inert authorization record after the exact object is
cached. Successful diagnostics contain:

```text
[NativeWorldTransport] state=audit-started ... activation=no lease=no
[NativeWorldTransport] state=cached ... disposition=published|hit ...
    audit=closed-bullworth publish=atomic activation=no lease=no
    restart-required=yes
[NativeWorldAuthorization] state=pending ... activation=no lease=no
    restart-required=yes
```

The separation is essential:

```text
downloaded bytes -> checksum -> closed semantic audit -> immutable cache
immutable cache  != trusted server authorization
trusted authorization + exact cached content + clean startup -> activation
```

### Inert authorization store

Core owns the record rather than `CGame` or the resource worker. It captures
the external network module's opaque server ID, numeric IPv4 endpoint, exact
resource identity, policy/content identity, connection generation, and
authorization epoch before asynchronous work starts, then revalidates the
snapshot before persistence. The full opaque server ID is hashed into the
record; diagnostics expose only bounded correlation data, including the first
eight ticket hex digits.

Records use a canonical manually encoded little-endian format protected with
current-user DPAPI and purpose entropy. The store rejects reparse/ownership
violations, uses a cross-process transaction lock, CSPRNG tickets, a 15-minute
TTL with a bounded 120-second rollback allowance, explicit pending/revoked/spent
states, a live-record cap of 64 and hard enumeration cap of 256, and verified
atomic temp/flush/reopen/rename/final-reopen publication. Verified ambiguity and
terminalization markers prevent a crash or failed rename from silently turning
an uncertain record into an activatable one.

Connection generation and epoch are persisted only as launch-local audit
provenance. Checkpoint B must not compare them with counters from the next
process. A reconnect to the exact same durable identity attaches to the
existing fresh pending record without refreshing its ticket or timestamps;
conflicting identities refuse. Disconnect and normal destruction preserve the
record, while an explicit resource-stop packet revokes it. Console commands are
`nativeworldauth status` and `nativeworldauth clear`. Checkpoint B adds a strict
direct-URI selector, a transaction-held startup record, ticket-qualified spent
receipts, cancellation linearized with claim, and a typed exact-cache lease. It
deliberately adds no Lua API and performs no GTA activation.

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
hash comparisons. The user performed the Checkpoint A in-game lifecycle; after
explicitly broadening authorization, the orchestrator automated the Checkpoint
B/C live gates through the documented VM workflow.

Confirmed checkpoints include:

- Checkpoint C was formatted with the pinned clang-format 21.1.7 executable.
  The focused suite reports 67 tests, including two optional
  environment-dependent skips. Two independent final security/lifecycle
  reviews found no remaining actionable P0-P2 issue.
- The affected client consumers (`Game SA`, `Client Core`, `Client
  Deathmatch`, `Multiplayer SA`, and `Client Webbrowser`) built Release|Win32
  through reviewed `vm-build.ps1` plan/execute and BuildOnly verification runs;
  every project completed with zero errors.
- Ticket `8ba5bfc8` completed the full authorized transaction: exact startup
  selection and cache audit, allowlisted executable preflight, durable claim,
  early model-store preparation, second-session identity validation, deferred
  hook installation, native registrar postconditions, and typed process-lease
  commit. The registrar reported archive 6, 952 models, 166 TXDs, collision
  slot 252, and seven IPL slots.
- An earlier active process using ticket `ce451b6c` survived two exact-server
  reconnects and a server/resource reload. Each new session logged
  `state=session-validated ... activation=active lease=process`; attempts by
  the startup resource to publish another descriptor correctly preserved the
  existing activation.
- The explicitly authorized runtime gate teleported the player to Bullworth
  `(-8148.06, 7648.97)`, returned to San Andreas `(1481.00, -1771.00)`, and ran
  the world-sync regression at Bullworth. COL SET, COL ADD, COL ADD INDEX, and
  MOVE all passed; MOVE ended with zero final error and no regression or
  overshoot. The temporary automation was removed from the canonical and VM
  resource copies after the run.
- The final diagnostic regression launch proved that a post-activation
  transport refusal now reports `activation=active lease=process
  existing-native-world=preserved`, rather than incorrectly implying that the
  active process reverted to stock.

- Checkpoint B was formatted with the pinned VM formatter. The focused suite
  reports 64 tests, including two optional environment-dependent skips, and an
  independent security/lifecycle re-review found no remaining P1/P2 issue in
  the startup transaction, cancellation/claim boundary, lease lifetime, or
  no-mutation path.
- The exact affected consumer set (`Game SA`, `Client Core`, `Client
  Deathmatch`, `Multiplayer SA`, and `Client Webbrowser`) built Release|Win32
  through the reviewed `vm-build.ps1` plan with zero errors and fresh verified
  outputs. Checkpoint B changed no server wire or server binary.
- The explicitly authorized automated live gate claimed ticket `fbdad662` only
  after exact URI selection, complete cache audit, allowlisted executable and
  patch-site preflight, and final cache revalidation. The resulting
  ticket-qualified `.spent` receipt preserved the original pending-file hash;
  the next network session published a distinct ticket. Completion logged
  `activation=no lease=released` and zero native writes, allocations, hooks,
  archives, and pool mutations.
- No-URI and wrong-port launches left the same pending ticket and file hash
  untouched. Temporarily moving the exact cache object aside caused
  `cache-invalid` terminal refusal and a spent receipt without recreating the
  object; restoring it reproduced the original manifest, IDE, and IMG hashes.
  Enabling the legacy environment selector alongside a valid record caused a
  terminal `selector-ambiguous` refusal, with no activation diagnostic.

- The current Checkpoint A tree was formatted with the pinned VM
  `clang-format.exe` 21.1.7 on the exact changed C++ files. PowerShell 7 was not
  available on the host or VM, so the repository wrapper could not be used.
- `python3 -m unittest discover -s utils/extended-world/tests -p 'test_*.py'`
  now reports 53 passing tests and two optional environment-dependent skips.
  The added deterministic model covers golden plaintext/wire records, every
  truncation boundary, trailing/unknown data, capability gating, TTL/bounds,
  publication/teardown, and reconnect attachment without ticket/time refresh.
- The regenerated VM build completed successfully for `Game SA`, `Client Core`,
  `Client Deathmatch`, `Multiplayer SA`, and `Client Webbrowser` as
  `Release|Win32`, plus server `Deathmatch` as `Release|x64`. MSBuild reported
  zero errors and the helper verified all expected outputs, including
  `Bin\server\x64\deathmatch.dll`.
- Independent store/security, wire/test/documentation, and compile/ABI review
  loops found no remaining actionable issue after the orchestrator verified and
  corrected their findings. The final re-reviews were clean.
- The VM runtime resource still contains the known 169.5 MB Bullworth payload;
  only its 437-byte opt-in `meta.xml` was synchronized and SHA-256 verified.
- The freshly built server was started from the VM-local `Bin\server` tree,
  loaded 239 resources with zero failures, and listened as one process on
  `22003/UDP` and `22005/TCP`. `ug-bw` was configured with `startup="0"` and
  `native-world-transport-test` with `startup="1"`. The user launched the
  client only after this non-game preparation was complete.
- The user live gate first cleared the store and observed `state=absent`, then
  connected and published ticket `601ba255` for the exact known Bullworth
  content with `activation=no lease=no`. A complete client process restart and
  reconnect produced `disposition=attached` with unchanged issued/expires
  values, proving that reconnect did not refresh the 15-minute lifetime.
- An explicit server-console resource stop produced client `state=revoked` for
  ticket `601ba255`; `pending.bin` was replaced by the ticket-qualified revoked
  ledger entry and F8 status reported absent. Restarting the resource produced
  the distinct ticket `3b76cf5b` with `disposition=published`; the user then
  observed `state=cleared removed=yes` followed by `state=absent`. Every line
  retained `activation=no lease=no`, and no refusal, crash, hang, native
  registration, or Bullworth activation appeared in the inspected logs.
- The server confirmed both connection/join cycles and the user authorized the
  checkpoint commit. A separate respawn result was not explicitly reported, so
  respawn remains a prescribed general regression case rather than claimed
  evidence for Checkpoint A.
- `utils/vm-build.ps1` was repaired to recognize the stable CEF 147
  `libcef_dll\CMakeLists.txt` marker. CEF 147 removed the prior
  `wrapper\cef_helpers.cc` path, although the pinned package was complete. The
  reviewed bootstrap then restored the missing VM-local CEF dependency and the
  helper completed normally.
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

A server RPC wire regression was also reproduced and fixed on 2026-07-17.
`moveObject`, `setColPolygonPointPosition`, and both `addColPolygonPoint` forms
serialized versioned positions into an intermediate version-zero bitstream,
then copied those legacy-width bits into current recipient streams. The result
was a stationary/snap-teleporting object and corrupted polygon coordinates or
indices. Dedicated semantic packets now serialize into each destination stream
inside `Write`, preserving both legacy and current layouts. The tracked
`test-resources/world-sync-regression-test` is the live regression gate. The
user confirmed all four checks at ordinary coordinates, X=+9500, and X=-9990;
all movement runs had continuous intermediate samples, no regression or
overshoot, and zero final error. Retain this three-position matrix before later
protocol, activation, or multi-pack checkpoints are called complete.

The tracked `test-resources/native-world-transport-test` contains metadata and
instructions only. Its large audited Bullworth payload is intentionally copied
only into the VM runtime resource and is never Git-indexed. Do not commit
generated city assets.

## Known boundaries

- Bullworth is still the only compiled policy; the transport is not arbitrary
  IDE support.
- An opted-in server can cause a strictly bound authorization record to be
  persisted after publication. Checkpoint B consumes it only for closed startup
  selection and one-shot claim; no code activates native GTA state yet.
- Only the legacy developer route depends on the local selector manifest and
  environment flag. The record-driven B route does not.
- The record is bound to the opaque server ID exposed by the established MTA
  session and the numeric endpoint. This is not a claim of PKI, authenticated
  DNS ownership, or any guarantee beyond the external network module.
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

## Authorized startup research checkpoint

The read-only lifecycle, identity, persistence, replay, cache-lease, and
failure-boundary design checkpoint is complete. Its authoritative result is:

```text
utils/extended-world/NATIVE_WORLD_ACTIVATION.md
```

The review found no actionable P0-P2 issue in the publish-only path because it
was inert. Checkpoint A supplies the explicit server/session binding, separate
protocol capability, DPAPI-protected pending record, and conflicting
cross-server refusal. Checkpoint B now supplies exact-cache startup lookup, a
transaction-typed lease, atomic claim, strict startup endpoint matching, and
zero-mutation read-only executable preflight. Second-session identity
revalidation, deferred hook installation, irreversible native commit, and
typed process-lease promotion are now implemented by Checkpoint C.
Checkpoint D adds the explicit local restart command and active-process command
guards without weakening the startup identity boundary.

The server config initializes an identity facility from a private key, and the
client's opaque server ID is the strongest available continuity input. The
mapping and handshake live in the external network module, so the visible tree
does not prove key possession. Describe the record as bound to the opaque ID
exposed by the established MTA session, not as PKI, authenticated DNS
ownership, or a stronger guarantee than the external module documents.

The key lifecycle decision is that record/cache/executable preparation happens
before GTA initialization, while the native pack hook waits for the second
session to reproduce the exact server ID and numeric endpoint immediately
before `StartGame`. This narrows the unavoidable two-launch trust gap without
claiming that the second server can be contacted before early model-store
preparation.

## Completed checkpoint: inert authorization record

Checkpoint A implementation, non-game validation, and the user-only live gate
are complete. Persistence, exact reconnect attachment, explicit resource-stop
revocation, fresh republish, and explicit clear all behaved as designed. Every
observed line retained `activation=no lease=no`; no native registration or
automatic activation occurred.

The completed sequence, retained as the reproducible regression recipe, is:

1. Launch the exact custom `Multi Theft Auto.exe`, open F8 at the main menu,
   run `nativeworldauth clear`, then `nativeworldauth status`; expect
   `state=cleared ...` followed by `state=absent`.
2. Run `connect 127.0.0.1 22003`, wait for publication, and record the complete
   transport and authorization lines. Expect cached `published|hit`, pending
   authorization `disposition=published`, and restart-required yes, always with
   activation/lease no.
3. Run `nativeworldauth status`, record ticket prefix/issued/expires, then close
   the client completely without stopping the resource. Reopen it within the
   15-minute TTL and check status before connecting; expect the same record.
4. Reconnect to the same endpoint. Expect `disposition=attached`, with the same
   ticket prefix and unchanged issued/expires rather than a refreshed record.
5. While connected, type `stop native-world-transport-test` in the server
   console. Expect `state=revoked`; client status must then be absent.
6. Type `start native-world-transport-test` in the server console, wait for a
   fresh pending record, clear it from F8, and verify status is absent.
7. Confirm GTA reaches spawn and record whether movement/respawn was exercised;
   the connect/close/reconnect/resource-stop sequence must have no crash, hang,
   native registration, Bullworth activation, or stock-world regression.
   Preserve the exact client/server logs for orchestrator inspection.

The completed read-only design/research checkpoint covered:

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
7. An implementation plan and independent security/lifecycle review before
   editing activation code.

The authorized-startup sequence is complete:

- **Checkpoint A — inert authorization record (complete):** receive, validate,
  persist, inspect, expire, attach on exact
  reconnect, revoke on explicit resource stop, and clear the record, while
  always logging `activation=no lease=no`.
- **Checkpoint B — startup selection (complete):** validate the
  record at clean startup, locate and fully revalidate the exact immutable
  object, acquire the pending
  activation lease, complete the read-only executable preflight, and only then
  claim the record atomically without committing GTA state.
- **Checkpoint C — native activation (complete):** feed the selected object
  into the existing preflight/commit path, revalidate the exact second session
  before `StartGame`, remove the environment/local-selector requirement for
  this route, and keep rollback/fatal boundaries intact.
- **Checkpoint D — restart/reconnect UX (complete):** expose an explicit local
  restart to the exact fresh numeric endpoint, refuse conflicting loader
  actions, suppress credentials, and make status/clear/restart process-aware.

Each checkpoint needs negative tests for wrong server, wrong content ID,
missing/corrupt cache, expired/replayed/tampered record, disconnect during
publication, resource stop, crash between write and consume, and a modified or
unsupported GTA executable. Request gameplay validation or use explicitly
authorized automation only after the relevant builds, logs, and non-game checks
pass.

## Completed checkpoint: restart and reconnect UX

Checkpoint D adds `nativeworldauth restart`. It inspects a structured pending
record under the authorization transaction, requires at least 60 seconds of
remaining freshness, constructs only the exact canonical numeric
`mtasa://<ip>:<port>` target, and schedules the loader restart only after an
exact write/flush/readback check. An unrelated non-empty loader action is never
overwritten. The restart diagnostic deliberately reports
`credential=suppressed` and never includes a password.

Prepared startup discards the supplied in-memory credential before it can be
copied or used and skips browser-password recovery before the credential-bearing
Deathmatch join. It does not erase MTA's general saved credential storage. This
is intentional: the external network module exposes the server identity only
after the join path begins, so passworded startup authorization is unsupported
until an identity proof is available before credentials could be disclosed.
The restart handoff must not restore password support by adding a password to
the authorization record, loader action, diagnostics, or connection CVARs.

Status now reflects the current process: an active native pack reports
`state=active activation=yes lease=process restart-required=no`. `clear` and
`restart` are refused throughout prepared, session-validated, hook-installed,
and active phases, so an authorization command cannot invalidate or replace a
process-global native registration.

The explicitly authorized automated gate used ticket `4e97a97f`. The first
client scheduled an exact restart and exited cleanly; the replacement process
was launched with `mtasa://127.0.0.1:22003`, retained the same ticket, audited
and claimed the cache, revalidated the session, and reached
`state=active lease=process`. Active status, clear refusal, restart refusal, an
exact `/reconnect`, and the extended-world teleport/line-of-sight/vehicle/camera
sanity at x=7000 all passed. After a clean exit, a launch without a URI reported
`state=absent`; restart was refused and did not exit or alter the loader action.
The focused suite reports 70 tests with two optional environment-dependent
skips. `Client Core` and `Client Deathmatch` each built twice as
`Release|Win32` with zero errors.

After the final credential-ordering and single-write loader hardening review,
ticket `93e09b84` repeated the real restart path: the old process logged the
exact passwordless scheduled action, the loader consumed it once, the new PIDs
retained the ticket, and launch 2 again reached `state=active lease=process`.

This gate did not claim live coverage for passworded servers, wrong-key reuse,
resource-stop races, expiry during restart, unsupported executables, cache
corruption, respawn, or the full world-sync resource. Those remain prescribed
negative/regression scenarios; Checkpoint C already supplied the x=9500,
water, COL synchronization, and `moveObject` live regression evidence.

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
- For Checkpoint C, the reviewed complete build set is `Game SA`, `Client
  Core`, `Client Deathmatch`, `Multiplayer SA`, and `Client Webbrowser` as
  `Release|Win32`. It changed no server wire/build definition, so no server
  rebuild or solution regeneration was required. Recompute the set for D.
- GUI launches through `prlctl exec` require `--current-user`; otherwise a
  command can report success without opening a visible client.
- Never replace the current custom `netc.dll` with the older MTA 1.6 module.

## Suggested opening message for a replacement orchestrator

```text
Read AGENTS.md, LIMIT_PATCHING.md, NATIVE_WORLD_HANDOFF.md,
utils/extended-world/NATIVE_BW_PACK.md, and the recent native-world commit
bodies completely. Recheck HEAD and the dirty tree before touching files.

Resume with global roadmap item 1, the constrained versioned generic
static-world pack policy, in NATIVE_WORLD_HANDOFF.md. Recheck the current local
commits, dirty tree, Checkpoint A/B/C/D evidence, and exact-cache/typed-lease
contract before editing. Preserve unrelated changes, keep the
orchestrator/independent-review loop and VM workflow. Do not perform in-game
tests without explicit user authorization; otherwise prepare exact instructions
and wait for feedback.
```
