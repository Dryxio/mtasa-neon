# Native world authorized startup design

Status: research completed on 2026-07-16; Checkpoint A was implemented and
user-live-validated on 2026-07-17. Checkpoints B, C, and D were implemented and
live validated under explicit user authorization on 2026-07-18. Record-driven
startup selection now performs the typed existing-cache transaction, one-shot
claim, early model-store preparation, exact second-session revalidation,
deferred hook installation, native commit, process-lease promotion, and an
explicit credential-free restart to the authorized numeric endpoint.

Read this together with `AGENTS.md`, `LIMIT_PATCHING.md`,
`NATIVE_WORLD_HANDOFF.md`, and `NATIVE_BW_PACK.md`.

## Outcome

The downloaded native-world cache is content-addressed, but content identity is
not server authorization. The activation route must bind one exact audited
cache object to the server that requested it, survive one clean process
restart, and refuse every ambiguous or replayed state before GTA consumes the
pack.

The implementation will use a two-launch transaction:

1. A capable server explicitly requests startup authorization in the same
   versioned `ResourceStart` group that carries the native-world offer.
2. The client downloads, audits, and atomically publishes the exact object.
3. While the original resource and connection are still current, the client
   writes one short-lived, tamper-evident pending authorization record.
4. A clean process launched for the exact numeric endpoint validates that
   record, fully revalidates and locks the exact cache object, completes the
   read-only executable preflight, atomically claims the record, and only then
   prepares the native model-store foundation.
5. The new network session must reproduce the recorded server identity and
   endpoint before the native pack hook is installed and `StartGame` is
   allowed to begin GTA's irreversible load path.

The record never selects the newest cache object and never authorizes a policy,
format, or content ID other than the exact values it contains.

## Verified lifecycle

The ordering is a hard constraint, not an implementation preference.

- Core parses the process command line in `CCore::CCore`, before the automatic
  connection is attempted.
- `CGameSA` is constructed while GTA is still at the frontend. It currently
  calls `CNativeModelStoreSA::InstallFromEnvironment` before constructing most
  game wrappers, then calls
  `CNativeWorldPackManagerSA::InstallFromEnvironment` immediately after
  constructing `CStreamingSA`.
- Core does not execute the `mtasa://` connection command until the menu has
  pulsed for at least five frames and the external network module is ready.
- `CConnectManager` resolves the requested host to an IPv4 address, starts the
  network, receives the server bitstream version, and loads Client Deathmatch.
- Client Deathmatch reaches `Packet_ServerConnected` before calling
  `CGame::StartGame`. GTA's later loading pass then reaches the native
  `LoadCdDirectory` call currently used by the pack hook.
- Server resources arrive after the client has joined. The native-world worker
  publishes only after the three downloads and the closed payload audit finish.
- A resource destructor cancels its worker and retires the future. A worker may
  still finish an atomic inert cache publication after late cancellation, but
  its result is discarded and must never create an authorization record.
- Native pack registration and its cache lease survive resource stops and
  reconnects for the process lifetime. The client may otherwise connect to a
  different server in the same process, so an authorized process must add an
  explicit endpoint pin.

Relevant code anchors are:

```text
Client/core/CCore.cpp
    CCore::CCore
    CCore::GetConnectParametersFromURI
    CCore::DoPostFramePulse

Client/core/CConnectManager.cpp
    CConnectManager::Connect
    CConnectManager::StaticProcessPacket

Client/game_sa/CGameSA.cpp
    CGameSA::CGameSA

Client/game_sa/CNativeModelStoreSA.cpp
    CNativeModelStoreSA::InstallFromEnvironment

Client/game_sa/CNativeWorldPackSA.cpp
    CNativeWorldPackManagerSA::InstallFromEnvironment
    LoadCdDirectoryHook
    RegisterPack

Client/mods/deathmatch/logic/CPacketHandler.cpp
    Packet_ServerConnected
    Packet_ServerJoined
    Packet_ResourceStart

Client/mods/deathmatch/logic/CResource.cpp
    VerifyNativeWorldTransportReady
    CResource::~CResource

Server/mods/deathmatch/logic/CGame.cpp
    InitialDataStream

Server/mods/deathmatch/logic/packets/CResourceStartPacket.cpp
    CResourceStartPacket::Write
```

## Server identity and trust boundary

