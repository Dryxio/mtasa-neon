# Native vehicle traffic V1

This resource keeps ambient road vehicles network-owned while using GTA only
for its road placement and owner-local driving AI.

Scope: stock four-wheel civilian road cars, one script-ped driver, world 0,
`DriveWander`, traffic lights/avoidance, one owner for both elements, observer
sync, owner epochs, lifecycle and cleanup. Motorcycles, boats, aircraft,
parked car generators, taxis/services/police, passengers and exact retail model
distribution are intentionally separate checkpoints.

The exact test executable audited for the road oracle is the VM-local
`gta_sa.exe` with SHA-256
`A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`.
`CCarCtrl::GenerateCarCreationCoors2` at `0x424210` is called only on the
candidate owner and returns scalar position/heading data. Native path addresses
are never serialized. Server-side creation remains atomic: vehicle plus driver,
or zero.

Runtime controls:

```text
cartraffic start [target-per-player] [global-cap]
cartraffic status
cartraffic stop
cartraffic cleanup
```

With exactly two clients in world 0, the checkpoint harness is:

```text
cartraffic test all
cartraffic test lifecycle
cartraffic test density
cartraffic test soak [cycles]
cartraffic cleanup
```

`all` requires a real owner A and observer B, sustained server movement with
the native Wander task on A, then a distinct A-to-B authority handoff.
`lifecycle` forces one stuck recovery and vehicle destruction. `density`
exercises staggered fill, caps and cleanup across several simultaneous pairs.
`soak` repeats the complete ownership cycle and only passes after both clients
acknowledge every cleanup. Observer interpolation is correlated against recent
server history; observers are never expected to run the GTA task.

The authoritative trace is the server log's `[car-traffic]` JSON stream. Native
placement is probabilistic, so individual `candidate-retry` entries are normal;
only a terminal `PASS-*` or `FAIL` entry is a harness verdict.
