# Taken traffic cars

A successful driver's takeover leaves the car in a transient server registry.
It no longer consumes an AI unit, but it still consumes one slot of the shared
physical traffic budget. Both ordinary target calculation and the outgoing
handover reserve subtract retained cars from the configured global cap.
Converting an existing traffic unit into a taken car therefore does not grow
the combined budget. At saturation ambient refill waits; taking a car is not
refused and occupied cars are not removed to recover capacity.

An empty car must remain unattached for 60 seconds and have no player within
90 metres in its dimension/interior before cleanup. Tow links and generic
attachments reset the grace period. A final occupancy/attachment check occurs
immediately before destruction. Failed destruction retains its budget slot.
Entry timeout and quit end the pending entry transaction but do not bypass
these checks. The one-second cleanup pulse samples the player inventory once.

This policy can intentionally retain cars indefinitely while they are occupied,
attached or watched nearby. Those cars keep their slots and suppress ambient
refill instead of allowing an unlimited replacement stream. There is no claim
that 64 is a safe local native pool budget when other resources add vehicles.

## Explicit adoption by another server resource

After the original driver has entered successfully, a trusted server resource
can call:

```lua
local accepted = exports["native-vehicle-traffic"]:adoptTrafficVehicle(vehicle)
```

The export checks `sourceResource`, requires a different running server
resource, rejects client context, and accepts only a currently retained car
whose driver's entry was confirmed. A true result permanently releases this
cleanup registry and its traffic budget slot. Calling code assumes its own
ownership, population and abandonment responsibilities. There is no remote
client event or client element-data override for this decision.

This is a cleanup-policy handoff, not a native resource-lifetime transfer or a
database purchase. The adopting resource must persist or migrate its car before
the creating traffic resource restarts. A resource that stores a car without
calling this contract has not transferred responsibility. No existing RPG
personal-vehicle path adopts ordinary stolen cars automatically.

## Isolated integration and deployment

The controller needs seven small server integration changes: two available-cap
expressions, retain at takeover creation, confirm at successful entry, and
expire at pending-timeout/quit/explicit-clear. `meta.xml` loads
`takeover_lifecycle.lua` before `server.lua` and exports `adoptTrafficVehicle`.
The existing client protocol, telemetry, density, staging and reveal logic do
not change. No C++ build is required.

The RPG deployment is a different server/client baseline from the current
canonical test resource. Apply only the separately reviewed patch generated
against that exact deployed server hash, upload this helper, and add the two
metadata entries to its existing XML. Do not upload the canonical metadata or
client: the current canonical metadata also refers to a transport script that
the RPG baseline does not have. Keep freeroam and BUST unchanged.

Deploy all three runtime artifacts before restarting the private RPG traffic
resource. Loading server hooks without the helper fails; loading the helper
without hooks cannot retain takeovers. Already untracked stolen cars cannot be
safely identified retroactively. A restart applies normal MTA resource-owned
element destruction, including existing resource vehicles; plan it separately
from the occupant-safe runtime cleanup policy.

Rollback restores the exact prior server and metadata pair and removes the
helper after stopping the resource. It restores the original unbounded takeover
lifecycle, so it is an operational fallback rather than a substitute fix.
Adopted cars remain subject to the resource-lifetime caveat above.

## Verification

`lua test-resources/native-vehicle-traffic/tests/takeover_lifecycle_spec.lua`
loads the actual helper. It covers pending entry, confirmed occupancy, unexpected
late passengers, proximity, tow and generic attachment, failed destruction,
server-only adoption, 64 retained cars stopping/recovering refill capacity, and
tick wrap. Syntax and XML checks are also required. These checks do not run GTA,
simulate native carjacking or certify a crowded client's pool capacity.
