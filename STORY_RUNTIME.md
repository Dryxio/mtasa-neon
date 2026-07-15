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

Tags, mission cameras, and mission audio should also gain native Neon services. They do not need to masquerade as ped tasks, but opcode handlers must be able to invoke them through similarly well-defined APIs.

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

- Sweet's initial seating and later vehicle behavior still use temporary warps, but his post-demonstration return to the Greenwood now uses MTA's synchronized native passenger-entry lifecycle. His first leave/walk/spray/return sequence therefore no longer teleports him between those tasks.
- MTA disables GTA's single-player tag manager. The server still approximates tag hit/progress rules in Lua, while Neon now renders the verified per-object Grove material alpha natively.
- The cutscene camera and actor lifecycle can conflict with other resource and streaming behavior.

The prototype should remain as a regression resource. Its ad-hoc actor and tag code should be replaced incrementally by the generic native APIs, making it the first end-to-end consumer of the new architecture.

### Mission instruction trace

The prototype includes an optional client-side instruction feed for development footage and runtime diagnosis. It presents completed, active, and queued operations as a moving timeline and is toggled locally with `/taguptrace` or `F7`. The detailed task trace runs on the leader/syncer client that actually assigns and observes Sweet's native tasks; other party members do not synthesize those observations. The feed is a passive observer: it cannot advance the mission, assign tasks, or grant tag progress.

Every displayed operation states its current implementation domain. Verified native opcodes/tasks, SCM waits and conditions, co-op adaptations, and temporary Lua substitutes are deliberately not conflated. This makes the trace useful as a visible coverage map while the native story runtime grows, but it is not evidence that SCM bytecode is already being interpreted. The reusable `TAGUP_TRACE` component accepts an ordered sequence plus status/progress updates; a future SCM runtime can feed the same presentation layer from unified, sequenced execution events instead of the prototype's mission-specific hooks.

## Current implementation slice: native task and tag primitives

The first engine primitives are client-side APIs for the local player, a client-local ped, or a server ped currently owned by that client's ped syncer. Task mutations require the ped to be living and streamed, return `false` when validation or ownership fails, and intentionally sit one layer below the final server task manager. Mission-actor classification is persistent client element policy and may be assigned before native stream-in. Together these APIs validate GTA constructors, task assignment, movement, cancellation, combat state, actor classification, and ordinary ped synchronization before persistent task IDs and syncer migration are introduced.

The exact current Lua surface is:

```text
setPedGoTo(ped, Vector3 target [, string movement = "walk", float radius = 0.5, float slowdownRadius = 2.0, int timeout = -2])
setPedEnterVehicle(ped [, vehicle, seatOrPassenger])
setPedExitVehicle(ped)
setPedDriveWander(ped, vehicle, float speed [, string|int drivingStyle = "stop_for_cars"])
setPedMissionActor(ped, bool enabled)
isPedMissionActor(ped)
setPedShootAt(ped, Vector3 target [, int duration = 1000, int burstLength = 5])
setPedWeaponShootingRate(ped, int rate)
setPedWeaponAccuracy(ped, int accuracy)
setObjectGangTagAlpha(object, int alpha | false)
```

`setPedGoTo` accepts `walk`, `run`, or `sprint`, requires a positive radius and a slowdown radius at least as large, uses `-2` for the untimed task, normalizes `-1` to 20 seconds, and accepts non-negative finite timeouts. `setPedEnterVehicle` and `setPedExitVehicle` already existed in MTA and deliberately use its vehicle request/confirmation protocol instead of directly assigning client-only tasks; for a synchronized server ped, the server confirms the transition before the syncer constructs GTA's enter/leave task. MTA seat `0` is the driver, so SCM passenger index `0` maps to explicit MTA seat `1`. `setPedMissionActor` accepts only script peds and persists the requested classification across local stream-out/model recreation; it snapshots the previous created-by byte plus perception/scanner state and restores all of it when disabled. `setPedDriveWander` requires the ped to already occupy the target vehicle, the driver seat to be free or occupied by that ped, a finite speed from 0 through 255, and the calling client to own both ped and vehicle synchronization. Its styles are the verified GTA values 0 through 6 and the matching readable names; it is indefinite until cancellation. `setPedShootAt` accepts a burst length from 1 through 32,767 and treats every negative duration as an indefinite task, exactly as the native constructor does; GTA's `(0, 0, z)` coordinate sentinel is rejected. Shooting rate, weapon accuracy, and gang-tag alpha each accept 0 through 255. Passing `false` as the tag alpha clears the opt-in renderer, while `true` is rejected. Go-to, drive-wander, and shoot-at forcibly replace the primary task; vehicle entry and exit participate in MTA's existing synchronized lifecycle. Their OOP aliases are `ped:setGoTo`, `ped:setEnterVehicle`, `ped:setExitVehicle`, `ped:setDriveWander`, `ped:setMissionActor`, `ped:isMissionActor`, `ped:setShootAt`, `ped:setWeaponShootingRate`, and `ped:setWeaponAccuracy`; the tag alias is `object:setGangTagAlpha`.

