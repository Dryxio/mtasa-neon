# Resource-owned native garage control

Client APIs: `acquireGarageControl(garageID)` and `releaseGarageControl(garageID)`.
Acquisition saves the current native type and uses script-controlled type 1,
retaining the original door and `setGarageOpen` animation while bypassing native
safehouse storage. Only one resource can hold a given garage. Repeated acquisition
by its owner succeeds; invalid IDs, another owner, and unowned release fail.

Release, including automatic resource teardown, sets OPENING before restoring
the saved type. It deliberately leaves the exit opening rather than restoring a
possibly closing state that could run solo storage against native traffic.
The lease owns the native type; it does not arbitrate legacy `setGarageOpen`
commands or server RPCs. The gamemode must still coordinate physical occupancy,
all clients' leases, and server-authoritative vehicle persistence.

## Manual checks

Use an isolated server without another resource controlling garage 5. The user
launches/connects the client. Start this resource and run `/garagelease_test`.
It checks invalid IDs, repeated acquisition, owner release, and repeated release.
It does not test physical storage, collision, visual animation, or persistence.

For ownership and teardown, run `/garagelease_hold`, then from a separate client
resource verify that acquiring or releasing garage 5 returns false. Stop this
resource, verify that garage 5 opens, and verify that the second resource can now
acquire it. Also check release while the door is closing with a native traffic
vehicle inside: releasing must not invoke the native safehouse storage branch.
Do not claim these manual checks passed until actually played.
