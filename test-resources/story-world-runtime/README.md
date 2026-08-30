# Story world runtime

This resource owns reusable adapters and lifecycle barriers for reconstructing
SCM-authored mission worlds in synchronized MTA resources.

- `createStoryScmVehicle` applies `CREATE_CAR`'s model-specific
  base-to-centre Z conversion on the authoritative syncer and requires three
  stable client samples before reporting `ready`.
- `createStoryScmPed` applies `CREATE_CHAR`'s verified `script Z + 1.0`
  conversion.
- `createStoryPlayerModelLease` temporarily applies one cutscene-safe player
  model to a participant set and snapshots every original model independently.
  Explicit release, player destruction, caller shutdown and a standalone
  runtime restart all restore the surviving players. Mission resources can
  therefore avoid collisions between an arbitrary multiplayer skin and GTA's
  reserved file-cutscene object slots without permanently changing appearance.
- `createStoryVehicleOccupancy` waits for every target vehicle and actor to be
  streamed on the coordinating client, publishes one server warp at a time,
  and requires three stable client samples of every requested seat before it
  emits `ready`. Native task cohorts can therefore be assigned only after GTA
  has instantiated the occupied vehicles, instead of racing replication after
  a cutscene or world rebuild. Each vehicle keeps its existing syncer, so a
  cooperative support vehicle remains owned by its own driver.
  `stageActors=true` additionally moves non-player actors beside their target
  vehicle before the preflight. This adapts SCM sequences that create an actor
  outside the client's streaming range and immediately insert it into a car;
  callers should use it only behind a mission transition or cutscene frame.
- `destroyStoryWorldElements` arms every participant before destruction and
  reports `ready` only after every client confirms that the old elements no
  longer exist. File cutscenes can therefore never overlap a stale mission
  world merely because server deletion and client presentation were scheduled
  in adjacent frames. Its optional fade-out completes before clients arm the
  destructive transaction. Cancellation, caller shutdown, or a standalone
  runtime restart explicitly restores that fade on every participant, so a
  failed transition cannot strand GTA on a black frame.

Placement, player-model, occupancy and teardown handles are caller-owned and
emit `onStoryScmVehicleStateChange`, `onStoryPlayerModelLeaseStateChange`,
`onStoryVehicleOccupancyStateChange` and `onStoryWorldTeardownStateChange`. A standalone runtime stop publishes an
explicit `failed` terminal state to every still-active external caller before
removing its handles; consumers can therefore restore mission controls instead
of waiting forever on an operation which disappeared during restart. It also
emits `onStoryWorldRuntimeStopping` before runtime-owned mission elements are
destroyed, allowing callers with no currently pending handle to fail with an
accurate lifecycle reason rather than a later actor-death predicate.
