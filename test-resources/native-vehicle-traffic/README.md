# Native vehicle traffic V2

This resource keeps ambient road vehicles network-owned while using GTA only
for its road placement and owner-local driving AI.

Scope: stock civilian road cars, vans, trucks, motorcycles, BMX bicycles and
quads in world 0. Each atomic unit contains a network vehicle, one script-ped
driver and optional script-ped passengers. The common owner runs GTA's native
`DriveWander`; observers receive ordinary ped/vehicle sync and never run the
driving task. Owner epochs, streamed leases, contextual models, GTA lane
placement, smooth handoff, density bubbles, player passenger entry/takeover,
stuck recovery, destruction and cleanup are part of the same lifecycle.

Boats, aircraft, trailers, parked car generators, mission routes, emergency
response and service-specific gameplay remain outside this civilian-road
checkpoint. Model distribution follows GTA popcycle/car-group context through
an explicit road-safe pool because MTA deliberately disables the retail
ambient vehicle streamer.

The exact test executable audited for the road oracle is the VM-local
`gta_sa.exe` with SHA-256
`A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`.
`CCarCtrl::GenerateCarCreationCoors2` at `0x424210` is called only on the
candidate owner and returns scalar position, heading, lane, speed and style
data. Native path addresses and autopilot internals are never serialized.
Vehicle and occupant models are explicitly loaded before proposal. Server-side
creation remains atomic: vehicle plus every occupant, or zero.

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
cartraffic test smooth
cartraffic test ownerquit
cartraffic test lifecycle
cartraffic test density
cartraffic test spatial
cartraffic test passengers
cartraffic test classes
cartraffic test interaction
cartraffic test soak [cycles]
cartraffic cleanup
```

`all` covers the base fixture and a distinct A-to-B handoff. `smooth` performs
A-to-B-to-A transfer with velocity restoration and bounded pose/heading jumps.
`ownerquit` pauses at `owner-quit-ready`; terminate that exact client process to
exercise a real disconnect and require resumed motion on the survivor.
`lifecycle` forces one stuck recovery and destruction. `density` fills four
simultaneous units. `spatial` separates the two player bubbles, merges them and
checks cap 2-to-1-to-2 reconciliation. `passengers` validates atomic seats and
handoff. `classes` covers car, van, truck, motorcycle, BMX and quad.
`interaction` uses GTA's real enter/exit tasks for a player passenger ride and
driver takeover; it does not synthesize lifecycle events or warp the player
into the vehicle.

`soak` deliberately cycles the core fixture/handoff/passenger/lifecycle paths
and reports `PASS-soak-core`. A V2 freeze additionally requires the individual
`classes`, `interaction`, `spatial` and `ownerquit` verdicts. Every terminal
test passes only after exact participants acknowledge task shutdown, restored
mission-actor state, released streaming leases and empty local registries.
Observer samples are monotonic and correlated to unique recent server poses.

The authoritative trace is the server log's `[car-traffic]` JSON stream. Native
placement is probabilistic, so individual `candidate-retry` entries are normal;
only a terminal `PASS-*` or `FAIL` entry is a harness verdict.
