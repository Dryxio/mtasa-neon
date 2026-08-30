# Story vehicle relocation test

This caller exercises the cross-mission occupied-vehicle relocation barrier on
the command player's current vehicle. All current seats are declared and every
connected player participates, allowing the preserved vehicle syncer to act as
the remote streaming/ground oracle.

```text
storyrelocatetest <x> <y> <targetZ> [heading] [center]
storyrelocatetestcancel
storyrelocatetestheadless
```

Choose a target roughly 1.6 km away to reproduce the BMX regression this
primitive addresses. The optional literal `center` switches from the default
SCM `scriptZ` conversion to the explicit MTA `centerZ` contract. Every run
also asserts that a simultaneous overlapping handle is rejected. The cancel
command exercises bounded rollback to the original transform and seats.
The headless command is console-safe: it chooses the first connected player,
creates and stages its own BMX, forces a cancellation only after the first
long-distance move has reached `verifying`, proves complete rollback, performs
a second SCM-Z outbound transaction, repeats grounded relocation while the
source BMX is intentionally frozen, and finally performs a center-Z return.
It then restores the player and destroys the BMX before its terminal verdict.
`PASS` is printed only after rollback has restored position, rotation, world,
exact seat map, syncer and physics flags, and all successful legs have completed
`moving>verifying>ready` with three stable client samples. `FAIL` includes the
runtime's observed physical reason. The test does not launch clients or move
anything until explicitly run.
