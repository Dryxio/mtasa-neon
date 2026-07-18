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

The prototype includes an optional client-side instruction feed for development footage and runtime diagnosis. It presents completed, active, and queued operations as a moving timeline and is toggled locally with `/taguptrace` or `F7`. The detailed task trace runs on the leader/syncer client that actually assigns and observes Sweet's native tasks; other party members do not synthesize those observations. The feed is a passive observer: it cannot advance the mission, assign tasks, or grant tag progress.

Every displayed operation states its current implementation domain. Verified native opcodes/tasks, SCM waits and conditions, co-op adaptations, and temporary Lua substitutes are deliberately not conflated. This makes the trace useful as a visible coverage map while the native story runtime grows, but it is not evidence that SCM bytecode is already being interpreted. The reusable `TAGUP_TRACE` component accepts an ordered sequence plus status/progress updates; a future SCM runtime can feed the same presentation layer from unified, sequenced execution events instead of the prototype's mission-specific hooks.

## Current implementation slice: native task and tag primitives

The first engine primitives are client-side APIs for the local player, a client-local ped, or a server ped currently owned by that client's ped syncer. Task mutations require the ped to be living and streamed, return `false` when validation or ownership fails, and intentionally sit one layer below the final server task manager. Mission-actor classification is persistent client element policy and may be assigned before native stream-in. Together these APIs validate GTA constructors, task assignment, movement, cancellation, combat state, actor classification, and ordinary ped synchronization before persistent task IDs and syncer migration are introduced.

The exact current Lua surface is:

