# Native task cohort headless harness

This harness validates the reusable `native-task-runtime` authority cohort used by story missions. It creates a driver, a passenger and their Voodoo, starts native `escort_left` and drive-by tasks on exactly one client, rapidly cancels and recreates the cohort on the same elements and owner, then hands the replacement cohort to the second client.

Start the resource and connect two clients. The run starts automatically and writes a bounded JSONL trace to the resource's runtime directory. `cohorttest` starts another run after cleanup. A terminal server-log line is always emitted:

```text
[native-task-cohort-test] PASS: rapid replacement, two active owners, atomic handoff and passive observation validated
```

The test also preconverges one owned syncer and leaves the others automatic. This
forces the first assignment through both the no-op and synchronous
`onElementStartSync` branches, and fails if `dispatched` or `active` is published
before `createNativeTaskCohort` has returned its handle.

Before the handoff, the harness cancels the first cohort, preconverges all three
owned elements to the same non-persistent owner, and immediately creates a new
cohort. Once admitted, it moves the owner more than 200 units away for at least
1.5 seconds and requires two client samples while driver, passenger and vehicle
remain owned by that player. Named `rapid_replace_*` failures identify whether
recreation, distance stress, client sampling or one specific authority lease
regressed. Every JSONL record includes the cohort generation; stress samples
also include the owner, per-element authority and client sample count.

The test fails after 60 seconds if task admission, movement, rapid replacement,
observer replication, authority convergence or handoff does not complete. To
cover event ownership across dependency lifecycle, let one run pass, restart
`native-task-runtime` alone, then run `cohorttest` again without restarting this
harness; the second run must receive the same state events and pass.
