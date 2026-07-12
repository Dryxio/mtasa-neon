# Dense entity performance investigation

This document records the evidence and reproducible measurement plan for the
historical frame-rate collapse around dense MTA players, peds, vehicles, and
objects. It deliberately does not raise any pool or streamer limit. Capacity
determines how many entities may reach the engine; it does not explain their
per-frame cost.

## Evidence boundary

The readable control flow below comes from the local `gta-reversed-dryxio`
tree. The relevant entry points were checked against the local GTA SA 1.0 US
executable (`gta_sa_compact1.0.exe`, SHA-256
`72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`).
Disassembly at `0x5684A0`, `0x553910`, `0x54DFB0`, and `0x5E65A0`
respectively confirms the native `CWorld::Process`, `CRenderer::PreRender`,
`CPhysical::ProcessCollision`, and `CPed::PreRenderAfterTest` entry points and
their expected loops/calls. GTA-reversed remains a readable reference; MTA
executes this binary rather than recompiling the reversed sources.

## Controlled profile result

An unattended 33-stage profile ran in the Windows VM with a server and VSync
limit of 120 FPS, unrelated Neon stress resources stopped, a fixed test origin,
standard models, a five-second warm-up, and one ten-second sample per stage.
The exact result table is archived in
`test-resources/entity-performance-test/results/2026-07-12-vm-profile.md`.
These are client-local entities: they isolate MTA/GTA engine and render costs but
do not contain real remote-player packet traffic or interpolation jitter.

The measured hotspot order is:

1. **Unresolved vehicle contacts and collision retries.** Sixteen deeply
   overlapping moving vehicles measured 157.23 ms average, 176.50 ms p95, and
   184.04 ms p99. The identical layout with collisions disabled measured
   11.90 ms average. The controlled delta is therefore approximately
   145 ms/frame of collision-related work. Four and eight deeply overlapping
   vehicles stayed near 21 ms, indicating a state-dependent threshold rather
   than a smooth per-vehicle slope.
2. **Dense realistic vehicle contacts plus visibility.** The `touching` grid
   measured 13.61, 26.22, and 38.89 ms average for 16, 32, and 64 moving
   vehicles. P95 grew to 50.80 ms at 64. Visible-pointer high-water increased
   from 56 to 72 to 92, so both actual visible density and physical contacts
   increased in this series.
3. **Near, streamed peds.** Idle visible peds measured 13.23, 16.81, 20.28,
   and 21.68 ms at 32, 64, 96, and 110. At 110 moving peds, visible and hidden
   results were 22.78 and 21.98 ms, while the far/streamed-out result was
   12.22 ms. Only about 0.8 ms separated visible from hidden, whereas moving
   the population outside normal streaming range removed about 9.8 ms. The
   dominant ped cost in this scenario is therefore near streamed simulation,
   animation/task/collision work, and MTA pulses rather than drawing/skinning
   visible peds alone.
4. **Mixed populations.** A 96-entity idle mixture measured 20.26 ms; 192 idle
   and moving mixtures measured 26.66 and 31.24 ms. This confirms that costs
   compose across managers and native entity categories before any pool raise.
5. **Visible separated vehicles.** Sixteen and 32 idle visible vehicles
   measured 11.95 and 14.58 ms against a 9.57 ms visible baseline. Results at
   48 and 64 were not monotonic because both reached the same visible-pointer
   high-water of 88; the six-unit grid expanded outside the frustum. The
   profile therefore proves visible cost, but not a 48-to-64 density slope.
6. **Simple standard objects.** From 128 through 1000 created objects, averages
   stayed near 9.8-10.0 ms against a 9.57 ms baseline, and 900 moving objects
   measured 10.26 ms. The visible list stayed near 88-89 while streaming
   RwObject high-water reached 966. This shows that total/streamed population
   alone is cheap for this simple model; it does not test 1000 objects inside
   one frustum, custom geometry, shaders, or attachments.

No new crash dump was produced and the client stayed responsive after all 33
stages. Because this is one full pass rather than three repeats, small deltas
remain provisional. Collision deltas and the visible/hidden/far separations are
large enough to guide the next instrumentation and optimization work.

### Varied built-in model profile

The same matrix was also run with deterministic cycles of 20 GTA vehicles,
16 peds, and 18 objects, all requested before measurement. The raw tables are in
`test-resources/entity-performance-test/results/2026-07-12-vm-varied-profile.md`.
It establishes that a one-model workload is not a sufficient production proxy:
at the original profile location, 64 separated idle varied vehicles measured
23.60 ms average versus 18.60 ms homogeneous, and 64 touching moving vehicles
measured 52.10 versus 38.89 ms. Moving 110 varied peds measured 24.91 versus
22.78 ms, while the moving 192-entity mixture measured 35.58 versus 31.24 ms.
The visible baseline was effectively unchanged at 9.49 versus 9.57 ms.

