# Native ped traffic V1

This test resource creates a small shared ambient pedestrian population without
reenabling GTA's unmanaged `CPopulation::AddToPopulation` loop.

The server first selects one versioned vanilla campaign population state. The
default `post_intro` preset starts from the stock 336-zone main.scm bootstrap,
then applies the first playable free-roam mutation (Grove strength 10 in GAN1
and GAN2). Every client must acknowledge the same revision before it may own AI
or propose a spawn; stale profiles and candidates are rejected.

Each ready client keeps GTA's stock zone-ped model residency current. It also reports
GTA's read-only population targets for the current zone, two-hour time bucket,
weekday/weekend state and current rain adjustment. The server caps its
conservative near-player target by that native target, preserves its
floating civilian/gang targets, and reproduces `FindNewPedType` from the shared
live population over those cap-scaled supported targets before requesting a
native civilian or exact-gang placement
candidate. GTA still chooses the model from its loaded `pedgrp.dat` entries;
the server validates the result, creates the real MTA ped, and assigns one persistent
syncer. Only that owner runs
`CTaskComplexWanderStandard`; other clients receive the existing compact native
locomotion presentation through normal ped sync.

The shared ped-sync path keeps its normal update interval as the baseline. A
material locomotion mode, speed, or direction change opens a bounded 800 ms
spatial burst at 100 ms, with a 400 ms cooldown and a global limit of 16 peds
per pulse. Vehicle evasions and impacts can opt into that same bounded writer
while their task animation is presented; there is no second high-rate network
lane. Scripted animations remain separate and do not consume this budget.

The first behavior checkpoint also enables GTA's stock pedestrian and parked
vehicle avoidance responses. Every client leases the same `ambient-wander`
profile, but only the current syncer may turn a potential encounter into
`CTaskComplexAvoidOtherPedWhileWandering` or `CTaskComplexWalkRoundCar`;
observers keep collision, ground and animation processing while presenting the
owner's synchronized detour.

The second behavior checkpoint admits GTA's stock gun-aimed-at, gunshot and
damage events on the owner. Ordinary bullet sync already recreates shot, whizz,
health and death processing. A client-observed `aim_weapon` transition, checked
against MTA's synchronized target ray by the server, bridges the shooter-local
aimed-at case to the owner as a real `CEventGunAimedAt`. The target ray alone is
not sufficient because MTA updates it continuously even when the aim control is
released.
Likewise, a hit detected by a non-owner is relayed to the owner. The resource
prefers the real `CEventDamage` created there by MTA's synchronized bullet
replay and injects a behavior-only event only when that native event is absent.
GTA then chooses cower, hands-up, duck, turn, flee or fight from the civilian
model's decision maker. Observers mirror the result through the existing
locomotion and animation-presentation lanes without installing local AI or
applying damage a second time.

The moving-vehicle checkpoint keeps that same authority model. GTA's native
`POTENTIAL_GET_RUN_OVER`, `VEHICLE_COLLISION`, `DAMAGE`, and
`GOT_KNOCKED_OVER_BY_CAR` chain runs only on the current ped syncer. It selects
the stock evasive step or dive, hit-by-car, fall/get-up and post-impact response
from the pedestrian's model profile. Observers receive the chosen transient
animation and its bounded spatial samples without running a competing local
collision response. `VEHICLE_HIT_AND_RUN` is deliberately excluded: it belongs
to the future crime/wanted checkpoint, not victim presentation.

The airborne checkpoint extends the shared native-task presentation to GTA's
`JUMP -> IN_AIR_AND_LAND -> LAND` lifecycle, including a blocked jump or a hard
fall/get-up. The owner publishes GTA's selected clip plus a physical airborne
semantic; observers mirror the animation and spatial samples without creating
their own in-air response. During a handoff in flight, the new owner preserves
the synchronized position and velocity and starts GTA's stock
`CTaskComplexInAirAndLand` at the transferred phase. This avoids depending on
the new client's local `EVENT_IN_AIR` geometry check while leaving GTA in
control of `SIMPLE_IN_AIR -> SIMPLE_LAND`. The exact scanner call is fenced on
observers so they cannot create a competing response.

