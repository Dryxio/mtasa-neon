# 2DFX runway showcase

Gameplay-only cinematic for the resource-owned model 2DFX API in PR #56.

The scene uses the Verdant Meadows runway between:

```text
100.74450, 2501.11401, 16.48438
424.00473, 2503.03442, 16.48438
```

The runway itself stays stock GTA. The only visible objects added for the main light show are real GTA lamp posts (model 1226); centerline, threshold, particle, roadsign, sun-glare and moon carriers use tiny invisible runtime anchors.

## Run

```text
start 2dfx-showcase
/2dfxshow
```

The full sequence lasts about 38 seconds. Setup, runtime-model allocation and targeted particle/roadsign restreams happen while the camera is black. The recorded timeline never calls `engineRestreamWorld()`.

Use `/2dfxshow stop` to abort and restore the player immediately.

## Production commands

- `/2dfxshow` or `/2dfxshow full` — full seven-shot cinematic.
- `/2dfxshow shot 1` — clean sunset runway / native `SUN_GLARE` establishing shot.
- `/2dfxshow shot 2` — night transition and far-to-near ignition of the physical lamp-post rows.
- `/2dfxshow shot 3` — model-level color chase, left/right blinking modes, centerline animation and corona pulses.
- `/2dfxshow shot 4` — close-up mutation and restoration of a cloned GTA lamp's native 2DFX when available.
- `/2dfxshow shot 5` — deliberate multicolor `coronamoon` burst arranged as an arc in the sky.
- `/2dfxshow shot 6` — native `smoke_flare`/`fire` particle gates plus generated native roadsign text.
- `/2dfxshow shot 7` — full-runway finale with lamp-post color waves, threshold lights and centerline strobes.
- `/2dfxshow final` — hold the final composition for screenshots.
- `/2dfxshow setup` — hold the final composition with HUD/chat visible for visual inspection.
- `/2dfxshow stop` — cleanup and restore player/camera/time/weather.

## What the lamp show is doing

Thirty physical model-1226 lamp posts are placed down each side of the runway. Each side is split into six model-level groups, five lamp posts per group. The runtime models inherit the 1226 visual, and their native light 2DFX is used when the clone exposes it. If a runtime slot does not clone the native effect, the showcase creates the equivalent LIGHT at the original 1226 effect position and reuses the original 1226 `coronaName`/`shadowName` instead of hard-coding a sprite.

The two physical rows are oriented from the native 1226 light-head position: the script rotates each lamp so its actual lamp head points toward the runway centre. This avoids hard-coded per-side yaw guesses and keeps the orientation correct if the runway axis changes.

This means one Lua mutation changes five real lamp posts at once. Left and right use separate runtime models, so the show can produce opposite color waves, alternating blink patterns, `warnlight`/`trafficlight` modes, corona-size pulses and range changes while still demonstrating GTA's model-level semantics.

The centerline and threshold lights reuse the native 1226 corona texture, but their LIGHT position is local `{0, 0, 0}` on anchors placed only a few centimetres above the runway. They therefore render at asphalt level instead of inheriting the several-metre-high lamp-head position. `coronamoon` is intentionally isolated to shot 5, where seven invisible anchors form a large multicolor moon arc in the sky; it is no longer used for ordinary runway lights.

The final scene contains roughly:

- 60 visible GTA lamp posts across both runway sides;
- 12 independently controlled lamp-post model groups (six left + six right);
- 24 ground-level centerline light anchors in six independently controlled groups;
- two 9-light ground-level threshold bars;
- native `showMode` blinking (`warnlight`, `trafficlight`, `on_off_at_5`);
- live color, corona-size and point-light-range mutations;
- seven intentional multicolor `coronamoon` sprites for the sky-burst shot;
- four native `smoke_flare` particle anchors and two native `fire` particle anchors;
- two native `ROADSIGN` effects reading `NATIVE_GTA_2DFX / SCRIPTED_IN_LUA` and `NO_SHADERS / NO_CUSTOM_ASSETS`;
- a sunset `SUN_GLARE` cluster at the far end.

## Visual claim

There are no custom DFFs, TXDs, shaders or textures in the showcase. Lamp posts and all texture names come from GTA itself; the spectacle is produced by native GTA 2DFX types controlled from Lua.

## Recording notes

Record 16:9. First run `/2dfxshow setup` and verify that both lamp-post rows sit just outside the asphalt edges, that both sets of lamp heads point inward, and that centerline/threshold coronas sit on the asphalt rather than floating at lamp height.

Then test `/2dfxshow shot 5` separately: the moons should read as one intentional sky arc, not as runway lights. Finally run `/2dfxshow shot 6` to verify particle/roadsign orientation before recording the full show.

The runway axis is computed directly from the two supplied endpoint coordinates, so changing the endpoints later automatically repositions every lamp, edge/center/threshold effect and camera path around the new line.
