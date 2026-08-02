# Native ped traffic V1

This test resource creates a small shared ambient pedestrian population without
reenabling GTA's unmanaged `CPopulation::AddToPopulation` loop.

Each client keeps GTA's stock zone-ped model residency current. The server then
rate-limits requests for native civilian/path-placement candidates, creates the
real MTA ped, and assigns one persistent syncer. Only that owner runs
`CTaskComplexWanderStandard`; other clients receive the existing compact native
locomotion presentation through normal ped sync.

The shared ped-sync path keeps its normal update interval as the baseline. A
material locomotion mode, speed, or direction change opens a bounded 800 ms
spatial burst at 100 ms, with a 400 ms cooldown and a global limit of 16 peds
per pulse. It uses the ordinary sequenced ped-sync lane and applies to any
eligible synchronized script ped, rather than being traffic-resource logic.
Physical and synchronized animations keep their separate presentation path.

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

The initial limits are intentionally conservative: 24 peds globally, 12 near a
player, four per 64 m cell, one candidate request every 500 ms, at least 10 m
between traffic peds, and a 20-slot reserve below MTA's 110-ped logical limit.
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
- `/pedtraffic cap 1..110` changes the test cap; keep 24 for the first run.
- `/pedtraffic weapon` gives the caller a pistol for the threat checkpoint.

The resource starts disabled. V1 is outdoor-only (`dimension=0`, `interior=0`)
and civilian-only. Vehicles, cops, gangs, dealers, couples, attractors,
conversations and headless/offline simulation are deliberately outside this
checkpoint.

## Engine APIs exercised

- `updateAmbientPedPopulationModels(Vector3 origin)` runs GTA's ped-only zone
  model residency pass. Call it while candidate generation is active.
- `getAmbientPedSpawnCandidate(Vector3 origin)` returns a read-only table with
  `model`, `pedType`, `x`, `y`, `z`, `direction`, and `pathLerp`, or `false`
  plus a bounded miss reason. It never creates an unmanaged GTA ped.
- `resetAmbientPedPopulationModels()` releases the eight stock population
  model slots retained by the update pass. Call it when generation stops; the
  client script also calls it during resource shutdown.
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

All six functions are client-side primitives. The server must still validate
the proposal, create and own the MTA ped, select exactly one syncer, and clean
up every resource-owned element.

## First two-client run

1. Put both players outdoors in the same neighbourhood and run
   `/pedtraffic debug on`, then `/pedtraffic on`.
2. Let the population reach 12 nearby peds. Both clients should see the same
   models and positions; only `[ped-traffic][client] accepted` owners run AI.
3. Stand or walk in front of several traffic peds, then observe peds passing a
   parked vehicle. `avoid-transition` must show the response only with
   `role=owner`; both clients should see the same walking detour without a run
   transition or correction snap.
4. Walk the clients to opposite sides of a ped. A handoff should log one
   `revoke`, one `released` transition and a higher assignment epoch, without a
   teleport or duplicate ped.
5. Disconnect the current owner. A nearby second client should receive the next
   epoch; with no nearby fallback, the server should despawn that ped.
6. Kill one traffic ped and verify its corpse is removed after eight seconds
   and replenished within the cap.
7. Run `/pedtraffic status`, then `/pedtraffic off`. The server must report zero
   resource-owned peds and each client must release its eight stock zone-model
   slots. No vehicle may be created at any point.

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