The climb checkpoint completes that physical branch with GTA's stock
`SIMPLE_CLIMB` task. The owner publishes the selected grab, pull-up or vault
clip together with its climb phase, heading, handhold, world-space contact and
the building, object or vehicle used as its anchor. Observers present that
result without running their own local climb scan. During a handoff, the new
owner resolves the same synchronized anchor and resumes the complex jump chain
at the transferred animation phase; if the anchor is no longer valid, it falls
back through GTA's native in-air/land lifecycle rather than leaving the ped
latched to stale geometry.

The initial limits are intentionally conservative: 24 peds globally, 12 near a
player, four per 64 m cell, one candidate request every 250 ms while below the
native target, at least 10 m between traffic peds, and a 20-slot reserve below
MTA's 110-ped logical limit. Peds outside the 120 m shared residency receive a
four-second grace before removal; a stable population-family change retires at
most one furthest surplus ped every two seconds instead of replacing a whole
street at once. Retirement is allowed only when that class is surplus for every
overlapping player, the ped is in a plain active lifecycle, and it is at least
90 m from every player.
Candidates are also rejected within 25 m of another player to avoid a spawn
which was hidden from the proposing client but visible to somebody else.
Ownership changes only after another player stays at least 20 m closer for
three seconds. The old owner kills its native task and releases its streaming
lease before the new epoch is assigned.

## Commands

- `/pedtraffic on` starts population generation.
- `/pedtraffic off` destroys only resource-owned traffic peds.
- `/pedtraffic status` prints counters and the current cap.
- `/pedtraffic debug on|off` enables bounded client/server telemetry.
- `/pedtraffic preset post_intro|post_cleaning_the_hood|post_green_sabre|post_home_coming`
  changes the authoritative campaign population state. Existing traffic is
  cleared and both clients must acknowledge the new revision before refill.
- `/pedtraffic cap 1..110` changes the test cap; keep 24 for the first run.
- `/pedtraffic weapon` gives the caller a pistol for the threat checkpoint.
- `/pedtraffic vehicle [model]` creates one resource-owned player vehicle for
  collision testing (default model 560). It is removed on `off`, quit or stop.
- `/pedtraffic airtest` makes the closest active traffic ped perform a native
  jump; add `handoff` to transfer ownership during the real in-air phase.
- `/pedtraffic climbtest` places a shared road barrier in front of the closest
  active ped and makes it enter GTA's native climb/vault task; add `handoff` to
  transfer ownership while `TASK_SIMPLE_CLIMB` is active. The resource removes
  the temporary barrier on completion, failure, `off` or resource shutdown.

The resource starts disabled. V1 is outdoor-only (`dimension=0`, `interior=0`)
and now admits GTA's civilian and resident-gang population classes. The native
profile reports cops and dealers too, but this checkpoint deliberately does not
spawn those classes yet. Ambient vehicle population, cops, dealers, couples, attractors, conversations,
headless/offline simulation, custom weather rules and public server density
controls remain outside this checkpoint. GTA's stock rain reduction is already
reflected in the native target. The optional command above creates a test
vehicle, not autonomous vehicle traffic. The existing ambient event scopes use
the spawned model's gang identity; persistent gang relations in GTA systems not
covered by those scopes remain a later behavior checkpoint.

## Engine APIs exercised

- `updateAmbientPedPopulationModels(Vector3 origin)` runs GTA's ped-only zone
  model residency pass. Call it while candidate generation is active.