Deep contact behaved in the opposite direction: 16 varied vehicles measured
17.70 ms versus 157.23 ms for identical models. The model-411 stack remained in
a pathological unresolved collision state, while varied collision shapes could
separate during warm-up. Both workloads must therefore remain in the suite:
normal varied density represents production better, while identical deep
overlap remains a targeted collision-retry stress case.

## Confirmed per-frame paths

### MTA streamer and wrapper work

Every `CClientStreamer::DoPulse` recalculates squared distances for all active
elements, sorts its `std::list` by distance, and then linearly walks that list
in `Restream`. This is done independently for markers, normal objects, object
LODs, pickups, players, vehicles, and lights. The relevant quantity is the
active set in nearby spatial sectors, not necessarily every element on the
server. A large far-away population should therefore be compared with the same
number near the camera.

The vehicle and ped managers linearly call `StreamedInPulse` for every native
streamed-in entity. Vehicle work includes collision-state enforcement, frozen
state maintenance, ground availability, train links, interpolation, door
interpolation, attachments, and state reconciliation. Ped work includes
controller state, frozen/health/armour state, task and vehicle transitions,
interpolation, keysync, contacts, and scripted-pad handling. The object manager
similarly walks every streamed-in object after the GTA world pass.

Neon timing checkpoints now expose aggregate `MTA_*Manager`, `MTA_Streamers`,
and per-streamer scopes in the existing opt-in `#0000 Log timing` diagnostic.
They surround whole loops rather than individual elements so measurement
overhead does not grow with entity count.

### GTA simulation and collision

`CWorld::Process` (`0x5684A0`) iterates GTA's moving-entity list. It first calls
`UpdateAnim`, then virtual `ProcessControl`, and removes entities that become
static from the moving list. It separately processes objects with control code.
This makes moving-versus-frozen comparisons essential, but MTA's frozen vehicle
state must not be equated with removal from GTA's moving list. MTA reapplies the
frozen matrix and zero velocities every wrapper pulse, and controlled tests
showed that deeply overlapping frozen vehicles continued to incur collision
work and could be slower because they were prevented from separating.

For unsafe moving entities, the same world pass invokes `ProcessCollision`,
then retries unsafe entities four more times, followed by another stuck check
and up to two shift passes. `CPhysical::ProcessCollision` (`0x54DFB0`) can split
a frame into multiple collision steps and performs sector/entity collision
queries before applying collision response. Dense touching vehicles can thus
cost substantially more than equally numerous separated vehicles, and the
cost may be nonlinear when many entities remain unsafe. Disabling collisions
is a diagnostic control, not a generally correct multiplayer optimization.

### Animation, PreRender, and drawing

`CEntity::UpdateAnim` calls `RpAnimBlendClumpUpdateAnimations`; it determines
whether the entity is on screen and passes that state into the animation
update. Visible peds then take a second important path:
`CPed::PreRenderAfterTest` (`0x5E65A0`) calls `UpdateRpHAnim`, which updates the
RenderWare skin hierarchy matrices. The same function handles IK/slope state,
ped shadows, weapons, rain effects, and other state-dependent effects.

`CRenderer::PreRender` (`0x553910`) performs virtual `PreRender` calls over the
visible LOD, visible entity, super-LOD, invisible-effect, and alpha lists. The
cost follows renderer list membership, not the total element count. Neon's
8192-entry lists prevent truncation but also permit much larger linear
PreRender workloads than GTA's original 1000-entry arrays.

Vehicle PreRender is also nontrivial. `CVehicle::PreRender` calculates lighting,
pre-renders occupants, handles model 2DFX, and updates environment-map state;
subclasses such as `CAutomobile::PreRender` add suspension, wheels, lights,
exhaust, rain, damage, and model-component work. Rendering then traverses the
model's atomics through visibility callbacks, so custom model atomic/triangle
counts and shaders can move the bottleneck from CPU submission to GPU work.

## Attribution matrix

The controlled resource is `test-resources/entity-performance-test`. Its three
camera modes have intentionally different meanings:

| Comparison | Mostly isolates | Important limitation |
| --- | --- | --- |
| visible vs hidden | PreRender, drawing, skinning, shadows/effects | Hidden entities remain near and may still have invisible-effect work |
| hidden vs far | streamed simulation and MTA manager work | Far entities are client-local, so there is no packet load |
| static vs idle vs moving | freeze maintenance, moving-list ProcessControl, animation, interpolation, physics | MTA freeze reapplies transforms and does not guarantee removal from native collision work |
| separate vs touching vs contact | ordinary density vs adjacent contacts vs deep-overlap retries | Packing also changes overdraw and visible ordering |
| collisions on vs off | collision-specific part of the contact delta | Collision-off is not multiplayer-correct for normal gameplay |
| standard vs replaced model | geometry, atomics, skin, materials, texture/shader cost | Use the same position/count/settings and a fixed replacement asset |

