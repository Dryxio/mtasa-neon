# Story runtime and native task architecture

## Vision

Neon should be able to run GTA: San Andreas story missions on an MTA server and adapt them for cooperative play without rewriting every mission by hand.

The story project also serves a broader MTA goal: expose reliable native ped and vehicle tasks that any resource can use. Story missions are a demanding real-world test corpus for that general-purpose API, not the only consumer of it.

The `main.scm` script is authoritative for mission control flow, variables, conditions, coordinates, and opcode parameters. It is not a complete description of the behavior behind each opcode. AI, pathfinding, animations, tags, camera behavior, audio, and many task semantics live in the GTA executable. Neon therefore needs both an SCM runtime and native engine integrations.

## Architecture decision

The target is a hybrid architecture:

```text
main.scm or a decoded SCM intermediate representation
                         |
                         v
       server-authoritative story runtime
           variables, threads, conditions,
           checkpoints, cleanup and co-op policy
                         |
                         v
                reusable opcode handlers
                         |
              +----------+-----------+
              |                      |
              v                      v
       ordinary MTA Lua API    Neon native task API
                              CTask, AI, tags, camera,
                              audio and engine behavior
                                         |
                                         v
                           synchronized GTA client state
```

The project will not use either of these extremes:

- Hand-authoring every mission as an independent Lua state machine. This is useful for a prototype, but duplicates opcode behavior and accumulates semantic drift.
- Moving the complete mission VM into the client game process. Independently running `main.scm` on every client would make authoritative co-op and recovery from desynchronization substantially harder.

The initial SCM runtime may be written in Lua for fast iteration. Dispatch performance is not the main risk; accurately implementing opcode semantics and synchronizing the resulting engine behavior are the hard problems. An intermediate representation, transpiler, JIT, or C++ VM can be considered later without changing the opcode and task contracts.

## Native tasks as a general MTA feature

Native tasks must be designed as a public Neon/MTA capability, not as private `scm*` functions. Potential consumers include roleplay NPCs, PvE modes, escorts, traffic, scripted cinematics, machinima, and automated gameplay tests.

The public surface should express durable concepts such as:

```lua
local task = setPedTask(ped, "go_to", {
    position = Vector3(x, y, z),
    movement = "run",
    radius = 1.0,
    timeout = 15000,
})

cancelPedTask(ped, task)
local state = getPedTaskState(ped, task)

addEventHandler("onPedTaskComplete", ped, function(task, result)
    -- Advance resource-owned behavior after authoritative completion.
end)
```

Names and signatures are provisional. The contract matters more than the exact syntax.

### Initial task families

The first useful native task set is driven by the needs of `SWEET1`/Tagging Up Turf but must remain generic:

- Go to a point with a movement mode and completion radius.
- Enter and leave a vehicle using a requested seat.
- Drive a vehicle to a point with speed and driving-style parameters.
- Look at, aim at, attack, and fight a target.
- Play a named animation with explicit blend, loop, and completion behavior.
- Compose a sequence of tasks and cancel or replace it safely.
- Report completion, interruption, timeout, entity destruction, and failure.

Tags now have a resource-owned native Neon service and validated client-to-server progress protocol. Mission cameras and mission audio also have resource-owned client services; they do not masquerade as ped tasks, but opcode handlers can invoke them through similarly well-defined APIs.

### Why C++ is required

Lua should orchestrate behavior, but it should not reimplement GTA pathfinding or approximate complex `CTask` classes with teleports and control-state polling. Native integrations can preserve GTA's movement, animation blending, vehicle entry logic, collision response, and task lifecycle.

The reverse-engineered `gta-reversed-dryxio` working tree at `/Users/salimtrouve/Documents/GitHub/gta-reversed-dryxio` is a semantic oracle for mapping opcodes to GTA classes, flags, parameters, and completion rules. Implementations still need to be adapted to MTA's abstractions and network model rather than copied blindly.

Reverse-engineered functions are not accepted as ground truth without verification. Before a native behavior is implemented in Neon, its relevant `gta-reversed-dryxio` code must be checked against the target GTA:SA assembly. The tooling for this gate is available at `/Users/salimtrouve/Documents/GitHub/auto-re-agent`. Contributors must read that project's local instructions, identify the exact executable address/version being compared, inspect control flow and parameter use, and record any discrepancy or remaining uncertainty before modifying MTA. A plausible C++ reconstruction alone is insufficient evidence. Any discrepancy conclusively established by the assembly must also be corrected in the canonical `/Users/salimtrouve/Documents/GitHub/gta-reversed-dryxio` working tree rather than documented only in Neon. The reverse-side correction must preserve the assembly evidence in its commit or accompanying documentation and add or update size/offset validation when the discrepancy affects a native layout.

MTA already contains task wrappers and a disabled historical Lua task surface. That code is useful reference material, but merely re-enabling it is not sufficient: the old interface does not define server authority, syncer migration, validation, or resource-scoped cleanup.

## Network and ownership contract

Every synchronized native task needs an explicit lifecycle:

1. A server resource creates a task with a stable task ID and immutable input parameters.
2. The server selects or observes the ped's current syncer.
3. That client constructs and executes the native GTA task.
4. Existing MTA element synchronization carries ordinary ped or vehicle movement.
5. The owner reports task state transitions with enough evidence for server validation.
6. The server emits authoritative completion or failure to the owning resource.
7. If the syncer changes, the task is reconstructed or safely resumed by the new owner.
8. Element destruction, resource shutdown, streaming loss, cancellation, and player disconnect all have defined cleanup behavior.

The server owns mission progress. A client completing a GTA task is an observation, not permission to advance arbitrary story state.

Tasks must not store unsafe raw references across entity destruction. Task handles must be generation-safe or tied to MTA element lifetimes, and resource shutdown must cancel all tasks owned by that resource.

## SCM runtime responsibilities

The story runtime should provide the parts that are actually script semantics:

- Global and local variables with SCM-compatible value behavior.
- Script threads, instruction pointers, waits, timers, gosubs, and condition flags.
- Entity handles and mission-scoped cleanup.
- Opcode decoding and parameter binding.
- Checkpoints, objectives, text, mission pass/fail, and saveable progress.
- A deterministic server-owned view of mission state.
- Co-op hooks where original single-player assumptions must be adapted.

Opcode handlers should be thin adapters whenever a generic API already exists. For example, an SCM `TASK_GO_STRAIGHT_TO_COORD` handler should validate and translate its arguments into a native `go_to` task instead of containing another navigation implementation.

Coverage should grow from mission working sets rather than from implementing every known opcode upfront. Each implemented opcode must have one canonical handler shared by all missions.

## Co-op policy layer

Faithful opcode execution alone does not define co-op behavior. A separate policy layer must decide:

- Which player fills the original protagonist role.
- Whether an objective is individual, shared, or requires every participant.
- Vehicle seat allocation and party regrouping rules.
- Failure behavior when one player dies, disconnects, or leaves the streamed area.
- Ownership transfer when the leader or current syncer changes.
- How cutscenes, interiors, rewards, weapons, and saved state apply to multiple players.
- Which single-player waits or branches require explicit multiplayer overrides.

These adaptations should be declarative hooks around the original mission whenever possible. They must not be scattered through otherwise reusable opcode handlers.

## Tagging Up Turf prototype

The resource in `test-resources/tagging-up-turf` is the current vertical slice. It proved that the route, shared objectives, authoritative stage progression, mission isolation, and state restoration can be built in MTA.

It also exposed the limit of mission-specific Lua approximations:

- Sweet's initial mission setup and the temporary driver/passenger seat changes around recording `207` still use authoritative seat placement. His demonstrated leave, walk, spray, return, passenger-entry, Ballas departure, and recorded-car movement otherwise use native tasks or playback without position teleports.
- MTA disables GTA's single-player tag manager. Neon now extends GTA's real spray path for explicitly owned MTA objects, while the server validates and mirrors native 8-alpha steps instead of detecting spray input in Lua.
- Tag ownership, streaming, recreation, and cleanup are generic C++ policy. Cross-client ownership generations and syncer migration remain future hardening beyond the current validated-report protocol.

The prototype should remain as a regression resource. Its ad-hoc actor and tag code should be replaced incrementally by the generic native APIs, making it the first end-to-end consumer of the new architecture.

### Mission instruction trace

The prototype includes an optional client-side runtime monitor for development footage and diagnosis, toggled locally with `/taguptrace` or `F7`. Its GTA:SA-styled feed uses the bundled Pricedown and Bank Gothic faces with the heavy outline of the stock HUD. Right-aligned text on one low-opacity black field below the normal HUD makes the exact primitive or opcode visually dominant, keeps the human-readable title legible, and shows the two most recent primitives with their completion status and execution domain. `/taguptracechat` independently writes each newly active operation to the existing local chatbox, which provides a longer history without repeating progress-only updates. It distinguishes the complete gold SCM command from the blue original GTA `CTask` class; primitives that do not construct a task remain explicitly categorized as camera, audio, streaming, predicate, animation, or server work. Spoken commentary can carry the full explanation without obscuring gameplay. The detailed task trace runs on the leader/syncer client that actually assigns and observes Sweet's native tasks; other party members do not synthesize those observations. Both presentations are passive observers: they cannot advance the mission, assign tasks, or grant tag progress.

Every displayed operation states its current implementation domain. Verified native opcodes/tasks, SCM waits and conditions, co-op adaptations, and temporary Lua substitutes are deliberately not conflated. This makes the trace useful as a visible coverage map while the native story runtime grows, but it is not evidence that SCM bytecode is already being interpreted. The reusable `TAGUP_TRACE` component accepts an ordered sequence plus status/progress updates; a future SCM runtime can feed the same presentation layer from unified, sequenced execution events instead of the prototype's mission-specific hooks.

## Current implementation slice: native task and tag primitives

The first engine primitives are client-side APIs for the local player, a client-local ped, or a server ped currently owned by that client's ped syncer. Task mutations require the ped to be living and streamed, return `false` when validation or ownership fails, and intentionally sit one layer below the final server task manager. Mission-actor classification is persistent client element policy and may be assigned before native stream-in. Together these APIs validate GTA constructors, task assignment, movement, cancellation, combat state, actor classification, and ordinary ped synchronization before persistent task IDs and syncer migration are introduced.

The exact current Lua surface is:

```text
acquireElementStreamingLease(element)
releaseElementStreamingLease(token)
isVehicleOnAllWheels(vehicle)
setPedGoTo(ped, Vector3 target [, string movement = "walk", float radius = 0.5, float slowdownRadius = 2.0, int timeout = -2])
setPedEnterVehicle(ped [, vehicle, seatOrPassenger])
setPedExitVehicle(ped)
setPedDriveWander(ped, vehicle, float speed [, string|int drivingStyle = "stop_for_cars"])
setPedDriveTo(ped, vehicle, Vector3 target, float speed [, string|int mode = "normal", string|int drivingStyle = "stop_for_cars"])
setPedDriveBy(ped, ped|vehicle|Vector3 target, float abortRange [, string|int style = "ai_all_directions", bool seatRHS = false, int frequency = 100])
setPedTaskSequence(ped, table tasks [, bool repeat = false])
getPedTaskSequenceProgress(ped)
setPedChatWith(ped, partner, bool leadSpeaker [, bool updateDirection = true [, bool conversationEnabled = true]])
setPedStandStill(ped [, int duration = 0])
setPedTurnToFace(ped, targetPed)
setPedGoToOffset(ped, target [, int timeout = -1, float radius = 0.5, float angle = 0.0, bool repeat = false])
setPedKillOnFoot(ped, target)
setPedWander(ped [, string movement = "walk", int direction = native random, bool wanderSensibly = true])
setPedScriptedSpeechMuted(ped, bool muted)
setPedFacialTalk(ped, int duration)
stopPedFacialTalk(ped)
setPedMissionActor(ped, bool enabled)
isPedMissionActor(ped)
setPedStoryProtected(ped, bool enabled)
isPedStoryProtected(ped)
setVehicleDoorLockMode(vehicle, int mode)
getVehicleDoorLockMode(vehicle)
setVehicleTyresCanBurst(vehicle, bool canBurst)
getVehicleTyresCanBurst(vehicle)
setVehiclePhysicalProofs(vehicle, bool bullet, bool fire, bool explosion, bool collision, bool melee)
setVehicleLoadCollisionFlag(vehicle, bool loadCollision)
setPedSuffersCriticalHits(ped, bool suffersCriticalHits)
getPedSuffersCriticalHits(ped)
setPedStayInSamePlace(ped, bool stayInSamePlace)
getPedStayInSamePlace(ped)
setPedNeverTargeted(ped, bool neverTargeted)
isPedNeverTargeted(ped)
setPedShootAt(ped, Vector3 target [, int duration = 1000, int burstLength = 5])
setPedWeaponShootingRate(ped, int rate)
setPedWeaponAccuracy(ped, int accuracy)
setObjectGangTagAlpha(object, int alpha | false)
acquireObjectGangTag(object [, int progress = 0])
setObjectGangTagProgress(object, int progress)
getObjectGangTagProgress(object)
releaseObjectGangTag(object)

onClientObjectGangTagProgress(int previousProgress, int currentProgress, element creator)
```

