# Entity performance test

This opt-in client resource isolates the cost of dense vehicles, peds, and
objects without requiring a populated server. It creates only local elements,
fixes the camera for repeatability, warms the scenario for five seconds, and
reports average, p95, p99, and worst frame time. It destroys every test element
and restores the camera after each run or resource stop.

Start this resource only after stopping unrelated Neon stress/demo resources,
especially Project2DFX, renderer-limit-test, coronas, Perry Island, and garage
preview. Disable VSync and the FPS limiter, or record their exact state. The
client's known approximately 74 FPS cap is explicitly warned about, but frame
time is the authoritative result.

## Commands

```text
/entitybench [baseline|vehicle|ped|object|mixed] [0|1-2000]
             [static|moving] [visible|hidden|far]
             [separate|contact] [on|off collisions] [5-60 seconds]
/entitybenchmodels [vehicle model] [ped model] [object model]
/entitybenchcancel
/entitybenchclear
```

`hidden` keeps the entities near the camera but points the camera away. This
preserves near-entity simulation while removing most entity rendering. `far`
creates the same total number about 1000 units away, outside the normal entity
streaming area. Comparing `visible`, `hidden`, and `far` distinguishes visible
render cost, near/streamed simulation cost, and total-element bookkeeping.

`separate` uses a six-unit grid. `contact` packs entities tightly enough to
create broad-phase and physical contacts when collisions are enabled. A
`static` element is frozen; `moving` peds animate and moving physical entities
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
