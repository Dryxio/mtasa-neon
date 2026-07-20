# Bullworth native pack tooling

`build_native_bw_pack.py` converts the generated `ug-bw` test resource into
ordinary GTA SA streaming artifacts without changing the running client or the
existing Lua city loader. It assigns Bullworth's 952 source models to the exact
compact range `18631..19582`, emits one IDE, one IMG VER2 archive, one merged
COL archive, and seven district-preserving binary IPLs.

The stock GTA root is mandatory because the generator refuses a model-ID
collision and records TXD/COL/IPL slot budgets. Standalone stock archive
inventory accounts for 3607 TXDs, while the audited initialized MTA runtime
pool has 3608 occupied slots because it also registers the stock cutscene TXD
`preq_cargo.txd`. The
manifest records the MTA snapshot plan (`3608..3773`) for review. At runtime
the registrar selects an identity-specific exact pool profile, inventories the
actual occupancy and cursor, then deterministically simulates native
`CPool::Allocate`. `mta-programdata` requires capacity 5000, occupied slots
`0..3607`, cursor 3607, and a reviewed fingerprint for the extra registered
stock cutscene TXD in slot 3607;
`hoodlum-raw` requires occupied slots `0..3606` and cursor 3606. Any holes or
other layout refuse the feature before mutation.

The reviewed `preq_cargo.txd` slot 3607 fingerprint is pool flag `0x01`, null
RenderWare dictionary, zero usages, parent `0xFFFF`, key `0xEA5A8E45`, and streaming entry
`archive=4, offset=13153, size=5` with all three links `0xFFFF`, flags zero,
and state not loaded. Matching only the occupied count is insufficient: a mod
that happens to consume the same slot is rejected.

Generate into a new or empty ignored/temporary directory:

```sh
python3 utils/extended-world/build_native_bw_pack.py \
    --resource test-resources/ug-bw \
    --stock-gta /Users/salimtrouve/Documents/GTA-SanAndreas \
    --output /tmp/mtasa-neon-bw-native
```

The generator parses all emitted artifacts back before reporting success.
`manifest.json` contains source/native IDs, model metadata, TXD slot/name plans,
and IMG offsets. `validation.json` and `validation.txt` contain counts, bounds,
cross-reference results, archive sector data, pool budgets, and required model
store capacities. They also enforce the 327,680-byte native collision read
buffer contract against every individual COL record (Bullworth's current
maximum is 256,716 bytes).

## Experimental native runtime

The native runtime is disabled by default and currently supports only two
exact GTA SA 1.0 US identities: the raw HOODLUM executable and MTA's audited
ProgramData runtime copy derived from it. It relocates the three IDE model
stores, raises the native
single-COL read buffer to 327,680 bytes, and registers the generated Bullworth
pack through GTA's own startup streaming path. Install these three runtime files:

```text
MTA\data\extended-world\bullworth\bw.ide
MTA\data\extended-world\bullworth\bw.img
MTA\data\extended-world\bullworth\native-world.json
```

`bw.col` and the seven binary IPLs are already entries inside `bw.img`; the
runtime does not need the large audit `manifest.json` or text validation
reports. `native-world.json` is the deliberately small, versioned runtime
schema. Paths passed to the
native loaders must currently be ASCII and shorter than `MAX_PATH`.

The runtime is now split into a generic static-world pack manager and an
immutable Bullworth policy. `CNativeWorldPackManagerSA` owns startup-hook
installation, exact preflight, mutable pool-allocation planning, native commit,
postconditions, IPL bootstrap, lifecycle state, and the reconnect-safe
streaming-buffer floor. `CNativeBullworthPackSA` contains only trusted client
policy: the opt-in variable and activation directory, file/archive/model count
ceilings, coordinate bounds, native pool capacities, stock occupancies,
executable-specific TXD fingerprints, and expected archive slot. The minimal
runtime manifest contains only its format and pack ID plus IDE/IMG leaf names,
byte lengths, and hashes. Model range/store deltas, TXD inventory, the unique
COL name, ordered IPL names, archive counts/sectors, and maximum entry size are
derived from the validated bytes instead of being redundant manifest claims.
The manager derives the even streaming-buffer
minimum from that validated maximum rather than storing a second 4,008-sector
constant.