The strongest existing continuity input exposed to the client is the opaque
value from `CNet::GetCurrentServerId(false)`. The server initializes an identity
facility from the configured `server-id.keys`; the server configuration
describes that private key as the mechanism that prevents another server from
reading this server's private client files. MTA already uses the opaque client
value to separate resource-private client storage.

This is evidence of intended key continuity, but the calculation and handshake
live in the external `netc.dll`, not in this repository. The visible tree does
not establish a PKI, a signed DNS name, a certificate chain, or an offline
signature over `ResourceStart`, nor does it prove from visible code that the
reported client value demonstrates possession of that private key. The correct
claim is therefore:

> The record was authorized in the established MTA session that exposed this
> opaque server ID; continuity relies on the external network-module contract.

It must not be described as proof of the server operator's legal identity, an
authenticated DNS name, or public-certificate authentication. A copied server
key may reproduce the identity if the external contract is key-derived. If the
external channel does not authenticate the value, an on-path peer may also be
able to report or substitute it. The numeric IP and port constrain the intended
reconnection target but are locators, not cryptographic identities.

The record stores a domain-separated SHA-256 of the complete raw server ID.
The six-character private-directory alias, server display name, external HTTP
URL, player/root IDs, client serial, `last-server-*` settings, and `offerId`
alone are not acceptable server identities.

## Versioned authorization request

Activation must have a capability independent of
`NativeWorldPackTransport`. Append a new bitstream version such as
`NativeWorldStartupAuthorization`; do not reinterpret the existing transport
version.

The server descriptor should remain closed and opt in explicitly, for example:

```xml
<native_world format="1" manifest="native/native-world.json"
              startup="true" policy="bullworth" />
```

For clients with only `NativeWorldPackTransport`, the server continues to send
the existing publish-only `N` group byte for byte. For clients with the new
capability and an opted-in resource, the server sends a distinct complete `A`
descriptor: the same closed transport header, then authorization wire version,
one-shot startup mode, and the compiled policy key, followed by exactly three
`F` chunks. Missing, truncated, duplicate, misplaced, unknown-version, or
unsupported-policy authorization fields are a protocol refusal. Absence of the
new request remains ordinary inert transport.

The server does not need to trust or duplicate the payload's self-asserted
hashes. After the closed client audit, the client binds the request to the
`contentId` it derived from the exact published bytes.

## Connection snapshot

The authorization request is captured on the Client Deathmatch main thread. A
snapshot contains:

```text
connection generation
complete raw server ID digest
numeric connected IPv4 address and game port
negotiated bitstream version
resource name, network ID, and start counter
authorization wire version and compiled policy key
transport format
```

The connection generation is owned by Core, not Client Deathmatch. It is a
monotone process-lifetime counter incremented before every network
reset/start attempt and again when that attempt is aborted, invalidated, or
unloaded. Core exposes only the current value. This prevents a recreated
Client Deathmatch instance or recycled address from producing an ABA match.

The worker receives value copies only. It must not call the external network
module or dereference a `CResource`.

The immutable worker offer separately binds the canonical manifest path and
the three tagged files into `offerId`; the resulting exact cache bytes bind
`contentId`. Those transport values do not live in the Core connection
snapshot.

After a successful cache publication returns to the main thread, Core
recaptures and compares every connection-owned snapshot field immediately
before publishing the pending record. The same live `CResource` control flow
and its immutable captured name/network ID/start counter guard the resource
fields. Cancellation, disconnect, resource stop, resource restart, another
connection generation, a changed server ID or endpoint, or a changed start
counter leaves at most an inert cache object and creates no record.

`offerId` remains useful diagnostics, but it is deterministic over the offer
and contains no current server/session identity. It is never sufficient to
authorize activation.

## Closed record

The persisted plaintext is a manually serialized, canonical binary payload,
not a compiler-laid-out C++ structure. Integer byte order, exact field order,
and maximum encoded length are fixed by record format 1. Unknown flags,
unexpected trailing bytes, invalid enum values, non-canonical strings, or any
length mismatch are refused.

Format 1 binds:

```text
record format and authorization wire version
one-shot startup mode
pack format and exact compiled policy key
exact 32-byte content ID
exact 32-byte offer ID for diagnostics
domain-separated 32-byte server ID digest
numeric IPv4 address and uint16 game port
resource name, network ID, and start counter
negotiated bitstream version
launch-1 connection generation and authorization epoch
client-generated 128-bit CSPRNG ticket ID
client issue time and expiry time
```

