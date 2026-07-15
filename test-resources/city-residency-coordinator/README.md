# Automatic custom-city residency coordinator

This client resource makes Vice City, Liberty City, complete Carcer City, and
Bullworth resident automatically as the local player approaches them. San
Andreas remains native and loaded throughout. The four city resources and this
coordinator must all be started.

Only one imported city owns dynamic DFF/TXD slots on a client at a time. A
local generation-token protocol asks the destination resource to release the
previous city, register its IMG-backed models and placements with the existing
per-frame budget, and synchronously preload geometry and collision around the
player. Radar tiles keep their independent resource lifetime and remain in F11
while a city's 3D content is not resident.

## Policy

The coordinator samples position every 250 ms and derives world-units-per-
second velocity from successive positions. It begins normal preparation inside
a 2000-unit approach envelope or when a 15-second swept trajectory intersects
a city plus 1000 units. Swept intersection, rather than one projected point,
prevents fast aircraft from skipping a narrow envelope between samples.

The exact generated placement bounds are stored in tracked `client.lua`. A
resident city is retained while the player returns to San Andreas. Switching
uses a 750-unit safe-hold boundary and a 15-second cooldown; a direct arrival
within 500 units bypasses both. If the player reaches an inactive city first,
the coordinator fades and freezes the locally controlled ped or vehicle until
registration and `enginePreloadWorldArea` complete, then restores the previous
frozen state. Failures use bounded retry backoff rather than repeatedly
allocating slots every frame.

Automatic decisions are suspended in nonzero interiors or dimensions. A city
that was already resident is retained, so returning to the exterior does not
force a needless reload.

## Commands and diagnostics

- `/cityresidencystats` prints mode, active/preparing/queued city, emergency
  state, position, derived velocity, prediction, and each provider stage.
- `/cityresidency auto` restores normal behavior.
- `/cityresidency off` disables new automatic switches without unloading the
  currently resident city.
- `/cityresidency vc|lc|bw|carcer` applies a persistent debugging override.
- `/cityresidencytest` runs deterministic geometry and prediction checks.

The existing `/vctest`, `/lctest`, `/cctest`, and `/bullytest` commands remain
one-shot prepare-before-teleport debugging paths. Their city resources now
publish state to the coordinator, but their server tokens and vehicle behavior
are unchanged.

Client resources can call the exported `getCityResidencyStats` function for a
structured snapshot.

## Validation matrix

Run `/cityresidencytest` first. In the Windows VM, then cover:

1. Fresh San Andreas connection followed by slow road/boat and maximum-speed
   aircraft approaches to every city.
2. Direct teleports into every city, including death/respawn there.
3. VC -> LC -> Carcer -> Bullworth switching and repeated approach-boundary
   crossings while watching `/cityresidencystats` for allocation thrash.
4. Two clients occupying different imported cities.
5. Coordinator restart and individual city-resource restarts while resident.
6. Reconnect, collision/LOS at each arrival point, and radar stats before and
   after 3D switches.

At every point exactly one provider may report `ready`. No
`engineRequestModel`/`engineRequestTXD` failure should appear, and the emergency
fade must remain owned by the current generation rather than by a stale city
callback.