This is a constrained static-world pipeline, not arbitrary IDE support. The
closed grammar proven by Bullworth permits `objs`/`tobj` IDE sections, DFF/TXD
entries, exactly one merged COL, and standalone binary IPLs. Format 1 remains
the immutable Bullworth policy. Format 2 identifies the compiled
`static-world-v1` audit profile plus an untrusted bounded pack ID. Its E1 route
is transport/cache only; E2 adds explicit one-shot authorization/startup for an
exact existing v2 object. Hot registration and multi-pack aggregate allocation
are not implemented yet. The existing
Bullworth environment flag, executable allowlist, trusted pool budgets,
registration order, diagnostics, and process-lifetime behavior are unchanged.

The JSON schema is closed: unknown or missing fields, duplicate JSON keys,
unsafe leaf paths, non-ASCII strings, non-uint32 numbers, and unsupported format
versions are rejected. A small local parser is used so `game_sa.dll` does not
gain a dependency on the deathmatch module's JSON-C library. The runtime then
independently reparses the IDE, complete IMG directory, and every binary IPL;
it derives and bounds their real names, types, counts, inventories, sizes,
model-store types, placements, coordinates, and maximum entry.
IDE rows additionally require safe native stems, one mesh, finite positive draw
distance, bounded uint flags, unique/balanced sections, and valid timed-object
hours. The three legacy files must be direct regular non-reparse children of
the seed directory. Before installing the GTA startup hook, the client copies
them into the common ProgramData cache at
`<MTA-data>/native-world-cache/v1/<policy-key>/<content-id>`. The content ID is
the SHA-256 of a domain-separated canonical tuple containing format, compiled
policy key, and both payload sizes and hashes. Source filenames are only locked
seed locators; cached files always use `native-world.json`, `world.ide`, and
`world.img`, so renaming identical source data cannot duplicate the large cache
object. The ID is stable across JSON whitespace or key order and is content
identity only: it does not prove who supplied or authorized the bytes. The
exact seed-manifest hash is still checked while the seed directory and all
three files are locked whenever a missing cache object is populated.

The cache writes a fixed canonical format-1 manifest, copies the payload into
a cryptographically random same-volume quarantine directory, flushes and
validates every file, then closes quarantine guards and publishes with one
directory rename. It immediately acquires final no-write/no-delete guards and
repeats sizes and hashes before returning the path. Existing entries undergo
the same guarded validation. A corrupt or power-loss object is moved away from
the semantic address and rebuilt; invalid objects are never traversed because
they could be junctions. Each directory component is opened without delete
sharing and verified by handle as a local non-reparse directory before the
next child is created.

The published manifest, IDE, and IMG use read sharing only. Their handles and
the verified cache-directory chain form a pending lease. Any refusal before
the irreversible IDE commit releases that lease; successful native activation
promotes it to process lifetime. GTA may therefore reopen the IDE at commit and
the IMG during later streaming without a replacement or ancestor rename
redirecting those path reads. This closes the earlier validation/use TOCTOU
without current-session activation: a different pack still requires a clean
client restart.

The environment-flag prototype still reads the installation copy of
`native-world.json` at each startup as its temporary local selector. After one
successful publication, `bw.ide` and `bw.img` may be removed from the install
seed and subsequent cache hits never read them there; keep the small selector
manifest in place. Removing that final install dependency requires the next
phase's authenticated, session-bound activation ticket. Cache content identity
alone is deliberately not authorization to choose a pack.

### Server transport and publish-only cache checkpoint

A resource can now distribute the three files through the normal resource HTTP
path by declaring an exact format-1 group:

```xml
<file src="native/native-world.json" download="true" native_world="true" />
<file src="native/world.ide" download="true" native_world="true" />
<file src="native/world.img" download="true" native_world="true" />
<native_world format="1" manifest="native/native-world.json" />
```

The descriptor and its three tagged files use a version-gated ResourceStart
encoding. A legacy client receives neither the descriptor nor the tagged files,
so it cannot partially interpret the new group. The server streams large file
bodies through a 64 KiB buffer instead of retaining an entire IMG in memory.

The capable client accepts exactly three unique automatic-download files: one
named `native-world.json`, one IDE, and one IMG. Their declared ceilings are
4 KiB, 1 MiB, and 256 MiB respectively. The downloader rejects missing or
mismatched Content-Length, received-byte overflow visible through the external
net module's status API, final disk-length divergence, and checksum mismatch.
The net module does not expose its write callback through the in-tree ABI, so
the exact cap cannot be enforced inside that callback; the prequeue, progress,
completion, disk-length, and checksum checks are the available in-tree
boundaries.

