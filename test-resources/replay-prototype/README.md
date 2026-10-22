# Player replay prototype

Client-side harness for validating an FPS-independent player replay before moving the recorder/playback path into native code.

## Scope

- Local player, on foot only.
- Keeps up to the last 60 seconds in memory.
- Samples at roughly 60 Hz using timestamps rather than playback frame numbers.
- Records transform, velocity, camera, aim endpoint, weapon state and key control states.
- Replays controls on a local ghost ped.
- Uses 500 ms transform keyframes to measure/correct simulation drift.
- Includes pause, resume, seek and a small debug overlay.

No network upload, persistence, vehicles, surrounding entities or anti-cheat scoring are included in this prototype.

## Test flow

Start the resource, then run:

```text
/replayrec
```

Run around, sprint, jump, crouch, aim and fire for 20-60 seconds while staying on foot.

Stop recording:

```text
/replaystop
```

Play it:

```text
/replayplay
```

Optional controls:

```text
/replaypause
/replayresume
/replayseek 15
/replayplay 15
/replaydebug
/replayclear
```

The ghost is semi-transparent so the recorded path is easy to distinguish from the local player.

## What to watch

The overlay reports the buffered duration, sample count, soft corrections and hard resyncs. A useful first validation is to repeat the same recording while changing the client FPS limit before playback and compare the ghost trajectory and correction counts.

The prototype intentionally keeps recording/playback logic isolated in a test resource. If the control-driven path proves stable enough, the next step is moving frame capture and the circular buffer into the client C++ layer, then exposing only replay handles/commands to Lua.
