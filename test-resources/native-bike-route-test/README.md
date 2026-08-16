# Native BMX route test

This resource isolates the first ride section of GTA:SA `INTRO1` / Sweet & Kendl before the mission resource depends on it. It uses the exact four `drive_to_hub1` `TASK_CAR_DRIVE_TO_COORD` children from the SCM, on a BMX (`481`), with the original speeds `8, 16, 23, 23` and `DRIVINGMODE_AVOIDCARS`.

The checkpoint exists to answer one question before widening the story runtime: does Neon's already-verified native `drive_to` sequence behave correctly when the controlled vehicle is a BMX, including rider presentation and an owner handoff?

## Commands

1. Connect both clients before starting the resource.
2. Run `/nativebikeroute` on client 1. Every connected player is snapshotted, moved into the isolated test dimension, hidden/frozen and given a camera following the rider. Client 1 owns the rider and BMX.
3. Confirm the rider pedals naturally through the cemetery-to-split route. On client 2, compare rider/BMX attachment, steering, lean/movement animation and stop/replacement state.
4. While the route is active, run `/nativebikeroutehandoff` on client 2. The server invalidates the old epoch, waits for the old owner to revoke its native task and leases, moves both syncers, and rebuilds the unfinished route on client 2 from the last accepted logical child.
5. A green `PASS` requires native task acceptance, logical index `3`, the server-observed BMX within 12 metres of the final SCM waypoint, rider still in driver seat, both syncers on the current owner, and at least one presentation sample from a non-owner when a second client is connected.
6. Run `/nativebikeroutecleanup` to restore every participant snapshot.

## Authority contract

Every assignment carries a session id and owner epoch. The server accepts task acceptance, route indices and authoritative samples only from the current owner and rejects stale epochs. The old owner must acknowledge revocation before the next epoch begins. The owner acquires streaming leases for both rider and BMX; observers never construct the native route and only report presentation state.

This is deliberately still a route handle, not a generic multi-actor scheduler. The later Voodoo checkpoint will introduce a coupled authority cohort for driver + passenger + vehicle because `TASK_CAR_MISSION` and `TASK_DRIVE_BY` must migrate together while the pursued player vehicle remains a dependency rather than an owned member.

## SCM evidence

`INTRO1` defines `drive_to_hub1` as:

```text
962.5424  -1128.5996  22.6656   8.0
1036.6025 -1148.9425  22.6562  16.0
1325.5830 -1150.1833  22.6484  23.0
1644.7734 -1051.3339  22.8984  23.0
```

All children use normal drive mode, desired model BMX and avoid-cars driving style. Vanilla also sets the BMX straight-line distance to `30` at creation and `10` after assigning the sequence. That autopilot byte is intentionally tracked as the next native primitive and is not approximated in Lua by this first checkpoint.

## PASS interpretation

A PASS here proves the existing `drive_to` family can simulate the exact first INTRO1 BMX route and survive one explicit two-client handoff. It does not yet prove the full mission, `TASK_CAR_MISSION`, Ballas drive-by coupling, `GET_CLOSEST_CAR_NODE` rubberbanding, physical proofs, split logic, recordings `201/205/206`, or final coop policy.
