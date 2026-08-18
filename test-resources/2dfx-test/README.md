# 2DFX regression harness

Client-side regression/showcase resource for the Neon 2DFX API.

## What it covers

- verifies that custom 2DFX from a previous resource run were removed on resource stop;
- rejects a missing `flags` field without crashing;
- rejects 24-byte particle names (native storage is `char[24]`, so usable C-string length is 23);
- adds light, particle and roadsign effects to model 1337;
- exercises custom property set/get/reset;
- confirms `getModel2DFXCount(model, false)` excludes custom effects;
- modifies a native light on model 1226, runs `engineRestreamWorld`, and verifies the override survives;
- removes/restores a native effect;
- provides repeated restream stress coverage.

The suite intentionally leaves its custom effects alive after completing. Stop/restart the resource: the next start begins by asserting that the previous resource-owned effects were cleaned up.

## Commands

- `/2dfxtest` — recreate showcase objects and run the regression suite.
- `/2dfxstress [count]` — run 1–50 `engineRestreamWorld` cycles and verify the custom count/type remains stable.
- `/2dfxcleanup` — explicitly reset this resource's model changes and destroy the showcase objects.

## Manual resource-lifecycle check

1. Start `2dfx-test` and let the automatic suite finish.
2. Confirm the custom light/particle/roadsign remain visible/active.
3. Restart `2dfx-test` without running `/2dfxcleanup`.
4. The first assertion on the new run must report `PASS: resource-stop cleanup from previous run`.

## Notes

The API is model-level by design, matching GTA SA's RenderWare 2DFX plugin. Resource ownership applies to Neon state management: custom effects are owned by their creator resource, while overrides/removals of original GTA effects are stacked per resource and rolled back when that resource stops.