After normal checksum verification, a cancellable worker performs the full
closed Bullworth audit without holding resource objects across threads. It
copies and hashes the files into a cryptographically random same-volume
quarantine, validates the copied bytes while verified no-write/no-delete
handles are held, publishes with one directory rename, then reacquires and
revalidates the immutable final object. Existing content-addressed objects take
the same guarded validation path. Lifecycle teardown retires or cancels the
worker without allowing it to access a destroyed resource.

Transport publication is deliberately inert. It does not call `AddArchive`,
install hooks, mutate pools or model stores, select an environment pack, acquire
an activation lease, or authorize the content. A successful line therefore
ends with `audit=closed-bullworth publish=atomic activation=no lease=no
restart-required=yes`; `disposition=published` and `disposition=hit` distinguish
a new immutable object from a validated cache hit. Publication failure retains
stock behavior.

The transport cache allows at most four content objects total under the pack
policy, at most 1 GiB of counted data, and requires the requested bytes plus a
64 MiB free-space margin. It safely collects only recognizable, verified
private quarantine/recovery remnants. Unsafe siblings, reparse points,
unverifiable entries, quota exhaustion, and immutable content conflicts are
refused rather than deleted or reused.

The repository test resource at
`test-resources/native-world-transport-test/meta.xml` contains only the
declaration; its audited Bullworth payload remains local test data. Validation
covered a fresh resource download, publication after moving the previous cache
object aside, exact SHA-256 equality with that known-good object, and reconnect
reuse with `disposition=hit`. The Game SA and Client Deathmatch Release/Win32
builds passed, and the extended-world Python suite reported 38 passing tests
with two optional skips.

### Generic static-world format-2 publication checkpoint

Checkpoint E1 adds a second closed descriptor without reinterpreting format 1:

```xml
<file src="native/native-world.json" download="true" native_world="true" />
<file src="native/world.ide" download="true" native_world="true" />
<file src="native/world.img" download="true" native_world="true" />
<native_world format="2" policy="static-world-v1"
              manifest="native/native-world.json" />
```

The manifest root is exactly `format`, `policy`, `pack_id`, and `files`.
`policy` must be `static-world-v1`; `pack_id` must match
`[a-z0-9_-]{1,15}`. The content ID uses the separate
`mta-native-world-cache-content-v2` domain and binds the format, policy, pack
identity, and both payload sizes and hashes. Objects live at
`native-world-cache/v2/static-world-v1/<content-id>`; the untrusted pack ID is
never a directory component or a selector for parser budgets, executable
patches, pools, archive slots, or native paths.

The E1 wire extension has its own append-only capability. A client without that
exact capability receives neither the format-2 descriptor nor its tagged
files. The E1 capability accepts format 2 only as an `N` group; E2 uses a
separate appended capability for `A`. Publish-only operation reuses the same
complete closed payload audit, immutable cache discipline, shared policy
quotas, and cancellation rules. It cannot create an authorization record,
retain or recover an activation lease, select startup content, install a hook,
or mutate native GTA state. Success must therefore report
`audit=closed-static-world-v1 publish=atomic activation=no lease=no
restart-required=no`.

`test-resources/native-world-static-transport-test` is the metadata-only E1
harness. Its initial live fixture may reuse Bullworth bytes to prove that the
generic identity and cache route are distinct; that does not prove a second
city or generic activation.

The authorized E1 live fixture did reuse the known Bullworth payload. A fresh
session published format-2 content ID
`668bd36a1a2f686975277291032a2d3bef6048057660310d2673d4f5403fa645`
under the v2 tree, and a clean second session reported `disposition=hit` for
the same ID. Both diagnostics ended with `activation=no lease=no
restart-required=no`; the activation store contained no format-2 record. A
following format-1 session retained the established Bullworth content ID and
pending-authorization behavior.

### Generic static-world format-2 authorization/startup checkpoint

Checkpoint E2 adds one exact startup tuple without changing E1 or format 1:

```xml
<native_world format="2" policy="static-world-v1"
              manifest="native/native-world.json" startup="true" />
```

