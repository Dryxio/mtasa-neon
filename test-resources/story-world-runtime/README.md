# Story world runtime

This resource owns reusable adapters and lifecycle barriers for reconstructing
SCM-authored mission worlds in synchronized MTA resources.

- `createStoryScmVehicle` applies `CREATE_CAR`'s model-specific
  base-to-centre Z conversion on the authoritative syncer and requires three
  stable client samples before reporting `ready`.
- `createStoryScmPed` applies `CREATE_CHAR`'s verified `script Z + 1.0`
  conversion.
- `createStoryPlayerModelLease` temporarily applies one cutscene-safe player
  model to a participant set and snapshots every original model independently.
  Explicit release, player destruction, caller shutdown and a standalone
  runtime restart all restore the surviving players. Mission resources can
  therefore avoid collisions between an arbitrary multiplayer skin and GTA's
  reserved file-cutscene object slots without permanently changing appearance.
- `createStoryFileCutscene` owns native file-cutscene playback for an immutable
  participant group. Every client acquires and loads its native camera lease
  before the server emits `loaded` and broadcasts the start command. Playback,
  natural completion and native release are separate all-client barriers, so a
  mission cannot advance while one peer is still loading, playing, or holding
  GTA's cutscene camera. Only the declared leader can forward native skip input;
  the owning server resource can also call `skipStoryFileCutscene`, and both
  paths broadcast the same skip command to the whole group.
- `createStoryVehicleRelocation` safely moves one or more vehicles, including
  occupied vehicles across long streaming distances. It leases each vehicle
  and declared occupant on the preserved syncer (or the explicit coordinator
  when no player syncer exists), freezes physics, zeros linear/angular motion,
  extracts and re-warps every declared seat, and keeps the vehicle frozen while
  the server and client prove the target. Only after three stable client
  samples of target position, ground contact, zero motion, sync ownership,
  physics flags and seats does it restore the caller-owned collision/frozen
  policy.
- `createStoryVehicleOccupancy` waits for every target vehicle and actor to be
  streamed on the coordinating client, publishes one server warp at a time,
  and requires three stable client samples of every requested seat before it
  emits `ready`. Native task cohorts can therefore be assigned only after GTA
  has instantiated the occupied vehicles, instead of racing replication after
  a cutscene or world rebuild. Each vehicle keeps its existing syncer, so a
  cooperative support vehicle remains owned by its own driver.
  `stageActors=true` additionally moves non-player actors beside their target
  vehicle before the preflight. This adapts SCM sequences that create an actor
  outside the client's streaming range and immediately insert it into a car;
  callers should use it only behind a mission transition or cutscene frame.
- `destroyStoryWorldElements` arms every participant before destruction and
  reports `ready` only after every client confirms that the old elements no
  longer exist. File cutscenes can therefore never overlap a stale mission
  world merely because server deletion and client presentation were scheduled
  in adjacent frames. Its optional fade-out completes before clients arm the
  destructive transaction. Cancellation, caller shutdown, or a standalone
  runtime restart explicitly restores that fade on every participant, so a
  failed transition cannot strand GTA on a black frame.

Placement, player-model, file-cutscene, vehicle-relocation, occupancy and teardown handles are caller-owned and
emit `onStoryScmVehicleStateChange`, `onStoryPlayerModelLeaseStateChange`,
`onStoryFileCutsceneStateChange`, `onStoryVehicleRelocationStateChange`,
`onStoryVehicleOccupancyStateChange` and
`onStoryWorldTeardownStateChange`. A standalone runtime stop publishes an explicit
`failed` terminal state to every still-active external caller before removing
its handles; consumers can therefore restore mission controls instead of
waiting forever on an operation which disappeared during restart. It also emits
`onStoryWorldRuntimeStopping` before runtime-owned mission elements are destroyed,
allowing callers with no currently pending handle to fail with an accurate
lifecycle reason rather than a later actor-death predicate.

## Safe occupied-vehicle relocation

```lua
local handle, reason = exports["story-world-runtime"]:createStoryVehicleRelocation(
    participants, coordinator,
    {
        {
            vehicle = bmx,
            x = 970.0873, y = -1107.7755,
            scriptZ = 22.8672, heading = 82.97,
            occupants = {
                {ped = leader, seat = 0},
                {ped = sweet, seat = 1},
            },
        },
    },
    {timeout = 20000, stableSamples = 3})
```

