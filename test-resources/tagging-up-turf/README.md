# Tagging Up Turf co-op prototype

This resource is the first vertical slice for Neon's planned story runtime. It recreates the gameplay route of GTA: San Andreas' `SWEET1` mission as an authoritative MTA Lua prototype. Its positions and stage order follow the decompiled `main.scm` mission, while its current actor, camera, and tag behaviors are deliberately replaceable experiments.

The target architecture is documented in [`STORY_RUNTIME.md`](../../STORY_RUNTIME.md). The long-term implementation will keep mission/SCM orchestration server-authoritative while moving reusable GTA tasks, tags, camera behavior, and other engine semantics into generic Neon C++ APIs.

## Play

1. Start the resource: `start tagging-up-turf`.
2. Connect one to three players.
3. Run `/tagup`. The player who starts the mission is the driver/leader.
4. Follow the objective banner and map markers. Hold fire with the spray can while aiming at each marked tag.

Development commands:

- `/tagupskip` advances the current stage (leader only).
- `/tagupabort` aborts and restores every participant's previous state.
- `/taguptrace [on|off]` or `F7` controls the local instruction-trace overlay.
- `/taguptracepreview` loads a presentation-only sample sequence for visual QA.

## Instruction trace component

`instruction_feed.lua` provides a generic client-side `TAGUP_TRACE` namespace. It is loaded before `client.lua` and receives presentation-only updates from the mission: toggling it cannot affect stage progression. The overlay is invisible by default, keeps completed instructions dimmed, highlights the current instruction and interpolated progress, and shows future instructions as queued.

The live sequence labels each entry honestly as a verified native opcode/task, SCM flow or condition, co-op adaptation, or temporary Lua substitute. Fast synchronous instructions can move directly into the dimmed history without being held on screen; the trace never delays gameplay just to animate the UI. `/taguptracepreview` remains a purely local visual sample.

The panel reuses the running `chatbox` resource's `fonts/Arial.ttf` through MTA's cross-resource path support. It creates larger dedicated header, instruction, detail, status, and footer fonts and falls back to built-in fonts if `chatbox` is unavailable. Its black, warm-orange, and ivory treatment intentionally follows San Andreas' original mission UI rather than Neon's modern green diagnostic styling.

```lua
TAGUP_TRACE.setSequence(steps, {title = "TAGGING UP TURF", subtitle = "CO-OP TRACE"})
TAGUP_TRACE.setCurrent(stepId [, detail])
TAGUP_TRACE.skipTo(stepId [, detail])
TAGUP_TRACE.setStatus(stepId, "queued" | "active" | "done" | "failed" | "skipped" [, detail])
TAGUP_TRACE.fail(stepId [, detail])
TAGUP_TRACE.setProgress(stepId, progress [, detail]) -- progress accepts 0..1 or 0..100
TAGUP_TRACE.reset()
TAGUP_TRACE.toggle([visible])
```

Sequence entries may be strings or tables containing `id`, `title`, `detail`, `status`, and `progress`. The component only draws local DX primitives and owns no timers, elements, mission tokens, or server events. The detailed native-task feed is intentionally available on the mission leader/syncer, which is the client that actually executes and observes Sweet's GTA tasks; other party members do not fabricate those observations locally.

## Current scope

The prototype includes an intro camera, shared co-op objectives, Sweet and his Greenwood, five sprayable tags, a Sweet demonstration, synchronized Ballas combat, mission failure, the return drive, rewards, and restoration of each player's pre-mission state. It uses dimension 4101 so it can run beside other test resources.

Tag hits and mission progress remain server-authoritative Lua because MTA disables GTA:SA's single-player `CTagManager`. The visual state now keeps each site's original tag model and drives its native Grove material through `setObjectGangTagAlpha`; it no longer crossfades two unrelated model IDs. The client reapplies this visual override whenever a tag streams in and clears it with `false` when the resource stops. A later synchronized native tag service can replace the Lua hit/progress rules without changing the material representation.

