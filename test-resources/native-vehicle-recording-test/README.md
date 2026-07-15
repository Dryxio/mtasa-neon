# Native vehicle recording test

This resource isolates GTA:SA's direct recorded-car playback from the Tagging
Up Turf mission. It uses Rockstar recording `207`, whose 35 frames move the
Greenwood from approximately `(2339.67, -1488.19, 23.61)` to
`(2381.07, -1528.44, 23.66)` in about eight seconds.

## Protocol

1. Start `native-vehicle-recording-test` and run `/nativecarrec`.
2. Confirm the Greenwood follows a smooth fixed trajectory while Sweet remains
   in passenger seat 1 and the driver's seat remains empty.
3. Wait for `PASS`; it requires a natural native completion in 6.5-12 seconds,
   a server-observed endpoint within six metres, the same vehicle syncer, and
   Sweet still seated.
4. Run `/nativecarreccleanup` to restore the player's previous position.

The client reports request, load, start, natural completion, streaming loss,
and ownership loss separately. Cleanup stops an active native slot before the
server destroys the vehicle. Gameplay verification remains manual: Codex only
builds and deploys this resource, then asks the user to run the protocol.

## API under test

```text
requestVehicleRecording(recordingId)
isVehicleRecordingLoaded(recordingId)
startVehiclePlayback(vehicle, recordingId)
stopVehiclePlayback(vehicle)
isVehiclePlaybackActive(vehicle)
```

All functions are client-only and return a boolean. Direct playback requires a
streamed, unfrozen, non-blown vehicle owned by the calling client as a local
element or current unoccupied-vehicle syncer. An empty driver seat or a locally
synchronized script-ped driver is accepted; player drivers are rejected. This
isolated harness deliberately uses the empty-seat case, while Tagging Up Turf
uses Sweet as the script-ped driver from the original mission. The game layer
rejects unknown IDs, unloaded buffers, duplicate vehicle playback, and a full
16-slot native pool before calling GTA.

## Assembly gate

The compact US executable with SHA-256
`72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b` was
checked through the auto-re-agent/Ghidra workflow. The relevant native entry
points are request `0x45A020`, loaded test `0x45A060`, direct start `0x45A980`,
stop `0x45A280`, and active test `0x4594C0`. Opcode `05EB` calls the direct
start with both `useCarAI` and `looped` false. GTA owns exactly 16 simultaneous
slots and does not guard a full pool, duplicate vehicles, unknown recording
numbers, or unloaded buffers; Neon adds those checks without copying the
reconstructed playback loop.

Recording timestamps are game milliseconds: recording `207` ends at `7719`.
The playback loop multiplies `currentTime - PPPPreviousTime` by `0.25`, but the
PPP sample is four timer updates old, so that factor averages four frame deltas
rather than slowing playback by four. Timer truncation and Lua polling explain
the approximately 8.1-second wall-clock result observed by the harness.

Manual validation observed natural completion at `8087 ms` and `8088 ms`.
Tagging Up Turf then completed the same recording in `8040 ms` with `0.00 m`
server-observed endpoint error before restoring Sweet as passenger and passing
the mission.