Every participant and entry is copied and validated before any mutation. The
coordinator must be a participant. A vehicle with a player syncer requires that
syncer in the participant set and keeps it throughout the transaction; only a
vehicle without a live player syncer falls back to the coordinator for the
streaming/ground oracle. The occupant list must be complete and reproduce the
current unique seat map exactly. The runtime never toggles player controls or
acquires a camera.
Vehicles and actors are reserved across SCM placement, occupancy and relocation
handles, not merely between relocations, so no second runtime transaction can
warp or position an element while this barrier owns it.
Ground proof requires collisions to be enabled and uses the preserved player
syncer as verifier, falling back to the coordinator only when no live player
syncer exists. A frozen source vehicle is valid: the transaction deliberately
keeps it frozen throughout proof and restores that frozen policy after `ready`.
Collision-disabled relocations and destinations where world collision cannot
be loaded reliably must opt into `requireGround=false`. Vehicle and occupant
streaming leases remain active for the whole proof.

Each entry must provide exactly one Z contract. `scriptZ` applies the same
model-specific base-to-centre conversion as SCM `CREATE_CAR`, measured by the
preserved syncer before movement. `centerZ` is already an MTA centre coordinate
and is never adjusted. Optional `heading`, `rx`, `ry`, `interior`, `dimension`
and `requireGround=false` override their documented defaults (current heading,
zero roll/pitch, current interior/dimension, ground required). `timeout`,
`positionTolerance`, `groundTolerance`, and `stableSamples` are bounded.

The public states are `preparing`, `moving`, `verifying`, `ready`, and `failed`.
Listen to `onStoryVehicleRelocationStateChange(state, snapshot)` with the handle
as `source`, or query `getStoryVehicleRelocationState(handle)`. Snapshots expose
the immutable expected targets, per-verifier observations, phase, generation,
and creation/state-change/move/verification timestamps. A caller may release
the handle at any time with `releaseStoryVehicleRelocation`; timeout, participant
or syncer disconnect, element destruction, caller stop, and runtime restart all
cancel client streaming leases and explicitly restore the original vehicle
transform, world, seat map, collision and frozen flags when the transaction
fails. Rollback first reasserts the captured original syncer, then keeps the
vehicle frozen through the same three-sample client proof and a final server
readback before restoring caller-owned physics. Failure snapshots include the
expected and observed server/client values needed to diagnose the first
divergence.
Active release is therefore asynchronous: it initiates rollback and reaches
`failed`; a second release of the terminal handle destroys it. `ready` is emitted only after every verifier has
reported at least three consecutive stable samples for every assigned vehicle.

## Grouped file-cutscene contract

Create a scene from a server mission resource with:

```lua
local handle, reason = exports["story-world-runtime"]:createStoryFileCutscene(
    participants, leader, "SWEET1A", 0,
    {
        allowLeaderSkip = true,
        loadTimeout = 15000,
        playTimeout = 180000,
        releaseTimeout = 10000,
        fadeIn = 1.0,
    })
```

`participants` is copied and must contain unique live players; `leader` must be
one of them. The native filename is restricted to 1 through 7 alphanumeric or
underscore characters, matching GTA's file-cutscene name field. `visibleArea`
is optional (`nil`) or an integer from 0 through 255. A player cannot belong to
two live grouped cutscenes, which also prevents two runtime handles from racing
for the single local native camera lease.

The public states are `loading`, `loaded`, `started`, `finished`, `released`,
and `failed`. Query them with `getStoryFileCutsceneState(handle)`, or listen for:

```lua
addEventHandler("onStoryFileCutsceneStateChange", root,
    function(state, snapshot)
        -- source is the caller-owned handle.
    end)
```

The snapshot contains `id`, `name`, `leader`, `participants`, barrier counts
(`loaded`, `started`, `finished`, `released`), `skipped`, and `skipSource`.
Failures additionally expose `reason`. Timeout options are milliseconds;
`fadeIn` is expressed in seconds. `failed` is not published until the runtime
has ordered safety release on every surviving participant; normal completion
similarly reaches `released` only after every client acknowledges
`releaseFileCutscene`. Disconnect, caller shutdown, handle destruction, and a
runtime restart release native leases with fade restoration. The runtime never
hands ownership of a black frame to the caller: a mission that needs another
transition must start it explicitly after the `released` barrier.

`skipStoryFileCutscene(handle)` broadcasts a server-authorized skip after the
start command has been issued. `releaseStoryFileCutscene(handle)` requests the
same barriered cleanup when playback is active. Once the handle is already
`released` or `failed`, calling it again destroys the terminal handle; callers
may instead destroy the element directly after consuming the terminal event.

For Sweet and Kendl, replace its mission-private prepare/loaded/start/skip/
finished/release RPC set with one runtime handle. Keep the existing pre-scene
world teardown and player-model lease outside this primitive; create the grouped
cutscene only after those two barriers are ready. Advance the mission only on
the handle's `released` event, fail it on `failed`, call
`skipStoryFileCutscene` for an administrative/headless skip, and then release
the terminal handle. The mission no longer needs client cutscene tokens,
per-client acknowledgement tables, or camera cleanup code.
