# Native task runtime

This resource owns long-lived native driving work above MTA's normal syncer
selection. It is deliberately separate from any story mission or test harness.

The server keeps a stable `native-drive-route` handle and an immutable route.
Each client assignment has a monotonically increasing owner epoch. A handoff
revokes the old primary task, releases both resource-owned streaming leases,
optionally proves that both native entities streamed out, and rebuilds the
unfinished route on the new epoch from the last server-accepted logical index.
The server repeats one immutable epoch until the selected client acknowledges
native task acceptance. Duplicate assignments are idempotent and ignored by a
client which already owns that epoch; a missing acknowledgement becomes an
explicit failure after ten seconds instead of leaving the task pending.

The second checkpoint adds a generic `native-task-cohort` handle. A cohort can
atomically own several peds and vehicles, retain read-only target dependencies,
and run `drive_route`, `drive_mission`, `drive_by`, or `none` member tasks. This
keeps one GTA process responsible for the native AI while observers receive the
normal synchronized result. Ped mission/proof policy and vehicle straight-line
distance are captured and restored at revocation.

## Server exports

- `createNativeDriveRoute(ped, vehicle, route, owner, options)` returns a stable
  task element or `false, reason`.
- `handoffNativeDriveRoute(task, newOwner, requireStreamOut)` changes ownership.
- `cancelNativeDriveRoute(task)` revokes and destroys the task handle.
- `getNativeDriveRouteState(task)` returns the authoritative state snapshot.
- `createNativeTaskCohort(owner, descriptor, options)` creates an atomic actor
  and vehicle simulation unit.
- `handoffNativeTaskCohort(cohort, newOwner, requireStreamOut)` revokes every
  old member before publishing the next immutable epoch.
- `cancelNativeTaskCohort(cohort)` restores policy and automatic sync selection.
- `getNativeTaskCohortState(cohort)` returns its authoritative state snapshot.

The creating resource owns the handle. Other resources cannot inspect, hand
off, or cancel it. All its tasks are cleaned when that resource stops. Optional
`fallbackOwners` can continue a route after owner disconnect; without one, the
stable handle becomes `orphaned` instead of silently restarting or teleporting.

`loadCollision` is an explicit vehicle policy. GTA does not expose a matching
getter, so this runtime cannot restore a prior unknown value; callers that set
it remain responsible for the vehicle's later lifecycle.

The resource emits `onNativeDriveRouteStateChange` from the task handle. Its
second argument is a snapshot containing the epoch, owner, logical route index,
stream-out evidence and first post-handoff discontinuity.

It emits `onNativeTaskCohortStateChange` from a cohort handle with the lifecycle
`assigning -> dispatched -> active`, or an explicit `failed`, `orphaned`,
`cancelled`, `revoking`, or `awaiting_streamout` state. Client evidence is
accepted only from the current owner with the exact epoch and nonce.

## Boundary of this checkpoint

This is a native locomotion owner, not a complete headless mission simulator.
Mission objectives, timers, success/failure zones and alternate scenarios must
remain authoritative server state that observes these task handles. A client
disconnect with no fallback leaves a handle `orphaned`, because no GTA process
exists to simulate it. A connected but frozen owner still needs the future
heartbeat checkpoint before automatic timeout reassignment is safe.

UI, dialogue and cutscene presentation remain outside this resource. They must
catch up from authoritative mission state after a handoff rather than drive
mission logic. The runtime synchronizes native simulation ownership and its
result, not arbitrary mission-local presentation state.
