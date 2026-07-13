# Entity performance test

This opt-in client resource isolates the cost of dense vehicles, peds, and
objects without requiring a populated server. It creates only local elements,
fixes the camera for repeatability, warms the scenario for five seconds, and
reports average, p95, p99, and worst frame time. It destroys every test element
and restores the camera after each run or resource stop.

Start this resource only after stopping unrelated Neon stress/demo resources,
especially Project2DFX, renderer-limit-test, coronas, Perry Island, and garage
preview. Disable VSync and the FPS limiter, or record their exact state. Frame
time is the authoritative result; a server-enforced cap can censor differences
between scenarios even when the resource itself is unchanged.

## Commands

```text
/entitybench [baseline|vehicle|ped|object|mixed] [0|1-2000]
             [static|idle|moving] [visible|hidden|far]
             [separate|touching|contact] [on|off collisions] [5-60 seconds]
/entitybenchmodels [vehicle model] [ped model] [object model]
/entitybenchmodelset [homogeneous|varied]
/entitybenchprofile [5-60 seconds per stage]
/entitybenchvariedprofile [5-60 seconds per stage]
/entitybenchattributionprofile [5-60 seconds per stage]
/entitybenchcollisionprofile [5-60 seconds per stage]
/entitybenchcollisionvariedprofile [5-60 seconds per stage]
/entitybenchresetorigin
/entitybenchcancel
/entitybenchclear
```

The first run locks the current player position and heading as the test origin.
Every later run in the resource session reuses that origin, so visible/hidden
baselines and entity scenarios render the same background even if gameplay
moves the player. Use `/entitybenchresetorigin` only when intentionally moving
the complete benchmark to a new location.

`/entitybenchprofile` runs the complete baseline, vehicle, ped, object, and
mixed matrix sequentially. It cleans up and restores the camera between stages,
then advances only after the preceding measurement really finishes; very slow
collision frames therefore cannot make stages overlap.

By default, every entity of a given type uses one model. Use
`/entitybenchmodelset varied` to cycle deterministic built-in GTA model lists:
20 vehicles, 16 peds, and 18 objects. The resource loads and holds references
to the complete set before measuring so one-off model I/O is not mistaken for
steady-state frame cost. `/entitybenchvariedprofile` selects that set and runs
the same 33-stage matrix as `/entitybenchprofile`; this makes homogeneous and
varied results directly comparable. Switching back to `homogeneous` releases
the held references.

`/entitybenchattributionprofile` runs only eight varied-model stages covering
the visible/hidden/far vehicle and ped controls plus the realistic touching
vehicle case. Use it with the local `timingdebug on` console command after a
client build containing the native timing hooks; it is the short attribution
matrix and does not replace the 33-stage regression profile.

`/entitybenchcollisionprofile` and `/entitybenchcollisionvariedprofile` run a
six-stage collision attribution matrix with homogeneous and varied models,
respectively. They include separated and touching populations, deep contact,
the collision-off control, and near moving peds. Run both with `timingdebug on`
after a build containing the internal collision counters; keep their model sets
separate so streaming and warm-up state do not contaminate the comparison.

`hidden` keeps the entities near the camera but points the camera away. This
preserves near-entity simulation while removing most entity rendering. `far`
creates the same total number about 1000 units away, outside the normal entity
streaming area. Comparing `visible`, `hidden`, and `far` distinguishes visible
render cost, near/streamed simulation cost, and total-element bookkeeping.

`separate` uses a six-unit grid. `touching` uses a dense vehicle-sized grid to
approximate adjacent traffic without spawning every entity at the same point.
`contact` is the deliberately pathological deep-overlap stress case used to
expose collision retries. A `static` element is frozen, `idle` is unfrozen with
no initial velocity, and `moving` peds animate while moving physical entities
receive linear and angular velocity. The scenario reports the actual created
count because GTA/MTA pool and streamer budgets can prevent all requested
elements from becoming native entities.