The server emits authorization metadata only to clients advertising the
append-only format-2 startup capability. A capable E1-only client receives the
same inert `N` descriptor; it is never silently upgraded. The client accepts
only authorization wire version 2, one-shot startup mode, format 2, and
`static-world-v1`. Every other cross-product is a closed protocol refusal.

After the exact object is audited and published, Core may persist the same
short-lived, endpoint/server/resource/content-bound authorization record used
by format 1, now typed by the `(format, policy)` pair. A clean launch for the
canonical numeric endpoint performs existing-object-only v2 lookup, locks and
fully reaudits the object, completes the executable allowlist preflight, claims
the ticket, prepares model stores, revalidates the second session, and then
uses the common manager commit path. The typed pending lease becomes a typed
process lease only after native postconditions succeed. Format-2 seed/local
startup remains forbidden, and an absent or corrupt exact object causes a
terminal ticket-qualified `cache-invalid` refusal without recreation.

`test-resources/native-world-static-startup-test` is the metadata-only E2
harness. Its authorized live fixture intentionally reused the same Bullworth
bytes and v2 content ID as E1. Ticket `f9d7b810` completed restart, exact-cache
reaudit, claim, native registration, and `state=active lease=process`; the
registrar reported archive 6, 952 models, 166 TXDs, collision slot 252, and
seven IPL slots. This proves the generic authorization/startup route, not a
second city. A pending ticket was revoked by ResourceStop, and another pending
ticket refused `cache-invalid` while the exact object was quarantined. The
object was not recreated and all three restored hashes were unchanged.

The compiled model-store policy records the aggregate static-world foundation
capacities (`32000` Atomic, `512` DamageAtomic, and `1024` Time). The frozen
four-city inventory requires `24339`, `152`, and `640`, leaving explicit
headroom of `7661`, `360`, and `384`. This checkpoint changes no FileID
partition: IDs at or above the stock DFF boundary remain invalid until the
separate FileID/streaming-table relocation is complete. Preflight reads the
capacities back from the actual relocation manifest, requires an exact match
with the selected compiled policy, then requires exact stock occupancy and
proves each derived IDE addition fits before `AddArchive` or any pool mutation.
It also hashes every DFF stem with GTA's own `CKeyGen::GetUppercaseKey` routine,
rejects collisions
within the pack, and scans all 20,000 occupied model-info pointers for the same
key. Stock model infos retain only the key rather than the original source
name, so a stock collision diagnostic can identify its model ID, key, and the
custom stem but not reconstruct the stock spelling.

### FileID runtime abstraction and stock-only relocation

The abstraction baseline on `master` validates ten HOODLUM instructions and
captures the complete stock layout before any native-world startup path can
install a patch:

```text
DFF 0          TXD 20000      COL 25000      IPL 25255
DAT 25511      IFP 25575      RRR 25755      SCM 26230
loaded 26312   requested 26314 total 26316
```

The isolated corrective follow-up validates and installs this intermediate
layout before the stores/pools checkpoint:

```text
DFF 0          TXD 32000      COL 37000      IPL 37255
DAT 37511      IFP 37575      RRR 37755      SCM 38230
loaded 38312   requested 38314 total 38316
```

DFF expands to 32,000. TXD, COL and IPL keep the capacities of their currently
installed pools (5,000, 255 and 256), because native code also uses partition
spans as pool-loop bounds. DAT stays at 64 entries, while IFP, RRR and SCM keep
their stock counts. Paths/nodes, missions and new DAT/IFP/RRR/SCM content remain
outside the static-world scope.

`CGame` now exposes the captured layout plus the model-pointer and streaming
arrays. Game SA, Multiplayer SA, Client Core and Client Deathmatch consume that
runtime state instead of module-load captures, stock `20000/25000` arithmetic,
or absolute ASM indexing. Naked multiplayer hooks receive an MTA-owned pointer
captured before hook installation. Model/TXD Lua image operations derive both
partition bases and counts from the runtime layout.

The capture manifest is `Client/game_sa/CFileIDRuntimeSA.Manifest.inc`; the
generated relocation manifest is `Client/game_sa/CFileIDRelocationSA.Manifest.inc`.
Runtime startup checks the PE32 headers, exact instruction bytes and operands,
strict partition ordering, sentinel positions, table stride/count, and full
readability of both arrays. It rechecks all 1,398 relocation writes directly
before commit. A mismatch aborts before FileID mutation. Successful diagnostics
first end in `state=prepared ... nativeWrites=no`, then
`state=installed ... total=38316 ... nativeWrites=yes datExpansion=no
pathsExpansion=no`.

