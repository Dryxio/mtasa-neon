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

The ranking in this document is a source-supported test priority, not a claimed
measured ranking. Populate the results table with repeated before/after samples
before describing any item as the dominant real-world hotspot.

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
This makes moving-versus-frozen comparisons essential: frozen entities can
retain visible render cost while avoiding much of the moving-list work.

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
| static vs moving | moving-list ProcessControl, animation, interpolation, physics | Freeze can change more than one native flag |
| separate vs contact | collision queries, retries, and response | Packing changes overdraw and visible ordering too |
| collisions on vs off | collision-specific part of the contact delta | Collision-off is not multiplayer-correct for normal gameplay |
| standard vs replaced model | geometry, atomics, skin, materials, texture/shader cost | Use the same position/count/settings and a fixed replacement asset |

The resource reports average, p95, p99, and worst frame time plus renderer
high-water values. The native timing log attributes anomalous slow frames to
MTA scopes, the existing `CWorld_Process`, `CGame_Process`, `NetPulse`, and
client pulse scopes. A visible-only regression with flat CPU scopes suggests
GPU or uninstrumented render-thread/driver work, but an external GPU capture is
required to prove GPU saturation or present/VSync waiting.

## Source-supported test priority

1. **Vehicle collision/physics under contact.** The collision retry structure
   makes this the strongest candidate for nonlinear spikes. Test moving contact,
   separated, and collision-disabled runs before changing vehicle limits.
2. **Visible ped/player PreRender and skin hierarchy updates.** Every visible
   ped can update animation matrices and execute shadow/weapon/effect branches.
   Compare visible and hidden moving peds, then shadows/effects settings.
3. **Vehicle PreRender/render.** Suspension, lights, occupants, reflections,
   components, and multi-atomic rendering make visible vehicles materially
   different from generic objects.
4. **MTA per-entity manager pulses and interpolation.** These are linear in the
   streamed-in population and are paid in addition to GTA work. Real remote
   player/vehicle samples are required; local elements do not reproduce packet
   jitter or remote interpolation targets.
5. **Streamer distance recomputation and list sorting.** Source complexity is
   linear plus sort per active streamer each frame. Measure near versus far and
   inspect the new per-streamer scopes before replacing containers or adding
   dirty-state caching.
6. **Generic visible object PreRender/render.** Usually simpler per entity, but
   the object budget is much larger and custom models/shaders can dominate.
7. **Networking and scripts.** `NetPulse` and existing Lua timing statistics
   must be measured on a recorded or multi-client workload. They are controlled
   away by the client-local baseline and cannot be dismissed from production
   behavior based on that baseline.

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