The connection generation and authorization epoch are persisted only as
launch-1 audit provenance and as part of same-process idempotence. They must
never be compared with the freshly initialized counters of a later process;
Checkpoint B revalidates the durable server, endpoint, resource, policy,
content, and cache identities instead.

The client fixes the lifetime to 15 minutes beginning only after successful
audit and cache publication. A server cannot lengthen it. Parsing rejects an
expiry interval other than the compiled lifetime, overflow, an expired record,
or a wall clock that has moved materially before the issue time. Wall-clock
freshness is a bounded replay control, not a trusted timestamp service.

The record contains no password, resource file path, cache directory path, raw
server key, hostname, or server-chosen executable path. The cache path is
derived only from the compiled cache root, policy key, and exact content ID.

## Tamper refusal and atomic persistence

The record lives outside resource storage and outside the common content cache
in a directory owned by the current Windows user:

```text
<LocalAppData>/<MTA product>/<major version>/native-world-activation/v1/pending.bin
```

The canonical payload is protected with Windows DPAPI for the current user,
using UI-forbidden operation and an application/domain-specific purpose. DPAPI
integrity detects modification, truncation, copying from another Windows user,
or an invalid envelope. It does not defend against malware or an administrator
running as the same user; such an attacker can also replace the client or its
GTA executable, and that limitation must remain documented.

Persistence follows the cache's established Windows safety model:

1. Require a fixed local drive and a bounded canonical absolute Windows path,
   represented with wide-character APIs so a legitimate Unicode user profile
   remains usable.
2. Open and verify every parent as the expected non-reparse directory by
   handle, without delete sharing.
3. Refuse unsafe or unknown siblings rather than following or deleting them.
4. Create a cryptographically random same-volume temporary file with
   `CREATE_NEW` and no write/delete sharing.
5. Write the complete envelope, flush it, close it, reopen it by handle, verify
   final path/type/size, unprotect it, and reparse it completely.
6. Publish `pending.bin` with one write-through same-volume rename. An existing
   unexpired different pending record is a conflict, not last-wins. Repeating
   the exact same authorization is idempotent: it leaves the existing ticket
   and original expiry unchanged.
   A reconnect with only new launch provenance may attach the new live
   resource for later revocation, but likewise cannot refresh the ticket or
   expiry.
7. Keep a separately flushed and verified ambiguity marker until the final
   pending record has been reopened, then remove it before reporting success.

Recognizable temporary or spent remnants may be collected only through the
same handle-verified deletion path. A spent ledger is indexed by ticket ID. A
new pending ticket may coexist with older, different spent ticket IDs; the same
ticket present as both pending and spent is a replay and is refused. Unknown
files, reparse points, multiple pending records, duplicate ticket IDs, or an
unprovable crash state cause stock behavior and no unsafe cleanup traversal.

Checkpoint A must provide engine diagnostics to inspect the decoded metadata,
expiry, and state and to clear a pending record deliberately. Untrusted Lua
resources do not receive the DPAPI blob or a general activation primitive.

## One-shot and crash behavior

Record states are:

```text
absent -> pending -> claimed/spent
```

A process launched for the exact recorded numeric endpoint takes an exclusive
activation transaction lock, validates the record and exact existing cache
object, acquires its pending cache lease, then atomically renames
`pending.bin` to a ticket-ID-qualified spent name before the first executable
write. That rename is the durable one-shot claim.

- A crash before the claim leaves a short-lived pending record.
- A crash after the claim burns the record. The next launch refuses the spent
  remnant and requires a fresh server authorization.
- A launch without a direct connect URI, or for a different endpoint, does not
  activate the record. It may leave it pending only until its short expiry.
- A claimed/spent record is never restored to pending.
- Restoring an old pending file is bounded by DPAPI integrity, the expiry, the
  exact endpoint and server ID, and the one-pending transaction rule. A bounded
  spent-ticket ledger rejects the same ticket ID within its possible lifetime.
  Expired spent entries are pruned only through verified-handle deletion, and a
  distinct freshly authorized pending ticket remains eligible.

This deliberately chooses a recoverable extra download/cache-hit cycle over
ambiguous replay after a crash.

## Exact-cache lookup and typed lease

The current cache activation API is unsuitable for authorization as-is. It can
recover an invalid object from a local seed, and its pending/process locks are
global vectors not typed by an authorization transaction.