The first simulation-island checkpoint adds resource-owned client streaming
tokens and corrects server syncer persistence to live on each ped or vehicle.
Tokens compose across resources, hold generation-safe element references, and
are released automatically on resource shutdown. The legacy
`setElementStreamable` boolean owns a separate reference, so it cannot consume
another resource's token. Persistent `setElementSyncer` assignments no longer
share one manager-global boolean which an unrelated element can reset. This
preserves an already-running native task while its owner is outside ordinary
MTA streaming distance. Task reconstruction after owner migration remains a
separate lifecycle checkpoint.

The local SWEET3 bytecode SHA-256
`601def3baae766ce6a23e2f0b9b48f6b33c9a64e2fc32eb4f22ddea8b868b0fa`
contains opcode `0587 SET_LOAD_COLLISION_FOR_CAR_FLAG drive_by_car1 FALSE` at
offset `0x829F5`. Its target handler at `0x48EBF4` clears physical mask `0x4000`
for `TRUE` at `0x48EC19` and sets it for `FALSE` at `0x48EC50`.
`setVehicleLoadCollisionFlag(vehicle, loadCollision)` exposes that exact
polarity, persists it across native vehicle recreation, and remains separate
from MTA's ordinary element collision toggle. The audited MTA lease path is
`CClientPed::StreamOut` through
`_DestroyModel`, where the GTA ped is removed from the pool and its task
manager pointer is cleared. In gta-reversed, physical flag `b15` (the target
`0x4000` mask) excludes an entity from reduced mission-collision requests in
`CColStore` and gates `CCarCtrl::SwitchBetweenPhysicsAndGhost`; no reverse
correction is needed. The isolated `native-simulation-lease-test` harness
requires horizontal movement plus a valid road-height Z, so falling through
unloaded collision cannot produce a false pass. Manual validation on 21 July
2026 kept both persistent syncers and both streaming leases active more than
`3.3 km` from the player. With `0587` applied, the Voodoo advanced through
native sequence indices `1`, `2`, and `3`, synchronized `259.47 m`
horizontally with `groundValid=true` and a stable Z near `13.2`, then continued
to index `4` after `/nativesimnear` returned the player beside its current
position.

The second simulation-island checkpoint adds the reusable Lua
`native-task-runtime` above those engine primitives. The server owns a stable
`native-drive-route` element, an immutable route descriptor, a monotonically
increasing owner epoch, and the last accepted logical waypoint. Its client
assignment owns both streaming leases, reconstructs only the unfinished route,
and rejects execution until the ped and vehicle are streamed, locally owned and
still paired in the driver seat. Revocation kills the old primary task, restores
the previous mission-actor policy, releases both leases, and can require native
stream-out of both elements before the server opens a new epoch. Stale client
evidence cannot mutate the new epoch. Caller resource stop, entity destruction,
explicit cancellation, and owner disconnect all have defined cleanup or
`orphaned` behavior.

Manual validation on 21 July 2026 deliberately moved the owner more than three
kilometres away, revoked epoch `1` at logical route index `3`, observed native
stream-out for both the Ballas driver and Voodoo, then reconstructed epoch `2`
at the same index. The first new authoritative sample had `0.00 m` positional
discontinuity; the server then observed `22.2 m` of new horizontal movement at
road-height Z `13.21` before the route advanced to index `5`. This proves route
reconstruction and ownership transfer, not a complete headless mission VM.
Objectives, timers, success and failure zones remain server state. A connected
but frozen owner still needs heartbeat-based reassignment, while native combat
requires a later multi-entity task-group policy for every participating actor,
vehicle and target.

A native-task-to-camera-scene handoff must overlap both streaming and sync
ownership. The successor first acquires leases for already resident actors and
vehicles. The server then cancels the predecessor route, reapplies persistent
syncers after the runtime's automatic-sync restoration, verifies both owners,
and acknowledges that commit before the client starts its fade and camera
preparation. Waiting for the entire remote-scene barrier before cancelling is
too late: a route that completes during the wait restores automatic sync and
can leave its driver resident but non-authoritative. Actors that were never
resident at the remote destination are leased only after the script camera has
moved the streaming centre and requested destination collision. The scene then
waits for stable positions, seats and native instances under black before
assigning replacement tasks. A boolean task acceptance is not a substitute for
either ownership or scene-readiness barrier.

`setPedGoTo` accepts `walk`, `run`, or `sprint`, requires a positive radius and a slowdown radius at least as large, uses `-2` for the untimed task, normalizes `-1` to 20 seconds, and accepts non-negative finite timeouts. `setPedEnterVehicle` and `setPedExitVehicle` already existed in MTA and deliberately use its vehicle request/confirmation protocol instead of directly assigning client-only tasks; for a synchronized server ped, the server confirms the transition before the syncer constructs GTA's enter/leave task. MTA seat `0` is the driver, so SCM passenger index `0` maps to explicit MTA seat `1`. `setPedMissionActor` accepts only script peds and persists the requested classification across local stream-out/model recreation; it snapshots the previous created-by byte plus perception/scanner state and restores all of it when disabled. `setPedStoryProtected` independently persists the five story-script actor flags for never targeted, no critical hits, cannot be dragged out, stay in the car when jacked, and do not exit an upside-down vehicle. Disabling it restores the native values that preceded the policy. `setPedStayInSamePlace` and `setPedNeverTargeted` are scalar policies for missions whose actors receive only those SCM flags; each persists across native model recreation and the explicit never-targeted value takes precedence over the grouped policy. Vehicle door modes use GTA's raw values `1` through `7`; mode `3` is `LOCKOUT_PLAYER_ONLY`. The tyre setter is independent from body damage proof and remains effective across the native vehicle's local recreation. `setPedDriveWander` requires the ped to already occupy the target vehicle, the driver seat to be free or occupied by that ped, a finite speed from 0 through 255, and the calling client to own both ped and vehicle synchronization. Its styles are the verified GTA values 0 through 6 and the matching readable names; it is indefinite until cancellation. `setPedDriveTo` requires the ped to occupy driver seat `0`, the client to own both elements, finite coordinates, speed below `255`, mode `0..3`, and style `0..6`. Its sequence descriptor is `{task="drive_to", x, y, z, speed, mode, vehicleModel, drivingStyle}`. The sequence child passes a null vehicle and radius `-1.0` to the original constructor so GTA binds the current vehicle at activation exactly like SCM `TASK_CAR_DRIVE_TO_COORD -1 -1`. `setPedDriveBy` requires an owned script ped already occupying an owned streamed vehicle. Its target may be a distinct live streamed ped, a distinct live streamed vehicle, or finite coordinates. The abort range must be non-negative, frequency is `0..100`, and style is `0..8` or one of `fixed_lhs`, `fixed_rhs`, `start_from_lhs`, `start_from_rhs`, `ai_side`, `fixed_fwd`, `fixed_back`, `ai_fwd_back`, and `ai_all_directions`. The task defaults to GTA's all-direction AI style, left-hand seat flag, and 100 percent firing frequency. `setPedShootAt` accepts a burst length from 1 through 32,767 and treats every negative duration as an indefinite task, exactly as the native constructor does; GTA's `(0, 0, z)` coordinate sentinel is rejected. Shooting rate, weapon accuracy, gang-tag alpha, and gang-tag progress each accept 0 through 255. Passing `false` as the low-level tag alpha clears only the opt-in renderer, while `true` is rejected. Native script-task calls queue GTA's own script-command event, which later clears competing event-response tasks and installs a clone in the primary slot. Their boolean confirms ownership transfer only, so consumers must observe activation or authoritative world state separately. Vehicle entry and exit participate in MTA's existing synchronized lifecycle. Tag ownership is exclusive per object and calling resource. Its OOP aliases are `object:acquireGangTag`, `object:setGangTagProgress`, `object:getGangTagProgress`, and `object:releaseGangTag`.

The Drive-Thru drive-by gate was audited against the compact GTA SA 1.0 executable at the same target hash. Opcode `0713` dispatches at `0x46D3A7`, resolves an optional ped or vehicle target plus a coordinate fallback, allocates the exact `0x44` task, calls `CTaskSimpleGangDriveBy` at `0x6217D0`, and sets its script-command byte at offset `0x0E` before ordinary scripted task dispatch. The current `gta-reversed-dryxio` constructor, clone, destructor, layout, and safe-reference behavior match the target, so no reverse correction was required. Since MTA represents synchronized NPCs internally as `CPlayerPed`, Neon redirects only script-command tasks on non-local peds through GTA's native AI branches at seven audited `IsPlayer` calls. Five calls live in `CTaskSimpleGangDriveBy::ProcessPed`; the firing helper repeats the decision at `0x627D81` and the IK helper repeats it at `0x62838D`. Without the latter two redirects, `CWeapon::Fire` retained the task target while `CPedIK::PointGunInDirection` read the local camera, visibly separating weapon orientation from real impacts. Manual drive-by state and the local player's camera/input branch remain unchanged. The isolated harness validated vehicle, ped, and coordinate targets with native task activation, ammo use, client/server damage, target destruction, cancellation, and cleanup observed separately.

MTA's established bad-drive-by-hitbox correction normally replaces GTA animation `232` with `235` and `236` with `231` for every gang-drive-by task. That prevents ordinary multiplayer players from becoming effectively invulnerable, but visually reverses cross-vehicle fire selected by GTA AI. Neon now preserves the original animation only for a script-command task on a non-local ped whose mission-actor policy has already disabled MTA player-weapon processing. Local and remote players retain the hitbox correction. Target assembly confirms that animation selection at `0x621960` or `0x627600`, consumption at `0x62D5A7`, aim geometry at `0x6286E3`, and task-target fire at `0x628328` are distinct steps. The integrated SWEET3 telemetry then exposed a separate CPlayerPed mismatch: target-directed `CWeapon::FireInstantHit` loads `CPed::m_pPlayerData` at `0x73FF1E` before applying random player spread. Vanilla NPCs skip that branch because their pointer is null, while MTA-backed mission peds did not. Neon now suppresses that one load only inside the script-command firing-helper window `0x627D81..0x62833E`. The native occupied-vehicle ignore at `0x740697` remains conditional on vehicle flag `0x42D & 4`; Neon does not override the collision policy. The current reverse matches those decisive instructions. The isolated harness uses accuracy `60`, requires at least forty shots per target form, places targets on both sides of the vehicle, and treats any shooter-vehicle health loss as a failure.