```text
isVehicleOnAllWheels(vehicle)
setPedGoTo(ped, Vector3 target [, string movement = "walk", float radius = 0.5, float slowdownRadius = 2.0, int timeout = -2])
setPedEnterVehicle(ped [, vehicle, seatOrPassenger])
setPedExitVehicle(ped)
setPedDriveWander(ped, vehicle, float speed [, string|int drivingStyle = "stop_for_cars"])
setPedChatWith(ped, partner, bool leadSpeaker [, bool updateDirection = true [, bool conversationEnabled = true]])
setPedStandStill(ped [, int duration = 0])
setPedGoToOffset(ped, target [, int timeout = -1, float radius = 0.5, float angle = 0.0, bool repeat = false])
setPedKillOnFoot(ped, target)
setPedWander(ped [, string movement = "walk", int direction = native random, bool wanderSensibly = true])
setPedScriptedSpeechMuted(ped, bool muted)
setPedMissionActor(ped, bool enabled)
isPedMissionActor(ped)
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

`setPedGoTo` accepts `walk`, `run`, or `sprint`, requires a positive radius and a slowdown radius at least as large, uses `-2` for the untimed task, normalizes `-1` to 20 seconds, and accepts non-negative finite timeouts. `setPedEnterVehicle` and `setPedExitVehicle` already existed in MTA and deliberately use its vehicle request/confirmation protocol instead of directly assigning client-only tasks; for a synchronized server ped, the server confirms the transition before the syncer constructs GTA's enter/leave task. MTA seat `0` is the driver, so SCM passenger index `0` maps to explicit MTA seat `1`. `setPedMissionActor` accepts only script peds and persists the requested classification across local stream-out/model recreation; it snapshots the previous created-by byte plus perception/scanner state and restores all of it when disabled. `setPedDriveWander` requires the ped to already occupy the target vehicle, the driver seat to be free or occupied by that ped, a finite speed from 0 through 255, and the calling client to own both ped and vehicle synchronization. Its styles are the verified GTA values 0 through 6 and the matching readable names; it is indefinite until cancellation. `setPedShootAt` accepts a burst length from 1 through 32,767 and treats every negative duration as an indefinite task, exactly as the native constructor does; GTA's `(0, 0, z)` coordinate sentinel is rejected. Shooting rate, weapon accuracy, gang-tag alpha, and gang-tag progress each accept 0 through 255. Passing `false` as the low-level tag alpha clears only the opt-in renderer, while `true` is rejected. Native script-task calls queue GTA's own script-command event, which later clears competing event-response tasks and installs a clone in the primary slot. Their boolean confirms ownership transfer only, so consumers must observe activation or authoritative world state separately. Vehicle entry and exit participate in MTA's existing synchronized lifecycle. Tag ownership is exclusive per object and calling resource. Its OOP aliases are `object:acquireGangTag`, `object:setGangTagProgress`, `object:getGangTagProgress`, and `object:releaseGangTag`.

The ped-task calls do not yet provide server-owned task handles, completion events, resource ownership, or reconstruction after syncer migration. Gang tags now have those missing local lifecycle guarantees: ownership and progress live on the MTA object above the streamed GTA instance, are reapplied on native recreation, and are revoked on explicit release or resource stop. `setObjectGangTagAlpha` remains a visual-only low-level override; synchronized gameplay should acquire the object and mirror validated native progress through `setObjectGangTagProgress`.

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

`05CA TASK_ENTER_CAR_AS_PASSENGER` was checked against the same compact 1.0 US executable before the existing MTA API was adopted. Handler `0x49036C` collects the character, vehicle, timeout, and passenger-index parameters; for a concrete seat it translates the SCM passenger index to GTA's door/seat node and creates the `CTaskComplexEnterCarAsPassengerTimed` wrapper. The direct task constructor at `0x640340`, its base constructor at `0x63A220`, and clone at `0x6437F0` establish a `0x50` direct-task allocation. Neon's interface matches those field offsets and now locks the size with compile-time assertions.

The audit found one candidate-reverse discrepancy: `gta-reversed-dryxio` value-initializes `m_EnterCarStartTime`, while the original constructor does not write offset `0x4C`. Neon calls GTA's original constructor, so it does not copy that mismatch. The opcode's timed wrapper is a separate `0x2C` object: on finite-time expiry it aborts the direct task and may construct `CTaskSimpleCarSetPedInAsPassenger` as a fallback warp. The public MTA call intentionally retains its existing request/confirmation lifecycle and direct native task, not that SCM-specific timeout warp. Mission code therefore applies a 15-second SCM-derived failure guard without claiming byte-for-byte timed-wrapper equivalence.

The isolated `test-resources/native-ped-enter-car-test` harness maps SCM passenger index `0` to MTA seat `1` and requires both client observation of `TASK_COMPLEX_ENTER_CAR_AS_PASSENGER` and server `onVehicleEnter` confirmation. `Tagging Up Turf` uses the same two-sided proof after the demonstration: Sweet walks back and enters the Greenwood while the player is free to spray the two Idlewood tags. Returning to the car can no longer advance the mission through the former Sweet warp.

The first chained mission test also exposed a lifecycle boundary absent from the isolated entry test: cancelling primary `GunControl` did not necessarily remove its `TASK_SIMPLE_USE_GUN` from secondary attack slot `0`. MTA's existing `EnterVehicle` path deliberately refuses while `IsUsingGun()` sees that secondary task. The demonstration cleanup now removes both the primary gun controller and its owned secondary attack task before requesting vehicle entry; refusal diagnostics record both task slots so a future regression is attributable instead of appearing as an unexplained entry failure.

Opcode `05CD TASK_LEAVE_CAR` was gated against the same executable before its existing MTA path was integrated into the mission. Handler `0x490554` allocates `0x34` bytes at `0x490574` and calls `CTaskComplexLeaveCar` constructor `0x63B8C0`; clone `0x63D9E0` repeats the `0x34` allocation, while destructor `0x63B970` accesses the `CTaskUtilityLineUpPedWithCar*` at offset `+0x1C`. The historic MTA interface omitted this owned pointer and described only `0x30` bytes, shifting all later fields. Neon restores the pointer, locks the interface size with a compile-time assertion, and changes the wrapper constructor's default door to `0xFF` so it reaches GTA's raw-door `0` automatic selection instead of silently forcing the front-left node.

This gate also found that the candidate `gta-reversed-dryxio` `ControlSubTask` reconstruction preserves `PAUSE`, while the executable preserves `DIE` (`0xD4`) alongside the verified leave-car subtasks. Neon therefore continues to call GTA's original constructor and vtable rather than copying the reconstructed lifecycle. The isolated `test-resources/native-ped-leave-car-test` harness requires both client observation of `TASK_COMPLEX_LEAVE_CAR` and the matching server `onVehicleExit` before reporting success. `SWEET1` then uses its black fade to place Sweet at exactly `2095.80, -1649.86, 12.70` with heading `277` before performing the sequence that walks to the tag and shoots it. `Tagging Up Turf` now preserves that order: the two-sided leave proof and actor-staging barrier must both complete before the native walk begins.

The next primitive, `setPedShootAt`, follows opcode `0668 TASK_SHOOT_AT_COORD`. Its gate used the same target executable and verified handler `0x4948EF`, the opaque `0x3C` `CTaskSimpleGunControl` allocation, and constructor `0x61F3F0`. The coordinate path passes a null target entity, a pointer to the target position, a null movement target, `FIREBURST`, burst length `5`, and the SCM duration unchanged. `SWEET1` therefore uses exactly `15000 ms`. Negative durations remain indefinite; there is no `-1` or `-2` normalization inside this task.

This gate found material errors in the candidate reverse: constructor booleans at offsets `0x38` and `0x39` have reversed initial values, offset `0x3A` is not initialized by the original constructor, and the reconstructed `ProcessPed` omits the native `PEDMOVE_STILL` assignment and a line-of-sight state update. Neon consequently treats the layout as opaque and calls GTA's original constructor and lifecycle rather than copying the reconstructed C++.

`GunControl` creates and commands GTA's secondary `UseGun` task, producing native aiming, animation, weapon FX, ammo consumption, and firing behavior. MTA deliberately removes world tags and patches `CTagManager::IsTag` to return false, so restoring the global manager would neither be safe nor discover resource-created objects. Neon instead extends the verified `CShotInfo::Update` spray call for explicitly acquired MTA objects. GTA still owns shot execution, surface response, the 8-alpha increment, the 255 cap, and the completion return that drives frontend audio. Lua receives an event only after this native path changes an owned object. The authoritative consumer accepts consecutive exact 8-alpha reports without an artificial minimum interval because the verified update loop can process several live spray shots in the same frame; dropping one valid step would permanently desynchronize the client's next `previousAlpha` from the server.

The first gameplay trace exposed two omitted adjacent SCM commands rather than an error in `GunControl`: `SWEET1` sets Sweet's weapon accuracy to `90` and shooting rate to `100` before performing the sequence. `SET_CHAR_SHOOT_RATE` opcode `07DD` was verified at handler `0x472759`; it writes the low byte directly to `CPed+0x719`, whose constructor default is `40`. The native gun task uses that byte for burst size and attack delay, explaining the observed one-second pauses at the default value. `SET_CHAR_ACCURACY` opcode `02E2` was verified at handler `0x48067D` and writes the low byte to `CPed+0x71A`. Neon exposes these bytes through `setPedWeaponShootingRate` and `setPedWeaponAccuracy` because hiding persistent combat state inside `setPedShootAt` would make the generic task API misleading.

The tag gate verified `CTagManager::SetupAtomic` (`0x49CE10`), its atomic alpha accessors (`0x49CD30`/`0x49CD40`), `RenderTagForPC` (`0x49CE40`), `CWorld::SprayPaintWorld` (`0x565B70`), and its call from `CShotInfo::Update` at `0x73A0FF`. Each tag model already contains both rival and `grove` materials; model `1524` is another tag site, not a painted replacement for model `1490`. The renderer writes `floor(alpha * alpha / 255)` to material index `1`. The spray path considers at most 15 nearby tags, copies the selected entity's forward vector, increments alpha by 8 with a 255 cap, returns 1 for a hit, and returns 2 for the transition to 255. Neon preserves those semantics for resource-owned objects while leaving MTA's global `IsTag` patch and default world unchanged. The isolated `test-resources/native-gang-tag-test` harness checks exact deltas, completion, synchronization, explicit cleanup, and restart cleanup.

`SWEET1` does not wait for the full `15000 ms` gun-task ceiling. It waits until sequence progress reaches the shoot step, then waits for the demonstration tag to reach 100%, interrupts the shoot task, and performs the following `WAIT 1000` before the checkout animation and dialogue. The regression resource mirrors that control flow, then uses MTA's synchronized animation pipeline for the exact non-looped `GRAFFITI_CHKOUT` parameters and waits for the syncer to observe natural completion. Its co-op preparation barrier waits for the camera lease plus both native samples on every participant before starting the shared timeline. The client preserves the SCM's CA-to-AR request order. The stock script issues those loads immediately before fixed-camera staging; the co-op harness reserves the same handles at the enter-car/drive transition so replacement banks with an unusually slow cold disk path can load during travel, then transfers them unchanged into the scene. It still requires GTA's real loaded state and polls both handles while Neon's mission-audio service recovers hardware requests that GTA silently accepted into a link without queueing. Structurally valid replacement banks whose tracks contain silence therefore use GTA's ordinary loaded/play/finished lifecycle and require no mission-specific timing fallback. The arrival coordinate blip also follows `ADD_BLIP_FOR_COORD`'s fixed cream destination colour rather than inheriting the Greenwood's friendly blue. Sweet receives `TASK_LEAVE_CAR` first and each player receives the same native task 600 ms later; only the SCM-style post-fade staging may forcibly remove a participant who is still seated. The same server-owned timeline gates all three verified camera shots, the native position/target tracks, `SWE1_AR`, `SWE1_CA`, global leader skip, and final camera/audio cleanup. Facial talk remains the known fidelity gap because its secondary task request is not exposed yet.

IPL rotations must follow GTA's loader rather than a raw quaternion-to-yaw conversion. GTA conjugates the stored quaternion, so yaw-only placements use `(360 - rawYaw) % 360`. The error was nearly invisible at headings around 0 and 180 degrees but turned the alley and rooftop tags by 180 degrees, leaving their painted faces against the wall and backface-culled.

## Current implementation slice: native script camera

Neon now exposes GTA's verified fixed/look-at, vector position and target tracks, persistence, fade, widescreen, and scripted near-clip primitives through a client-side, resource-owned camera service. Acquisition returns a generation token, so delayed callbacks from an older run cannot control a later lease owned by the same resource. `isScriptCameraLeaseActive` lets a timeline detect that an authoritative camera takeover revoked that token. The service snapshots the previous MTA camera state and uses an independent, reference-counted gameplay-input inhibitor rather than changing resource-visible control binds. The outermost inhibitor also snapshots and sets only GTA pad bit `bPlayerSafe` (`0x20` in `DisablePlayerControls` at `CPad+0x10E`), then restores that bit on the final release. It never calls the patched-out `CPlayerInfo::MakePlayerSafe` and therefore does not inherit its task, invulnerability, explosion, projectile, or world-cleanup side effects.

The lease is restored on explicit release, resource stop or restart, disconnect, and authoritative server camera takeover. Normal cleanup forces the screen visible; a successful timeline may explicitly preserve the current fade while restoring gameplay camera and controls, enabling the SCM pattern of staging a skip under black before fading back in. Legacy client camera and near-clip setters are rejected while a lease is active, preventing MTA's fixed-camera hook from racing GTA's native vector processors. The isolated `test-resources/native-script-camera-test` harness validates fixed framing, simultaneous eased position and target tracks, fade out/in, explicit abort, and cleanup across resource restart. Manual validation completed the full sequence in `8620 ms`, including a `4267 ms` move/track and one-second native fades, then successfully restarted the resource after native lease cleanup.

The ASM gate also corrected `CCamera::VectorTrackRunning` in `gta-reversed-dryxio`: `0x474870` is the function entry, while the former `0x474891` annotation points to an internal parity branch. The camera service is generic and local to each player; cooperative mission code must synchronize only the timeline, readiness, and skip decision at the server layer.

## Current implementation slice: native file cutscenes

Neon now exposes GTA's stock file-cutscene lifecycle through the same resource-exclusive camera lease. `requestFileCutscene` validates a one-to-seven-character name against GTA's cutscene audio-track table before calling `CCutsceneMgr::LoadCutsceneData`; load, start, native finish, skip-input query, synchronized skip, native skipped state, fade, and deletion remain fixed-address operations behind the append-only `CGame` interface. A file-cutscene token cannot be used by ordinary script-camera setters. Explicit release, resource destruction, disconnect, or an authoritative camera takeover deletes native cutscene data before restoring the captured camera, near clip, widescreen, and input state.

The compact target executable, SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`, maps load to `0x4D5E80`, start to `0x5B1460`, native camera-spline completion to `0x5B0570`, skip input to `0x4D5D10`, skip to `0x5B1700`, deletion to `0x4D5ED0`, and track-name lookup to `0x5AFA50`. Load status is the dword at `0xB5F84C`; running, processing, and skipped are bytes at `0xB5F851`, `0xB5F852`, and `0xB5F854`. GTA normally consumes skip input at call site `0x5B1947`. Neon intercepts that one call while a managed file cutscene is active, letting Lua query the original Enter, Space, mouse-left, gamepad-cross, and focus-loss edge while a server broadcasts the authorized decision to every participant.

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
