# Native task cohort headless harness

This harness validates the reusable `native-task-runtime` authority cohort used by story missions. It creates a driver, a passenger and their Voodoo, starts native `escort_left` and drive-by tasks on exactly one client, then hands the whole cohort to the second client.

Start the resource and connect two clients. The run starts automatically and writes a bounded JSONL trace to the resource's runtime directory. `cohorttest` starts another run after cleanup. A terminal server-log line is always emitted:

```text
[native-task-cohort-test] PASS: two active epochs, atomic handoff and passive observation validated
```

The test fails after 45 seconds if task admission, movement, observer replication, authority convergence or handoff does not complete.