Add an existing-object-only lookup that:

- derives exactly `<cache>/v1/<policy>/<contentId>`;
- never scans for a recent object and never rebuilds from a seed;
- holds verified non-reparse parent, directory, manifest, IDE, and IMG handles;
- requires the canonical manifest, exact sizes, exact hashes, and recomputed
  semantic content ID;
- repeats the complete closed IDE/IMG/IPL/RenderWare/COL audit while the object
  is locked;
- returns an opaque RAII lease bound to `(policy, contentId, ticketId)`;
- can be committed only with that exact transaction token;
- releases on expiry, cancellation, endpoint/server mismatch, precommit
  refusal, or Checkpoint B completion; and
- promotes its handles to process lifetime only after native registration
  postconditions succeed.

Missing, corrupt, conflicting, reparse, or unauditable cache state performs an
explicit verified refusal transition from pending to spent and continues with
stock behavior. This terminal refusal is distinct from an activation claim and
occurs only after the authorization record itself was validated. Ticket
consumption never repairs cache content.

## Two-launch state machine

### Launch 1: publish and authorize

```text
server authorization request
  -> exact ResourceStart group
  -> download size/checksum verification
  -> closed worker audit
  -> locked quarantine audit
  -> atomic cache publication or exact hit
  -> main-thread connection/resource recheck
  -> DPAPI pending-record publication
  -> activation=no, lease=no, restart-required=yes
```

An explicit server resource-stop packet after record publication revokes that
resource's still-pending record. Generic Client Deathmatch destruction during
disconnect or process shutdown must not revoke it, because the clean restart is
the intended transition. The implementation must distinguish those paths
rather than clearing from `CResource::~CResource`.

Revocation first writes and verifies a durable terminalization marker, then
renames the pending record to its ticket-qualified spent name and verifies it.
If marker creation fails, it attempts the same-volume spent rename directly;
if that also fails, it removes the exact pending file through a verified
handle. Any surviving recognized marker blocks startup until explicit clear.

### Launch 2: prepare, reconnect, commit

The first implementation accepts only a direct `mtasa://<numeric-ip>:<port>`
startup target matching the record. This removes DNS re-resolution ambiguity.
Checkpoint D lets Core produce and relaunch that exact numeric target through
the explicit local `nativeworldauth restart` command.

The normative order before any native mutation is:

1. Take the exclusive activation transaction lock.
2. Parse the DPAPI record, match the direct numeric startup endpoint, and check
   its freshness, wire/record/pack versions, and compiled policy.
3. Open, fully audit, and lease the exact existing cache object.
4. Validate the executable identity and patch manifests read-only.
5. Recheck cancellation and time immediately before the claim. Reject if
   `now > expires`, if `now + 120 seconds < issued`, or if
   `expires - issued != 900 seconds`.
6. Atomically claim the record.
7. Begin native mutation only after the durable claim succeeds.

If strict cache or executable validation fails before step 6, the implementation
uses the explicit pending-to-spent terminal-refusal transition described above;
it does not pretend that an activation claim or lease commit occurred.

Checkpoint B stops here, releases the lease, logs `activation=no`, and performs
no native allocation, executable write, hook installation, or GTA registration.

Checkpoint C prepares the model-store foundation early enough for GTA's
initialization. It does not install the native pack hook yet. The process is
pinned to the recorded numeric endpoint; any attempt to connect elsewhere is
refused.

When the second session reaches `Packet_ServerConnected`, the client compares
the current raw server ID digest, numeric endpoint, and negotiated bitstream
version with the claimed record. Only an exact match authorizes installation
of the already-prepared pack hook immediately before `CGame::StartGame`.
Mismatch terminates the prepared process with a clear diagnostic; it never
connects to another server and never attempts a hot rollback.

Once a Checkpoint C process has claimed the ticket or prepared native stores,
every failure before this identity match is also process-terminal. This
includes `StartNetwork` failure, timeout, user cancellation, connection
refusal, wrong mod, incompatible version, unload, and return-to-menu paths. An
implementation may later add a strictly bounded in-memory retry to the same
numeric endpoint while the same transaction remains unexpired, but it must
never expose an unpinned menu or accept another server in that process.

The exact 120-second backward-clock tolerance and the ordinary expiry check are
repeated immediately before installing the pack hook and calling `StartGame`.
Expiry or clock refusal at that point releases the lease and terminates the
prepared process.