These calls do not yet provide server-owned task handles, completion events, resource ownership, or reconstruction after syncer migration. `setObjectGangTagAlpha` is a visual-only override for streamed native tag models `1490` and `1524` through `1531`; it does not update `CTagManager` progress, must be reapplied after stream-in or game-object recreation, and must be cleared with `false` before a resource relinquishes an object that will survive it.

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

`setPedMissionActor` reproduces the native setter rather than changing only the byte. Because `SetCharCreatedBy(PED_MISSION)` also resets the decision maker and changes perception/scanner fields—and setting `PED_GAME` later does not restore those values—Neon snapshots the complete affected state before activation and restores it when disabled. The policy lives on `CClientPed`, survives local native model recreation, rejects player elements, and is intentionally client-local; synchronized resources must replicate their desired policy to all potential syncers. Its three new `CPed` virtual methods are appended after the established interface rather than inserted between existing methods, preserving the vtable indices consumed by independently built MTA modules such as `core.dll`. The ASM audit also corrected `gta-reversed-dryxio`'s `CTaskSimpleCarDrive` layout: task offset `+0x14` is an embedded `0xC` `CTaskTimer`, not a pointer plus unrelated fields, and `CPed::m_nCreatedBy` is now locked to `+0x484`. The drive-wander harness requires the native task to remain present for at least 15 seconds before passing.

`05CA TASK_ENTER_CAR_AS_PASSENGER` was checked against the same compact 1.0 US executable before the existing MTA API was adopted. Handler `0x49036C` collects the character, vehicle, timeout, and passenger-index parameters; for a concrete seat it translates the SCM passenger index to GTA's door/seat node and creates the `CTaskComplexEnterCarAsPassengerTimed` wrapper. The direct task constructor at `0x640340`, its base constructor at `0x63A220`, and clone at `0x6437F0` establish a `0x50` direct-task allocation. Neon's interface matches those field offsets and now locks the size with compile-time assertions.

The audit found one candidate-reverse discrepancy: `gta-reversed-dryxio` value-initializes `m_EnterCarStartTime`, while the original constructor does not write offset `0x4C`. Neon calls GTA's original constructor, so it does not copy that mismatch. The opcode's timed wrapper is a separate `0x2C` object: on finite-time expiry it aborts the direct task and may construct `CTaskSimpleCarSetPedInAsPassenger` as a fallback warp. The public MTA call intentionally retains its existing request/confirmation lifecycle and direct native task, not that SCM-specific timeout warp. Mission code therefore applies a 15-second SCM-derived failure guard without claiming byte-for-byte timed-wrapper equivalence.

The isolated `test-resources/native-ped-enter-car-test` harness maps SCM passenger index `0` to MTA seat `1` and requires both client observation of `TASK_COMPLEX_ENTER_CAR_AS_PASSENGER` and server `onVehicleEnter` confirmation. `Tagging Up Turf` uses the same two-sided proof after the demonstration: Sweet walks back and enters the Greenwood while the player is free to spray the two Idlewood tags. Returning to the car can no longer advance the mission through the former Sweet warp.

The first chained mission test also exposed a lifecycle boundary absent from the isolated entry test: cancelling primary `GunControl` did not necessarily remove its `TASK_SIMPLE_USE_GUN` from secondary attack slot `0`. MTA's existing `EnterVehicle` path deliberately refuses while `IsUsingGun()` sees that secondary task. The demonstration cleanup now removes both the primary gun controller and its owned secondary attack task before requesting vehicle entry; refusal diagnostics record both task slots so a future regression is attributable instead of appearing as an unexplained entry failure.

