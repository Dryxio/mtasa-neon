# Native ped navigation closed harness

This bounded two-client resource validates the low-level `setPedNavigateTo`
contract without a mission state machine. Run `/nativepathharness` with two
clients connected. The harness moves both players into a temporary isolated
dimension, freezes them, creates one server ped with an explicit syncer, and
restores both players after the terminal verdict.

`PASS` requires all of the following:

- invalid movement, radius, slowdown, height-threshold and timeout descriptors
  are rejected before dispatch;
- the owner accepts and observes `TASK_COMPLEX_FOLLOW_NODE_ROUTE`;
- the server independently observes at least ten metres of synchronized motion
  and a final position within 1.5 metres of the target;
- the observer sees passive synchronized motion but never owns the native task;
- the native task ends within 1.25 metres of the target.

The harness times out after 45 seconds and fails closed on any rejected phase.
Use `/nativepathcleanup` only to abort a stuck run. Visual animation agreement
is intentionally left to `native-ped-navigate-test` and human review.