The existing native failure boundary remains authoritative:

- every failure before `LOAD_OBJECT_TYPES` releases the pending lease and
  preserves stock world registration where possible;
- `LOAD_OBJECT_TYPES` is the irreversible IDE commit;
- failure after that point is fatal; and
- successful postconditions promote the exact typed lease to process lifetime.

The environment/local-selector route remains a separate developer path during
migration. A common startup selector must inspect both sources before the
current `CNativeModelStoreSA::InstallFromEnvironment` call or any replacement
can write executable memory. A valid record and the legacy environment switch
present together are ambiguous and make that selector refuse both before
either route mutates state; neither route may select itself independently.

## Progressive implementation checkpoints

### Checkpoint A: inert authorization record (complete)

- Add the separate protocol capability and explicit server resource opt-in.
- Capture the connection/resource snapshot on the main thread.
- After successful publication, validate, persist, inspect, expire, revoke, and
  deliberately clear the DPAPI-protected record.
- Keep all success diagnostics at `activation=no lease=no`.
- Do not read the record during game startup and do not touch model stores,
  executable bytes, native archives, GTA pools, or activation leases.

### Checkpoint B: startup selection without native mutation (complete)

- Match a direct numeric startup endpoint.
- Validate the one-shot record without claiming it yet.
- Perform existing-object-only complete cache revalidation and acquire the
  typed pending lease.
- Perform read-only executable/patch-manifest validation and recheck freshness.
- Claim the record atomically only after those checks, then release the lease
  deliberately.
- Keep all diagnostics at `activation=no` and make zero executable writes.

### Checkpoint C: native activation (complete)

- Feed the record-selected object into the existing preflight/commit path.
- Prepare model stores before GTA population.
- Reverify server identity and endpoint before installing the pack hook and
  entering `StartGame`.
- Remove the environment/local-selector requirement for this route.
- Pin the active process to the authorized server and preserve all current
  rollback/fatal boundaries.

### Checkpoint D: restart and reconnect UX (complete)

- Expose an explicit user-driven clean restart to the recorded numeric endpoint
  only while the process is still unprepared.
- Require a structured fresh record with at least 60 seconds remaining, refuse
  a conflicting loader action, and verify the exact scheduled action after its
  durable write.
- Discard the supplied in-memory credential and skip saved-password recovery in
  the prepared launch before the credential-bearing Deathmatch join. This does
  not erase MTA's general saved credential storage. Passworded servers remain
  unsupported until server identity can be verified before a password could be
  sent.
- Report pending versus active process state accurately and refuse clear or
  another restart once native preparation has begun.
- Never add the server password to diagnostics, the authorization record, the
  loader action, or connection CVARs as part of the restart handoff.

## Required negative tests

Checkpoint A must cover, without native activation:

- missing, duplicate, unknown, truncated, and unsupported wire fields;
- legacy transport clients receiving no authorization extension;
- absent server ID, invalid server ID length, changed endpoint, connection
  generation, resource ID, or start counter;
- disconnect or resource stop during download, audit, quarantine fill,
  publication rename, result delivery, record write, and record rename;
- a late worker publication producing an inert cache object but no record;
- record field bounds, non-canonical strings, integer overflow, lowercase/raw
  digest conversion, and unexpected trailing bytes;
- modified, truncated, copied-user, wrong-purpose, or unprotectable DPAPI data;
- expired records, issue time in the future, clock rollback, conflicting
  pending records, and explicit clear;
- reparse parents/files, ancestor rename attempts, unsafe siblings, write/flush
  failure, reopen mismatch, and crash at each persistence boundary; and
- resource restart revocation versus disconnect/process-shutdown preservation.

Checkpoint B additionally covers:

- no startup URI, hostname URI, wrong numeric IP or port;
- wrong policy, pack format, content ID, offer ID, or bitstream capability;
- absent, corrupt, semantically invalid, reparse, or conflicting cache object;
- a cache changed between record validation, audit, and lease acquisition;
- unsupported or modified GTA executable and changed patch-site bytes;
- the same ticket present as pending and spent, duplicate pending records, a
  second claim, copied old pending data, a distinct fresh ticket alongside an
  older spent ticket, and a crash immediately before and after claim;
- `StartNetwork` failure, timeout, cancellation, refusal, wrong mod/version,
  and unload before `Packet_ServerConnected`, proving the prepared process
  never returns to an unpinned menu;