Opcode `05CD TASK_LEAVE_CAR` was gated against the same executable before its existing MTA path was integrated into the mission. Handler `0x490554` allocates `0x34` bytes at `0x490574` and calls `CTaskComplexLeaveCar` constructor `0x63B8C0`; clone `0x63D9E0` repeats the `0x34` allocation, while destructor `0x63B970` accesses the `CTaskUtilityLineUpPedWithCar*` at offset `+0x1C`. The historic MTA interface omitted this owned pointer and described only `0x30` bytes, shifting all later fields. Neon restores the pointer, locks the interface size with a compile-time assertion, and changes the wrapper constructor's default door to `0xFF` so it reaches GTA's raw-door `0` automatic selection instead of silently forcing the front-left node.

This gate also found that the candidate `gta-reversed-dryxio` `ControlSubTask` reconstruction preserves `PAUSE`, while the executable preserves `DIE` (`0xD4`) alongside the verified leave-car subtasks. Neon therefore continues to call GTA's original constructor and vtable rather than copying the reconstructed lifecycle. The isolated `test-resources/native-ped-leave-car-test` harness requires both client observation of `TASK_COMPLEX_LEAVE_CAR` and the matching server `onVehicleExit` before reporting success. `Tagging Up Turf` uses the same two-sided proof before starting Sweet's walk to the demonstration tag from the native task's natural exit transform; the former Lua position/heading alignment was removed because it introduced a visible teleport absent from the SCM task sequence.

The next primitive, `setPedShootAt`, follows opcode `0668 TASK_SHOOT_AT_COORD`. Its gate used the same target executable and verified handler `0x4948EF`, the opaque `0x3C` `CTaskSimpleGunControl` allocation, and constructor `0x61F3F0`. The coordinate path passes a null target entity, a pointer to the target position, a null movement target, `FIREBURST`, burst length `5`, and the SCM duration unchanged. `SWEET1` therefore uses exactly `15000 ms`. Negative durations remain indefinite; there is no `-1` or `-2` normalization inside this task.

This gate found material errors in the candidate reverse: constructor booleans at offsets `0x38` and `0x39` have reversed initial values, offset `0x3A` is not initialized by the original constructor, and the reconstructed `ProcessPed` omits the native `PEDMOVE_STILL` assignment and a line-of-sight state update. Neon consequently treats the layout as opaque and calls GTA's original constructor and lifecycle rather than copying the reconstructed C++.

`GunControl` creates and commands GTA's secondary `UseGun` task, producing native aiming, animation, weapon FX, ammo consumption, and firing behavior. It does not restore tag progress: MTA deliberately removes world tags and patches `CTagManager::IsTag` to return false. Tag state and synchronized spray progress remain a separate native service; the Lua prototype updates a server-authoritative visual material alpha after observing the native firing task, but must not describe that update as `CTagManager` gameplay progress.

The first gameplay trace exposed two omitted adjacent SCM commands rather than an error in `GunControl`: `SWEET1` sets Sweet's weapon accuracy to `90` and shooting rate to `100` before performing the sequence. `SET_CHAR_SHOOT_RATE` opcode `07DD` was verified at handler `0x472759`; it writes the low byte directly to `CPed+0x719`, whose constructor default is `40`. The native gun task uses that byte for burst size and attack delay, explaining the observed one-second pauses at the default value. `SET_CHAR_ACCURACY` opcode `02E2` was verified at handler `0x48067D` and writes the low byte to `CPed+0x71A`. Neon exposes these bytes through `setPedWeaponShootingRate` and `setPedWeaponAccuracy` because hiding persistent combat state inside `setPedShootAt` would make the generic task API misleading.

The tag visual gate verified `CTagManager::SetupAtomic` (`0x49CE10`), its atomic alpha accessors (`0x49CD30`/`0x49CD40`), and `RenderTagForPC` (`0x49CE40`). Each tag model already contains both rival and `grove` materials; model `1524` is another tag site, not a painted replacement for model `1490`. The original renderer writes `floor(alpha * alpha / 255)` to material index `1`, a detail omitted by the candidate reverse's linear assignment. MTA globally forces this result to zero. Neon therefore keeps `IsTag` and the gameplay manager disabled, but `setObjectGangTagAlpha` lets explicitly opted-in MTA objects render a synchronized logical alpha through the verified material formula. The default path remains unchanged.