The Sweet and Greenwood property slice was verified against the compact GTA SA 1.0 executable at the same target hash. `LOCK_CAR_DOORS` opcode `020A` reaches handler `0x47E40A` and writes its raw mode to `CVehicle+0x4F8`. `SET_CAN_BURST_CAR_TYRES` opcode `053F` reaches `0x48D356`; a false script argument sets the `bTyresDontBurst` bit at `CVehicle+0x42B`. The actor handlers write the exact `CPed` bits used by the policies: `SET_CHAR_CANT_BE_DRAGGED_OUT` at `0x484468` writes `0x20000000` at `+0x46C`, `SET_CHAR_SUFFERS_CRITICAL_HITS` at `0x48A023` inversely controls `0x1000` at `+0x470`, `SET_CHAR_STAY_IN_CAR_WHEN_JACKED` at `0x48D1DF` writes `0x800000` at `+0x470`, `SET_CHAR_NEVER_TARGETTED` at `0x48D9E2` writes `0x10000000` at `+0x470`, and `CPed::SetStayInSamePlace` at `0x481090` writes `0x200000` at `+0x46C`. The current `gta-reversed-dryxio` command implementations and layouts match these target writes, so no reverse correction was required. The scalar stay policy uses a non-virtual public bridge to the existing CPed virtual, preserving every established cross-module vtable index.

Drive-Thru's restaurant reconstruction required the independent forms of two already understood policy families. `setPedSuffersCriticalHits` persists only the inverse no-critical-hits bit, allowing an enemy mission actor to retain ordinary targeting and vehicle-jacking behavior. `SET_CAR_PROOFS` opcode `02AC` enters at `0x47F908`; the shared block beginning `0x47F8AA` writes bullet `0x40000`, fire `0x80000`, explosion `0x800000`, collision `0x100000`, and melee `0x200000` at `CPhysical+0x40`. The reconstructed handler in `gta-reversed-dryxio` matches the target. `setVehiclePhysicalProofs` persists that exact five-flag tuple across local native vehicle recreation. Its `CVehicle` virtual was appended after the existing story methods.

Managed file cutscenes now reproduce the temporary streaming gate used by vanilla mission scripts. `SWITCH_STREAMING OFF` reaches target handler `0x48478E` and writes the inverse script argument to global `0x9654B0` at `0x4847A1`. `CCutsceneMgr` clears it during postload and deletion at `0x5AFBE8` and `0x5AFF90`. Neon sets the flag only immediately before the verified native file-cutscene loader and leaves restoration to the same native lifecycle, with an explicit failure-path clear if no cutscene state was acquired.

The facial-talk slice was verified against the compact GTA SA 1.0 executable, SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`. `START_CHAR_FACIAL_TALK` opcode `0967` reaches handler `0x478221`, reads the duration as a raw signed millisecond value, obtains secondary task slot `3`, and calls `CTaskComplexFacial::SetRequest` at `0x691230` with `TALKING`, the supplied duration, `NONE`, and `0`. `STOP_CHAR_FACIAL_TALK` opcode `0968` reaches `0x478266` and calls `StopAll` at `0x691250` on the same persistent controller. The public API accepts a non-negative duration and never creates a replacement facial controller. It runs locally on every participant for streamed script peds because facial animation is presentation rather than authoritative movement. The reverse correction `6bc6dd0f` preserves the constructor's untouched duration fields and the simple facial task's raw timer-start behavior.

The native body-turn slice was verified against the same target executable. `TASK_TURN_CHAR_TO_FACE_CHAR` opcode `0639` reaches handler `0x492A36`, resolves the target ped, allocates the exact `0x28` `CTaskComplexTurnToFaceEntityOrCoord`, and calls its entity constructor at `0x66B890` with change-rate multiplier `0.5` and maximum heading error `0.2` radians. GTA registers a safe reference to the live target, continuously retargets its `CTaskSimpleAchieveHeading` subtask, and dispatches the complex task through the primary scripted-command path. The API therefore has no duration or stop argument and requires ownership only for the actor. Reverse commit `30102a45` corrects the swapped entity/coordinate constructor labels found during this audit.

The low-level ped-task calls deliberately remain immediate client commands rather
than server-owned lifecycle objects. The optional `native-task-runtime` composes
the drive-to subset into server-owned handles with completion events, resource
ownership, cleanup and reconstruction after syncer migration. Extending that
layer to combat groups and additional task families remains future work. Gang
tags provide their own local lifecycle guarantees: ownership and progress live
on the MTA object above the streamed GTA instance, are reapplied on native
recreation, and are revoked on explicit release or resource stop.
`setObjectGangTagAlpha` remains a visual-only low-level override; synchronized
gameplay should acquire the object and mirror validated native progress through
`setObjectGangTagProgress`.

The client-only `isVehicleOnAllWheels` predicate reproduces opcode `09D0` without reusing MTA's broader `isVehicleOnGround` approximation. The target executable, SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`, dispatches jump-table index 12 to handler `0x47AA8D`. It resolves one vehicle, reads raw vehicle class at `+0x590`, and returns true only for class `0` automobile with contact byte `+0x960 == 4` or class `9` bike with contact byte `+0x804 == 4`. Every other class returns false. There is no speed, orientation, damage, suspension-ratio, or ground-distance fallback. The canonical reverse correction `bd843cef` registers the missing handler, locks those contact offsets plus the adjacent drive-wheel offsets, and fixes `CBike::GetAllWheelsOffGround` to read `+0x805`; opcode `09D0` itself does not call that helper.

The installed `main.scm`, SHA-256 `601def3baae766ce6a23e2f0b9b48f6b33c9a64e2fc32eb4f22ddea8b868b0fa`, applies the same triple condition at all three `SWEET1` destinations. Idlewood uses `LOCATE_CAR_3D`/seated/`09D0` at offsets `0x7984E`, `0x79874`, and `0x7987C`; Ballas uses `0x7AA59`, `0x7AA7F`, and `0x7AA87`; Grove uses `0x7BD00`, `0x7BD26`, and `0x7BD2E`. Each loop evaluates after `WAIT 0` and exits only when the 4 m axis-aligned locate box, CJ seating, and all-four-contact predicate are true in the same iteration. The mission resource polls that native predicate on the streamed vehicle syncer, while the server still authorizes stage progression. Its arrival guard and resend timers capture their live state table through Lua closures because MTA deep-copies table arguments passed to `setTimer`. Manual validation through both rolled-vehicle checkpoints observed the intended differential, with legacy ground state true while `09D0` remained false, followed by the exact four-contact pass and successful Idlewood and Ballas scene progression.

The SWEET1 Ballas slice adds the verified native building blocks behind opcodes `0677`, `05BA`, `06A8`, `05E2`, `05DE`, and `0A09`. The target executable maps them to `CTaskComplexPartnerChat` constructor `0x684290`, `CTaskSimpleStandStill` constructor `0x62F310`, `CTaskComplexSeekEntityRadiusAngleOffset` constructor `0x493730`, `CTaskComplexKillPedOnFoot` constructor `0x620E30`, `CTaskComplexWanderStandard` constructor `0x48E4F0`, and the scripted-speech methods at `0x5EFF80` and `0x5EFF90`. Repeating offset movement uses GTA's real mission sequence pool and `CTaskComplexUseSequence` constructor `0x635450`, including mission slots, reference counting, and deferred flush, instead of running a Lua movement loop. The target audit also corrected timer ordering, UseSequence cloning, speech-disable flags, and Wander cloning in the canonical reverse repository.

Scripted task dispatch was verified against the same target executable after direct primary-slot assignment made the paired Ballas conversation disappear within 500 ms. `CRunningScript::GivePedScriptedTask` at `0x465C20` routes ordinary peds through `CEventScriptCommand`; Neon calls the narrower verified helper `CPedIntelligence::AddTaskPrimaryMaybeInGroup` at `0x600E20`. For a player or ungrouped ped, that helper constructs the `0x18` event at `0x4B0A00`, clones it into the event group at `0x4AB420`, and destroys the temporary plus original task at `0x4B0A50`. `CEventHandler::ComputeScriptCommandResponse` at `0x4BA7C0` later clones the queued task, calls `ClearTaskEventResponse` at `0x681BD0`, then installs the primary task through `0x681AF0`. The current `gta-reversed-dryxio` reconstruction matches this control flow, ownership, and the `CPedIntelligence+0x68` event-group offset, so no reverse correction was required for this finding. Neon deliberately does not allocate an SCM scripted-task record because those global records exist for opcode status and script-thread cleanup, neither of which owns MTA resource progression.

A second target audit separated dispatch from the remaining short PartnerChat lifetime. `RequestPedConversation` at `0x4E50E0` validates speech state and reserves conversation slots, but it does not validate that the installed samples contain audible duration. With structurally present yet empty ped-speech tracks, reservation succeeds and `CTaskComplexPartnerChat` takes its audio branch. `CTaskComplexChat::ControlSubTask` at `0x683060` then observes `GetPedTalking()==false` and marks both paired chat tasks complete in a few frames. Scripted-speech mute does not reliably prevent that reservation for these MTA mission peds, so the generic `setPedChatWith` surface exposes a `conversationEnabled` compatibility option. Passing `false` reproduces the verified state reached after native reservation failure: Neon constructs with conversation enabled so `0x6842EB` first normalizes the direction counter to `4`, then clears only the byte at task offset `0x74` before dispatch, matching the transition at `0x681F6C`. Constructing directly with `false` is not equivalent because it preserves the opcode's `-1` direction sentinel and makes the paired task terminate early. The corrected state selects PartnerChat's existing timed `CTaskSimpleChat` and `CTaskSimpleStandStill` fallback explicitly. No Lua conversation animation, positioning, or timing loop is introduced. The target constructor, clone, first/next/control state machine, PartnerChat sequence builder, and Chat control path match the current reverse for this behavior. The same audit corrected one separate reverse discrepancy in `ReleasePedConversation`: target `0x4E52A0` always clears the global conversation-active flag when either reserved slot is already missing, while the prior reconstruction returned with that flag still set. Neon calls the original target routine, so this reverse-only correction does not alter the current client binary.

The Ballas PartnerChat regression also exposed a separate opcode-boundary conversion. `COMMAND_CREATE_CHAR` (`009A`) reads the script coordinates, optionally resolves the ground when the script Z uses its sentinel, then adds the `1.0f` constant at `0x858624` to Z through the instruction at `0x4676BF` before placing the ped. MTA's `createPed` consumes its Z directly and does not perform that SCM conversion. A story resource or future runtime handler must therefore retain the raw SCM coordinate as source data and call `createPed(model, x, y, scriptZ + 1.0, heading)` when reproducing `009A`. This is an opcode adapter rule, not a global offset for ordinary MTA ped creation or later coordinate-setting opcodes. Conformance must observe the spawned client ped near `scriptZ + 1.0` before assigning movement tasks. In the failing Tagging Up Turf trace, both Flats started at the raw ground Z; collision recovery lifted the first mover by exactly one metre and aborted PartnerChat in state 3. Applying the opcode conversion let both actors reach state 6 at the native separation.