- `getAmbientPedSpawnCandidate(Vector3 origin[, string populationClass,
  int gangId])` returns a read-only table with
  `model`, `pedType`, `populationClass`, `gang`, `x`, `y`, `z`, `direction`,
  and `pathLerp`, or `false` plus a bounded miss reason. It never creates an
  unmanaged GTA ped. Omit the optional arguments for GTA's local automatic
  choice; pass `"civilian", -1` or `"gang", 0..7` when a server has already
  arbitrated the class from synchronized live counts. A forced gang miss never
  falls back to a civilian.
- `getAmbientPedPopulationProfile()` returns GTA's current supported target as
  `supportedTarget`, split into `civilianTarget` and `gangTarget`. `target` also
  includes the effective `copTarget` and reported `dealerTarget`; `rawCopTarget`
  preserves the pre-`noCops` popcycle value for diagnostics. Zone metadata includes
  `zoneType`, `dealerStrength`, `raceFlags`, `noCops`, `timeIndex`, `weekend`,
  and the ten native `gangWeights`. The civilian target already contains GTA's
  stock rain adjustment.
- `resetAmbientPedPopulationZonesToBootstrap()` restores the stock main.scm
  population initialization inside the active reversible lease.
- `setAmbientPedPopulationZoneState(label, state)` applies validated optional
  `populationType`, `races`, `dealerStrength`, `noCops`, and indexed
  `gangStrengths` fields to one INFO zone. The harness uses it only while
  applying a server-issued world revision.
- `resetAmbientPedPopulationModels()` releases the eight stock civilian slots
  and the ped-only gang residency, then restores the zone metadata snapshot.
  Call it when generation stops; the client script also calls it during
  resource shutdown.
- `acquirePedNativeEventProfile(ped, "ambient-wander")` leases the narrow
  avoidance and civilian-threat policy. Acquire it on every client and release
  the token on element or resource teardown; only the current syncer reports
  the lease as active.
- `addPedNativeGunAimedAtEvent(ped, aimingPed, token)` inserts GTA's real aimed-at
  event for the active ambient owner. The profile token fences this cross-owner
  bridge against observers and unrelated resources.
- `addPedNativeDamageResponseEvent(ped, attacker, weapon, bodypart, token)`
  inserts a behavior-only GTA damage event on the active owner. MTA keeps the
  physical hit and health pipeline; the replay only selects the model's stock
  flee or fight response and cannot apply the damage twice.

All population and behavior functions are client-side primitives. The server must still validate
the proposal, create and own the MTA ped, select exactly one syncer, and clean
up every resource-owned element.

## First two-client run

1. Put both players outdoors in the same neighbourhood and run
   `/pedtraffic debug on`, then `/pedtraffic on`.
2. At Grove Street, confirm native Grove-family models can appear; repeat in a
   Ballas neighbourhood and confirm the resident gang family changes. Both
   clients should see the same models, population classes and positions; only
   `[ped-traffic][client] accepted` owners run AI. The native target may keep
   the nearby count below 12 for the current zone/time profile.
3. Stand or walk in front of several traffic peds, then observe peds passing a
   parked vehicle. `avoid-transition` must show the response only with
   `role=owner`; both clients should see the same walking detour without a run
   transition or correction snap.
4. Walk the clients to opposite sides of a ped. A handoff should log one
   `revoke`, one `released` transition and a higher assignment epoch, without a
   teleport or duplicate ped.
5. Disconnect the current owner. A nearby second client should receive the next
   epoch; with no nearby fallback, the server should despawn that ped.
6. Kill one traffic ped and verify its corpse is removed after 30 seconds
   and replenished within the cap.
7. Run `/pedtraffic status`, then `/pedtraffic off`. The server must report zero
   resource-owned peds and each client must release its eight stock zone-model
   slots. No vehicle may be created at any point.

## Population world-state checkpoint

1. Enable debug and traffic at Grove Street. Both clients must log the same
   `population-world-ready` revision before the first candidate request.