`SWEET1` does not wait for the full `15000 ms` gun-task ceiling. It waits until sequence progress reaches the shoot step, then waits for the demonstration tag to reach 100%, interrupts the shoot task, and performs the following `WAIT 1000` before the checkout animation and dialogue. The regression resource now mirrors that control-flow distinction: it first observes the native gun task, advances a server-authoritative temporary tag percentage, cancels the task at 100%, and waits one second. The checkout animation, audio, and mission camera are still pending, so this is not yet a timing-perfect cutscene port.

IPL rotations must follow GTA's loader rather than a raw quaternion-to-yaw conversion. GTA conjugates the stored quaternion, so yaw-only placements use `(360 - rawYaw) % 360`. The error was nearly invisible at headings around 0 and 180 degrees but turned the alley and rooftop tags by 180 degrees, leaving their painted faces against the wall and backface-culled.

## Current implementation slice: recorded-car playback

Neon now exposes GTA's direct recorded-car player as a generic, client-side, resource-owned service: `requestVehicleRecording`, `isVehicleRecordingLoaded`, `startVehiclePlayback`, `stopVehiclePlayback`, and `isVehiclePlaybackActive`. Only the direct non-AI, non-looped path used by opcode `05EB` is public in this slice. The game wrapper validates the registered recording table, streamed buffer, duplicate vehicle state, and all 16 native slots before calling GTA; this prevents the original executable's slot-zero fallback and full-pool out-of-bounds write from becoming public API behavior.

The mutating Lua calls identify their owning resource. A recording request is reference-tracked across resources, a vehicle playback has one resource owner, and resource shutdown or native vehicle destruction releases the active slot before its raw GTA vehicle reference becomes invalid. A streamed server vehicle may start only on its current unoccupied-vehicle syncer. If ownership migrates, the old client stops rather than fabricating a resume point, because MTA's packets replicate vehicle transforms but not the recording frame index. Player drivers are refused; an owned script ped driver is permitted after its previous vehicle task is gone, matching the original mission sequence.

The ASM gate used the compact GTA SA 1.0 executable with SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`. It verified request `0x45A020`, loaded test `0x45A060`, direct start `0x45A980`, stop `0x45A280`, active test `0x4594C0`, and the SCM handlers for `07C0`, `07C1`, `05EB`, `05EC`, and `060E`. It also corrected material errors in `gta-reversed-dryxio`: the playback-streaming index has 16 entries rather than three, request does not immediately unload its buffer, frame indices are byte offsets, paused playback freezes time, smoothing starts at frame two, unused-recording removal scans the full table, skip-to-end clears collision-force disabling, and `CTheScripts::CleanUpThisVehicle` had its mission-vehicle predicate inverted.

The isolated `test-resources/native-vehicle-recording-test` harness uses recording `207`: 35 frames from approximately `(2339.67, -1488.19, 23.61)` to `(2381.07, -1528.44, 23.66)`, ending at timestamp `7719 ms` and completing in roughly eight wall-clock seconds. The playback loop's `0.25` multiplier does not make that duration four times longer: `m_snPPPPreviousTimeInMilliseconds` is four timer updates old, so GTA averages four frame deltas before advancing the recording clock. `Tagging Up Turf` requests it during the rooftop stage, stops and verifies Wander, temporarily places Sweet in the driver seat, starts playback only on the vehicle syncer, waits for natural completion, validates the synchronized endpoint server-side, and restores Sweet as passenger. The former post-rooftop Lua vehicle placement is removed. The camera, dialogue, and audio surrounding the original recorded sequence remain separate pending primitives.

Manual in-game validation completed the direct playback in `8040 ms`, reached the recording endpoint with `0.00 m` server-observed error, restored Sweet to passenger seat 1, and completed the remaining drive home through the mission-passed state. The isolated harness separately observed natural completions at `8087 ms` and `8088 ms`; these measurements justify the shared `6500..12000 ms` conformance window without weakening endpoint or ownership checks.

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

- Add a synchronized tag service with native spray progress and tag replacement.
- Add the camera and mission-audio primitives required by `SWEET1`.
- Replace Sweet's teleports and client control-state AI in the prototype with native tasks.
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