The relocation reserves 32,000 DFF IDs and relocates the remaining current-size
partitions. The final 8,000 TXD, 512 COL and 1,024 IPL spans move atomically
with their stores in the next checkpoint. It patches all relocated table
pointers and pure base operands, makes 34
16-bit streaming-list reads unsigned, preserves the `nextModelOnCd` `0xFFFF`
termination branch with a dedicated hook, relocates the four sentinels, and
hooks save/load so the vanilla 26,316-byte flag block, including four sentinel
records, remains compatible.

The 1,398-write manifest includes 122 operands/opcodes in MTA's active appended
HOODLUM region above `0x01000000`. Omitting that region sign-extended FileID
40,001 in `CStreamingInfo::AddToList` and crashed at `0x01567513`; the expanded
ProgramData executable is therefore the required regeneration/validation
reference, not the low raw executable alone.

The first 1,398-site live retry exposed why the final store spans cannot be
reserved early. `CStreaming::Update` uses the relocated pointers at
`0x00410B32` and `0x00410BE0` as its COL loop bounds while indexing the
separately allocated stock `CColStore` pool. The 512-entry reservation reached
slot 466 and crashed at `0x00410B57`; only 255 slots existed. FLA implements
the pool-count/allocation changes as a separate patch family. The compact
layout and validator now require exact 5,000/255/256 TXD/COL/IPL spans, and the
test suite pins this crash loop's pointer distance.

The same contract can be checked off-game with:

```sh
python3 utils/extended-world/validate_native_file_id_runtime.py \
  --exe /path/to/gta_sa.exe
python3 -m unittest utils.extended-world.tests.test_native_file_id_runtime
```

The earlier expanded-span build was VM-synchronized and compiled successfully,
but failed the stock-SA live gate above. The compact correction is locally
validated, VM-synchronized with verified hashes and rebuilt successfully in
`Game SA` plus `Client Deathmatch` as `Release|Win32`. It passed the stock-SA,
Bullworth, reconnect and dynamic-replacement live gate recorded below. No
consumer may regain a private pointer or reconstruct a partition from a
constant.

The user-run 2026-07-18 live gate used format-1 ticket `46a33f60`. The exact
cached Bullworth payload passed its semantic and executable preflights, the
authorized restart reached `state=active activation=yes lease=process`, and
the registrar committed archive 6, 952 models, 166 TXDs, collision slot 252
and IPL slots 191 through 197. Post-registration diagnostics reported
`14854/32000` Atomic, `136/512` DamageAtomic and `175/1024` Time. Those values
remained unchanged after an exact reconnect, with no fatal, capacity,
preflight, exception or crash diagnostic. Requests below the pack's streaming
floor were safely clamped to 4008 blocks. Once active, duplicate transport
offers were refused with `existing-native-world=preserved`, which is the
intended process-lease behavior rather than a failed activation.

The separate FileID-runtime baseline used format-1 ticket `7a1a461a` on
2026-07-18. Its stock launch and authorized replacement process both emitted
the complete captured layout ending in `total=26316 ... nativeWrites=no`.
Bullworth activated with the same archive, model, TXD, COL and IPL plan above;
an exact reconnect revalidated the same process lease, preserved the model
store occupancy and 4008-block streaming floor, and `/nativebw` returned to the
native city afterwards. No new client/server dump, FileID mismatch, native
preflight failure, capacity failure, exception or fatal diagnostic appeared.
This proves the abstraction against the stock table; it does not constitute a
runtime test of the new 38,316-entry compact relocation or authorize any new
streamed content type.

The separate compact-relocation gate completed on 2026-07-20 with format-1
ticket `7f93d606`. The client progressed from the captured stock layout
(`total=26316`, `nativeWrites=no`) to the installed target layout
(`total=38316`, 1,398 patch sites, `nativeWrites=yes`, `datExpansion=no`,
`pathsExpansion=no`). Bullworth registered archive 6, 952 models, 166 TXDs,
collision slot 252 and IPL slots 191 through 197. `/nativebw`, `/nativeback`,
an exact reconnect and a post-server-restart `/nativebw` all passed with the
same active process lease, stable native model-store diagnostics and the
4,008-block streaming floor.

