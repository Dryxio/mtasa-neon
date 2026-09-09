# Burglary native foundation — 2026-09-09

This checkpoint adds native household pickup and a source-filtered native noise
query for the future Neon RPG burglary V1. It does not implement the burglary
mission. The user authorized compilation and installation on the Windows VM and
explicitly deferred all client launch, computer use and in-game checks.

## Lua contract (client)

```lua
local accepted = pickUpPedObject(localPlayer, object)
local objectOrFalse, state = getPedCarriedObject(localPlayer)
local levelOrFalse = getPedNoiseLevel(localPlayer) -- sample at source position
local heardOrFalse = getPedNoiseLevel(localPlayer, listenerX, listenerY, listenerZ)
local accepted = putDownPedObject(localPlayer)
local cancelled = cancelPedCarryObject(localPlayer)
```

`pickUpPedObject` submits GTA's `CTaskComplexGoPickUpEntity`, which approaches,
faces the object, chooses the pickup height animation, and transfers it to a
secondary HoldEntity task. A `true` return only acknowledges submission.
`starting` means queued; `picking_up` means the native approach/pickup is observed;
`holding` requires the processed hold task and animation association. Existing
`putting_down` and `released` semantics and the `onClientPedCarryStateChange`
event are retained. No timer manufactures a successful pickup. Pickup has a
15-second recovery bound; existing immediate box startup/put-down keep 5/8 seconds.

Accepted stock models, resolved from the stealable lounge groups in
`data/furnitur.dat` against `data/maps/interior/props.ide`:

| Kind | Models |
| --- | --- |
| TVs | 2312, 2316, 2317, 2318, 2320, 2322 |
| Hi-fi | 1809, 2101, 2102, 2103, 2226 |
| Video player | 1783 |
| Consoles | 1719, 2028 |
| Existing crate | 1271 |

The existing `setPedCarryObject(ped, object, "box")` still starts holding model
1271 immediately. Household pickup shares its lease checks: living, grounded,
on-foot streamed executor; one object per holder; one holder per object; unit
scale; no attachments; same interior/dimension; distance at most 3 metres. It
rejects remote players and NPCs this client does not own/synchronize. The native
model collision bounds must exist. Custom replacements of these models have not
been visually validated.

Only the submitting resource can put down/cancel. Cleanup restores native flags,
including the temporary `bIsLiftable` flag needed by GTA's domestic drop path.
Death, damage, ownership loss, teardown, resource stop and deadlines retain the
existing cargo recovery behavior. Native transforms alone do not replicate a
cargo relationship: the burglary server must own reservations, object/reward
state and executor selection. The old cargo fixture remains a crate example.

`getPedNoiseLevel` reads `CEventGlobalGroup::GetSoundLevel`, filtered by the exact
native source entity. Invalid/unavailable/non-owner peds or invalid coordinates
return `false`; no matching positive event returns `0`. The optional position
belongs to the listener; the default samples at the source. Values are native
logarithmic sound levels, not a percentage and not the user's speaker volume.
The query does not consume events, retain history, raycast walls, select a
listener's dimension, create suspicion or provide server authority. Call from a
per-frame client phase and accumulate suspicion in the mission. Global events
are flushed at the start of `CWorld::Process`; phase visibility and coverage of
footsteps/drops/shots still require the deferred gameplay check. MTA NPC/player
classification may also change which events are produced.

## Native evidence and differences