The original voice lines and SCM bytecode are not yet executed. The server owns progression and validates interactions, while each ped's MTA syncer runs the temporary combat controller. This resource is a regression harness and architecture probe, not the intended mission-by-mission implementation strategy.

Sweet's first Idlewood demonstration now exercises Neon's native `setPedExitVehicle`, `setPedGoTo`, `setPedShootAt`, and `setPedEnterVehicle` paths using the `SWEET1` profiles. The vehicle exit is no longer a server-side removal: the leader/syncer requests MTA's authoritative exit lifecycle, observes `TASK_COMPLEX_LEAVE_CAR`, and the server independently confirms `onVehicleExit` before it advances. Sweet keeps the native task's natural exit position and heading, then walks directly to `2100.48, -1649.14, 12.47` with the SCM timeout of 20 seconds; no Lua alignment teleport occurs between the two tasks. He then starts a shoot task whose SCM ceiling remains 15 seconds and whose burst length is five. Before firing, the syncer applies shooting rate `100` through `setPedWeaponShootingRate` and accuracy `90` through `setPedWeaponAccuracy`. The leader must observe the native shooting task before the server starts authoritative demonstration-tag progress. Reaching 100% interrupts the shoot task, removes the owned secondary `UseGun` task, and waits the SCM's following 1000 ms. Sweet then walks back to the Greenwood and enters MTA passenger seat `1` (SCM passenger index `0`) while the player sprays Idlewood; the syncer must observe `TASK_COMPLEX_ENTER_CAR_AS_PASSENGER` and the server must independently receive `onVehicleEnter`. Returning to the car no longer warps Sweet. The checkout animation, original dialogue, and mission camera that occur before control returns in `SWEET1` are still explicitly out of scope.

At the Ballas destination, the old `removePedFromVehicle` plus Sweet position teleport has also been removed. Every co-op participant requests the synchronized native leave-car lifecycle while the SCM camera position is temporarily reproduced in Lua. Sweet carries a synchronized mission-actor policy which every client applies through `setPedMissionActor`; this reproduces SCM `CREATE_CHAR` and prevents GTA's four-second ambient passenger-abandonment sequence. Only after the server confirms that all players are outside does it persistently assign the leader as syncer for both Sweet and the Greenwood. The leader then calls `setPedDriveWander(sweet, greenwood, 20.0, "avoid_cars")`, observes `TASK_COMPLEX_CAR_DRIVE_WANDER`, and waits the SCM's following 1000 ms before the Ballas tag stage begins. The verified installed `main.scm` contains no active seat-shuffle opcode here: Sweet intentionally remains passenger while GTA's autopilot drives the empty-driver vehicle.

During the rooftop stage the leader preloads Rockstar recording `207`. Once the last tag is complete, the client cancels and verifies the end of Wander, the server moves Sweet from passenger to driver without moving the Greenwood, and only the vehicle's current syncer calls `startVehiclePlayback`. The first native frame performs the repositioning, so the former server-side position, rotation, and velocity writes are gone. GTA plays the 35-frame route for roughly eight seconds while ordinary unoccupied-vehicle sync replicates its transform. The client observes the native active state through natural completion, the server validates the recorded endpoint, and Sweet is restored to passenger seat 1 before the return drive. The original camera, dialogue, and audio around this sequence remain pending and are not presented as implemented.

The complete mission path has been validated in game: recording `207` ended naturally after `8040 ms`, the server measured `0.00 m` endpoint error, Sweet returned to passenger seat 1, and the subsequent Grove Street drive reached the mission-passed state.

The server validates every task token, ped and vehicle identity, mission stage, reporting client, sync ownership, server-side occupant transition, weapon, distance, and progress tick. Streaming loss, ownership loss, refusal, destruction, premature task termination, and guard timeout terminate the mission with explicit diagnostics instead of leaving the demo stuck. Progress drives the existing tag's synchronized Grove-material alpha up to `255`; this remains a temporary server-owned substitute and does not claim that MTA's disabled `CTagManager` accumulated native gameplay progress.