The Perry slice then completed its 285-model TXD/COL/DFF load and exercised the
script replacement lifecycle before resource stop ran `releaseSlice()` and
`engineFreeModel()` cleanup. Perry was returned to `startup=0`; the subsequent
server restart/reconnect left it absent and Bullworth operational. No FileID,
preflight, capacity, exception, fatal, or new-dump diagnostic appeared. This is
observable lifecycle coverage rather than a per-slot high-water proof because
the resource does not log each released slot. The compact stock-only FileID
checkpoint is complete. The next native capacity change must install the
8,000/512/1,024 TXD/COL/IPL stores, matching FileID spans, allocations, loop
bounds, sentinels and validators atomically.

Format 1 accepts exterior static binary IPLs only: every placement has area
flags zero, no LOD link (`lodIndex == -1`), X/Y in `[-10000, 9999]`, and Z in
`[-5000, 5000]`. Every IMG entry has exactly one dot and a safe dot-free stem,
preventing GTA's native extension split from disagreeing with preflight.

Hashes supplied by this untrusted manifest prove that the bytes match its own
claims; they do not authenticate a server or publisher. Resource transport can
now provide cache input, but it is still an unauthenticated, self-asserted
source and cannot select or activate a pack. A later server activation record
must authorize the exact semantic content ID. The closed payload validator also
parses and bounds the reviewed RenderWare DFF/TXD and COL grammar before the
cache path reaches GTA.

Random quarantine or invalid siblings that cannot be safely verified are left
inert and are never activation candidates. The transport path now applies a
verified non-reparse remnant collector and object/byte quotas; future activation
work must still bind one exact semantic content ID to the authenticated session.

The switch is read once, before GTA populates its model stores. In the Windows
VM, set it in the process-start environment and then launch the client from the
same PowerShell process:

```powershell
$env:MTA_NATIVE_BW_MODEL_STORES = '1'
Start-Process -FilePath 'C:\dev\mtasa-vm-custom\Bin\Multi Theft Auto.exe' `
  -ArgumentList 'mtasa://127.0.0.1:22003' `
  -WorkingDirectory 'C:\dev\mtasa-vm-custom\Bin'
```

Rollback is a clean restart with the variable removed (or set to anything
other than the exact value `1`):

```powershell
Remove-Item Env:MTA_NATIVE_BW_MODEL_STORES -ErrorAction SilentlyContinue
```

The startup hook always lets GTA load its stock CD directories first. It then
parses the complete IDE and IMG directory and verifies the executable patch,
stock occupancies, free IDs, pool capacities, planned streaming slots, all 166
new TXD names, archive availability, IMG bounds, entry names, and entry sizes
before changing native state. It also verifies the exact reviewed SHA-256 of
both runtime files, including every streamed payload sector. Registration is
process-global and deliberately survives resource stops, reconnects, and
streaming reinitialization. The authorized record-driven route therefore pins
the process to its exact numeric server endpoint and refuses a different
server before unloading the valid session; changing servers requires a clean
restart. The legacy environment-selector route has no server record and is not
covered by that isolation contract. The largest native IMG entry is `bw.col` at 4,007
sectors, so GTA initially rounds its split streaming buffer to 4,008 sectors.
MTA's script-managed IMG inventory cannot see this native archive; while the
registrar is active, its central buffer setter therefore preserves that 4,008
sector minimum across disconnect cleanup and logs any smaller request as
`[NativeBW] streamingBuffer=request-clamped`. Do not run
the old `ug-bw` custom-streaming resource in the same process because it owns
the same models and placements through a different lifecycle.

Failures before the IDE commit are fail-soft: the registrar either has not
mutated state or rolls back its archive and empty TXD slots, logs
`[NativeBW] registrar=refused`, and continues with the stock world. Once GTA has
constructed the IDE model entries, there is no safe complete inverse. A failed
postcondition therefore logs `[NativeBW] registrar=fatal` and terminates that
opt-in process instead of continuing with partially registered native state.
Success logs `[NativeBW] registrar=active` with archive 6, 952 models, 166 TXDs,
the actual planned TXD range and span holes, the exact planned COL slot, the
ordered IPL slot list, and 1,126 verified directory entries. The preceding
pool diagnostics give
the selected profile, capacity, occupied/free counts, allocation cursor,
highest occupied slot, holes below that high-water mark, and the simulated
plans. The strict stock layouts currently plan COL slot 252 and IPL slots
`191,192,193,194,195,196,197`; registration refuses any reordered or holey
layout instead of assuming those IDs. The audited current TXD runtime reports
capacity 5000, occupied 3608,
cursor/highest 3607, zero holes, and therefore allocates Bullworth at
`3608..3773`.