2. With `post_intro`, GAN1/GAN2 must report Grove strength 10. Switch to
   `post_cleaning_the_hood`; existing traffic is cleared and the new revision
   must report Grove 40 on both clients.
3. Switch to `post_green_sabre`; GAN1/GAN2 become Ballas 10/25 and Grove 0.
   Switch to `post_home_coming`; both become Grove 40 and Ballas 0.
4. During every switch, no profile or candidate carrying the previous revision
   may be accepted. `/pedtraffic status` and periodic debug telemetry must show
   the same preset and revision.

## Population arbitration and model-diversity checkpoint

1. Put both clients together at Grove Street, run `/pedtraffic debug on`,
   `/pedtraffic preset post_cleaning_the_hood`, then `/pedtraffic on`.
2. Server `spawn` lines must show the floating civilian/gang targets, live
   counts, randomized sub-two deficits, chosen gang score and native model.
   There must be no `population-hint-mismatch` and no old
   `population-class-quota` rejection storm.
3. Let the population refill repeatedly for at least 30–60 gang creations.
   Telemetry `models=` must contain only Grove models 105/106/107 for gang 1;
   all three should appear over the stock two-model residency rotation. A short
   run may legitimately repeat one skin because vanilla loads only two gang
   models at once and shuffles only the loaded entries.
4. Repeat in Ballas territory and confirm models 102/103/104 over time. Repeat
   once in SF or LV to verify the regional `pedgrp.dat` column is used instead
   of the Los Santos column. Both clients must see the same server-created
   elements and models throughout.

## Threat checkpoint

1. Run `/pedtraffic weapon` on each client in turn.
2. Aim at a traffic ped without firing. If the shooter is not the owner, the
   server and owner log one `gun-aim-bridge`; only the owner logs an active
   `threat-transition`, while both clients see the same stock response.
3. Fire beside several peds, then fire a line within two metres of one without
   hitting it. GTA may choose turn, duck, hands-up, cower or flee from the
   model's decision maker; event 49 normally has no separate civilian response,
   so the nearby event 15 is expected to dominate.
4. Aim until a civilian raises its hands, then inflict one non-lethal shot. The
   observer and server must log one `damage-bridge`; the owner normally logs
   `skipped=local-native`, or `accepted=true` when the fallback was necessary.
   It must then log `damage-transition` followed by GTA's model-dependent flee
   or fight response instead of resuming the aimed-at task's slow walk. The
   observer logs the health change but no native threat task of its own.
5. Repeat from the other client and force a handoff during an active response.
   The old owner must return to `state=none`; the observer must never report a
   native threat task, correction snap or duplicate damage.

## Moving-vehicle checkpoint

1. Enable debug traffic, wait for nearby pedestrians, then run
   `/pedtraffic vehicle` on CL1.
2. Approach a ped without touching it at low and medium speed. The owner alone
   should log `vehicle-transition`; both clients should show the same native
   step, dive, gesture or unchanged response selected by GTA.
3. Repeat with a light bump and a harder non-lethal impact. Compare the initial
   force, fall, ground pose, get-up and subsequent flee or attack. Vanilla does
   not guarantee a flee after every impact.
4. Repeat after choosing a ped owned by CL2 so the vehicle driver and ped owner
   differ. This is the required cross-owner case; record whether either client
   reports damage without the owner reporting the matching native response.
5. Test a frontal approach, a side impact and a ped briefly carried onto the
   bonnet or trapped under the vehicle. Then force an ownership handoff during
   a dive or fall/get-up transition.
6. Run the same matrix from CL2, then `/pedtraffic off`. No observer should
   retain a native vehicle-response task, animation or residual velocity.

Current boundaries for this checkpoint are explicit. GTA's rare
`VEHICLE_COLLISION -> JUMP -> IN_AIR_AND_LAND` branch now uses the shared
airborne presentation, while the remote driver's horn-only gesture is not
bridged. Climbing uses the same owner-only physical presentation and transfers
its synchronized world anchor during a handoff. Car-impact fall/get-up is
cleared narrowly on the old owner during a handoff; bullet, explosion and
unrelated physical-response tasks are preserved.

