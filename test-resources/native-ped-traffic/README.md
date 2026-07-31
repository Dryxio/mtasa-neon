# Native ped traffic V1

This test resource creates a small shared ambient pedestrian population without
reenabling GTA's unmanaged `CPopulation::AddToPopulation` loop.

Each client keeps GTA's stock zone-ped model residency current. The server then
rate-limits requests for native civilian/path-placement candidates, creates the
real MTA ped, and assigns one persistent syncer. Only that owner runs
`CTaskComplexWanderStandard`; other clients receive the existing compact native
locomotion presentation through normal ped sync.

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

All three functions are client-side primitives. The server must still validate
the proposal, create and own the MTA ped, select exactly one syncer, and clean
up every resource-owned element.

## First two-client run

1. Put both players outdoors in the same neighbourhood and run
   `/pedtraffic debug on`, then `/pedtraffic on`.
2. Let the population reach 12 nearby peds. Both clients should see the same
   models and positions; only `[ped-traffic][client] accepted` owners run AI.
3. Walk the clients to opposite sides of a ped. A handoff should log one
   `revoke`, one `released` transition and a higher assignment epoch, without a
   teleport or duplicate ped.
4. Disconnect the current owner. A nearby second client should receive the next
   epoch; with no nearby fallback, the server should despawn that ped.
5. Kill one traffic ped and verify its corpse is removed after eight seconds
   and replenished within the cap.
6. Run `/pedtraffic status`, then `/pedtraffic off`. The server must report zero
   resource-owned peds and each client must release its eight stock zone-model
   slots. No vehicle may be created at any point.

## Checkpoint evidence

The V1 was built as `Release|Win32` for the affected client projects and
checked in game with two clients in Los Santos and Las Venturas. Two forced
owner departures produced 23 successful epoch changes with zero client task
failures. The observing client retained the same walking presentation and the
second run showed no visible freeze, teleport, disappearance, or walk-to-run
transition. Resource restart and last-player departure also returned the
resource-owned population to zero.
