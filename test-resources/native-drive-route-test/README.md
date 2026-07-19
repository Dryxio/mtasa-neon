# Native eight-point drive route test

This resource isolates the eight `TASK_CAR_DRIVE_TO_COORD` children at the
start of SWEET3's Ballas pursuit. It does not run Drive-Thru or approximate the
route with Lua movement.

## Protocol

1. Run `/nativedriveroute` while on foot.
2. The harness creates the SCM Voodoo at `2411.3098, -1928.8369`, puts Ballas2
   in the driver seat, puts the tester in passenger seat 1, and gives the
   tester ownership of both synchronized elements.
3. Confirm `ACCEPT native=true` appears. This proves only that GTA consumed the
   fresh scripted task.
4. Watch the independently reported `INDEX natif` transitions from `0` through
   `7`. The initial `-1` may appear before GTA activates the queued event.
5. Compare the `POS client` and `POS server` lines in the logs. They report raw
   synchronized coordinates, nearest route index, and distance independently
   from sequence progress.
6. At Grove Street, confirm the server reports a final position within 15
   metres only after native index 7 has also been observed.
7. Run `/nativedriveroutecleanup` to destroy the harness and restore the
   tester's original position.

## Exact sequence data

Every child uses mode `normal`, desired vehicle model `412` (Voodoo), driving
style `avoid_cars`, and the eight coordinates and speeds copied from local
`sweet3.sc`. Sequence children intentionally contain no explicit vehicle. This
matches the SCM placeholders `-1, -1`; GTA binds the driver's current vehicle
when each native child activates.

## Native gate

Opcode `05D1` reaches the handler at `0x490649`, allocates `0x3C` bytes, and
calls `CTaskComplexDriveToPoint` constructor `0x63CE00`. Its parameters are the
vehicle, target vector, cruise speed, mode `0..3`, desired model, radius `-1.0`,
and driving style `0..6`. Neon calls this original constructor and dispatches
the original GTA sequence task.

## Validated result

Manual validation on 20 July 2026 observed `ACCEPT native=true`, every native
index from `0` through `7` in order, and natural completion at `77584 ms`. The
final client and server samples both reported
`(2504.281, -1673.919, 13.205)`, `1.81 m` from the eighth target. A collision
with roadside geometry did not reset or interrupt the sequence. Cleanup stops
the client monitor before destroying the synchronized elements, preventing a
late report from sending invalid element handles.
