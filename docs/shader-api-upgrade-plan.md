# MTA:SA Client — Expanded DX9 Rendering API

## Context

MTA:SA's client `dx*`/`engine*` Lua API currently covers 2D drawing, single shaders applied to world textures, single-target render-to-texture, and a backbuffer-grab "screen source." It has **no** true multi-camera rendering, no Multiple Render Targets, no custom depth-stencil targets, no render-to-cubemap, and no shadow-mapping primitives — confirmed by direct source search (no `SetRenderTarget` call anywhere touches index >0; no mirror/reflection/second-camera precedent exists anywhere in `game_sa`/`multiplayer_sa`).

The goal is to expose a general-enough Direct3D 9 rendering framework that resource authors can build shadow systems, mirrors, security cameras, G-buffers, reflection probes, etc. on top of — without handing Lua raw D3D state, without breaking any existing resource, and without pretending DX9 has capabilities it doesn't. This is designed architecture-first: one scoped state-restoration primitive that every later feature (MRT, custom depth, scene views, cubemaps, shadow cameras) is a client of, not six independent ad-hoc save/restore implementations.

Scope note: an earlier "cloud shadows" idea was replaced with the ability to apply a custom shader to the sky itself, analogous to `engineApplyShaderToWorldTexture`. GTA:SA's sky (`CClouds::RenderSkyPolys`, hooked at `0x714650`) draws untextured, vertex-colored gradient polygons with nothing bound in sampler stage 0, so texture-name matching (how `engineApplyShaderToWorldTexture` actually works) cannot select it — a dedicated sky-draw hook is used instead.

This plan front-loads the *smallest safe foundation* — capability reporting, diagnostics, MRT, custom depth, safe render passes, and **one** independent off-screen world view — before multi-view scheduling or any shadow work.

---

## Architecture (built once, used by everything below)

### A1. `CRenderStateScope` — the one RAII save/apply/restore primitive

New: `Client/core/Graphics/CRenderStateScope.h/.cpp`, alongside `CRenderItemManager`.

```cpp
class CRenderStateScope
{
public:
    explicit CRenderStateScope(IDirect3DDevice9* pDevice);   // captures current state
    ~CRenderStateScope();                                     // restores unconditionally

    bool ApplyRenderTargets(const std::array<IDirect3DSurface9*, 4>& targets, IDirect3DSurface9* pDepthStencil);
    bool ApplyViewport(const D3DVIEWPORT9& vp);
    bool ApplyCamera(const CCameraSnapshot& cam);   // wraps CCameraSA matrix + active-cam index + FOV/clip
private:
    IDirect3DSurface9* m_SavedRT[4];
    IDirect3DSurface9* m_SavedDepthStencil;
    D3DVIEWPORT9       m_SavedViewport;
    CCameraSnapshot    m_SavedCamera;
};
```

- Captures render-target slots 0–3, depth-stencil surface, viewport unconditionally on construction; camera state only when `ApplyCamera` is used. Restoration happens in the destructor — unconditional, so it runs even on early-return paths.
- This is a **stack**, not the existing single global slot: `CRenderItemManager::SaveDefaultRenderTarget()`/`RestoreDefaultRenderTarget()` stays untouched for legacy `dxSetRenderTarget` behavior. `CRenderStateScope` instances nest on the C++ call stack; every new feature (render passes, scene views, cubemap faces, shadow cameras) is **required** to enter/exit exclusively through this class — no feature hand-rolls its own `GetRenderTarget`/`SetRenderTarget` pair.

### A2. Device-loss and resource-stop integration (one place, not per-feature)

- **Device loss**: every new render-item type (typed RT, depth-stencil target, MRT set, scene view, cubemap target) is a `CRenderItem` subclass, so it already participates in the existing `CRenderItemManager::m_CreatedItemList` → `OnLostDevice()`/`OnResetDevice()` fan-out, itself driven by `CGraphics::OnDeviceInvalidate`/`OnDeviceRestore` (`Client/core/Graphics/CGraphics.cpp:1704`/`1737`) ← `CProxyDirect3DDevice9::Reset` (`Client/core/DXHook/CProxyDirect3DDevice9.cpp:837`) → `CDirect3DEvents9::OnInvalidate`/`OnRestore`. No parallel fan-out list is created. `CRenderStateScope` itself is stack-lifetime only and holds no cross-frame D3D resources, so it needs no lost-device handling at all.
- **Resource-stop**: every new Lua-facing element is a `CClientRenderElement` subclass created with `SetParent(pParentResource->GetResourceDynamicEntity())`, exactly like today's `CClientRenderTarget`/`CClientScreenSource`. `CResource::~CResource()` → `DeleteClientChildren()` already recursively destroys these; destructors release the underlying `CRenderItem` via the existing refcount path. Composite objects (a scene view owning a color target + depth target) hold those as private, non-script-visible `CRenderItem*` refs so they die atomically with the parent.
- **Memory/queue accounting**: extends `CRenderItemManager::UpdateMemoryUsage()`/`CanCreateRenderItem()` (KB-based gating, `CRenderItemManager.cpp` ~912/960) rather than adding a parallel budget system. New count-based caps (max scene views, max render-pass nesting) are separate fields on the new capabilities struct, enforced at creation/queue time.

