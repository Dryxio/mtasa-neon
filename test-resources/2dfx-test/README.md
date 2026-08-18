# 2DFX regression harness

Client-side regression/showcase resource for the Neon 2DFX API.

## What it covers

- verifies that custom 2DFX from a previous resource run were removed on resource stop;
- rejects a missing `flags` field without crashing;
- rejects 24-byte particle names (native storage is `char[24]`, so usable C-string length is 23);
- adds light, particle and roadsign effects to model 1337;
- sequences streaming-sensitive custom additions instead of issuing them while the model is still being rebuilt;
- exercises custom property set/get/reset;
- confirms `getModel2DFXCount(model, false)` excludes custom effects;
- modifies/resets a native light on model 1226;
- removes/restores a native effect while waiting for the targeted model restream to settle;
- provides explicit single and repeated global-restream coverage.

The automatic suite intentionally leaves its custom effects alive after completing. Stop/restart the resource: the next start begins by asserting that the previous resource-owned effects were cleaned up.

`engineRestreamWorld()` is deliberately **not** run by the automatic startup suite because it can cause a noticeable global streaming hitch. Global restream coverage is manual.

## Commands

- `/2dfxtest` — recreate showcase objects and run the normal regression suite.
- `/2dfxrestream` — run one explicit `engineRestreamWorld()` and verify custom/native override persistence. A noticeable hitch during this command is expected.
- `/2dfxstress [count]` — run 1–50 explicit `engineRestreamWorld()` cycles and verify the custom count/type remains stable. Repeated hitches are expected while this stress test runs.
- `/2dfxcleanup` — explicitly reset this resource's model changes and destroy the showcase objects.

PASS/FAIL/SKIP results are printed directly in the in-game chat and are also mirrored to `outputDebugString`.

## Manual resource-lifecycle check

1. Start `2dfx-test` and let the automatic suite finish.
2. Confirm the custom light/particle/roadsign remain visible/active.
3. Restart `2dfx-test` without running `/2dfxcleanup`.
4. The first assertion on the new run must report `PASS: resource-stop cleanup from previous run`.
5. Run `/2dfxrestream` separately when you want to exercise the expensive global-restream path.

## Notes

The API is model-level by design, matching GTA SA's RenderWare 2DFX plugin. Resource ownership applies to Neon state management: custom effects are owned by their creator resource, while overrides/removals of original GTA effects are stacked per resource and rolled back when that resource stops.
