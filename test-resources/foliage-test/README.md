# Custom foliage test and showcase

This opt-in client resource validates the custom `foliage` element added for
issue #40 and also provides deterministic visual scenes that are easy to record
for a PR/demo video. It uses only the public Lua API exposed by the feature; no
test-only native hooks are required.

Run it in a normal streamed GTA:SA world area. Plant availability is driven by
GTA surface properties, so the resource probes surfaces instead of hardcoding a
single `plants.dat` assumption. A surface that has no plant definitions, or a
native plant pool that is already heavily occupied, can legitimately reject a
triangle.

## Commands

```text
/foliage_test
/foliage_test_all
/foliage_probe [count]
/foliage_stress [count=32] [cycles=5]
/foliage_captest
/foliage_demo [surface]
/foliage_demo_surfaces
/foliage_demo_dimension
/foliage_video [surface|surfaces]
/foliage_cinematic
/foliage_lifetime [surface]
/foliage_overlay
/foliage_clear
/foliage_help
```

### Functional regression

`/foliage_test` checks:

- registration of all seven foliage Lua functions;
- automatic discovery of working native plant surfaces;
- creation and `foliage` element typing;
- surface and density getter round-trips;
- density values `0`, `0.5`, `1`, `2`, and the supported upper bound `10`;
- rejection of negative and greater-than-10 density values;
- generic `setElementPosition` translation/rebuild;
- three-vertex getter/setter rebuild and round-trip;
- valid surface switching plus out-of-range surface rejection;
- element dimension change and restore;
- OOP `density` property and `Foliage.create` registration;
- rejection of invalid create arguments and degenerate triangles;
- creation on several independently probed surfaces;
- explicit destruction and invalidation of the element handle.

`/foliage_test_all` runs that suite and then the cap test. The cap test attempts
65 simultaneous custom foliage elements and expects number 65 to be rejected
when the harness owns all 64 custom slots. If creation stops earlier, the result
is reported as a warning rather than a false failure because vanilla GTA plant
triangles or foliage from another resource may already occupy native capacity.

`/foliage_stress 32 5` is the recommended repeated-lifetime smoke test. It
performs five create/destroy cycles of 32 foliage elements and reports the
actual number created.

### Resource lifetime

`/foliage_lifetime` creates one persistent patch owned by this resource. Stop or
restart `foliage-test` from the server console. The patch must disappear without
calling `/foliage_clear`. The stop handler deliberately does not destroy foliage
itself, so this exercises normal resource `ElementGroup` teardown and the native
foliage destructor path.

## Visual showcase

`/foliage_demo` creates four outlined triangles near the player using the same
working surface at densities `0x`, `0.5x`, `1x`, and `2x`. The zero-density
triangle remains outlined so the empty control is still visible.

`/foliage_demo_surfaces` probes up to four surfaces and renders one `1x` patch
for each, with a floating surface label. This is useful for showing that the API
is not a single hardcoded grass preset.

`/foliage_demo_dimension` moves all current demo elements to a different
dimension and back. The foliage should disappear and reappear while the green
triangle guides remain visible, making dimension streaming easy to verify on
video.

`/foliage_cinematic` toggles a slow orbit camera around the current showcase.
`/foliage_video` is the one-command recording mode: it builds a showcase and
enables the orbit camera. Run `/foliage_video` again to restore the player
camera and clean the demo. Use `/foliage_video surfaces` for the multi-surface
scene, or `/foliage_video <surfaceId>` to force a known working surface for the
density scene.

The HUD is intentionally compact in video mode. `/foliage_overlay` can hide it
entirely for clean B-roll; world-space patch labels and triangle boundaries stay
visible.

## Suggested PR video

A short capture can demonstrate both correctness and the visible result:

1. Start the resource and run `/foliage_test_all`. Hold on the PASS/FAIL HUD long
   enough to show the regression result.
2. Run `/foliage_video`. Record one orbit around the `0x`, `0.5x`, `1x`, and
   `2x` patches.
3. Stop video mode, run `/foliage_video surfaces`, and record the distinct
   surface variants.
4. Stop video mode, run `/foliage_demo`, then `/foliage_demo_dimension` twice to
   capture the foliage disappearing and returning by dimension.
5. Optionally finish with `/foliage_lifetime`, then restart the resource from the
   server console to show automatic cleanup.

For an apples-to-apples density shot, keep the same camera, weather, time of
day, graphics settings, and location. Avoid running unrelated vegetation or
limit-stress resources during the cap test.

## Expected limitations

This harness cannot prove the final rendered plant count from Lua because GTA's
native plant manager owns the generated plants. It validates the public element
state and exercises the native add/release paths; the density, dimension, and
resource-stop behavior should also be checked visually. A cap test that reaches
fewer than 64 custom elements is inconclusive rather than automatically broken
when the shared GTA plant pool is already under pressure.