GTA creates every IMG-backed IPL slot with dynamic streaming disabled. Stock
`*_stream` IPLs are enabled when their parent text IPL is linked, but these
seven standalone district IPLs intentionally have no text parent. The
registrar therefore enables dynamic streaming for exactly its seven owned
slots after directory validation. It leaves their rectangles in the native
flipped state so the later stock `LoadAllRemainingIpls` startup pass still
calculates their real bounding boxes, inserts them into GTA's IPL quadtree, and
unloads them for normal position-driven streaming.

### In-game checkpoint

Use a clean process with the old `ug-bw` resource stopped. With the switch
enabled, confirm an `active` registrar line before connecting, then test spawn,
teleport or flight to Bullworth, collision in all seven districts, district
transitions, leaving and re-entering the city, one resource restart, and two
disconnect/reconnect cycles. Finish with a clean restart with the switch off
and verify normal San Andreas. The current generated IPLs intentionally have
all `lod_index` values set to `-1`; this checkpoint validates native spatial
streaming and collision, not full long-distance LOD equivalence with GTA UG.

When enabled, the patch refuses unsupported or modified executables before its
first allocation or executable write. The relocated allocations intentionally
remain alive for the process because MTA attaches after GTA's CRT static model
arrays have already been constructed.

The executable allowlist is exact, not signature-only:

| Identity | SHA-256 | Image size | Timestamp | Checksum |
| --- | --- | ---: | ---: | ---: |
| `hoodlum-raw` | `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b` | `0x008B1000` | `0x427101CA` | `0x00DC5BEA` |
| `mta-programdata` | `77485627b4ef17f92819318050d501e171c7ab84ceffe5091b973b9e29f9cc98` | `0x01177000` | `0x437101CA` | `0x00DC29E6` |

Both also require PE32/i386, image base `0x00400000`, and every constructor,
CRT wrapper, pointer, grower, and collision-site byte from the manifest. The
ProgramData variant's appended `.text`, `.init`, `.data`, and `.HOODLUM`
sections do not change those audited sites. Preflight refusal reasons are
written to MTA's debug-event logfile as well as the debugger output.

### Why the FLA CRT count patches are validation-only

FLA also changes sites that OLA does not: Atomic's vector count immediates at
`0x4C5CBB+1` and `0x4C5845+1`, redirects DamageAtomic's constructor/destructor
at `0x4C5D10` and `0x4C5860`, and changes Time's count immediates at
`0x4C5DDB+1` and `0x4C58A5+1`. That is necessary for an ASI loaded before the
GTA CRT initializes the inline stores.

MTA's `game_sa` module is created from the `Direct3DCreate9` hook, after those
CRT constructors ran. The HOODLUM wrappers at `0x84BBF0`, `0x84BC10`, and
`0x84BC50` call the three constructor routines once and register shutdown
wrappers at `0x856230`, `0x856240`, and `0x856260`; those wrappers branch to the
three destructors only during CRT termination. The runtime therefore manually
constructs every new slot and keeps the relocated arrays process-lifetime,
while leaving the already-registered destructors aimed at the original static
arrays. Repointing only those late destructor wrappers would instead make them
destroy storage that the CRT never constructed.

The checked-in manifest validates 16 bytes at all six FLA-only routines and the
complete 10-byte `mov ecx` plus `call`/`jmp` sequences at all six CRT wrappers.
Any changed target refuses the feature before allocation or executable writes.

The validator also inventories every executable immediate equal to each store
base, array base, and Atomic's special `+0x1C` field. Atomic's non-aliased
one-past address (`0xB1BF54`) has no executable references. DamageAtomic and
Time end at `0xB1C934` and `0xB1E128`, which are the bases of adjacent stock
stores; their add paths are count-driven, so those aliased addresses are not
repointed. MTA's later `CModelInfoSA::StaticSetHooks` only hooks
`NodeNameStreamRead` and does not overlap any manifest instruction.

Run the focused round-trip test with:

```sh
python3 -m unittest utils/extended-world/tests/test_build_native_bw_pack.py
```

The test skips when the ignored local Bullworth assets or stock GTA root are
not present.
