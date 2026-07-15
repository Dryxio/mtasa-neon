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

## Current scope

The prototype includes an intro camera, shared co-op objectives, Sweet and his Greenwood, five sprayable tags, a Sweet demonstration, synchronized Ballas combat, mission failure, the return drive, rewards, and restoration of each player's pre-mission state. It uses dimension 4101 so it can run beside other test resources.

Tag hits and mission progress remain server-authoritative Lua because MTA disables GTA:SA's single-player `CTagManager`. The visual state now keeps each site's original tag model and drives its native Grove material through `setObjectGangTagAlpha`; it no longer crossfades two unrelated model IDs. The client reapplies this visual override whenever a tag streams in and clears it with `false` when the resource stops. A later synchronized native tag service can replace the Lua hit/progress rules without changing the material representation.

The original voice lines and SCM bytecode are not yet executed. The server owns progression and validates interactions, while each ped's MTA syncer runs the temporary combat controller. This resource is a regression harness and architecture probe, not the intended mission-by-mission implementation strategy.

Sweet's first Idlewood demonstration now exercises Neon's native `setPedGoTo` and `setPedShootAt` primitives using the exact `SWEET1` profiles. Sweet is aligned at `2095.80, -1649.86, 12.70`, walks to `2100.48, -1649.14, 12.47` with the SCM timeout of 20 seconds, then starts a shoot task whose SCM ceiling remains 15 seconds and whose burst length is five. Before firing, the syncer applies shooting rate `100` through `setPedWeaponShootingRate` and accuracy `90` through `setPedWeaponAccuracy`. The leader is selected as Sweet's syncer and must observe the native shooting task before the server starts authoritative demonstration-tag progress. Reaching 100% interrupts the shoot task, waits the SCM's following 1000 ms, and advances; it no longer waits blindly for the full task duration. The checkout animation, original dialogue, and mission camera that occur before control returns in `SWEET1` are still explicitly out of scope.

The server validates every task token, ped identity, mission stage, reporting client, sync ownership, weapon, distance, and progress tick. Streaming loss, ownership loss, refusal, destruction, premature task termination, and guard timeout terminate the mission with explicit diagnostics instead of leaving the demo stuck. Progress drives the existing tag's synchronized Grove-material alpha up to `255`; this remains a temporary server-owned substitute and does not claim that MTA's disabled `CTagManager` accumulated native gameplay progress.