- lease token mismatch, double commit, expiry/cancellation while auditing, and
  release on every refusal; and
- proof that no hook, allocation, pool mutation, archive registration, or
  executable write occurred.

Checkpoint C/D live tests must cover the correct server, wrong server at the
same endpoint/key-change case, different endpoint in the same process, clean
restart, disconnect/reconnect, resource restart, respawn, normal San Andreas
rollback, Bullworth traversal, cache corruption, unsupported executable, and
crash-artifact inspection. Agents normally prepare builds, logs, and exact
steps; they may automate in-game actions only under explicit user authorization.

## Build and review scope

Checkpoint A changes a shared protocol capability plus server resource code,
Client Deathmatch lifecycle code, the Core persistence store, and the shared
Game SA offer type. Adding the Core `.cpp` requires solution regeneration. The
reviewed complete VM build set is:

```text
Game SA                 Release|Win32
Client Core             Release|Win32
Client Deathmatch       Release|Win32
Multiplayer SA          Release|Win32
Client Webbrowser       Release|Win32
Deathmatch              Release|x64
```

Trace any added shared ABI to all producers and consumers before finalizing the
set. Run the focused extended-world tests, new deterministic record tests,
`git diff --check`, and the repository C++ formatter. Review server/session
binding, DPAPI/file atomicity, cancellation, and the no-activation invariant
independently before requesting any live connection test.

Checkpoint A's user-run connection lifecycle verified initial publication,
complete client process restart, reconnect attachment without ticket or expiry
refresh, explicit ResourceStop revocation, fresh resource republish, and
explicit clear. Every observed diagnostic retained `activation=no lease=no`;
the inspected logs contained no native registration or activation.

Checkpoint B's authorized automated live gate verified the exact numeric URI
selection, complete existing-object-only cache audit, typed lease, read-only
allowlisted executable/patch preflight, atomic pending-to-spent claim, explicit
lease release, and a distinct authorization published by the following network
session. No-URI and wrong-port launches preserved the exact pending record hash.
A temporarily absent exact cache object caused a ticket-qualified terminal
refusal without cache recreation; the original object was restored with all
three hashes unchanged. A simultaneous legacy environment selector caused a
terminal `selector-ambiguous` refusal. Every B completion retained
`activation=no` and logged zero native writes, allocations, hooks, archives,
and pool mutations. The five affected client consumers built Release|Win32
with zero errors, and the focused suite reports 64 tests with two optional
environment-dependent skips.

Checkpoint C's explicitly authorized automated gate validated two complete
activations and exact-session binding. Ticket `8ba5bfc8` progressed through
model-store preparation, deferred hook installation, archive/model/TXD/IPL
postconditions, `state=active`, and `lease=process`. An active process using
ticket `ce451b6c` survived two exact-server reconnects and server/resource
reloads. Bullworth travel, return to San Andreas, and the COL/moveObject
regression suite passed. The final five-project Release|Win32 build completed
with zero errors, and the focused suite reports 67 tests with two optional
environment-dependent skips. Wrong-key-at-the-same-endpoint, live cache
corruption, unsupported-executable, respawn, and crash-artifact scenarios remain
prescribed negative/regression coverage rather than claimed live evidence.

Checkpoint D's explicitly authorized automated gate validated an exact
credential-free restart using ticket `4e97a97f`. The original client durably
scheduled and read back `mtasa://127.0.0.1:22003`, exited cleanly, and the
replacement process retained the same ticket through audit, claim, session
validation, native commit, and `state=active lease=process`. Active status,
clear refusal, restart refusal, exact-server reconnect, and the extended-world
teleport/line-of-sight/vehicle/camera sanity passed. A later no-URI launch
reported `state=absent`; restart refusal neither exited the process nor changed
the loader action. `Client Core` and `Client Deathmatch` built twice as
`Release|Win32` with zero errors, and the focused suite reports 70 tests with
two optional environment-dependent skips. Passworded-server startup,
wrong-key-at-the-same-endpoint, expiry during restart, resource-stop races,
live cache corruption, unsupported executables, respawn, and the full
world-sync resource remain prescribed coverage rather than claimed D evidence.
After the final credential-ordering and single-write loader hardening review,
ticket `93e09b84` repeated the passwordless restart with new process IDs and
again reached `state=active lease=process`.