### A3. Recursive-render prevention

A single global re-entrancy counter guards the one place that manually re-invokes world rendering (Stage 1 item 13's queued scene-view render loop). It refuses to run re-entrantly, and a queued view cannot itself request further scene views in Stage 1 (request is rejected with an actionable error, not silently dropped). This is the only re-entrancy guard needed anywhere in the plan — render passes (item 12) never re-enter `Render3DStuff`, they only rebind state around ordinary Lua-safe `dx*` drawing calls already issued from `onClientRender`.

---

## Stage 1 — Capability reporting, diagnostics, accounting, MRT, custom depth, render passes, ONE scene view

This is the mandated first milestone (nothing here ships before this stage is stable).

**5. Capability reporting** — new `SDxCapabilities` struct in `Client/sdk/core/CRenderItemManagerInterface.h`, populated once in `CRenderItemManager::OnDeviceCreate` via `GetDeviceCaps`/`CheckDeviceFormat`/`CheckDepthStencilMatch`, reusing the depth-format probing already there (`RFORMAT_INTZ`/`DF24`/`DF16`/`RAWZ`). New Lua function `dxGetRenderCapabilities()` (a **new** function, not appended to `dxGetStatus`'s existing table — zero risk to scripts that assume its current shape), registered in `CLuaDrawingDefs::LoadFunctions()` next to `dxGetStatus` (`CLuaDrawingDefs.cpp` ~line 54). Fields: SM3 support, per-format sampleable/renderable/blendable flags (reusing the `_D3DFORMAT` enum at `CLuaFunctionParseHelpers.cpp:827`), max simultaneous RTs, independent-MRT-blend (honestly reported, near-always false), cubemap-RT support, max scene views, max render-pass nesting. Every later creation function validates against this and returns `false, errorMessage` (matching `dxCreateShader`'s existing convention) instead of silently degrading or crashing.

**6. Shader compile diagnostics** — extends the existing `ID3DXBuffer*` error capture in `CRenderItem.EffectTemplate.cpp`. Add a structured (best-effort parsed) diagnostics table as a **new optional third return value** of `dxCreateShader` on failure/warning (old 2-value call sites unaffected). Add `dxGetShaderDiagnostics(shader)` for technique/pass/parameter introspection.

**6A. Extended shader language/compiler path** — research and then add an opt-in compiler mode without changing the legacy D3DX Effect path used by existing resources. “Loops are unsupported” must first be separated into three cases: loops rejected by the old HLSL/effect parser, loops that compile but exceed Shader Model 2 instruction/flow-control limits, and dynamic loops that require a `vs_3_0`/`ps_3_0` technique. DX9 can execute SM2/SM3 bytecode but cannot gain SM4+ features from a newer parser, so this milestone improves authoring and compilation only within real DX9 limits.

- Preserve the current `dxCreateShader` argument parsing, D3DX flags, technique selection and cache identity as the default `legacy` mode. Existing shader source must compile and select techniques exactly as before.
- Prefer a final options table over a positional boolean: `dxCreateShader(source, macros, priority, maxDistance, layered, elementTypes, { languageMode = "extended", preferredProfiles = { pixel = "ps_3_0", vertex = "vs_3_0" }, strict = true, warningsAsErrors = false })`. The table is optional and last, so old calls remain source-compatible. Final syntax must follow the repository's argument-parser conventions after overload testing.
- Probe whether the available redistributable compiler can compile individual `vs_2_0`/`ps_2_0`/`vs_3_0`/`ps_3_0` programs while retaining the existing safe effect abstraction. Do not replace `ID3DXEffect` blindly: if a newer compiler cannot produce a compatible effect object, introduce an internal compiled-program/technique representation or explicitly limit extended mode to syntax preprocessing plus D3DX effect compilation.
- Extended mode may support bounded `for` loops on SM2 by compile-time unrolling when bounds are constant and validated. Dynamic loops are enabled only for validated SM3 techniques and reported as unsupported on SM2 hardware; they are never silently rewritten into an unbounded CPU/GPU workload.
- Add controlled includes, macros, source identifiers, line remapping, strict/warnings-as-errors flags, cache keys containing compiler mode/profile/options/include hashes, instruction/register/sampler reporting, and deterministic profile fallback chains. Includes remain resource-root confined by `CIncludeManager`.
- Capability and diagnostic output reports compiler modes, supported profiles, whether loop unrolling is available, selected profile, fallback reason, and whether the final bytecode used static or dynamic flow control.
- Security budgets limit source size, include depth/count, macro count, preprocessing expansion, loop-unroll count, compile time where enforceable, shader instruction count, constant registers and samplers. Compilation remains outside unsafe GTA render hooks.
- Definition of done: legacy shaders are byte-for-byte behavior-compatible; deterministic tests cover a constant SM2 loop, a dynamic SM3 loop, an SM2 fallback technique, excessive/unbounded-loop rejection, compiler errors with correct source lines, device reset, and unsupported-profile errors.

**7. Render statistics** — extend `SDxStatus` additively (never remove/rename existing fields) with per-frame counters (render-pass begins, scene-view renders, MRT binds, pass nesting depth). New `dxGetRenderStatistics()`. Counters live in a small new `CRenderStatsCollector`; when nothing new is called they stay at zero — no added per-frame cost.

**8. Automatic shader value bindings** — opt-in annotation on D3DX effects (e.g. `string mtaSemantic = "ViewProjectionMatrix";`) resolved once at `CShaderItem` construction into fixed `D3DXHANDLE`s, refreshed in the existing `CShaderInstance::ApplyShaderParameters()` path. Shaders without annotations are byte-for-byte unaffected. No raw matrix/state is ever handed to Lua directly — only through this mechanism or existing `dxSetShaderValue`.

**9. Typed render targets** — `dxCreateRenderTarget`'s signature is unchanged; add `CheckDeviceFormat` validation before `CreateTexture` so an unsupported format fails with an actionable message instead of silently misbehaving. Extend the format enum only for genuinely missing entries, each capability-gated.

**10. Custom depth-stencil targets** — new `Client/core/Graphics/CRenderItem.DepthStencilTarget.cpp` (`CDepthStencilTargetItem : public CRenderItem`), wrapping `CreateDepthStencilSurface`, in either a non-sampleable hardware Z format or (only when capability-confirmed) a sampleable fourCC format. New `Client/mods/deathmatch/logic/CClientDepthStencilTarget.h/.cpp` (mirrors `CClientRenderTarget`). Lua: `dxCreateDepthStencilTarget(sizeX, sizeY [, format])`. Requesting `sampleable=true` on unsupported hardware fails explicitly — never silently returns a non-sampleable surface while claiming success. Distinct from the existing primary-scene "readable depth buffer" mechanism (`PreDrawWorld`/`SaveReadableDepthBuffer`), which is untouched.

**11. MRT binding + validation** — new `CMrtSetItem : public CRenderItem` holding up to 4 `CRenderTargetItem*` (slot 0 mandatory) + optional depth target. Validates equal dimensions across slots and slot count against capability limits at bind time, not per-frame. Lua: `dxCreateMrtSet(table renderTargets [, depthStencilTarget])`.

**12. Safe scoped render passes** — `dxBeginRenderPass(target [, target2, target3, target4] [, depthStencilTarget] [, clear=true])` / `dxEndRenderPass()`, generalizing the existing `dxSetRenderTarget` begin/restore pattern scripts already know. Each `dxBeginRenderPass` pushes a `CRenderStateScope` and validates via the MRT rules above even for a single target; nesting bounded by capability. Runs entirely on the already-safe Lua call stack (from `onClientRender` etc.) — never re-enters GTA/RenderWare internals. If a resource is stopped mid-open-pass, the element destructor force-closes the scope rather than leaving dangling bindings (explicitly tested in Stage 6).

**13. ONE independent off-screen scene view** — the actual multi-camera proof of concept.
- `CClientSceneView` owns a script-visible color target, a private depth target, a camera snapshot and a queued flag. The Stage-1 implementation is deliberately capped at one view globally while the GTA/RenderWare lifecycle is hardened.
- Lua: `dxCreateSceneView(sizeX, sizeY [, colorFormat])`, `dxSetSceneViewCamera(view, camX,camY,camZ, lookX,lookY,lookZ [, fov] [, nearClip, farClip])` (mirrors `setCameraMatrix`'s shape), `dxRequestSceneViewRender(view)` (enqueues only — idempotent per frame, rejected with an actionable error once the Stage-1 cap of **1** view/frame is exceeded), `dxGetSceneViewTexture(view)` (returns a normal texture usable by `dxDrawImage`/`dxSetShaderValue`).
- Add a matrix-based camera setter alongside the position/target overload, accepting a complete validated camera transform (right/front/up/position basis, with FOV/projection parameters kept explicit). This must not replace or reinterpret the existing coordinate API; both forms feed the same normalized internal `CMatrix`, reject non-finite or degenerate bases, and preserve backward compatibility.
- **Queue consumption point**: a validated hook on GTA Idle's call to `CRenderer::ConstructRenderList` executes the queued view immediately before primary visibility construction. Rendering from the sky hook was rejected after runtime testing: GTA had already prepared camera-dependent primary lists and ped state there, causing secondary entities to leak into the primary frame. After the secondary pass returns, GTA's original `ConstructRenderList` and `PreRender` naturally rebuild primary state.
- **RenderWare-backed target lifecycle**: a raw `IDirect3DSurface9` binding is insufficient for a full GTA `RenderScene`. `CRenderer::RenderEverythingBarRoads` internally cycles `RwCameraEndUpdate`/`RwCameraBeginUpdate`; the RenderWare D3D9 backend then rebinds targets from the camera's attached `RwRaster`. The working implementation creates matching `rwRASTERTYPECAMERATEXTURE` and `rwRASTERTYPEZBUFFER` rasters, temporarily attaches them to the GTA RenderWare camera, clears and begins the RW camera update, renders, ends the update, restores the original rasters, and copies the color result into the SceneView texture without sharing D3D ownership between RenderWare and `CRenderTargetItem`.
- **Per-view render procedure**, protected by scoped state and a recursion guard: consume request → apply secondary GTA camera → attach private RenderWare color/depth rasters → `RwCameraClear`/`RwCameraBeginUpdate` → build secondary visibility lists → invoke the real GTA `RenderScene` (`0x53DF40`) directly → drain/reset the global weapon-ped queue into the secondary target → reproduce `CPostEffects::ColourFilter`'s per-frame tint against a private scratch texture (see below) → `RwCameraEndUpdate` → restore RW rasters and GTA/D3D camera state → copy color into the script-visible texture. No Lua callback runs inside this native render context.
- **Post-effects colour-filter parity**: `RenderScene` alone omits GTA's monolithic `RenderEffects()` (deliberately — its particle/corona/moving-thing queues are shared globals that a second per-frame world pass cannot safely re-enter). `RenderEffects()`'s last step, `CPostEffects::Render() → ColourFilter()`, is a real per-frame full-screen additive tint driven by `CTimeCycle`'s current `PostFx1`/`PostFx2` colours, applied to the primary view every frame regardless of the shadow/particle work around it — omitting only this one step left every SceneView consistently duller/flatter than the primary camera, independent of lighting, camera position or vertex prelight. GTA's own `ColourFilter` can't be called directly: its quad geometry/UVs are baked once to the primary screen's raster size, and its backing raster (`pRasterFrontBuffer`) is a shared global the primary frame's own later pass also depends on. `ApplySecondarySceneColourFilter` (`Client/multiplayer_sa/CMultiplayerSA.cpp`) instead reproduces the same two-pass additive blend against a private scratch texture sized to each SceneView's own raster, scoped with a D3D9 state block so RenderWare's parallel fixed-function state cache is never left stale — it touches no GTA globals (`pRasterFrontBuffer`, `cc_vertices`, the `CPostEffects::Render` smoothing state), so it cannot desync the primary frame's own colour-filter pass.
- **Proof-of-concept acceptance**: an in-game resource positions a genuinely independent camera, requests one render per frame and draws it via `dxDrawImage`; static world, vehicles and peds render from the secondary angle without leaking onto the primary framebuffer. Full definition of done still includes item 14's reset, restart, failure-path and long-run restoration tests.

**14. Camera/state and RenderWare-resource validation** — harden the proof of concept before raising the view cap. Replace function-local persistent RW rasters with explicitly owned resources that are destroyed on resource stop/shutdown and invalidated/recreated across device loss/reset; validate every early-return path; test resource restart, alt-tab, queued requests during reset, long-run camera drift, global visibility/ped-list cleanup and unsupported `StretchRect` format combinations. A non-empty queue must be dropped safely while the device is unavailable. Runtime resolution changes are outside this milestone because MTA does not expose an in-game resolution switch; target-size changes remain validated through destroy/recreate tests.

**Stage 1 zero-cost confirmation**: when unused, the pre-`ConstructRenderList` hook performs one null-handler/empty-queue check, `CRenderStateScope` is never constructed, no RenderWare scene-view raster is created, no extra world pass executes, and all new counters stay at zero.

---

## Sky shaders (independent of the shadow work; ships alongside Stage 1)

A resource can assign a `dxShader` to replace how the sky itself is drawn, the same conceptual shape as `engineApplyShaderToWorldTexture` but for the sky.

- Verified: `CClouds::RenderSkyPolys` (hooked at `Client/multiplayer_sa/CMultiplayerSA_Rendering.cpp:607-633`, `HOOKPOS_CClouds_RenderSkyPolys = 0x714650`) draws untextured vertex-colored polygons — the existing texture-name-match mechanism has nothing to bind to, so it cannot be reused as-is.
- The existing hook is an unconditional inline patch (`pushad; call OnMY_CClouds_RenderSkyPolys; popad; jmp 0x714655`) that always falls through into GTA's original sky-drawing code afterward — it has no way to *suppress* the original draw today.
- Design: convert this single hook site into a conditional trampoline (same technique already used elsewhere in this codebase for call-based hooks, e.g. `Render3DStuff`): if a resource has assigned a sky shader (via new `engineApplyShaderToSky(shader)`), skip the call into GTA's original `RenderSkyPolys` body and instead have MTA draw its own sky geometry using that shader's technique; otherwise fall through to GTA's original code exactly as today. Requires locating `RenderSkyPolys`'s exit point (implementation-time disassembly task) so the conditional skip lands correctly.
- Feed the replacement draw the automatic-value bindings from Stage 1 item 8 relevant to sky rendering: sun direction, moon direction, horizon/sky ambient colors already tracked by MTA's existing weather/time system, camera view/projection.
- `engineRemoveShaderFromSky()` restores default rendering. Ownership/cleanup follows the same `SetParent`-based pattern as every other shader-application function.
- **Definition of done**: a resource can replace the sky's rendering with a custom shader driven by real sun/time/weather values, and removing the shader restores GTA's exact default sky with no artifacts.

---

## Stage 2 — Multiple scheduled scene views

Raise the Stage-1 cap of 1 view/frame to a capability-reported maximum, still hard-enforced. Add `dxSetSceneViewUpdateMode(view, mode [, value])` with `manual` (the backward-compatible default used with `dxRequestSceneViewRender`), `once`, `always`, `every_n_frames`, and millisecond `interval` modes. Resource authors select target dimensions and update frequency directly; the scheduler does not throttle by resolution or pixel count. Reuses Stage 1 item 13's exact per-view render procedure — this stage is scheduling around it, not a new rendering mechanism. Done when N views (N = capability max) render simultaneously with correct independent restoration, re-validated per item 14's test at N>1.

### Per-view shader isolation and output processing

Scene rendering and processing its finished texture are separate operations and must remain separate in the Lua API:

- **Scene/material shader set**: each `SceneView` may own an optional, resource-scoped set of world-material shader assignments used only while that view is rendering. When enabled, the view starts from the unmodified GTA material path and ignores ordinary `engineApplyShaderToWorldTexture` assignments made for the primary scene or another view; only assignments explicitly attached to that view participate. This requires a scoped shader-match context around the native secondary render, not temporary mutation of the global replacement map. The context must be restored even when rendering fails.
- API: `engineApplyShaderToSceneViewWorldTexture(shader, sceneView, textureName [, targetElement, appendLayers = true])` and `engineRemoveShaderFromSceneViewWorldTexture(shader, sceneView, textureName [, targetElement])`. World-material assignment follows the existing `engineApplyShaderToWorldTexture` namespace; SceneView creation, output textures and post-processing remain in the `dx*` namespace. The first safe version supported wildcard texture matching across the whole view only; target-element filtering (mirroring `engineApplyShaderToWorldTexture`'s own `targetElement`/`appendLayers` parameters, resolved against the SceneView's per-draw-call rendering entity rather than the global match-channel system) has since been added once the isolated matcher was proven. Both elements must belong to the calling resource. The earlier `dxApplyShaderToSceneViewWorldTexture` and `dxRemoveShaderFromSceneViewWorldTexture` development names were compatibility aliases during that proving period; they have been removed now that the function signatures reached parity with `engineApplyShaderToWorldTexture` — scripts must use the `engine*` names.
- **Camera transforms and projection**: retain `dxSetSceneViewCamera` and add `dxSetSceneViewMatrix(sceneView, matrix [, fov])` for mirrors, portals, editor cameras and entity-attached views without lossy position/target reconstruction. `dxSetSceneViewOrthographicProjection(sceneView, width, height, nearClip, farClip)` / `dxSetSceneViewPerspectiveProjection(sceneView)` add an explicit, validated (finite, positive width/height/near/far, far > near) projection mode; perspective (driven by the existing FOV path) remains the default and no existing call changes behavior. GTA's own camera model (`CCam`/`CCamera::CalculateDerivedValues`) has no orthographic concept at all - it always derives a perspective frustum from the active `CCam`'s FOV - so the orthographic case overrides the RenderWare camera's projection directly via the real `RwCameraSetProjection`/`RwCameraSetViewWindow`/`RwCameraSetNearClipPlane`/`RwCameraSetFarClipPlane` entry points, applied *after* `CopyCameraMatrixToRWCam`/`CalculateDerivedValues` position the view matrix (still valid for an orthographic camera - only the projection shape differs) but before `ConstructRenderList`/`RenderScene` read the camera's frustum. Because every SceneView render this frame shares one physical RenderWare camera object sequentially (there is no per-frame view-count cap - see Stage 2's scheduler note below), the projection mode is set unconditionally on every render (perspective included), not only when orthographic - otherwise one view's orthographic mode would leak into the next view sharing that same camera object. No hardware capability gate applies: orthographic projection is a plain RenderWare/D3D9 projection-matrix shape, not a GPU feature, so "capability-safe fallback" here means rejecting invalid parameters, not probing device caps. Known limitation, not a correctness bug: GTA's own coarse sector/entity visibility selection (`ConstructRenderList`) has no orthographic awareness either and still culls against the perspective frustum `CalculateDerivedValues` computed first; an orthographic SceneView's candidate entity list can be a superset of what a tightly-fit orthographic frustum would select, while RenderWare's own per-object frustum test during `RenderScene` does use the corrected projection, so this affects draw-call count, not what is actually drawn.
- **Perspective output dimensions and aspect ratio**: `dxCreateSceneView(sizeX, sizeY, ...)` already defines the color/depth target resolution, including shadow-map resolution, so no duplicate width/height setter is required initially. Perspective SceneViews must, however, derive their RenderWare view window from `sizeX / sizeY` on every render instead of inheriting the primary display's aspect ratio. Extend `dxSetSceneViewPerspectiveProjection` with an optional validated aspect-ratio override for deliberately non-matching projections; without it, target dimensions are authoritative. First document whether the existing FOV value is horizontal or vertical in GTA's camera convention, then derive the other axis consistently. SceneView dimensions remain immutable for the initial API: changing shadow resolution recreates the SceneView, avoiding partial color/depth recreation and stale texture handles during device reset.
- **Output post-processing**: `dxGetSceneViewTexture(sceneView)` remains a side-effect-free texture getter. The initial declarative API is `dxSetSceneViewOutputShader(sceneView, shader [, inputName = "SceneViewTexture"])` and `dxRemoveSceneViewOutputShader(sceneView)`. It renders fullscreen into a private intermediate target after a successful world update and copies only the complete result back to the public texture, preventing read/write feedback and partial-frame exposure. Ordered multi-effect chains extend this into alternating private targets only after the single-effect lifecycle is proven.
- Stopping either owning resource removes its per-view shader entries and post-process passes. With no per-view assignments or output passes, the existing SceneView behavior and render cost remain unchanged.

## Stage 3 — Cubemap render targets

New `CCubemapRenderTargetItem` wrapping `IDirect3DCubeTexture9` (cube textures already exist as a texture *type* in the engine, `TTYPE_CUBETEXTURE`, just not yet as a render target). Rendering a face reuses the scene-view render procedure unchanged, called once per face with a 90° FOV camera per fixed orientation, targeting `GetCubeMapSurface(face, 0)`. Lua: `dxCreateCubemapRenderTarget(edgeSize)`, `dxSetCubemapRenderTargetCamera(view, x,y,z)`, `dxRequestCubemapRenderTargetRender(view [, faceMask])` (partial per-frame face updates for budget control). Gated on Stage 1's cubemap capability flags. Done when a reflection-probe demo samples a live cubemap with correct per-face orientation and no primary-camera drift.

## Stage 4 — Shadow-mapping primitives (gated on Stage 1 being stable in real use)

1. **Shadow-map camera** — a `CSceneViewItem` variant with orthographic/frustum-fit projection for directional lights; spot lights reuse the existing perspective SceneView position/direction/FOV/near/far configuration and the target-aspect correction above without a separate camera API. `dxCreateSceneView(sizeX, sizeY, ...)` selects shadow-map resolution. The native-depth path disables color writes and populates only a sampleable depth target; an explicitly requested encoded-depth path instead writes linear or radial depth to a compatible color target and reports itself as encoded depth, never as native sampleable depth. Reuses items 10 and 13 entirely.
2. **Sun-direction shadow example** — wires a shadow-map camera to the existing sun vector, samples it in a world-texture shader via `engineApplyShaderToWorldTexture` using item 8's automatic light-space matrix.
3. **Point-light shadow cubemaps** — reuse Stage 3's renderable cubemap and render six square 90-degree perspective views from one light position. Store radial distance consistently across all faces (normally encoded into a renderable color cubemap on DX9), expose the six face matrices/orientations, bias/filter controls and partial face updates. Clearly document cubemap seams and the cost of up to six world renders per complete update.
4. **Cascaded shadow prototype (optional, explicitly last)** — N orthographic shadow cameras via Stage 2's scheduling, cascade-split selection in example shader code. Not enabled by default. Only attempted once the single directional shadow path is proven.

Capability-gated throughout: unsupported native sampleable depth returns an actionable error unless the resource explicitly requests the encoded-depth fallback. The fallback exposes its actual color format and depth encoding so shaders cannot mistake it for native depth. Spot-light targets additionally validate the requested 2D format; point-light shadows validate renderable cubemap formats and never assume depth cubemaps are sampleable on DX9.

**Update after implementation**: native sampleable depth for SceneViews turned out not to be a hardware-capability gap at all - it is architecturally impossible for *any* SceneView regardless of GPU. A SceneView's off-screen world render goes through a private RenderWare camera whose Z-buffer is an RW-owned raster; RenderWare's D3D9 driver unconditionally rebinds the device's depth-stencil surface to that raster's own plain (non-sampleable) surface every render, and D3D9's `StretchRect` requires matching formats for depth-stencil copies (unlike color), so there is no way to bridge that surface into an INTZ/DF24/DF16/RAWZ target afterward either. `dxCreateSceneView`'s `sampleableDepth` parameter therefore rejects `true` unconditionally, not conditionally on a capability check - see `docs/shader-api-upgrade-progress.md`'s Stage 4 entry for the full investigation. The encoded-depth path is consequently the *only* depth mechanism for SceneViews, not a fallback for weaker hardware. Standalone `dxCreateDepthStencilTarget` sampleable targets (used with `dxBeginRenderPass`/`dxCreateMrtSet`, which never go through RenderWare's world-render camera) are unaffected by this and remain genuinely capability-gated as originally planned.

## Stage 5 — Debug/demo Lua resources

One resource per subsystem: capabilities viewer, shader-diagnostics viewer, render-stats HUD, MRT demo, custom-depth demo, render-pass demo, single/multi scene-view demo, cubemap reflection demo, sky-shader demo, sun-shadow demo, point-light cubemap-shadow demo, cascaded-shadow demo (if built). No new C++ — thin, independently-toggleable Lua scripts, each documenting the exact API surface it exercises.

## Stage 6 — Regression, performance, documentation

- **Regression**: existing `dx*` resources run unmodified; `dxGetStatus`'s key set diffed to be byte-identical (catches accidental breakage of the untouched legacy API).
- **Stress**: max views × max MRT targets × nested passes × rapid create/destroy churn; device-loss injected mid-scene-view-render; resource-stop mid-open-pass.
- **Zero-cost-when-idle evidence**: frame-time comparison of an existing large resource, untouched, against pre-change baseline.
- **Documentation**: full Lua reference (signatures, capability preconditions, error returns) plus a DX9-limitations appendix.

---

## Explicit DX9 boundaries (not attempted)

No compute/geometry/tessellation shaders (DX9/SM3 has none). No guaranteed independent per-MRT blend state — reported via a capability flag, never emulated. No guaranteed sampleable depth — only the known vendor fourCC formats; absence is reported, never faked. No guaranteed SM3 — capability-gated with SM2 fallback for automatic values. No GPU-driven/compute-based cascade selection — cascades (if built) are CPU-side and optional. No raw `IDirect3DDevice9*`/format handles ever cross into Lua.

## Rejected / deferred approaches

- **Emulating MRT via repeated single-target passes**: rejected as default behavior — silently multiplies draw cost and breaks assumed single-pass blending; true MRT is used where supported, unsupported hardware gets an explicit error, not an automatic cost-hiding fallback.
- **Cascaded shadows first**: rejected per explicit instruction — highest-complexity, most failure-prone feature, deferred until Stage 1 and Stage 2 are proven.
- **Raw D3D state/handles in Lua**: rejected — everything goes through `CRenderStateScope`; Lua only ever sees element handles and declarative parameters.
- **Arbitrary Lua callbacks inside GTA/RenderWare internals**: rejected — replaced by the queue-and-render-at-a-controlled-point model, so no Lua code ever executes on the GTA render call stack.
- **A second, parallel GPU-memory budget system**: rejected — extends `CRenderItemManager`'s existing KB accounting; only adds count-based caps where the KB model can't naturally express a limit.
- **Cloud-shadow world-space projection** (original request item): dropped in favor of the sky-shader hook above.

## Critical files

- `Client/core/Graphics/CRenderItemManager.h/.cpp` — capability struct, `CanCreateRenderItem`/`UpdateMemoryUsage`, `PreDrawWorld`, `OnLostDevice`/`OnResetDevice` fan-out target for every new item type.
- `Client/sdk/core/CRenderItemManagerInterface.h` — new `eRenderItemClassTypes` entries, `SDxStatus`/`SDxCapabilities` fields, new interface virtuals.
- `Client/mods/deathmatch/logic/luadefs/CLuaDrawingDefs.cpp/.h` — new `dx*` registrations, following `DxCreateRenderTarget`/`DxGetStatus` exactly.
- `Client/mods/deathmatch/logic/CClientGame.cpp` and `Client/multiplayer_sa/CMultiplayerSA.cpp` — scene-view queue consumption before primary `ConstructRenderList` and the RenderWare-backed secondary world-render lifecycle.
- `Client/multiplayer_sa/CMultiplayerSA_Rendering.cpp` (lines 607-633, `HOOKPOS_CClouds_RenderSkyPolys`) — sky-shader trampoline conversion point.
- `Client/core/Graphics/CGraphics.cpp` (`OnDeviceInvalidate`/`OnDeviceRestore`, lines 1704/1737) — mandatory registration point for every new item manager's lost/reset handling.
- `Client/mods/deathmatch/logic/CClientRenderElement.h`, `CClientRenderElementManager.h/.cpp` — base pattern every new script-facing element must follow for ownership/cleanup.
- `Client/core/DXHook/CProxyDirect3DDevice9.cpp` (`Reset`, line 837), `CDirect3DEvents9.cpp` (`OnInvalidate`/`OnRestore`, lines 509/564) — device-loss lifecycle everything must integrate with.

## Verification

Because this is engine/native code with no existing automated D3D test harness in this repo, verification is staged:

1. **Build**: existing build must still succeed unmodified after each stage lands (no existing `dx*` Lua function signature changes).
2. **Regression-in-game**: load an existing resource that uses `dxCreateRenderTarget`/`dxCreateShader`/`dxCreateScreenSource`/`engineApplyShaderToWorldTexture` unmodified; confirm pixel-identical behavior and no new console warnings.
3. **Stage 1 acceptance test** (in-game, manual + a purpose-built debug resource per Stage 5): create a scene view, position a second camera, request a render every frame, draw the result texture on-screen via `dxDrawImage`, and visually confirm (a) the secondary view shows the correct independent camera angle, (b) the primary on-screen view is unaffected frame-to-frame, (c) `dxGetRenderStatistics()`/`dxGetRenderCapabilities()` report sane values.
4. **Device-loss test**: trigger alt-tab while a scene view, MRT set, and render pass are all in use; confirm no crash, no leak, and correct re-creation. Exercise target-size changes by destroying and recreating the owning elements rather than assuming an unsupported in-game resolution switch.
5. **Resource-stop test**: stop a resource mid-open-render-pass and mid-queued-scene-view-render; confirm no dangling bound render target/depth surface.
6. **Sky shader test**: assign and remove a sky shader repeatedly across a day/night and weather-change cycle; confirm default sky is bit-for-bit restored when removed.
7. Each later stage repeats the applicable subset of 3–5 at its own scale (multiple views, cubemap faces, shadow maps) before being marked `done` in `docs/shader-api-upgrade-progress.md`.
