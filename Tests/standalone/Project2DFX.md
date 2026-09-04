# Project2DFX standalone verification

Run `utils/test-project2dfx.sh` on the host for ASan/UBSan, or invoke
`utils/test-project2dfx.ps1` in Windows PowerShell in the VM for an x86 MSVC
build with stack/runtime checks. The Windows runner copies only its explicit
inputs, verifies their hashes, and compiles on VM-local storage.

The tests include the same `CDistantLightsSA.h` used by `CCoronasSA.cpp`.
They cover strict DAT parsing, retained placement replay, quaternion transforms,
corona distance/alpha decisions, timed blinking, point-light arguments and
unclamped colors, cone dimensions/fades, renderer state restoration and failed
state capture, and nearest-light budgets. The shipped DAT has 1,050 valid rows
and one malformed row (the merged alpha/position on `radarmast1_lawn`).

The harness uses a native-call spy and a fake render-state store. It does not
execute GTA, exercise entity pools or map removal, or validate GPU pixels.
Rebuild now reloads DAT and replaces derived lights using retained static IPL
placements plus current pools; script-driven removal/movement of those static
placements is not tracked. Searchlights reuse Neon's heli-cone renderer, so
its shading is not a pixel-equivalent port of Project2DFX's custom mesh.

For visual testing, enable Project2DFX at night. With the `project2dfx-test`
resource running, `/project2dfx on 2000`, `/project2dfx off`,
`/project2dfx rebuild`, and `/project2dfxstats` allow comparison and repeated
rebuild checks. Inspect nearby type-2 lights within 22 metres, and searchlights
between 45 and 300 metres. Test interiors, day/night, repeated enable/disable,
streaming, and reconnection separately in the real client.

## Searchlight preference

Both settings interfaces expose Light beams under Project2DFX. The persisted
`distant_lights_searchlights_enabled` preference defaults to true. The master
switch still gates rendering without overwriting the separate beam preference.
The user confirmed the cones and their near-distance fade visually in game.

## Distance and transition checkpoint

Automatic range defaults on and reads GTA's resolved far clip (including
Extended World and server overrides) each pulse. Advanced settings retain a
manual range and expose radius growth, near alpha, distance to full alpha,
boost start and maximum boost. The no-distance curve now uses the model's
reference distance and omits the extra far-range alpha fade, as upstream does.

`engineSetDistantLightsDrawDistance` explicitly switches to manual mode.
`engineSetDistantLightsAutomaticDrawDistance(true)` restores auto mode;
`engineGetDistantLightStats().automaticDrawDistance` reports the mode and
`drawDistance` reports the effective range. `/project2dfx on` defaults to auto;
an explicit numeric range selects manual mode. Profiling restores the prior mode.

Native adapters submit 300 m for model coronas and 550 m for traffic lights
only while Project2DFX is enabled. They do not mutate shared model definitions.
Installation validates the executable instructions before installing any hook.
The Win32 harness executes the same four adapters through test continuations,
checks enabled/disabled ranges, stack balance and unchanged model data.
These tests do not prove pixel parity or validate the in-game menu visually.
