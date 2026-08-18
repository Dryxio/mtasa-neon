# 2DFX cinematic showcase

Gameplay-only showcase for the resource-owned model 2DFX API in PR #56.

## Run

```text
start 2dfx-showcase
/2dfxshow
```

The full cinematic is about 42 seconds. HUD/chat are hidden while recording and the player is staged in an isolated dimension over Verdant Meadows. Setup/restream work is performed while the camera is faded to black.

Use `/2dfxshow stop` to abort and restore the player immediately.

## Production commands

- `/2dfxshow` or `/2dfxshow full` — full seven-shot cinematic.
- `/2dfxshow shot 1` — sunset / native SUN_GLARE.
- `/2dfxshow shot 2` — model-level neon light wave.
- `/2dfxshow shot 3` — mutate and restore a vanilla GTA light.
- `/2dfxshow shot 4` — native particle 2DFX on rooftop-style anchors.
- `/2dfxshow shot 5` — generated native roadsign text.
- `/2dfxshow shot 6` — two native escalators moving in opposite directions.
- `/2dfxshow shot 7` — final wide shot with everything active.
- `/2dfxshow final` — hold the final composition indefinitely for screenshots.
- `/2dfxshow setup` — hold the final scene with chat/HUD visible for visual inspection.
- `/2dfxshow stop` — cleanup and restore player/camera/time/weather.

## Visual claims

The scene deliberately uses no custom DFF, TXD, shader or texture. The visible feature work is GTA's native 2DFX system driven from Lua:

- six independently allocated lamp models receive custom `LIGHT` effects and animate as a boulevard wave;
- a real GTA lamp 2DFX is recolored/enlarged and then restored;
- `PARTICLE` uses native `fire` and `smoke_flare` systems;
- `ROADSIGN` generates the showcase title/capability text from native roadsign glyphs;
- paired `ESCALATOR` effects create and animate GTA's actual escalator step objects;
- multiple `SUN_GLARE` directions are staged at sunset so the camera can catch the native glare response;
- resource teardown resets all model mutations and frees every runtime model slot.

## Recording notes

Record 16:9. Start recording immediately before `/2dfxshow`; the initial fade hides model allocation and targeted restreams. No `engineRestreamWorld()` is used by the showcase.
