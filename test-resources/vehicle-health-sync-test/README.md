# Vehicle health synchronization harness

This isolated two-client harness protects the generic vehicle-health network
path used by every mission and resource. In particular, it detects accidental
saturation at `2047.5`, the largest value represented by the legacy 12-bit,
half-unit `SVehicleHealthSync` field.

Start the resource with two clients connected, then invoke the console-safe
command:

```text
vehiclehealthsynctest
```

The harness selects a deterministic syncer and an independent observer, saves
both players, and stages an occupied Greenwood in an isolated dimension. It
then proves the following sequence:

1. both native clients have streamed the default `1000` health state;
2. a server-set `3100` remains exact on the server, syncer, and observer for at
   least 4.5 seconds, long enough for occupied vehicle puresync to overwrite it
   if the compact-field regression is present;
3. the client syncer changes its local native vehicle from `3100` to `2700` and
   puresync carries the exact `-400` damage delta to the server and observer;
4. the same authority path carries `2700` to `1800`, including the `-900`
   transition through the old `2047.5` boundary;
5. after the driver leaves, that same player remains the explicit remote
   syncer; the server repairs the unoccupied vehicle from `1800` to `2400`,
   both clients ACK the full-float RPC, and four seconds of remote puresync must
   preserve the exact `+600` server-set delta without reclamping it.

No phase passes because a timer elapsed. Every stable sample is an ACK from
both clients followed by a fresh server health, occupancy, world, and syncer
recheck. Minimum dwell times contain multiple such proofs and all phase/global
timeouts are bounded. A failure line always identifies `phase`, `expected`,
`actual`, and `player`.

Each run writes at most 257 records (the 256-record body plus a forced terminal
record) to `@vehicle-health-sync-<run>.jsonl` and mirrors every JSON object to
the server log with prefix `[vehicle-health-sync-jsonl]`. The terminal line is:

```text
[vehicle-health-sync-test] PASS phase=unoccupied_remote_sync_2400 expected=2400 actual=2400 player=server+syncer+observer ...
```

Use `vehiclehealthsynctestabort` to exercise manual abort and restoration; an
intentional abort is a terminal `FAIL` with `cleanup=true`, never a gameplay
pass. Stopping the resource likewise restores both players, destroys the test
vehicle, closes the trace, and emits a bounded terminal `FAIL` identifying
`resource_stop_abort`. The harness does not start clients, restart resources,
or change server configuration by itself.

## VM protocol

Copy only this resource directory into
`C:\dev\mtasa-vm-custom\Bin\server\mods\deathmatch\resources`, start
`vehicle-health-sync-test`, connect two clients, and run
`vehiclehealthsynctest` in the existing server console. Correlate the terminal
line with both client logs and the run JSONL file. Run once with an occupied
vehicle, repeat after reconnecting the observer, then run a third time and
issue `vehiclehealthsynctestabort` during `occupied_server_3100`; every run must
clean up without a server/client error.
