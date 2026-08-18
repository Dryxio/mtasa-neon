# 2DFX runway showcase

Gameplay-only cinematic for the resource-owned model 2DFX API in PR #56.

The scene uses the Verdant Meadows runway between:

```text
100.74450, 2501.11401, 16.48438
424.00473, 2503.03442, 16.48438
```

No mall, buildings, escalators or decorative showcase props are created. Runtime models are invisible anchors; the camera should show only the normal GTA runway and native 2DFX output.

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
- `/2dfxshow shot 2` — night transition and far-to-near runway edge ignition.
- `/2dfxshow shot 3` — model-level color chase, centerline animation and flash modes.
- `/2dfxshow shot 4` — close-up mutation and restoration of a real GTA lamp 2DFX.
- `/2dfxshow shot 5` — native `smoke_flare` and `fire` particle gates at the far threshold.
- `/2dfxshow shot 6` — native generated roadsign text on the runway.
- `/2dfxshow shot 7` — full-runway finale with color waves, threshold lights and centerline strobes.
- `/2dfxshow final` — hold the final composition for screenshots.
- `/2dfxshow setup` — hold the final composition with HUD/chat visible for visual inspection.
- `/2dfxshow stop` — cleanup and restore player/camera/time/weather.

## What the light show is doing

The runway is split into six longitudinal segments. Each segment gets three independently allocated runtime models: left edge, right edge and centerline. Multiple invisible objects share each model, so changing one model-level 2DFX instantly changes a whole physical section of runway.

That is used to demonstrate the actual model-level nature of GTA's 2DFX system rather than faking a per-object light animation.

The final scene contains roughly:

- 60 edge-light anchors across both runway sides;
- 24 centerline light anchors;
- two 9-light threshold bars;
- six independently controlled longitudinal light segments per side;
- six independently controlled centerline segments;
- native `showMode` blinking/strobe changes;
- live color, corona-size and point-light-range mutations;
- one real GTA lamp whose original native 2DFX is mutated and restored;
- four native `smoke_flare` particle anchors and two native `fire` particle anchors;
- two native `ROADSIGN` effects reading `NATIVE_GTA_2DFX / SCRIPTED_IN_LUA` and `NO_SHADERS / NO_CUSTOM_ASSETS`;
- a sunset `SUN_GLARE` cluster at the far end.

## Visual claim

There are no custom DFFs, TXDs, shaders or textures in the showcase. The runway remains the stock GTA environment; the visual spectacle is produced by native GTA 2DFX types controlled from Lua.

## Recording notes

Record 16:9. First run `/2dfxshow setup` and make sure both runway edges are correctly aligned with the asphalt and the roadsigns face the intended camera direction. If those are aligned, `/2dfxshow` should be ready to record without additional world restreams.
