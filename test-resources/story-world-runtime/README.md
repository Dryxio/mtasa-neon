# Story world runtime

This resource owns reusable adapters and lifecycle barriers for reconstructing
SCM-authored mission worlds in synchronized MTA resources.

- `createStoryScmVehicle` applies `CREATE_CAR`'s model-specific
  base-to-centre Z conversion on the authoritative syncer and requires three
  stable client samples before reporting `ready`.
- `createStoryScmPed` applies `CREATE_CHAR`'s verified `script Z + 1.0`
  conversion.
- `destroyStoryWorldElements` arms every participant before destruction and
  reports `ready` only after every client confirms that the old elements no
  longer exist. File cutscenes can therefore never overlap a stale mission
  world merely because server deletion and client presentation were scheduled
  in adjacent frames. Its optional fade-out completes before clients arm the
  destructive transaction.

Placement and teardown handles are caller-owned and emit
`onStoryScmVehicleStateChange` and `onStoryWorldTeardownStateChange`.