## Airborne lifecycle checkpoint

1. Keep both clients near the same active traffic ped and run
   `/pedtraffic airtest`. Compare launch, glide, landing, position and velocity.
2. Repeat with `/pedtraffic airtest handoff`. The server transfers the ped only
   after its owner reports the real `TASK_SIMPLE_IN_AIR` phase.
3. Repeat from each client so both ownership directions are covered. The old
   owner must stop integrating the jump, the new owner must finish through
   GTA's native in-air/landing chain, and observers must never create a second
   airborne task.
4. Watch for a double launch, upright frame in mid-air, landing teleport,
   residual vertical velocity or Wander restarting before ground contact.

`setPedJump(ped [, allowClimb = true])` is the reusable owner-side primitive
used by this deterministic test. The harness passes `false` so a nearby ledge
cannot turn the baseline into the separate climb lifecycle.

## Climb/vault lifecycle checkpoint

1. Stand on flat outdoor ground with both clients close together, enable debug
   traffic, and wait for an active ped within 30 metres.
2. Run `/pedtraffic climbtest`. The harness moves that ped in front of one
   resource-owned road barrier, then dispatches `setPedJump(ped, true)` on its
   owner. Both clients should see the same launch, grab/vault, pull-up and
   recovery without a duplicate local climb.
3. Repeat with `/pedtraffic climbtest handoff`. The server transfers ownership
   only after the old owner reports the real `TASK_SIMPLE_CLIMB` phase. The new
   owner must remain attached to the same barrier and complete the jump chain
   without a position, heading or animation reset.
4. Repeat in both ownership directions. Logs should contain `launch`, `climb`
   and the subsequent native phase (`in_air`, `land` or completion); a blocked
   attempt is reported as `blocked`, and a test that never reaches climb fails
   explicitly with `climb-not-entered`. Completion follows the stable removal
   of GTA's jump task chain rather than `isPedOnGround`, which remains false on
   some network objects even after a successful pull-up.
5. Run `/pedtraffic off` during a prepared or active climb and confirm that the
   temporary barrier and traffic ped are both removed.

## Checkpoint evidence

The V1 was built as `Release|Win32` for the affected client projects and
checked in game with two clients in Los Santos and Las Venturas. Two forced
owner departures produced 23 successful epoch changes with zero client task
failures. The observing client retained the same walking presentation and the
second run showed no visible freeze, teleport, disappearance, or walk-to-run
transition. Resource restart and last-player departure also returned the
resource-owned population to zero.

The later behavior run checked one cross-owner aim, non-lethal damage,
hands-up interruption, physical reaction, recovery, flee, parked-vehicle
avoidance, and surrounding panic with two clients. The synchronized ped matched
the owner's straight-line sprint speed and the burst budget never deferred a
candidate. Complex collision turns can still diverge transiently by roughly
one to two metres before reconverging because each client reaches the local
collision on a different frame. Both clients then quit cleanly and the server
returned `spawned=191`, `despawned=191`, and `active=0` without a sync or script
error.

The moving-vehicle run confirmed owner-only native evasive steps, dives,
impact falls and get-up presentation across two clients, including bounded
spatial bursts during abrupt collision detours. The final airborne regression
then alternated three forced in-flight handoffs between both clients. All three
completed without a timeout, script error, crash or observer-side native task;
two explicitly recorded the new owner's complete `IN_AIR -> LAND` chain. The
third reached the ground before the next 50 ms diagnostic sample and completed
without a residual task or velocity.

The climb regression then completed both the uninterrupted and forced-handoff
barrier cases on two clients. Observers received the native climb animation
through completion, while the new owner reconstructed `TASK_SIMPLE_CLIMB` on
the same barrier and finished without a timeout, task failure or crash.