The resource is intentionally client-local. It measures engine, renderer, MTA
streamer, and wrapper costs without network traffic or server scripts. It does
not represent remote-player packet decoding or real interpolation jitter; those
require recorded traffic or multiple real clients and must be measured as a
separate layer.

## Minimum comparison matrix

Use 15-second samples, repeat every row three times, and keep camera, resolution,
graphics settings, weather, and time of day unchanged:

```text
/entitybench baseline 0 static visible separate off 15
/entitybench vehicle 16 static visible separate on 15
/entitybench vehicle 32 static visible separate on 15
/entitybench vehicle 48 static visible separate on 15
/entitybench vehicle 64 static visible separate on 15
/entitybench vehicle 64 static hidden separate on 15
/entitybench vehicle 64 moving visible separate on 15
/entitybench vehicle 64 moving visible contact on 15
/entitybench vehicle 64 moving visible contact off 15

/entitybench ped 32 static visible separate on 15
/entitybench ped 64 static visible separate on 15
/entitybench ped 96 moving visible separate on 15
/entitybench ped 110 moving hidden separate on 15

/entitybench object 128 static visible separate on 15
/entitybench object 512 static visible separate on 15
/entitybench object 900 moving visible separate on 15
/entitybench object 1000 static hidden separate on 15
/entitybench object 1000 static far separate on 15

/entitybench mixed 96 static visible separate on 15
/entitybench mixed 192 moving visible separate on 15
```

Run the same matrix with shadows/effects at their lowest and normal settings.
For custom models, load the replacement in a separate controlled resource, use
`/entitybenchmodels` to select the replaced IDs, and compare against the same
IDs before replacement. Shader and attachment tests need a fixed asset and
shader workload; do not mix arbitrary production resources into the baseline.

## Native timing log

In Settings > Advanced > Diagnostics, select `#0000 Log timing` before a stress
run. Slow frames are written to `timings.log`. Neon adds aggregate scopes for
MTA vehicle, ped, player, and object manager pulses and for every streamer.
Existing scopes report `CWorld_Process`, `CGame_Process`, `NetPulse`, and the
whole client pulse. These scopes are deliberately outside per-element loops so
instrumentation overhead does not scale with entity count.

The timing logger records anomalous slow frames, not distributions. Use the
resource output for average/p95/p99/worst distributions and `timings.log` to
attribute the worst frames. GPU saturation is inferred only when visible cost
grows while `CWorld_Process` and MTA scopes do not; confirm it with an external
GPU capture before treating that inference as proof.

Neon also provides the local F8 command `timingdebug [on|off]`. While enabled,
the logger writes one aggregate snapshot per second in addition to anomalous
frames, including sections down to 0.5 ms. The per-entity native checkpoints
add diagnostic overhead, so compare subsystem shares and call counts from this
mode; use a run with timing disabled for authoritative FPS distributions. Keep
the client in the foreground so a long Present gap is not aggregated as one
artificial multi-second frame.

Builds with deep collision instrumentation also write one `Entity collision`
detail line per category and snapshot. It includes unique entities, retries,
unsafe counts after attempts one through six, sector and shift work,
broad-phase candidates, `ProcessEntityCollision`, returned contacts,
`ProcessColModels`, and exact repeated query counts. These fields are
diagnostic counters, not a stable public log format.

## Archived results

- `results/2026-07-12-vm-profile.md`: homogeneous 33-stage baseline;
- `results/2026-07-12-vm-varied-profile.md`: deterministic varied-model profile;
- `results/2026-07-12-vm-native-attribution.md`: entity-level native scopes;
- `results/2026-07-12-vm-deep-collision-attribution.md`: internal collision
  counters, paired homogeneous/varied collision profiles, and the rejected
  AABB-prefilter experiment.

The AABB experiment recorded zero useful rejections, failed its go/no-go gate,
and was rolled back. It is preserved only as an archived negative result; no
AABB prefilter is active in the current instrumented collision path.