The compatibility profile for SCM opcode `05D3 TASK_GO_STRAIGHT_TO_COORD` was verified against the compact GTA SA 1.0 executable at `/Users/salimtrouve/Documents/GTA-SanAndreas/GTA_SA.EXE` (SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`):

- Handler case `0x4907CE` collects ped/sequence, position, move state, and timeout.
- Timeout `-2` constructs the `0x28` base task at `0x668120` with radius `0.5`, movement-state radius `2.0`, no forced overshoot, and exact stopping enabled.
- Every other timeout constructs the `0x38` timed task at `0x6685E0`; `-1` is normalized to `20000 ms` by the opcode handler.
- A timed expiration is not a failure: GTA finds a suitable ground Z and relocates the ped to the target before completing the task.

The reverse matched the relevant assembly except for a missing NaN guard in the high-level `gta-reversed-dryxio` reconstruction. Neon calls the original GTA constructors and does not copy that reconstructed implementation.

The standalone resource in `test-resources/native-ped-go-to-test` is the conformance harness for this slice. Its initial walk, run, sprint, cancellation, cleanup, and terminal-distance checks allowed `Tagging Up Turf` to adopt the API; it remains the isolated regression test for future task changes.

Opcode `05D2 TASK_CAR_DRIVE_WANDER` was gated against the same compact executable at handler `0x490762`. It collects ped/sequence, vehicle, float speed, and driving style, allocates exactly `0x24` bytes, calls `CTaskComplexCarDriveWander` constructor `0x63CB10`, and assigns through the ordinary script task helper. The verified layout places vehicle at `+0x0C`, cruise speed at `+0x10`, desired model at `+0x14`, 32-bit style at `+0x18`, driver flag at `+0x1C`, original autopilot bytes at `+0x1D..+0x1F`, and setup flag at `+0x20`. Neon locks this opaque layout and calls GTA's constructor, vtable, and destructor rather than copying the candidate reverse.

The audit corrected four reverse discrepancies in `/Users/salimtrouve/Documents/GitHub/gta-reversed-dryxio`: the speed/style base constructor must set the driver flag, Wander clone must retain the derived type, the broken-engine path must not clear the law-enforcer flag, and the derived size assertion must validate the derived class. The installed `main.scm` also disproved an active shuffle shown by one generated decompile: SWEET1 contains `05D2` with speed `20.0` and style `2` directly after CJ leaves, while Sweet remains passenger. The standalone `test-resources/native-ped-drive-wander-test` harness therefore seats Sweet in passenger seat 1 with the driver seat empty, gives one client persistent ownership of both ped and vehicle, and requires both native task observation and server-observed movement before passing.

The first long-running harness exposed a separate actor-classification requirement: MTA constructs script peds through `CPlayerPed`, whose `CPed` base defaults `m_nCreatedBy` to `PED_GAME`. The verified byte is `CPed+0x484`; `CPed` constructor `0x5E8030` writes value `1`, while SCM `CREATE_CHAR` calls `SetCharCreatedBy` `0x5E47E0` with `PED_MISSION=2`. `CTaskSimpleCarDrive::ProcessPed` `0x644470` checks that byte at `0x644BB9`. A `PED_GAME` first passenger left without a driver and group for 4000 ms receives an ambient sequence that leaves the car and starts another Wander task. Story Sweet never takes that branch because SCM created him as a mission actor.

`setPedMissionActor` reproduces the native setter rather than changing only the byte. `SetCharCreatedBy(PED_MISSION)` also resets the decision maker and changes perception/scanner fields, while setting `PED_GAME` later does not restore those values. Neon therefore snapshots the complete affected state before activation and restores it when disabled. The policy also disables MTA's player-weapon processing for the actor. MTA constructs every script ped as a `CPlayerPed` and normally enables `CRemoteDataStorageSA::m_bProcessPlayerWeapon`; the hook at `CWeapon::Fire` then replaces an explicit task target with `m_shotSyncData.m_vecShotTarget`. That is correct for replicated player input but made `TASK_SHOOT_AT_COORD` discard the coordinate already verified at constructor `0x61F3F0`. SCM mission actors are ordinary AI peds and do not take that substitution, so Neon lets their native gun task own the target and restores the MTA policy when mission-actor mode is disabled. The policy lives on `CClientPed`, survives local native model recreation, rejects player elements, and is intentionally client-local; synchronized resources must replicate their desired policy to all potential syncers. Its three new `CPed` virtual methods are appended after the established interface rather than inserted between existing methods, preserving the vtable indices consumed by independently built MTA modules such as `core.dll`. The ASM audit also corrected `gta-reversed-dryxio`'s `CTaskSimpleCarDrive` layout: task offset `+0x14` is an embedded `0xC` `CTaskTimer`, not a pointer plus unrelated fields, and `CPed::m_nCreatedBy` is now locked to `+0x484`. The drive-wander harness requires the native task to remain present for at least 15 seconds before passing.

An attempted generic mission-NPC mode that forced decision maker `-1` and returned false from `CPed::IsPlayer` for an entire syncer frame was rejected after Drive-Thru regression testing. GTA task code branches on `IsPlayer` during ordinary movement, vehicle entry and occupant finalization, while MTA still owns a real `CPlayerPed`, player data and pad context. The mixed identity accepted entry tasks but left their visible movement and MTA vehicle notification incomplete. The replacement is a separate resource-owned, syncer-only native event-profile lease. It persists across native ped generations, deactivates automatically when the client loses synchronization ownership, is exclusive per ped, and is revoked during resource teardown. The initial `"mission"` adapter changes the decision maker to `-1` only around the verified `CEventGroup::Add` call to `CEventEditableResponse::ComputeResponseTaskType`, then restores it synchronously. `CPed::IsPlayer` returns false only when called from `CEventVehicleOnFire::AffectsPed` at target return address `0x4B5005`; all normal task execution retains MTA's player identity. In-game validation on 2026-07-21 confirmed deactivation and reactivation of the same lease across a deliberate syncer cycle, `TASK_SIMPLE_CAR_GET_OUT`, and the stock smart-flee subtree. The integrated Drive-Thru run leased the profile for both Ballas without regressing crew entry, completed the vehicle-to-foot transition, and passed the chase checkpoint. The isolated harness was manually cleaned before its final eight-metre and resumed-combat verdict, so that terminal guard remains intentionally unclaimed.

`05CA TASK_ENTER_CAR_AS_PASSENGER` was checked against the same compact 1.0 US executable before the existing MTA API was adopted. Handler `0x49036C` collects the character, vehicle, timeout, and passenger-index parameters; for a concrete seat it translates the SCM passenger index to GTA's door/seat node and creates the `CTaskComplexEnterCarAsPassengerTimed` wrapper. The direct task constructor at `0x640340`, its base constructor at `0x63A220`, and clone at `0x6437F0` establish a `0x50` direct-task allocation. Neon's interface matches those field offsets and now locks the size with compile-time assertions.

The audit found one candidate-reverse discrepancy: `gta-reversed-dryxio` value-initializes `m_EnterCarStartTime`, while the original constructor does not write offset `0x4C`. Neon calls GTA's original constructor, so it does not copy that mismatch. The opcode's timed wrapper is a separate `0x2C` object: on finite-time expiry it aborts the direct task and may construct `CTaskSimpleCarSetPedInAsPassenger` as a fallback warp. The public MTA call intentionally retains its existing request/confirmation lifecycle and direct native task, not that SCM-specific timeout warp. Mission code therefore applies a 15-second SCM-derived failure guard without claiming byte-for-byte timed-wrapper equivalence.

The isolated `test-resources/native-ped-enter-car-test` harness maps SCM passenger index `0` to MTA seat `1` and requires both client observation of `TASK_COMPLEX_ENTER_CAR_AS_PASSENGER` and server `onVehicleEnter` confirmation. `Tagging Up Turf` uses the same two-sided proof after the demonstration: Sweet walks back and enters the Greenwood while the player is free to spray the two Idlewood tags. Returning to the car can no longer advance the mission through the former Sweet warp.

The first chained mission test also exposed a lifecycle boundary absent from the isolated entry test: cancelling primary `GunControl` did not necessarily remove its `TASK_SIMPLE_USE_GUN` from secondary attack slot `0`. MTA's existing `EnterVehicle` path deliberately refuses while `IsUsingGun()` sees that secondary task. The demonstration cleanup now removes both the primary gun controller and its owned secondary attack task before requesting vehicle entry; refusal diagnostics record both task slots so a future regression is attributable instead of appearing as an unexplained entry failure.

Opcode `05CD TASK_LEAVE_CAR` was gated against the same executable before its existing MTA path was integrated into the mission. Handler `0x490554` allocates `0x34` bytes at `0x490574` and calls `CTaskComplexLeaveCar` constructor `0x63B8C0`; clone `0x63D9E0` repeats the `0x34` allocation, while destructor `0x63B970` accesses the `CTaskUtilityLineUpPedWithCar*` at offset `+0x1C`. The historic MTA interface omitted this owned pointer and described only `0x30` bytes, shifting all later fields. Neon restores the pointer, locks the interface size with a compile-time assertion, and changes the wrapper constructor's default door to `0xFF` so it reaches GTA's raw-door `0` automatic selection instead of silently forcing the front-left node.

This gate also found that the candidate `gta-reversed-dryxio` `ControlSubTask` reconstruction preserves `PAUSE`, while the executable preserves `DIE` (`0xD4`) alongside the verified leave-car subtasks. Neon therefore continues to call GTA's original constructor and vtable rather than copying the reconstructed lifecycle. The isolated `test-resources/native-ped-leave-car-test` harness requires both client observation of `TASK_COMPLEX_LEAVE_CAR` and the matching server `onVehicleExit` before reporting success. `SWEET1` then uses its black fade to place Sweet at exactly `2095.80, -1649.86, 12.70` with heading `277` before performing the sequence that walks to the tag and shoots it. `Tagging Up Turf` preserves that order: the two-sided leave proof and actor-staging barrier must both complete before the composed native sequence begins.

The next primitive, `setPedShootAt`, follows opcode `0668 TASK_SHOOT_AT_COORD`. Its gate used the same target executable and verified handler `0x4948EF`, the opaque `0x3C` `CTaskSimpleGunControl` allocation, and constructor `0x61F3F0`. The coordinate path passes a null target entity, a pointer to the target position, a null movement target, `FIREBURST`, burst length `5`, and the SCM duration unchanged. `SWEET1` therefore uses exactly `15000 ms`. Negative durations remain indefinite; there is no `-1` or `-2` normalization inside this task.

This gate found material errors in the candidate reverse: constructor booleans at offsets `0x38` and `0x39` have reversed initial values, offset `0x3A` is not initialized by the original constructor, and the reconstructed `ProcessPed` omits the native `PEDMOVE_STILL` assignment and a line-of-sight state update. Neon consequently treats the layout as opaque and calls GTA's original constructor and lifecycle rather than copying the reconstructed C++.

`GunControl` creates and commands GTA's secondary `UseGun` task, producing native aiming, animation, weapon FX, ammo consumption, and firing behavior. MTA deliberately removes world tags and patches `CTagManager::IsTag` to return false, so restoring the global manager would neither be safe nor discover resource-created objects. Neon instead extends the verified `CShotInfo::Update` spray call for explicitly acquired MTA objects. GTA still owns shot execution, surface response, the 8-alpha increment, the 255 cap, and the completion return that drives frontend audio. Lua receives an event only after this native path changes an owned object. The authoritative consumer accepts consecutive exact 8-alpha reports without an artificial minimum interval because the verified update loop can process several live spray shots in the same frame; dropping one valid step would permanently desynchronize the client's next `previousAlpha` from the server.

The first gameplay trace exposed two omitted adjacent SCM commands rather than an error in `GunControl`: `SWEET1` sets Sweet's weapon accuracy to `90` and shooting rate to `100` before performing the sequence. `SET_CHAR_SHOOT_RATE` opcode `07DD` was verified at handler `0x472759`; it writes the low byte directly to `CPed+0x719`, whose constructor default is `40`. The native gun task uses that byte for burst size and attack delay, explaining the observed one-second pauses at the default value. `SET_CHAR_ACCURACY` opcode `02E2` was verified at handler `0x48067D` and writes the low byte to `CPed+0x71A`. Neon exposes these bytes through `setPedWeaponShootingRate` and `setPedWeaponAccuracy` because hiding persistent combat state inside `setPedShootAt` would make the generic task API misleading.

The tag gate verified `CTagManager::SetupAtomic` (`0x49CE10`), its atomic alpha accessors (`0x49CD30`/`0x49CD40`), `RenderTagForPC` (`0x49CE40`), `CWorld::SprayPaintWorld` (`0x565B70`), and its call from `CShotInfo::Update` at `0x73A0FF`. Each tag model already contains both rival and `grove` materials; model `1524` is another tag site, not a painted replacement for model `1490`. The renderer writes `floor(alpha * alpha / 255)` to material index `1`. The spray path considers at most 15 nearby tags, copies the selected entity's forward vector, increments alpha by 8 with a 255 cap, returns 1 for a hit, and returns 2 for the transition to 255. Neon preserves those semantics for resource-owned objects while leaving MTA's global `IsTag` patch and default world unchanged. The isolated `test-resources/native-gang-tag-test` harness checks exact deltas, completion, synchronization, explicit cleanup, and restart cleanup.

The native sequence slice was checked against the compact GTA SA 1.0 executable. `OPEN_SEQUENCE_TASK`, `CLOSE_SEQUENCE_TASK`, and `PERFORM_SEQUENCE_TASK` reach the global template pool and `CTaskComplexUseSequence` constructor `0x635450`; `GET_SEQUENCE_PROGRESS` returns the current task index from the active UseSequence object. `CTaskSequences::GetAvailableSlot` at `0x632E00` divides the 64 templates into two banks of 32 and accepts only a closed template whose first task pointer is null. The previous `gta-reversed-dryxio` implementation was a stub returning `-1`; reverse commit `864b7ab8` reconstructs the verified scan. Neon now transfers up to eight factory-created child interfaces into one template without leaving stale MTA task wrappers, dispatches the UseSequence through the existing script-command event path, and exposes its current index through `getPedTaskSequenceProgress`.

The SWEET3 drive-route gate adds opcode `05D1`. The local SCM opens one
sequence containing eight `TASK_CAR_DRIVE_TO_COORD -1 -1` children from the
restaurant to Grove Street, with speeds `13, 15, 18, 18, 20, 20, 20, 15`, mode
`normal`, desired model `412`, and style `avoid_cars`. The target handler at
`0x490649` allocates `0x3C` and calls constructor `0x63CE00`; the original
vehicle-less base path binds the driver's current vehicle when a child starts.
Mode dispatch reaches GTA's normal, accurate, straight-line, or racing car
missions without a Lua steering loop. The adjacent `SetUpCar` audit found that
the executable rounds cruise speed through `0x821B40`; reverse commit
`53779e52` replaces the prior truncation. The isolated
`test-resources/native-drive-route-test` harness reports native acceptance,
each sequence index, client position, and independently synchronized server
position as separate evidence. Manual validation on 20 July 2026 observed
native acceptance, every index from `0` through `7` in order, natural sequence
completion at `77584 ms`, and matching client/server final coordinates
`(2504.281, -1673.919, 13.205)`, only `1.81 m` from the eighth target. A road
collision did not reset, interrupt, or reorder the native sequence.

`SWEET1` does not wait for the full `15000 ms` gun-task ceiling. It waits until sequence progress reaches the shoot step, then waits for the demonstration tag to reach 100%, interrupts the sequence, and performs the following `WAIT 1000` before the checkout animation and dialogue. The regression resource now dispatches one `leave_car -> go_to -> shoot_at` template and observes native indices `1` and `2`; it no longer assigns those three template tasks separately from Lua. It then uses MTA's synchronized animation pipeline for the exact non-looped `GRAFFITI_CHKOUT` parameters and waits for the syncer to observe natural completion. Its co-op preparation barrier waits for the camera lease plus both native samples on every participant before starting the shared timeline. The client preserves the SCM's CA-to-AR request order. The stock script issues those loads immediately before fixed-camera staging; the co-op harness reserves the same handles at the enter-car/drive transition so replacement banks with an unusually slow cold disk path can load during travel, then transfers them unchanged into the scene. It still requires GTA's real loaded state and polls both handles while Neon's mission-audio service recovers hardware requests that GTA silently accepted into a link without queueing. Structurally valid replacement banks whose tracks contain silence therefore use GTA's ordinary loaded/play/finished lifecycle and require no mission-specific timing fallback. The arrival coordinate blip also follows `ADD_BLIP_FOR_COORD`'s fixed cream destination colour rather than inheriting the Greenwood's friendly blue. Sweet receives `TASK_LEAVE_CAR` first and each player receives the same native task 600 ms later; only the SCM-style post-fade staging may forcibly remove a participant who is still seated. The same server-owned timeline gates all three verified camera shots, the native position/target tracks, `SWE1_AR`, `SWE1_CA`, global leader skip, and final camera/audio cleanup. Both dialogue windows also drive the persistent native facial controller with the SCM's `FACTALK` request. Manual `/tagupidlewood` validation observed the native shoot index after `14949 ms`, the authoritative tag reach 100%, sequence cancellation, the exact following `WAIT 1000`, and normal camera/audio cleanup after a `27420 ms` demonstration scene.

IPL rotations must follow GTA's loader rather than a raw quaternion-to-yaw conversion. GTA conjugates the stored quaternion, so yaw-only placements use `(360 - rawYaw) % 360`. The error was nearly invisible at headings around 0 and 180 degrees but turned the alley and rooftop tags by 180 degrees, leaving their painted faces against the wall and backface-culled.

## Current implementation slice: native script camera

Neon now exposes GTA's verified fixed/look-at, vector position and target tracks, persistence, fade, widescreen, and scripted near-clip primitives through a client-side, resource-owned camera service. Acquisition returns a generation token, so delayed callbacks from an older run cannot control a later lease owned by the same resource. `isScriptCameraLeaseActive` lets a timeline detect that an authoritative camera takeover revoked that token. The service snapshots the previous MTA camera state and uses an independent, reference-counted gameplay-input inhibitor rather than changing resource-visible control binds. The outermost inhibitor also snapshots and sets only GTA pad bit `bPlayerSafe` (`0x20` in `DisablePlayerControls` at `CPad+0x10E`), then restores that bit on the final release. It never calls the patched-out `CPlayerInfo::MakePlayerSafe` and therefore does not inherit its task, invulnerability, explosion, projectile, or world-cleanup side effects.

The lease is restored on explicit release, resource stop or restart, disconnect, and authoritative server camera takeover. Normal cleanup forces the screen visible; a successful timeline may explicitly preserve the current fade while restoring gameplay camera and controls, enabling the SCM pattern of staging a skip under black before fading back in. Legacy client camera and near-clip setters are rejected while a lease is active, preventing MTA's fixed-camera hook from racing GTA's native vector processors. The isolated `test-resources/native-script-camera-test` harness validates fixed framing, simultaneous eased position and target tracks, fade out/in, explicit abort, and cleanup across resource restart. Manual validation completed the full sequence in `8620 ms`, including a `4267 ms` move/track and one-second native fades, then successfully restarted the resource after native lease cleanup.

The ASM gate also corrected `CCamera::VectorTrackRunning` in `gta-reversed-dryxio`: `0x474870` is the function entry, while the former `0x474891` annotation points to an internal parity branch. The camera service is generic and local to each player; cooperative mission code must synchronize only the timeline, readiness, and skip decision at the server layer.

## Current implementation slice: native file cutscenes

MTA normally replaces GTA model slot `1`, the original `CSPLAY` `CClumpModelInfo`, with its `TRUTH` special-character mapping during startup. Managed file cutscenes preserve the original model-info pointer and streaming metadata, restore them only while native cutscene data owns the slot, and reinstall the exact MTA mapping after teardown. The clothes pipeline also keeps cutscene-player clumps out of the ordinary CJ cache and limits model-zero memory retention to model `0`. This distinction is required because gameplay CJ has 37 bones while the `CSPLAY` hierarchy and `SWEET1A` facial tracks have 61. Reusing a cached gameplay clump produced the former stretched jaw and neck, while routing `csplay` through the special-model loader produced invalid IMG reads and freezes. Manual `/tagupsweet1a` validation completed without a freeze or crash, kept CJ visible from the opening shot, and rendered the native facial animation without deformation.

Neon now exposes GTA's stock file-cutscene lifecycle through the same resource-exclusive camera lease. `requestFileCutscene` validates a one-to-seven-character name against GTA's cutscene audio-track table before calling `CCutsceneMgr::LoadCutsceneData`; its optional visible-area argument reproduces the SCM's world-only `SET_AREA_VISIBLE` without moving the local player to another interior. Load, start, native finish, skip-input query, synchronized skip, native skipped state, fade, and deletion remain fixed-address operations behind the append-only `CGame` interface. A file-cutscene token cannot be used by ordinary script-camera setters. Explicit release, resource destruction, disconnect, or an authoritative camera takeover deletes native cutscene data before restoring the captured world area, camera, near clip, widescreen, and input state.

The compact target executable, SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`, maps load to `0x4D5E80`, start to `0x5B1460`, native camera-spline completion to `0x5B0570`, skip input to `0x4D5D10`, skip to `0x5B1700`, deletion to `0x4D5ED0`, and track-name lookup to `0x5AFA50`. Load status is the dword at `0xB5F84C`; running, processing, and skipped are bytes at `0xB5F851`, `0xB5F852`, and `0xB5F854`. GTA normally consumes skip input at call site `0x5B1947`. Neon intercepts that one call while a managed file cutscene is active, letting Lua query the original Enter, Space, mouse-left, gamepad-cross, and focus-loss edge while a server broadcasts the authorized decision to every participant.

`SWEET2A` exposed a second MTA model-slot collision in this lifecycle. GTA assigns the first nonstandard cutscene model to `CUTOBJ01` (model `300`) at `0x5B1114` before the loaded-slot scan at `0x5B112B`. MTA replaces `CUTOBJ01` through `CUTOBJ13` (models `300..312`) with playable `CPedModelInfo` mappings during startup. `SWEET1A` happened to put the skinned ped `cssmoke` first, masking the mismatch; `SWEET2A` puts the unskinned cigarette prop `CsCigRydr3` first. Loading that prop through `CPedModelInfo::CreateHitColModelSkinned` at `0x4C6D90` supplies a null hierarchy to `RpHAnimIDGetIndex` at `0x7C51A0` and faults at `0x7C51A8`. The compact target and the actual ProgramData runtime executable, SHA-256 `77485627b4ef17f92819318050d501e171c7ab84ceffe5091b973b9e29f9cc98`, agree on the decisive assignment, scan, and collision-building bodies; the runtime has only its existing entry detour at `0x4C6D92`. The current `gta-reversed-dryxio` reconstruction matches this control flow, so no reverse correction was required. Managed cutscenes now preserve the original generic mappings and streaming metadata before MTA replaces those 13 slots, install them together with `CSPLAY` for the complete native lifecycle, and verify every mapping before entering GTA. Teardown restores each exact MTA model-info object, then calls GTA's verified `RequestSpecialModel` path at `0x409D10` with `RYDER2` through `PSYCHO` so extra.img offsets, sizes, and texture mappings are resolved afresh. Recopying a captured live `CStreamingInfo` caused GTA's model `304` KENDL load-error dialog after `SWEET2A` and is deliberately avoided. The validated Drive-Thru run completed the full file cutscene, restored every slot without a model-load dialog, and reached the Cluckin' Bell checkpoint without a client or server error.

`SWEET3A` exposed the matching teardown hazard. `CStreaming::RemoveModel` unloads a CUTOBJ clump but retains its CUTS.IMG offset and size. After Neon swaps the MTA `CPedModelInfo` pointer back in, `RequestSpecialModel` at `0x409D10` takes its early request branch when the ped's already-correct name and that stale directory range are both present; the next stream pulse consequently feeds a cutscene prop to the ped collision builder and faults at the same `0x7C51A8`. The decisive `0x409D10` prefix is byte-identical in the compact executable and the actual ProgramData runtime above, and the current reverse reconstruction shows the same name-plus-directory predicate. Teardown now clears the already-unlinked `CStreamingInfo` before restoring each MTA model-info pointer, forcing `RequestSpecialModel` to resolve the correct extra.img directory entry and TXD. This is intentionally different from restoring a captured live streaming record, whose linked-list and load-state fields are unsafe to replay.

The same scene exposed a pre-existing MTA object-area mutation. GTA's `CObject::Init` writes universal area `13` at `0x59FAA0`, so the `CCutsceneObject` instances created at `0x5B02A0` remain visible when the SCM selects interior area `1`. Multiplayer initialization changes only the immediate byte at `0x59FAA3` to `0`, historically making pickup objects belong to the normal world; that global patch also made every native cutscene actor and prop area `0`. The result was exact inverse evidence: actors on a black area-0 background before the visible-area lease, then the correct interior with all actors filtered out after it. The compact and actual ProgramData executables are byte-identical at the constructor, object-array/count writes (`0xBC3F18` and `0xBC3FE4`), and renderer area predicate; `gta-reversed-dryxio` matches the unpatched behavior. After native load status reaches `2`, Neon now restores area `13` only on the current manager-owned cutscene objects before exposing the loaded state to Lua. The pickup patch and all ordinary MTA objects remain unchanged.

The audit corrected two adjacent discrepancies in the canonical `gta-reversed-dryxio` worktree. Space uses `IsStandardKeyJustPressed`, not the down-state helper, and `CPad::Clear(false, false)` runs after native cutscene teardown rather than before `MakePlayerSafe`. Neon calls the target routines directly and does not copy the reverse parser or lifecycle. The installed `main.scm`, SHA-256 `601def3baae766ce6a23e2f0b9b48f6b33c9a64e2fc32eb4f22ddea8b868b0fa`, loads `SWEET1A`, waits for native load, starts, fades in over one second, waits for native completion, disables player control, performs an immediate fade out, waits for black, and clears the cutscene before creating Sweet, the Greenwood, and the mission tags.

`Tagging Up Turf` now follows that boundary. Its server enters a dedicated `sweet1a` stage, waits for every participant to load and start the local native playback, accepts skip input only from the leader, waits for every native finish and black fade, then waits for every native release. Only after that release barrier does it create the synchronized mission entities and begin the already validated world intro. `/tagup` and `/tagupsweet1a` exercise this path. Manual single-player validation completed `SWEET1A` naturally in roughly 34 seconds, released the native lease, completed `SWE1_AA` through `SWE1_AE`, and restored camera and audio without an error.

That validation also exposed a collision in the generated extended-world patch manifest. Address `0x00858B34` is GTA's shared `60.0` animation-tick constant, but an earlier manifest replaced it with the world-sector half-width `200.0` even though all sector instruction operands already pointed to the dedicated `g_worldSectorCountHalf`. ANPK therefore stretched every `SWEET1A` hierarchy by exactly `200 / 60`, from `33.200` to `110.667` seconds, and left the animated spray prop behind the camera. The manifest generator now excludes this shared address while retaining the extended sector redirections. Temporary diagnostics then measured every hierarchy at `33.200` seconds with playback speed `1.0`, matching the cutscene camera and restoring the spray-prop motion.

The cinematic vehicle slowdown was separately verified from `SWEET1` through the GTA SA 1.0 executable. The mission's grounded 4 m locate gate does not brake the Greenwood itself: opcode `SET_PLAYER_CONTROL OFF` reaches its handler at `0x47D3C8`, calls `CPlayerInfo::MakePlayerSafe(true, 10.0)` at `0x56E870`, and sets `bPlayerSafe` at `0x56E89D`. `CAutomobile::ProcessControlInputs` consumes the resulting nonzero `DisablePlayerControls` word at `0x6ADDCC`, applies full brake and handbrake, clears throttle, clamps velocity magnitude to `0.28`, and lets physics finish the stop. `SWEET1` does not use `APPLY_BRAKES_TO_PLAYERS_CAR`, zero velocity, or freeze the vehicle. The generic inhibitor now reproduces that native pad-to-vehicle path for every control-inhibiting camera lease; MTA continues to synchronize the locally simulated vehicle transform.

`Tagging Up Turf` first consumes that generic service at the Ballas arrival. Each participant requests native mission-audio event `37420` (`SWE1_AV`), acquires a local camera lease, and applies the `SWEET1` fixed camera, point-at target, widescreen state, and control inhibition. A server-owned camera barrier prevents any participant from beginning `TASK_LEAVE_CAR` until every client has prepared the shot. Each client then enforces the SCM's minimum 100 ms camera lead-in, requests the native leave task, and only afterward begins polling the already requested audio handle. A second server barrier waits for every handle to report loaded before broadcasting synchronized playback. Each client reports only the line's natural native finish. The server waits for every participant's audio completion and authoritative vehicle exit before it permits Sweet's `TASK_CAR_DRIVE_WANDER`, then completes the following `WAIT 1000` window from native task acceptance before releasing every lease. Explicit abort, failure, stage replacement, timeout, lease takeover, and resource shutdown restore the owned camera and audio handles through the same scene cleanup. Idlewood, Ballas, and Grove now combine the SCM's 4 m axis-aligned box with the exact syncer-local `09D0` all-four-contact predicate.

The audio ordering was checked against the installed `main.scm` at `/Users/salimtrouve/Documents/GTA-SanAndreas/data/script/main.scm`, SHA-256 `601def3baae766ce6a23e2f0b9b48f6b33c9a64e2fc32eb4f22ddea8b868b0fa`. `SWEET1` loads slot 1 with event `37420` before enabling widescreen and the fixed camera, performs `WAIT 100`, requests CJ's leave-car task, waits for the sample to load, plays it with `SWE1_AV`, waits for `HAS_MISSION_AUDIO_FINISHED`, waits for CJ to be outside, starts `05D2`, then performs `WAIT 1000`. This slice reuses the already verified mission-audio service and requires no new executable wrapper or reverse-side correction.

The two cinematic vehicle-arrival gates are evaluated by the leader/syncer on every client frame, matching the SCM `WAIT 0` cadence instead of a periodic network poll. On entry, the client acquires the native control-inhibiting camera lease before sending the authoritative progress request, then promotes that same lease into the server-approved scene. Their visible areas use the generic client `renderScriptImportantArea` primitive, which calls GTA's verified SCM important-area renderer every frame and preserves its three concentric red layers, pulse, additive alpha, and ground correction. Collision remains a separate inclusive 8 x 8 x 8 box, as it is in `LOCATE_CAR_3D`.

## Current implementation slice: native mission audio

Neon now leases GTA's four native mission-audio slots through opaque client-side handles. `requestMissionAudio` accepts only GTA's verified script-event families, refuses a slot with unknown native ownership, and confirms the event stored by GTA because native preload can fail silently. Load, one-shot play, natural-finish query, explicit release, and resource-stop cleanup all validate the calling resource and the handle generation before touching a slot.

The cold-load recovery path was verified against both the compact executable with SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b` and MTA's actual ProgramData runtime copy with SHA-256 `77485627b4ef17f92819318050d501e171c7ab84ceffe5091b973b9e29f9cc98`; the relevant instruction ranges are byte-identical. `CAEScriptAudioEntity::PreloadMissionAudio` at `0x4EC190` calls the hardware loader before storing the event in its link. `CAEAudioHardware::LoadSound` at `0x4D8ED0` silently returns when effects loading is disabled, and `CAEMP3BankLoader::LoadSound` at `0x4E07A0` deduplicates against 50 request entries before writing the next ring entry without proving that an older pending request was not displaced. In either case the stored event can match while `GetMissionAudioLoadingStatus` (`0x4EBF60`, forwarding to `0x4D8F00`) remains zero indefinitely. Neon therefore re-arms an owned, unplayed, still-matching event at most once every 500 ms while Lua polls its load state; the native duplicate scan makes this idempotent when the original request is still queued. Native clear was checked separately at `0x5072F0`/`0x4EC040`: it cancels the bank-slot sounds and clears the entity, position, and sound pointers but leaves the event returned by `0x4EC020` at link offset `+0x14`. Neon's wrapper resets that event to `-1` only after its own clear, preventing a Client Deathmatch reload from turning released handles into four apparently foreign slots while preserving the rule that genuinely unknown native events are never preempted. The relevant `gta-reversed-dryxio` reconstruction matches these branches, so no reverse-side correction was required.

Audio remains local output rather than server state. Cooperative timelines preload on every participant, wait at a server readiness barrier, broadcast the play decision, and advance only after every client reports natural completion or the server guard expires. The isolated `test-resources/native-mission-audio-test` resource exercises individual `SWE1_AR`, `SWE1_CA`, and `SWE1_CB` events, the two-slot story sequence, invalid requests, pool exhaustion, mid-play clear, and resource-restart cleanup.

Mission-passed music uses a separate verified path. Opcode `0394` reaches handler `0x4842AF`, accepts selectors `1` and `2`, adds `10`, calls `CAudioEngine::PreloadBeatTrack` at `0x507F40`, then calls `PlayPreloadedBeatTrack(true)` at `0x507180`. `playMissionPassedTune` exposes only those two selectors and delegates to Neon's existing `PlayBeatTrack` wrapper, which performs the same pair of native calls. MTA replaces the radio manager's public stop entry at `0x4E9820` with `ret 8`; calling vanilla beat preload while the radio is playing therefore left the manager in `PLAYING` and trapped the main thread in the service loop at `0x507F96..0x507FC1`. The wrapper now invokes MTA's existing bypass into `0x4E9823` first, putting the native manager into `STOPPING` while preserving MTA's logical radio state. Vanilla preload then owns the ordinary service loop, station save, retune event, and eventual mission-track playback without a mission-specific timeout. `Tagging Up Turf` uses selector `1` and awards the visible SCM amount of `$200`; manual `/tagupfinal` validation confirmed both effects after the `17546 ms` Grove finale. Campaign progression, respect, gang-zone, wanted-level, statistics, pickup, and unlock bookkeeping remain intentionally outside the current multiplayer conformance scope.

## Current implementation slice: native mission text

Neon now exposes GTA's mission GXT block and native small-message, help-message, and big-message queues through a client-side resource lease. GTA has only one loaded mission-text table, so one resource owns it at a time; another resource cannot replace the block while the lease is live. Explicit release and resource destruction clear every tracked message and help pointer before ownership disappears. The block itself remains loaded as a harmless cache until a later lease replaces it.

The gate used the compact GTA SA 1.0 executable with SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`. `CText::LoadMissionText` at `0x69FBF0` clears displayed messages, unloads the previous mission arrays, and loads the named table. `CText::Get` at `0x6A0050` searches the main table and then the loaded mission table. `PRINT_NOW` reaches `CMessages::AddMessageJump` at `0x69F1E0`; the wrapper preserves the executable's subtitle-option suppression for spoken `~z~` text. `PRINT_HELP` uses `CHud::SetHelpMessage` at `0x588BE0`, `PRINT_WITH_NUMBER_BIG` uses `CMessages::AddBigMessageWithNumber` at `0x69E5F0`, and the matching clear functions are `0x69EA30` and `0x69EBE0`. The relevant `gta-reversed-dryxio` branches match the executable, so this gate required no reverse-side correction.

`Tagging Up Turf` acquires block `SWEET1`, removes its former always-on DX mission HUD, and submits the original mission keys at the corresponding stage, audio, help, pass, and failure points. This restores GTA's own language selection, font, placement, colors, queue behavior, and subtitle preference. The optional F7 instruction trace remains deliberately separate because it is a development diagnostic rather than mission presentation.

## Current implementation slice: recorded-car playback

Neon now exposes GTA's direct recorded-car player as a generic, client-side, resource-owned service: `requestVehicleRecording`, `isVehicleRecordingLoaded`, `startVehiclePlayback`, `stopVehiclePlayback`, and `isVehiclePlaybackActive`. Only the direct non-AI, non-looped path used by opcode `05EB` is public in this slice. The game wrapper validates the registered recording table, streamed buffer, duplicate vehicle state, and all 16 native slots before calling GTA; this prevents the original executable's slot-zero fallback and full-pool out-of-bounds write from becoming public API behavior.

The mutating Lua calls identify their owning resource. A recording request is reference-tracked across resources, a vehicle playback has one resource owner, and resource shutdown or native vehicle destruction releases the active slot before its raw GTA vehicle reference becomes invalid. A streamed server vehicle may start only on its current unoccupied-vehicle syncer. If ownership migrates, the old client stops rather than fabricating a resume point, because MTA's packets replicate vehicle transforms but not the recording frame index. Player drivers are refused; an owned script ped driver is permitted after its previous vehicle task is gone, matching the original mission sequence.

The ASM gate used the compact GTA SA 1.0 executable with SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`. It verified request `0x45A020`, loaded test `0x45A060`, direct start `0x45A980`, stop `0x45A280`, active test `0x4594C0`, and the SCM handlers for `07C0`, `07C1`, `05EB`, `05EC`, and `060E`. It also corrected material errors in `gta-reversed-dryxio`: the playback-streaming index has 16 entries rather than three, request does not immediately unload its buffer, frame indices are byte offsets, paused playback freezes time, smoothing starts at frame two, unused-recording removal scans the full table, skip-to-end clears collision-force disabling, and `CTheScripts::CleanUpThisVehicle` had its mission-vehicle predicate inverted.

The isolated `test-resources/native-vehicle-recording-test` harness uses recording `207`: 35 frames from approximately `(2339.67, -1488.19, 23.61)` to `(2381.07, -1528.44, 23.66)`, ending at timestamp `7719 ms` and completing in roughly eight wall-clock seconds. The playback loop's `0.25` multiplier does not make that duration four times longer: `m_snPPPPreviousTimeInMilliseconds` is four timer updates old, so GTA averages four frame deltas before advancing the recording clock. `Tagging Up Turf` requests it during the rooftop stage, stops and verifies Wander, temporarily places Sweet in the driver seat, starts playback only on the vehicle syncer, waits for natural completion, and validates the synchronized endpoint server-side. The post-rooftop scene now runs concurrently with that playback and restores Sweet as passenger only after both the recording and dialogue have finished.

Manual in-game validation completed the direct playback in `8040 ms`, reached the recording endpoint with `0.00 m` server-observed error, restored Sweet to passenger seat 1, and completed the remaining drive home through the mission-passed state. The isolated harness separately observed natural completions at `8087 ms` and `8088 ms`; these measurements justify the shared `6500..12000 ms` conformance window without weakening endpoint or ownership checks.

## Current implementation slice: post-rooftop recorded-car scene

The installed `main.scm`, SHA-256 `601def3baae766ce6a23e2f0b9b48f6b33c9a64e2fc32eb4f22ddea8b868b0fa`, contains opcode `0A0B` at file offset `0x7BA22` with point `(2385.4443, -1529.3350, 24.0351)` and heading `82.7482`. Its executable handler at `0x47B7E4` stops the timer at `0x561AA0`, multiplies the heading by the radians constant at `0x8595EC`, calls `CRenderer::RequestObjectsInDirection` at `0x555CB0` with loading-scene flag `0x20`, calls `CStreaming::LoadScene` at `0x40EB70`, then updates the timer at `0x561B10`. `enginePreloadWorldAreaInDirection` reproduces that order and argument layout exactly. MTA's timer detour preserves the overwritten instruction before returning to GTA, and its current `OnGameTimerUpdate` callback is empty, so the wrapper uses the normal hooked entry rather than bypassing MTA's contract.

The same SCM segment reports vehicle event `1147` twice through opcode `09F7`, at offsets `0x7BB36` and `0x7BBB6`. `reportVehicleMissionAudioEvent` calls GTA's vehicle-attached script-audio wrapper at `0x507390`, preserving the native bank, attenuation, lifetime, and physical attachment instead of emulating a horn control. The public function accepts only the verified `1000..1190` one-shot family and a streamed vehicle.

`Tagging Up Turf` begins this sequence after the recording's two-second lead, fades out for 300 ms, performs the directional load, installs the original fixed camera, and fades in for 300 ms. After the SCM's 3500 ms lead it reports the first horn, crosses a per-client load barrier for event `37430` (`SWE1_BH`), reports the second horn, and plays the dialogue on every participant. The server advances only after both the recorded-car endpoint and every client's natural audio finish are validated. It then returns surviving Flats to native on-foot Wander, restores Sweet to passenger seat 1, and releases every camera and audio lease before the return drive. `/taguppickup` starts directly at this scene for manual validation. Its validated one-player checkpoint run completed `0A0B` in `65 ms`, loaded `SWE1_BH` in `230 ms`, observed its natural finish after `1178 ms`, restored Sweet to passenger seat 1, released the camera and audio leases, and advanced through `return_after_roof` to `drive_home` without a Tagging Up Turf error.

The first integration attempts reached the black fade but never invoked `0A0B`: the resource passed its live scene table as a `setTimer` argument, while MTA's `CLuaArguments` path deep-copies tables before the callback. The copied table failed the resource's identity guard and returned silently, leaving the server to release the camera at its 60-second guard. The scene now captures the original table in a Lua closure. The same latent pattern in the Sweet demonstration audio poll was corrected at the same time. Before/after diagnostics around the directional load distinguish any future native streaming delay from mission orchestration.

## Current implementation slice: Grove Street finale

The final `SWEET1` world scene is now implemented and manually validated in the `Tagging Up Turf` conformance resource. The oracle is the installed `main.scm` at `/Users/salimtrouve/Documents/GTA-SanAndreas/data/script/main.scm`, SHA-256 `601def3baae766ce6a23e2f0b9b48f6b33c9a64e2fc32eb4f22ddea8b868b0fa`. The relevant bytecode begins with the animation request near `0x7BF25`, installs the fixed camera near `0x7BFC6`, starts the 18-second position and target vectors near `0x7C003` and `0x7C028`, stages CJ and Sweet near `0x7C052`, starts `IDLE_CHAT` near `0x7C2DE`, starts both `hndshkfa` animations near `0x7C3DD`, and assigns Sweet's final walk near `0x7C4D5`.

After the verified Grove `LOCATE_CAR_3D`, seated-player, and `09D0` gate, every participant acquires a control-inhibiting camera lease and completes a one-second fade-out barrier. The server removes the spray can, selects the unarmed slot, places CJ at `2511.3518, -1672.14, 12.4588` with heading `180`, places Sweet one metre south with heading `0`, and keeps additional co-op players outside the shot. Each client applies near clip `0.2`, widescreen, the original fixed shot, persistent vector move and track, and the one-second fade-in. Native mission-audio events `37435`, `37436`, `37437`, `37438`, `37443`, `37441`, and `37442` play in the exact `SWE1_BN`, `BO`, `BP`, `BQ`, `BX`, `BT`, `BU` order, with a per-participant load and natural-finish barrier for every line.

The server starts synchronized `PED/IDLE_CHAT` for CJ during `SWE1_BQ`. During `SWE1_BT`, CJ and Sweet receive the non-looped `GANGS/hndshkfa` animation, and the leader's client must observe both actors enter and naturally leave that animation before `SWE1_BU` can load. The final line gives CJ a two-second native look-at and gives Sweet the existing verified `setPedGoTo` walk task toward `2517.3972, -1677.3524, 13.2548` with the SCM's 20000 ms timeout profile. The resource waits for the final audio, task acceptance, and the following 1500 ms before all clients restore camera and controls and the server awards mission completion. Enter, Space, and `/tagupskip` share one leader-authorized skip path; abort, failure, death, destruction, stream loss, lease takeover, timeout, and resource shutdown share the same cleanup. `/tagupfinal` stages the real arrival state and then runs this complete scene for isolated testing. All required primitives already existed, so this slice adds no C++ API or root README surface.

The validated `/tagupfinal` run loaded and naturally completed all seven mission-audio events, observed both `GANGS/hndshkfa` animations end, accepted Sweet's native walk task, restored every camera and audio lease, and awarded mission completion after `17651 ms`. No timeout, ownership failure, or cleanup error was recorded.

## Current implementation slice: complete multiplayer-visible Drive-Thru

The `drive-thru` conformance resource now reproduces the multiplayer-visible SWEET3 pursuit from the end of native file cutscene `SWEET2B` through the death of both Ballas. The server reconstructs CJ, Sweet, Ryder, Smoke, the Greenwood, the Voodoo, and its driver under the black fade, then crosses a client streaming and policy barrier. The driver receives the installed SCM's exact eight-point Voodoo sequence through `native-task-runtime`, which composes the generic `drive_to` descriptor with persistent owner epochs and resource streaming leases. Only after an active logical route index is observed does the server create the Ballas passenger and the two distant Grove support actors. The passenger targets the Greenwood, while Ryder and Sweet target the Voodoo through the already validated generic drive-by primitive. The screen fades in only after all three native tasks are active.

The chase monitor retains the SCM's visible failure and transition conditions. Voodoo destruction or health at or below `250` assigns `TASK_COMPLEX_KILL_PED_ON_FOOT` to each surviving Ballas, then retargets Ryder and Sweet to the corresponding enemies. The same transition occurs when either Ballas dies during the vehicle chase. GTA's kill-on-foot task creates its own leave-car subtask when the attacker is still seated, matching the SCM's direct `TASK_KILL_CHAR_ON_FOOT` calls without a Lua exit command. Both Ballas must be dead before the checkpoint advances. Protagonist, Greenwood, Grove-support and vehicle monitoring is scoped to the exact SCM loops rather than remaining globally active during later return scenes. Reaching the final Grove box with both Ballas alive starts the alternate failure scene instead of failing immediately.

MTA's entity-add packet represents vehicle health with `SVehicleHealthSync`, a 12-bit field at `0.5` precision whose maximum is `2047.5`. SWEET3 creates the Voodoo with health `2700`, so the initial reconstruction barrier acknowledges the unavoidable compact value only long enough to prove streaming and ownership. The server then sends ordinary `SET_ELEMENT_HEALTH`, whose full-float payload reaches `CClientVehicle::SetHealth`, and the route barrier requires the client to observe `2700` before constructing the native drive sequence. This preserves the vanilla local damage budget without widening MTA's protocol or adding a mission-specific C++ primitive.

Manual validation on 20 July 2026 completed native `SWEET2B` naturally in `83313 ms`, then exposed and confirmed the initial health-saturation diagnosis. Two corrected pursuit runs passed reconstruction, observed the route and all three drive-bys, exchanged real gunfire, entered foot combat at the `250` threshold, and reached the terminal checkpoint only after both Ballas were dead. The server recorded the transitions at `14:18:20` and `14:19:54` and the corresponding passes at `14:18:30` and `14:20:06`, with no Drive-Thru error.

After that validated pursuit, the resource now continues through the complete multiplayer-visible return path. It uses the installed SCM's exact Grove and Smoke `LOCATE_CAR_3D` boxes, nonzero important-area flags, seated-player check, and `09D0` grounded-car condition. The native important-area renderer supplies the world checkpoints while separate server validation remains authoritative. Native events `37651..37681` retain the four-second driving leads, original speaker mapping, text keys `SWE2_MA..PE`, and natural-finish barriers. The Grove scene begins on its exact fixed shot, changes to the original 17-second vector move and track after the third scene line, and gives CJ the verified native look-at after the first. Its fixed phase does not enable vector persistence before the SCM resets and configures those vector components. The Smoke scene sets vehicle camera mode `1`, enables persistence with its initialized vectors, starts its exact 17-second move and track immediately, waits 500 ms, and applies CJ's native look-at before dialogue. Both scenes observe the resulting camera matrix independently from native setter acceptance before dialogue begins.

Sweet, Ryder, and Smoke leave through the generic sequence surface rather than Lua animation or position approximations. Each sequence combines the existing `leave_car` and `go_to` descriptors with the installed coordinate, movement mode, timeout, and actor delay. Client task acceptance, non-negative native sequence progress, and the SCM departure waits are observed separately before camera release. Natural completion restores the camera directly. The leader-authorized skip path uses the original 500 ms fade-out and 500 ms black hold, removes the actors while black, and fades back in. Completion gives the visible `$200`, prints `M_PASSS`, and plays mission-passed tune `1`. Respect, wanted-level reset, mission counters, progress flags, and save-stat bookkeeping remain outside the multiplayer conformance scope. The complete happy return path was manually validated on 20 July 2026.

The subsequent Lua parity pass restores SWEET3's two return-to-car reminder cycles from the installed audio table: seven Ryder/Sweet lines while the full crew remains and five Smoke lines after the Grove drop-off. Leaving the Greenwood clears the active conversation channel, consumes that interrupted line, plays the three-second reminder, then presents `TW2_X`; re-entry resumes at the next dialogue index. CJ's existing weapon loadout is no longer stripped at mission start, while isolated harness cleanup still restores its snapshot. Natural mission-audio completion clears the native small-message queue in the same place as `load_and_play_audio_sweet3`. Server death, disappearance, explosion, and low-health monitoring is now stage-scoped to the corresponding SCM loops instead of remaining active during the return cameras. These changes consume existing Lua and native mission-audio/GXT surfaces and add no C++ primitive.

The final SWEET3 failure-parity checkpoint adds the three missing sequence descriptors required by the installed SCM. Opcode `0622` enters its target branch at `0x491FFF` and constructs `CTaskComplexLeaveCar` at `0x63B8C0` with door `0`, delay `0`, sensible `false`, and force `false`. Opcode `05DD` enters at `0x490E63` and constructs the exact `0x40` `CTaskComplexSmartFleeEntity` at `0x65C430` with screaming enabled, the script distance and timeout, a `1000` ms position check, and the target tolerance stored at `0xC18CF0`. Opcode `05BE` enters at `0x48FD8D` and constructs the exact `0x28` `CTaskComplexDie` at `0x630040` with unarmed means, group `0`, animation `15`, blend `4.0`, and animation speed `0.0`. These instruction paths were checked against the compact GTA SA 1.0 executable with SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b` before changing Neon. The local reverse tree matched all constructors and layouts; its smart-flee copy constructor alone omitted the target's move-state copy at `0x65CF98`, corrected separately in gta-reversed-dryxio commit `c0a03947` without building that project.

Drive-Thru now plays `SWE2_KA`, `SWE2_KB`, or `SWE2_KC` at the Greenwood's `250` health threshold, dispatches immediate-leave plus ten-second smart-flee sequences to the phase-appropriate crew, and waits for both native sequence activation and natural audio completion before `SWE3_D`. The alternate Grove failure reproduces the SCM fixed camera and world loads, speed `1` straight-line Voodoo task, coordinate drive-by, scripted deaths after `2000` and `2500` ms, speed `30` escape, black teardown, and camera restoration before `TW2_Y`. `TASK_DIE` sets native health to zero without MTA's ordinary damage event, so the authoritative syncer separately reports the observed death task and zero health before the server starts the escape. Direct inspection of the installed `main.scm` also disproved the generated decompile's `SET_TIME_SCALE 0.75`: the target SWEET3 bytecode proceeds directly from `TASK_DRIVE_BY` to `TIMERA = 0`, and the resource therefore leaves game speed untouched. The passive `/drivethrutracechat` presentation mirrors Tagging Up Turf's chat tracer while remaining entirely client-local and unable to advance mission state. Early in-game runs exposed a native-task guard timeout, shots continuing after scripted deaths, weapon orientation following the local camera, and terminal route completion restoring the driver's automatic syncer. The task lifetime, remaining native gang-drive-by AI `IsPlayer` branches and two-phase route-to-camera ownership handoff corrected those defects. The 21 July `/drivethrusimfar` regression reached route index `7` off stream, committed driver and Voodoo ownership, passed the five-entity scene barrier for three stable samples, activated the slow drive and coordinate drive-by, observed both deaths, started the escape and restored the camera before the expected `TW2_Y` failure. Dedicated tests of every Greenwood low-health branch remain pending.

## Current implementation slice: Nines and AK's awaiting full in-game validation

The `nines-and-aks` resource maps the complete multiplayer-visible `sweet2` graph from native `SWEET3A` through the return from Binco. It composes the existing mission text and audio leases, managed file cutscenes, script cameras, mission actors, native task sequences, navigation, vehicle predicates, and deterministic server cleanup. Its server owns stage transitions, actors, vehicles, player snapshots, hit evidence and terminal decisions. Its client owns the local file-cutscene, camera and audio generations, syncer-native actor tasks, temporary bottles, navigation and tutorial presentation. Campaign counters, persistent unlocks, shop blips, respect and save statistics remain explicitly outside the multiplayer scope. The resource README contains the phase coverage, adaptation ledger, failure matrix and manual validation checklist.

`SWEET3A` required two generic managed-cutscene corrections beyond the existing CUTOBJ mapping. Teardown now clears stale, already-unlinked CUTS.IMG streaming records before asking MTA to resolve its restored special-character models from extra.img. Native load completion also repairs area `13` only on the current manager-owned cutscene objects, compensating for MTA's global area-0 pickup initialization without changing ordinary objects. The optional `requestFileCutscene(name, visibleArea)` argument owns and restores the SCM world-area selection as part of the same exclusive lease. These changes fixed the observed post-cutscene `0x7C51A8` access violation and the inverse visibility failure where either the interior or its actors rendered, but not both.

The shooting range is driven by the installed `main.scm` and raw bytecode rather than trusting decompiled floating-point constants. Round 1 restores its bottle, Smoke and break close-ups. Round 2 uses the native 8000 ms camera position and target tracks. Round 3 maps five sequence progress steps to the five original close-ups and audio lines. `DYN_WINE_BIG` positions add the installed `0.240264` base-to-centre offset to the corrected raw Z values, while the native shoot tasks keep the unadjusted SCM targets. The Tampa explicitly opts into MTA's otherwise disabled server-vehicle petrol-cap weakpoint, stays proofed during both scripted shots, then becomes destructible at the same five-second control barrier. The departure waits through the original black staging, directional preload, four cameras, audio gates, native entries and authoritative seat observation before the return drive.

User testing reached the Emmet range and exposed the original cutscene background, actor visibility, teardown crash and camera-sequencing defects. The corrected C++ file-cutscene path built successfully as `Game SA`, `Release|Win32`; the Lua resource passes `luac -p` and was deployed on a server start reporting `253 loaded, 0 failed`. The final camera, raw bottle-height, Tampa weakpoint and departure corrections are statically mapped and awaiting a fresh complete in-game run. This checkpoint therefore does not claim full 1:1 runtime validation.

## Current implementation slice: synchronized CJ appearance

Manual validation confirmed CJ's fresh-game world appearance in `/tagupfinal`, correct native `CSPLAY` rendering in `/tagupsweet1a`, and the transition into the world intro. Multi-participant appearance synchronization and final post-mission restoration remain pending.

World-space `SWEET1` scenes previously used whatever arbitrary MTA skin the mission leader had when starting the resource. The native `SWEET1A` file scene owns its cutscene actors, but every following camera and gameplay stage uses the synchronized player element. The resource now snapshots the leader's original model and all 18 clothing slots before mutation, switches only that leader to CJ model `0`, clears stale accessories, and applies the fresh-game clothing state installed by `main.scm`: `vest/vest` in slot `0`, `player_face/head` in slot `1`, `jeansdenim/jeans` in slot `2`, and `sneakerbincblk/sneaker` in slot `3`. `SWEET1` itself does not replace CJ's clothes. Server-side readback must match all four texture/model pairs before the mission can start.

MTA already synchronizes `setElementModel`, `getPedClothes`, `addPedClothes`, and `removePedClothes`, so the world-space appearance slice requires no new public C++ primitive or root README entry. On mission success, failure, abort, or resource shutdown, the leader is temporarily rebuilt as model `0` because GTA clothing changes are valid only for CJ; the saved clothing state is restored first, then the original player model is reapplied. Other co-op participants retain their original skins throughout. Multi-participant appearance synchronization and final post-mission restoration remain pending.

## Delivery plan

### Phase 0: establish the oracle and traces

- Record the original `SWEET1` opcode working set and important parameters.
- Map those opcodes to relevant `gta-reversed-dryxio` functions and `CTask` classes.
- Capture expected single-player stage transitions and observable task completion conditions.
- Retain the current MTA prototype as a comparison and network test harness.

### Phase 1: native client task foundation

- Introduce resource-owned native task handles and lifecycle management.
- Implement go-to, enter-vehicle, leave-vehicle, animation, and task sequencing.
- Add client diagnostics that show task type, owner, state, target, and failure reason.
- Exercise tasks on local peds independently of any SCM runtime.

### Phase 2: authoritative task networking

- Add server creation, cancellation, status, and completion events.
- Define syncer ownership and migration.
- Validate cleanup on resource restart, disconnect, death, element destruction, and stream transitions.
- Test one, two, and three connected players before mission integration.

### Phase 3: native story primitives

- Extend the native gang-tag service with server-owned generations and migration when the reporting player or NPC syncer changes.
- Validate the integrated native `SWEET1A` file cutscene and the implemented mission camera/audio scenes with multiple participants.
- Replace the remaining setup/seat-placement and combat-control substitutes with verified native tasks or services.
- Complete Tagging Up Turf end to end as the first conformance mission.

### Phase 4: minimal SCM runtime

- Decode the mission's variables, control flow, waits, conditions, and entity handles.
- Implement its opcode working set through the generic APIs.
- Run the mission from SCM-derived data instead of the hand-authored Lua stage table.
- Keep co-op adaptations in a separate mission policy definition.

### Phase 5: expand by mission coverage

- Select the next mission based on new opcode and engine-feature coverage.
- Add handlers and native primitives once, with conformance tests.
- Track implemented, partial, unsupported, and multiplayer-overridden opcodes.
- Grow toward the campaign without creating mission-local substitutes for missing runtime behavior.

## First milestone completion criteria

The native-task foundation is not complete merely when a ped moves once. The first milestone requires evidence that:

- A server resource can assign, inspect, cancel, and observe completion of a native ped task.
- Go-to, enter-vehicle, leave-vehicle, and animation tasks use GTA task classes rather than teleport loops.
- Task ownership survives a deliberate syncer change.
- All task state is cleaned up after resource stop and element destruction.
- Legacy resources and peds without native tasks behave unchanged.
- A small standalone test resource reproduces the behavior deterministically.
- Tagging Up Turf uses the new APIs for Sweet's first drive and demonstration sequence.

## Validation strategy

Validation should proceed at three levels:

1. Native task tests exercise each API independently and expose detailed diagnostics.
2. Opcode conformance tests compare handler inputs and observable results with original GTA behavior.
3. Mission tests cover one-player and co-op flows, including reconnect, resource restart, death, task interruption, actor streaming, vehicle destruction, and mission cleanup.

The Windows VM remains the runtime target: source changes are made in the canonical macOS tree, synchronized into the VM-local build copy, built as `Release|Win32` for the client and `Release|x64` for the server where applicable, then exercised against the local server.

## Non-goals

- Do not implement every SCM opcode before validating a complete mission.
- Do not reproduce native pathfinding, vehicle AI, or complex task graphs in Lua when GTA already has the required behavior.
- Do not let every client independently decide mission progress.
- Do not expose raw GTA pointers or task objects as durable Lua handles.
- Do not make the story runtime a mandatory dependency for resources using native tasks.
- Do not import external game source or assets into this repository; use reverse-engineered behavior as implementation guidance and keep test data separate.

## Change documentation

Commits for this project should describe the prompt or goal, the gameplay or engine motivation, the architectural reasoning, the affected task/opcode contract, and exactly how the change was tested. A green build alone is not sufficient evidence for networking or lifecycle changes.