The resource reports average, p95, p99, and worst frame time plus renderer
high-water values. The native timing log attributes anomalous slow frames to
MTA scopes, the existing `CWorld_Process`, `CGame_Process`, `NetPulse`, and
client pulse scopes. A visible-only regression with flat CPU scopes suggests
GPU or uninstrumented render-thread/driver work, but an external GPU capture is
required to prove GPU saturation or present/VSync waiting.

## Remaining attribution priority

Targeted native checkpoints and an eight-stage attribution pass are archived in
`test-resources/entity-performance-test/results/2026-07-12-vm-native-attribution.md`.
They resolve two earlier questions: collision consumed 19-25 ms of a 21-30 ms
`CWorld_Process` in the 64-vehicle touching case, and 7-10 ms of a 10-15 ms
`CWorld_Process` for 110 near moving peds. MTA managers, native ProcessControl,
animation, and PreRender were individually much smaller in those samples.

1. **Split native collision internally.** The entity-level scope now proves the
   dominant native function and records more collision calls than entities per
   frame because unsafe entities are retried. The next hook should separate
   broad-phase sector scans, `ProcessEntityCollision`, retry passes, and shift
   passes before changing collision semantics.
2. **Measure vehicle render CPU versus GPU.** Separated and touching visible
   vehicles are expensive, and native PreRender includes suspension, lighting,
   occupants, reflections, and effects. The measured automobile PreRender scope
   was only 0-1 ms while visible vehicles still added substantial frame time;
   time `Render`/atomic traversal and use a GPU capture before attributing the
   remaining delta specifically to GPU saturation.
3. **Measure real synchronization.** The current resource intentionally has no
   remote packet decoding, sync-owner traffic, or realistic interpolation
   targets. A recorded packet workload or multiple clients must compare
   `NetPulse`, MTA manager scopes, and native world work under the same camera.
4. **Measure streamer sorting directly.** Source complexity is linear plus a
   sort per active streamer each frame, but the current frame-level profile
   cannot rank it against native ped work. Use the new aggregate per-streamer
   scopes before implementing dirty-state caching or replacing containers.
5. **Add a fixed-frustum object density profile.** Standard-object runs proved
   total and streaming counts are cheap, not that 1000 visible custom objects
   are cheap. Keep the camera and footprint fixed, increase actual visible
   high-water, then repeat with controlled custom geometry, shaders, shadows,
   and attachments.
6. **Measure production scripts separately.** Existing Lua timing statistics
   should be captured with a representative resource set. The clean baseline
   prevents scripts from contaminating engine attribution but cannot dismiss
   them as a production-server bottleneck.

## Optimization gates

Safe MTA-side candidates, if measurements justify them, include reducing
unchanged wrapper work, making streamer order maintenance dirty-driven, and
using cache-friendlier active containers while preserving exact distance and
stream-limit semantics. These should first demonstrate identical streamed sets
and interpolation results under movement, attachment, dimension change,
resource restart, and reconnect.

Frequency scaling is plausible for purely visual work: distant/offscreen
animation association updates, skin matrices, shadows, and optional effects can
potentially run at lower rates with interpolation. It is unsafe to frequency-
scale collision, authoritative vehicle physics, controller/task processing, or
sync-owner state without a multiplayer-specific correctness proof. Visible
remote player animation may be decimated only if weapon bones, hit reactions,
attachments, IK, and event timing remain correct.

GTA hooks would be required to separate native `ProcessControl`, collision,
animation, PreRender, and render CPU time more finely or to add native visual
LODs. gta-reversed makes those hook boundaries understandable, but any change
still needs GTA SA 1.0 US binary verification and lifecycle testing. No GTA
single-player branch should be removed merely because its name appears
irrelevant: MTA reuses tasks, contacts, damage, weapons, audio, occupants, and
render state through those paths.

Pool increases are gated on measured headroom at the current MTA budgets. A
capacity increase is reasonable only after the dominant slopes and worst-frame
spikes are reduced or bounded, and after the same scenario is repeated below,
at, and above the previous budget without correctness or lifecycle regressions.

## Results template

Record at least three repeats per row and retain raw `console.log` and
`timings.log` files. Do not average different limiter, VSync, resolution,
weather, time-of-day, model, or resource configurations.

| Scenario | Count | Actual native/visible | avg ms | p95 ms | p99 ms | worst ms | Key timing scopes | Notes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| Baseline | 0 | | | | | | | |
| Vehicles separated/static/visible | 64 | | | | | | | |
| Vehicles contact/moving/visible | 64 | | | | | | | |
| Vehicles contact/moving/collision off | 64 | | | | | | | |
| Peds moving/visible | 110 | | | | | | | |
| Peds moving/hidden | 110 | | | | | | | |
| Objects static/visible | 1000 | | | | | | | |
| Objects static/far | 1000 | | | | | | | |
| Mixed moving/visible | | | | | | | | |