Reference executable: `gta_sa_compact1.0.exe`, PE32 x86,
SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`.
The readable reversed declarations were checked against that binary before
implementation. See [raw evidence](burglary-native-2026-09-09-evidence.txt) and
`test-resources/native-cargo/tests/pickup_retail.py` for repeatable disk checks.

- Root constructor `0x6919C0`: entity at `+0x0C`, group at `+0x2C`, animation
  reference flag at `+0x30`, size `0x34`, type 310. Retail vtable `0x870B98`.
- First `0x693610` computes approach/hold offsets from actual collision bounds.
  Next `0x691AE0` creates a `0x4C` SimplePickUpEntity with animation 317, then a
  `0x3C` secondary HoldEntity with animation 318. Control is `0x691D50`.
- Retail clone `0x692C80` reconstructs from entity/group, dropping transient
  progress. Our private root is `0x38` bytes with a generation at `+0x34`;
  its clone preserves that generation. First/Next/Control require the same live
  object, holder and generation. Cancellation therefore invalidates queued
  script-event clones as well as active pickup. Generations never wrap/reuse.
  The retail root vtable is not patched globally. The original root destructor
  still unregisters its entity/animation and deletes its child.
- Existing Hold/PickUp/PutDown deleting-destructor guards protect leased MTA
  objects from the retail removal flag. No raw task pointer is retained across
  a native script-command submission. Interrupted leaves lose their entity
  references before restoring object ownership.
- Sound aggregation `0x4AB900` calls `0x4B2850` with a source filter and combines
  positive levels using `0x4AC050`. Quiet-event constructor `0x5E05B0` with time
  `-1` captures the source position; the zero vector passed by footsteps/drop
  is not the final event position. Drop tests object bit `0x2000` at `0x6932EB`.
- **Conclusive reversed correction:** `CEvent::GetSoundLevel` clamped distance
  with `min(distance, 1)` in both branches. Retail uses `max(distance, 1)`.
  Both expressions are corrected in the canonical `gta-reversed-dryxio`
  `source/game_sa/Events/Event.cpp`. The Neon query calls the retail executable;
  it does not reproduce the incorrect reversed formula.

## Multiplayer police decision

Neon disables `CWanted::UpdateEachFrame` at `0x53BFF6` and `CWanted::Update` at
`0x60EBCC`; the existing `CMultiplayerSA.cpp` also suppresses wanted helicopters.
The ambient cop adapter documented in `STORY_RUNTIME.md` uses base wander and
suppresses the retail cop scanner. MTA-created peds are `CPlayerPed`, while
retail cop pursuit/arrest code accesses `CCopPed` fields. Giving a ped a police
skin or displaying three stars cannot safely restore solo dispatch/arrest.

For burglary V1, the mission should own an alert/escalation deadline and spawn
its own response actors/vehicle, using the existing safe movement/combat APIs
on the selected executor. Native cop dispatch, arbitrary retail cop tasks and
automatic multiplayer arrest remain outside this checkpoint. No global wanted
or cop hook is enabled here.

## Validation and installation

Disk ABI and stock furniture correspondence checks, existing cargo Lua state and
server specs (39 assertions), Lua syntax/XML and the three required Neon RPG
specs pass. These checks do not prove pickup gestures, path completion,
interior transitions, sound coverage or observer replication.

The repository formatter was attempted under the installed Windows PowerShell
5.1 and rejected that version (requires 7+). Its exact SHA-256-pinned
clang-format 21.1.7 executable was then applied only to the ten owned C++ files.
No unrelated dirty source was synchronized or formatted.

The old binaries and the ten prior VM source files were backed up under
`C:\dev\neon-checkpoints\burglary-native-20260909-1713` before synchronization.


Final Windows build: `Game SA`, `Client Deathmatch` and the affected consumer
`Multiplayer SA` succeed in `Release|Win32`, zero errors. Game SA hook checking
passes. Existing warnings remain (3 link warnings for Game SA, 1301 client
warnings including repeated SDK/vendor declarations, 9 Multiplayer hook warnings).
Core only sees forward declarations and does not consume either extended
interface; no server uses them. Core/netc stay byte-for-byte unchanged.
All ten canonical/VM C++ hashes match; the installed x86 client contains both new
Lua registration names and the `picking_up` state string. No client process was
running at final verification. No client, resource or server was launched.

| Installed file under `C:\dev\mtasa-vm-custom` | SHA-256 |
| --- | --- |
| `Bin\MTA\game_sa.dll` | `aa90520066a620fed0eee681bc2b2c949d032eee3ffbcdaef57172f25665dfc4` |
| `Bin\mods\deathmatch\client.dll` | `263e2dda5758fb065fe985d6695e57e55e12cafadd2bf4548a32aed55b994aa0` |
| `Bin\MTA\multiplayer_sa.dll` | `67e932398f453fb1c6b102c51ad1683a84509501002265809a8358ad33562205` |

The VM's installed `gta_sa.exe` has SHA-256
`a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26`.
Its native-only ABI checks also pass. The full compact-reference check rejects
its obfuscated SCM call-site bytes; the adapter directly calls native tasks and
does not execute those wrappers. The compared pickup root, clone, transition,
control, processing, sound aggregation and vtables are byte-identical. Hold,
drop and quiet-event constructor bodies match except for three five-byte
prologue jumps to push thunks returning to the next original instruction.
Those differences are disassembled in the raw evidence, not called gameplay
proof. The installed GTA executable is unchanged.

The local catalogue and LuaLS outputs were refreshed without dropping any
previous symbol: only `pickUpPedObject` and `getPedNoiseLevel` were added.
`neon catalogue verify` reports zero errors/divergences. Both additions remain
`runtime-only` because the pinned wiki has no semantic signature for them;
this document supplies the human-readable contract. Provenance uses the local
`codex/burglary-catalogue-snapshot-20260909` registration snapshot
`8c0b8dbd3aa2982b07f52b33153f15156ad0e8ad`, a child of the previously pinned local
snapshot `3f39f74a6c087c54f5d0c3f80842628e6fb3f368`. This preserves the user's
existing uncommitted catalogue features. The snapshot branch and already-dirty
generated files are not published as part of the scoped engine source commit.

The reversed attenuation correction is published as `1ac5b12d` in
`Dryxio/gta-reversed-dryxio`. Deferred acceptance remains the user's burglary V1
playtest: pickup on floor/table, carry/drop, cancellation/restart/stream-out,
interior transition, noise coverage and syncer/observer behavior.
