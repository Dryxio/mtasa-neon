# MTA Neon limit patching guide

This document records the local references, workflow, and lessons learned while
developing limit patches for MTA Neon. Read it together with `AGENTS.md` before
changing a GTA limit.

## Local source trees

The relevant repositories on the macOS host are:

```text
MTA Neon (canonical working tree)
/Users/salimtrouve/Documents/GitHub/mtasa-neon

GTA: San Andreas reversed source
/Users/salimtrouve/Documents/GitHub/gta-reversed-dryxio

RenderWare reimplementation
/Users/salimtrouve/Documents/GitHub/librw

fastman92 Limit Adjuster source and documentation
/Users/salimtrouve/Documents/GitHub/mta-misc/fastman

Open Limit Adjuster (Project2DFX dependency/reference)
/Users/salimtrouve/Documents/GitHub/III.VC.SA.LimitAdjuster
```

Useful subpaths include:

```text
GTA reversed game source
/Users/salimtrouve/Documents/GitHub/gta-reversed-dryxio/source/game_sa

GTA reversed executable and Ghidra material
/Users/salimtrouve/Documents/GitHub/gta-reversed-dryxio/gta_sa.exe
/Users/salimtrouve/Documents/GitHub/gta-reversed-dryxio/gta_sa_compact.exe
/Users/salimtrouve/Documents/GitHub/gta-reversed-dryxio/gta_sa_compact.exe_ghidra

Fastman92 main source
/Users/salimtrouve/Documents/GitHub/mta-misc/fastman/source code/fastman92 limit adjuster/fastman92 limit adjuster/Source files

Fastman92 limit implementations
.../Source files/Modules

Fastman92 GTA structures
.../Source files/GameStructures/Rockstar Games

Fastman92 documentation
/Users/salimtrouve/Documents/GitHub/mta-misc/fastman/documentation
```

`gta-reversed-dryxio` is the best readable description of GTA behavior and
structures. Fastman92 is the best index of known executable references and the
extra code patches needed to make a particular limit movable. `librw` is useful
when a limit reaches RenderWare data structures or algorithms, but it is a
reimplementation/reference: GTA SA under MTA is not automatically executing
the code in the local `librw` tree.

## A limit is usually several limits

Do not assume that changing the first constant found implements a limit
adjuster. Trace the entire path from script/server data to rendering. A visible
cap can exist independently in:

1. MTA element creation, streaming, visibility, or manager code.
2. MTA's GTA wrapper and its wrapper-object arrays.
3. GTA's native fixed arrays, initialization loops, update loops, and render
   loops.
4. Hard-coded pointers to the start, individual fields, or end of a native
   array.
5. Immediate operands in native search/allocation functions.
6. RenderWare pools, index formats, or renderer batches.
7. Script/API validation, serialization, networking, or server-side limits.

The corona experiment demonstrated this directly. Raising only MTA's marker
streaming budget changed the observed ceiling from 32 to 62, not to the desired
120. The remaining ceiling was GTA's 64-entry corona pool, with two entries
reserved by MTA's allocator.

## Implemented reference: 4096 coronas

The first Neon limit patch is commit `98bdc2187` and expands the GTA SA 1.0 US
corona pool from 64 to 4096 entries.

The main implementation is in:

```text
Client/game_sa/CCoronasSA.cpp
Client/game_sa/CCoronasSA.h
Client/game_sa/CRegisteredCoronaSA.cpp
Client/game_sa/CRegisteredCoronaSA.h
Client/mods/deathmatch/logic/CClientMarker.cpp
```

The reusable in-repository test resource is:

```text
test-resources/corona-limit-test
```

It provides `/coronatest [count]`. The supported scripted maximum is 4094,
because `CCoronasSA::FindFreeCorona` intentionally starts at slot 2.

### Corona patch design

GTA's original array begins at `0xC3E058` and contains 64 structures of `0x3C`
bytes. Neon allocates a zero-initialized static array of 4096 structures and
patches every known GTA SA 1.0 US instruction that directly addresses the old
array, one of its fields, or its end.

The replacement has process lifetime. This is required because GTA can use the
array before or after MTA recreates its C++ wrapper objects. A replacement owned
by a temporary manager instance would leave patched GTA code with dangling
pointers.

Each `CRegisteredCoronaSA` wrapper stores its explicit slot ID. It must not
derive the ID by subtracting the old fixed address after relocation.

GTA initialization and rendering counts are patched to 4096. GTA's native
`RegisterCorona` and `UpdateCoronaCoors` searches deliberately remain limited
to the first 64 slots. Their original bound uses short/immediate instruction
forms that cannot represent 4096 by replacing one byte. Vanilla effects use
those first slots; MTA scripted coronas allocate directly across the relocated
array. Supporting more than 64 native GTA-created coronas would require the
instruction/code-moving work shown by Fastman92, not just another constant.

Fastman92's corresponding Windows x86 implementation is
`OtherLimits::SetCoronaLimit` in:

```text
.../Source files/Modules/OtherLimits.cpp
```

Its pointer list and disassembly comments were used as the address reference.
The readable GTA behavior and layouts are in:

```text
gta-reversed-dryxio/source/game_sa/Coronas.cpp
gta-reversed-dryxio/source/game_sa/Coronas.h
gta-reversed-dryxio/source/game_sa/RegisteredCorona.cpp
gta-reversed-dryxio/source/game_sa/RegisteredCorona.h
```

### Failure that looked unrelated

The first relocation attempt crashed while writing the first executable patch
at `0x6FAACF` (the observed exception was at `0x6773A474`, accessing
`0x006FAACF`). The cause was using `MemPutFast` on protected executable memory.
Use MTA's protection-aware `MemPut` for these writes. A crash during patch
installation can happen before any new array entry is used, so inspect the
faulting write before blaming array layout or loop bounds.

Temporary exception instrumentation was useful for locating that failure, but
was removed from the final implementation.

### Validation already performed

The corona change was built successfully as:

```text
Game SA.vcxproj                 Release|Win32
Client Deathmatch.vcxproj      Release|Win32
```

It was tested in the Parallels VM against the local MTA server. Progressive
tests visibly produced 32, then 62, then 120 coronas as the independent limits
were removed. The final 4096 configuration was also tested in game successfully.

## Implemented reference: distant renderer capacity

The Project2DFX prerequisite patch expands three independent GTA renderer and
streaming capacities without changing the player's far clip, model LOD
distances, or server resource behavior:

```text
Visible entity pointer list       1000 -> 8192
Visible LOD pointer list          1000 -> 8192
Streaming RwObject instance list  2500 -> 30000
```

The authoritative constants are in `Client/sdk/game/Common.h`. GTA's two fixed
visible-pointer arrays are redirected to process-lifetime MTA storage in
`Client/multiplayer_sa/CMultiplayerSA_Rendering.cpp`. An extra, non-counted
sentinel entry is allocated for each list because GTA stores a candidate before
the existing MTA counter hook clamps the count. The known GTA SA 1.0 US pointer
operands are:

```text
Visible LOD list       0x5534F5  0x553923  0x553CB3
Visible entity list    0x553529  0x553944  0x553A53  0x553B03
```

The address inventory and the 30000-entry Project2DFX default come from the
MIT-licensed Open Limit Adjuster. Its implementation is in:

```text
/Users/salimtrouve/Documents/GitHub/III.VC.SA.LimitAdjuster/src/limits/EntityPtrs
```

The streaming list was already expanded by MTA from GTA's original 1000 entries
to 2500, but MTA wrote only the low 16 bits of GTA's 32-bit allocation-size
immediates. `Client/game_sa/CGameSA.cpp` now writes the complete DWORD at
`0x5B8E55` and `0x5B8EB0`, making the 30000-entry allocation (360000 bytes)
well-defined.

Sparse high-water telemetry reports visible entities, visible LODs, and used
streaming RwObject links. `/renderstats` prints both current and session
high-water values to chat and `console.log`; `/renderstats reset` starts a new
measurement window. The reusable, opt-in test resource is:

```text
test-resources/renderer-limit-test
```

Use `/renderlimittest [distance]` to extend far clip and valid model LOD
distances temporarily. `/renderdensitytest [count]` creates an opt-in wedge of
non-colliding test buildings inside the current camera frustum, allowing the
visible-entity list to be tested above its old boundary. `/renderlimitclear`
destroys those buildings and restores the original rendering settings.

The first density test exposed a null-child dereference in
`CQuadTreeNodesSAInterface::RemoveAllItems` while the building pool removed the
world for resizing. GTA's quadtree children are allocated lazily, so a non-leaf
node can legitimately contain null child pointers. The traversal now skips
missing children, matching GTA's other recursive quadtree operations and making
pool resize cleanup safe for partially populated IPL trees.

This patch raises capacity only. Defaults remain visually unchanged until a
resource or player setting requests longer draw distances, so it is a safe
prerequisite for a later native Project2DFX port rather than Project2DFX itself.

### Renderer-capacity validation

The opt-in density test created 1400 buildings in the camera frustum and
recorded a visible-entity high-water of 1033/8192, proving that rendering
continues beyond the old 1000-entry allocation. Cleanup destroyed all 1400 test
buildings and restored the original far clip and model LOD distances. A separate
5000-unit draw-distance run measured 4677 streaming RwObject links, exceeding
MTA's previous 2500-entry allocation. No new crash dump was produced.

The following Release|Win32 projects were built successfully:

```text
Game SA.vcxproj
Multiplayer SA.vcxproj
Client Deathmatch.vcxproj
```

`Client Core.vcxproj` remains independently blocked in the VM copy by the
missing `vendor/discord-rpc/discord/include/discord_rpc.h`; renderer statistics
therefore use the client Lua API and test resource rather than requiring a core
command.

## Project2DFX phase 1: native distant static lights

Neon integrates the distant static-corona part of
`ThirteenAG/III.VC.SA.IV.Project2DFX` directly into the MTA client. It does not
load an ASI plugin or Project2DFX's bundled limit adjuster. The implementation
uses MTA's relocated 4096-entry corona pool and the renderer capacities above,
so corona ownership and lifetime remain under MTA.

The feature is disabled by default. A client resource can enable it, select a
draw distance from 300 to 5000 units, rebuild its world-light cache, and read
current counts through these client Lua functions:

```text
engineSetDistantLightsEnabled
engineSetDistantLightsDrawDistance
engineRebuildDistantLights
engineGetDistantLightStats
```

The opt-in test resource is:

```text
test-resources/project2dfx-test
```

It provides `/project2dfx on [300-5000]`, `/project2dfx off`,
`/project2dfx rebuild`, and `/project2dfxstats`. It also disables the feature
when the resource that enabled it stops.

For repeatable performance comparisons, `/project2dfxprofile [seconds]` runs an
automatic `off`, 2000, 3000, and 5000-unit sequence. Every stage has a five
second warm-up followed by a 5-to-60-second measurement window. It reports FPS,
average/p95/p99/worst frame time, active coronas, and renderer high-water
counters, then restores the previous distant-light state. Keep the camera fixed
for the entire sequence. `/project2dfxbench [off|2000|3000|5000] [seconds]`
runs a single stage, and `/project2dfxbenchcancel` safely stops either test.

Project2DFX's `SALodLights.dat` is installed as
`MTA/data/SALodLights.dat`. MTA resolves each model-name section against GTA's
model table, scans the native building and dummy pools, transforms the local
light offsets into world coordinates, and registers the closest eligible
coronas every frame. The data and the adapted behavior retain Project2DFX's MIT
license in `MTA/data/Project2DFX-LICENSE.txt`. If the DAT is absent, the code
falls back to GTA's embedded model 2DFX effects so the feature fails soft.

Phase 1 intentionally covers night-time static coronas only. Project2DFX
searchlight cones, distant cars, static shadows, live GTA traffic-controller
state, wet-weather behavior, and IDE/far-clip tweaks are separate features and
are not silently enabled here. DAT rows marked as traffic lights use
Project2DFX's directional red/yellow/green clock phases so opposing colors do
not render together. The DAT's corona rows are retained when a row also asks
for a searchlight, but the cone itself is omitted.

### Project2DFX phase-1 validation

`Game SA.vcxproj` built successfully as `Release|Win32`. In the Parallels VM,
the native-effects fallback found only 17 usable lights, confirming that GTA's
embedded effects are not a substitute for the Project2DFX data. With the DAT
installed, the same world scan produced 1843 instantiated definitions and 1317
active coronas at night with a 2000-unit distance. The client remained running
and no crash artifact newer than the earlier quadtree failure was produced. An
off/on lifecycle test released every active corona, then rebuilt 2083
definitions with 1533 active at 3000 units as additional world objects streamed
in. Visual testing caught simultaneous red and green distant traffic lights;
applying Project2DFX's directional clock phases corrected the intersections
while preserving the intended corona size, intensity, and color falloff.

## Workflow for the next limit

1. Define precisely what is being raised: created, streamed, updated, rendered,
   loaded from a map, or synchronized. Record the vanilla and desired values.
2. Search MTA with `rg` for the constant, manager, API name, allocation type,
   and nearby error messages. Check both client and server code.
3. Read the corresponding GTA reversed class and all functions that iterate or
   index the relevant storage.
4. Search Fastman92's `Modules` and `GameStructures` trees for the same limit.
   Treat its implementation as an address/checklist reference, not as code that
   must be imported wholesale.
5. Classify every Fastman92 patch: start pointer, end pointer, field pointer,
   allocation size, loop count, immediate comparison, code cave, or unrelated
   platform/version support.
6. Inspect `librw` only when the data crosses into RenderWare or when GTA
   reversed does not explain the renderer-side restriction.
7. Implement the smallest coherent MTA-integrated patch for GTA SA 1.0 US.
   Preserve native behavior where possible and document deliberately unchanged
   sub-limits.
8. Add a deterministic test resource or command that can test below, at, and
   above the old boundary. Test progressive counts so separate ceilings become
   visible.
9. Build and run using the canonical/VM workflow in `AGENTS.md`. Test resource
   restart, reconnect, respawn, cleanup, and a short gameplay session as
   applicable.
10. Verify structure sizes and field offsets with `static_assert`, format C++
    using the repository tool, review the diff, then write a commit message that
    includes motivation, design decisions, and exact testing.

## Safety and maintainability rules

- Current absolute addresses are GTA SA 1.0 US-specific. Do not claim support
  for another executable version without signatures or a separately verified
  address table.
- Patch every direct reference before allowing GTA to iterate a relocated
  array. A missed field reference can silently corrupt the old array or crash
  only on a rare render path.
- Patch instruction operands at the correct operand address and width. Copying
  the instruction address instead of the immediate/pointer offset corrupts code.
- Use one authoritative constant per implemented limit and keep MTA wrapper
  storage, GTA counts, API validation, and test-resource bounds consistent.
- Prefer process-lifetime storage for pointers permanently installed into GTA.
- Keep original native reservations and sentinel rules unless their full role is
  understood.
- Test old boundary minus one, old boundary, old boundary plus one, the target,
  and cleanup. A visually successful low count does not validate an expanded
  pool.
- Large limits have performance costs even when memory is safe. Look for linear
  searches and full-array per-frame loops; 4096 coronas are manageable, but the
  same strategy may not scale to every pool.
- Do not copy all of Fastman92 into MTA. Its size comes largely from supporting
  many independent limits, games, executable versions, and platforms. Port the
  verified subset required for one MTA feature and retain provenance in comments
  and this guide.

## Implemented reference: extended map/world bounds

Extended MTA World Phase 1 expands the usable XY domain for MTA-created
buildings and synchronized entities. It has been tested
with San Andreas at its original coordinates and with custom Perry Island
terrain around X=9000.

This is not one constant. The implementation changes four connected layers:

1. GTA's native main-building and LOD spatial grids.
2. GTA code that computes, clamps, indexes, scans, renders, collides with, and
   removes entries from those grids.
3. MTA client/server `createBuilding` validation and MTA code that scans the
   active GTA grid.
4. Versioned client/server network encodings whose old quantized XY range ended
   at approximately +/-8192.

The implemented geometry is:

```text
World size                 20000 units
Supported XY domain        [-10000, +9999]
Main sectors               400 x 400, 50 units each
Main-sector allocation     1,280,000 bytes (160000 * 8)
LOD sectors                100 x 100, 200 units each
LOD-sector allocation      40,000 bytes (10000 * 4)
Repeat sectors             unchanged at 16 x 16
```

The positive script/API bound is +9999 rather than +10000. GTA converts a
coordinate with `floor(coordinate / sectorSize + halfGridSize)`, so +10000 is
the first coordinate outside a 400-wide grid; -10000 is still inside it.

The main implementation is in:

```text
Client/game_sa/CWorldSectorLimits.cpp
Client/game_sa/CWorldSectorLimits.h
Client/game_sa/CWorldSectorCodeMover.cpp
Client/game_sa/CWorldSectorCodeMover.h
Client/game_sa/CWorldSectorManifest.inc
Client/game_sa/CWorldSectorDirectPatchManifest.inc
Client/game_sa/CWorldSA.cpp
```

The manifests can be regenerated from the local Fastman92 source with:

```text
utils/extended-world/extract_world_sector_manifest.py
```

### GTA sector relocation

The canonical GTA SA 1.0 US HOODLUM reference is
`MapLimits::PatchWorldSectors_GTA_SA_PC_1_0_HOODLUM()` in Fastman92
`Modules/MapLimits.cpp`. Its full patch is needed because GTA embeds the old
grid addresses, dimensions, strides, half sizes, and coordinate constants in
hundreds of instructions. Patching only the obvious array pointers or LOS hooks
either corrupts memory or leaves independent paths using the vanilla grid.

Neon allocates zero-initialized, process-lifetime replacements with
`VirtualAlloc`: 160000 main sectors and 10000 LOD sectors. The vanilla 120x120
and 30x30 lists are copied into the centered portion of the new grids so San
Andreas remains at the same coordinates. The 16x16 repeat-sector array used by
dynamic vehicles, peds, and objects keeps its original dimensions and storage;
the Fastman92 references to it are retained so the whole world-sector patch
remains coherent.

The generated normal-executable manifests currently install:

```text
Moved instruction sites       392
Direct pointer operands        841
Direct integer operands         35
Direct float operands           17
Redirected functions/hooks      12
Total direct patch entries     905
```

Fastman92 lists 397 `CCodeMover` calls. Five target compact-executable mirror
addresses above `0x01000000`; MTA runs the normal HOODLUM image, so the generator
intentionally excludes those five and emits 392 moved sites. Of the 12 redirects,
11 come from Fastman92 and one is Neon's additional `CWorld::GetSector` fix
described below.

All direct patch operands and relocation sources are read and prepared before
the first executable write. The captured bytes are checked again immediately
before commit, so an unreadable address or an instruction changed concurrently
aborts before mutation. The implementation then writes a 150000-byte executable
code arena, redirects the 392 original instruction sites to it, and applies the
direct patches.

This check must not be mistaken for executable-version verification: the
current manifests do not contain golden original bytes, and this installer does
not calculate a GTA executable signature or CRC. It targets GTA SA 1.0 US
HOODLUM addresses and currently assumes MTA has supplied the compatible image.
Adding an explicit version/signature gate remains necessary before claiming
that the patch safely rejects every unsupported executable. The installation is
also not transactional or hot-swappable: there is no runtime grid resizing and
no rollback mechanism after writes begin. Never switch grids while sector lists
contain entities.

### The Fastman92 `CCodeMover` port

There is an important provenance detail. The local Fastman92 release contains
the complete `MapLimits.cpp` call sites, addresses, comments, and encoded
`CCodeMover::FixOnAddress` bytecode, but its referenced
`Source files/Core/CCodeMover.h` and implementation are absent. Therefore Neon
did not copy or compile Fastman92's private/general code mover.

`CWorldSectorCodeMover` is a clean, minimal Neon implementation reconstructed
from the observable bytecode contract used by this one WorldSectors function.
It deliberately supports only the five opcodes present in that function:

```text
0x00  end of recipe
0x01  emit literal bytes
0x02  copy bytes from a GTA address
0x03  emit a relocated 32-bit relative address
0x05  emit a named variable using the requested width
```

For each recipe it builds replacement machine code in Neon's executable arena,
appends the continuation jump when required, and replaces the original GTA
instruction range with a relative jump plus NOP padding. Relative displacements
are recalculated for the arena's actual runtime address. Source bytes captured
during preparation are checked again before commit.

`extract_world_sector_manifest.py --target world` parses only the named
HOODLUM WorldSectors function, requires exactly 397 calls and exactly the
supported opcode set, and emits deterministic C++ manifests. It also extracts
Fastman92's direct
`PatchPointer`, `PatchUINT32`, `PatchFloat`, and `RedirectCode` calls. This keeps
the large address list auditable and reproducible without importing the entire
Limit Adjuster. Fastman92's source manifest is MIT-licensed and the generated
files retain that attribution.

Do not reuse `CWorldSectorCodeMover` for another Fastman92 module merely because
its name is similar. Other modules use additional bytecode opcodes and may rely
on behavior this minimal interpreter does not implement. Audit and extend the
format explicitly for each new use.

### The missing renderer path found during testing

The first full sector patch produced San Andreas LODs but not its normal HD
buildings. Runtime inspection showed that the new 400x400 and 100x100 grids were
populated correctly. The remaining path was GTA's out-of-line
`CWorld::GetSector` at `0x407260`, called by `CRenderer::SetupScanLists`.

That helper was not included in Fastman92's WorldSectors manifest: it still
clamped X and Y to 119, multiplied Y by the old stride 120, and returned an
entry from `0xB7D0B8`. Neon adds a twelfth direct redirect to
`GetExtendedWorldSector`, which clamps to 0..399 and indexes the relocated
400-wide grid. This restored normal San Andreas buildings and was confirmed in
game. Keep this Neon-specific entry when regenerating the direct manifest.

### Dynamic entities, buildings, and MTA-side scans

`CWorldSA::InstallHooks` installs the full sector patch. A smaller
`CEntity::Add`/`Remove` bounds patch remains only as a fallback if full sector
installation fails; the successful full patch already includes the relevant
Fastman92 world-bound changes.

MTA code that formerly assumed `ARRAY_StreamSectors` was always the vanilla
120x120 array now asks for the active grid and dimension. This affects stale
building-list cleanup and IPL restream scans in `CBuildingsPoolSA` and
`CModelInfoSA`. The client and server building managers accept XY in
`[-10000,+9999]` instead of approximately `[-3000,+3000]`.

One test exposed a separate MTA wrapper issue: physical collision and LOS hit a
script-created building at X=9500, but the LOS result returned no MTA element.
`CPoolsSA::GetEntity` mapped peds, vehicles, and objects but not the building
pool. `CBuildingsPoolSA::GetBuilding` and the corresponding lookup in
`CPoolsSA::GetEntity` now return the building wrapper, so LOS reports the
correct element. This is not a coordinate-limit patch itself, but it is required
for correct extended-world query behavior.

### Network position range

MTA-created vehicles and other dynamic entities already worked beyond the
vanilla building grid, but several network formats clipped XY near +/-8192.
Neon adds the version gate `ExtendedWorldPositions` and changes only new-format
connections:

```text
Quantized XY             SFloatSync<14,10> -> SFloatSync<15,10>
Low-precision XY bound   +/-8192 -> +/-10000
Absolute camera range    +/-8192 -> +/-16384
```

Legacy bitstream versions retain their old field widths and bounds. Both client
and server must support the new bitstream version to synchronize the extended
range. Unit tests cover new and legacy round trips, boundary positions,
low-precision positions, and camera serialization.

#### Recipient-versioned server RPC payloads

`CPlayerManager` groups recipients by bitstream version and asks each packet to
serialize into that group's destination stream. A packet containing a
versioned position must therefore retain semantic values until `Write`; it must
not serialize them first into a default/version-zero `CBitStream` and then copy
the raw bits into the destination. Such a copy keeps the legacy 14-bit XY
layout while a current client reads the 15-bit layout, shifting every trailing
field.

This affected the `moveObject`, `setColPolygonPointPosition`, and both
`addColPolygonPoint` server RPC paths. `CObjectMoveRPCPacket` and
`CColPolygonPointRPCPacket` now write their copied animation or point directly
into the recipient-versioned stream. The RPC IDs, element IDs, reliability
flags, optional polygon index, and legacy layout remain unchanged.

`Tests/client/SyncMovement_Tests.cpp` protects the legacy/current wire widths
and the raw-copy failure mode. The live `world-sync-regression-test` resource
checks all four affected call paths. On 2026-07-17 the pre-fix build reproduced
all four failures; the fixed build then passed at ordinary San Andreas
coordinates, X=+9500, and X=-9990. In each fixed `MOVE` run the client observed
35-36 intermediate samples, no regression or overshoot, and zero final error.

### Validation performed

The patch was built as `Game SA.vcxproj Release|Win32` and exercised against the
local custom server. `/bwtest 9500` confirmed:

```text
createBuilding   OK
teleport         OK at approximately (9499.9, 49.5, 20.1)
line of sight    OK, including the returned building element
vehicle sync     OK
vehicle camera   OK
```

The original San Andreas HD buildings, LODs, rendering, and collision remained
visible after the `CWorld::GetSector` redirect. A generated Perry Island slice
was then loaded around X=9000; terrain, roads, collisions, and LODs were
confirmed in game. The reusable resources and generator are:

```text
test-resources/extended-world-test
test-resources/perry-island-slice
utils/extended-world/build_perry_slice.py
```

This validates the main Phase 1 path, not every boundary or lifecycle case.
Before calling the implementation production-complete, still test -10000,
-9999, +9999, sector edges, building removal/recreation, resource restart,
reconnect, world reset, shutdown, high/low linking at several distances, and a
larger Perry load while monitoring building-pool and pointer-node usage.

## Implemented reference: extended custom water bounds

The custom-water patch extends `createWater` from the vanilla approximately
`[-3000,+3000]` XY domain to `[-10000,+9999]`, matching the Phase 1 world
domain. It deliberately does not replace or enlarge GTA's infinite outside-
world ocean: empty extended-world space still inherits that ocean and its
independently configurable level.

The implemented water geometry is:

```text
Supported custom-water XY     [-10000, +9999]
Water blocks                  40 x 40, 500 units each
Water-zone entries            1600 (formerly 12 x 12 = 144)
Vanilla ocean renderer grid   unchanged at 12 x 12
Polygon/vertex pool counts    unchanged
Network encoding              unchanged (signed 16-bit XY already sufficient)
```

The client implementation is in `Client/game_sa/CWaterManagerSA.cpp` and
`CWaterManagerSA.h`; server-side API validation is in
`Server/mods/deathmatch/logic/CWater.cpp`. The reusable in-repository test
resource is `test-resources/extended-water-test`.

### Water relocation design

The GTA water manager indexes custom polygons in fixed 500-unit blocks. Neon
relocates the 144-entry block array to zero-initialized process-lifetime storage
with 1600 entries, then copies the vanilla 12x12 block data into the centered
portion of the new 40x40 grid. MTA's zone wrappers, polygon insertion/removal,
index rebuild, point lookup, and line scans all use the relocated array and the
40-wide stride.

Fastman92's canonical address reference is
`MapLimits::PatchWaterMapSize_GTA_SA_PC_1_0_HOODLUM()` in
`Modules/MapLimits.cpp`. The local Fastman92 tree again does not contain the
general `CCodeMover` implementation, so Neon reuses its audited minimal
`CWorldSectorCodeMover` only after verifying that this water function uses a
subset of the already-supported recipe opcodes. The deterministic 14-recipe
manifest is generated with:

```text
python3 utils/extended-world/extract_world_sector_manifest.py \
  ".../Modules/MapLimits.cpp" Client/game_sa/CWaterMapManifest.inc \
  --format cpp --target water
```

Most Fastman92 water-map constants widen the custom polygon index. Two native
functions need different treatment in MTA:

- `CWaterLevel::BlockHit` is replaced so extended block coordinates can mark
  custom polygons while outside-ocean render requests are translated back to
  GTA's original 12x12 coordinate system.
- `CWaterLevel::GetWaterLevelNoWaves` searches extended custom water first,
  then falls back to GTA's infinite ocean outside +/-3000 at the current native
  outside-world water level.

MTA's `TestLineAgainstWater` follows the same priority: custom polygons are
tested first, then the original infinite-ocean plane. This distinction matters
for Perry Island, which intentionally uses the base game's ocean rather than a
large scripted water polygon.

The patch keeps the existing water polygon, vertex, triangle, quad, and
zone-poly pool capacities. It lifts where custom water may be placed, not how
many water polygons may exist. Absolute executable addresses remain GTA SA 1.0
US HOODLUM-specific and have the same version-gating caveat as the world-sector
patch.

### Water validation performed

`Game SA.vcxproj Release|Win32` and `Deathmatch.vcxproj Release|x64` built
successfully. In-game tests covered the old boundary and extended positions at
X=2990, X=9500, X=9990, and X=-9990. At extended positions they confirmed:

```text
createWater             OK at Z=8
getWaterLevel           OK at Z=8
line of sight           OK at Z=8
Dinghy buoyancy          OK
destroy/recreate cycle  OK
```

Before the patch, the same X=9500 test correctly failed to create custom water
and observed only the base ocean at Z=0. Perry's existing ocean remained
available after the patch.

### Configurable procedural seabed boundary

GTA renders the apparent infinite seabed separately from water polygons. It is
immediate-mode `seabd32` geometry generated by `RenderSeaBedSegment` and
`RenderDetailedSeaBedSegment`, not map objects that a resource can destroy.
Neon filters the four calls to those functions in GTA's water renderer while
leaving the separate infinite-ocean render pass and water physics untouched.

Servers can control the square outer boundary with:

```lua
setWorldSeaBedOuterBoundary(7500) -- accepted range: 3000..10000
getWorldSeaBedOuterBoundary()     -- applied boundary, or false when unlimited
resetWorldSeaBedOuterBoundary()   -- restore GTA's unlimited default
```

Requested values are rounded up to GTA's 500-unit seabed blocks. The setting
is stored server-side, broadcast to connected clients, and sent to joining
clients through a dedicated RPC so the existing map-info packet layout remains
stable. The default is unlimited for backward compatibility.

The reusable `test-resources/seabed-boundary-test` resource validates that the
seabed is visible inside the selected boundary and omitted outside it. Tests at
X=9500 with boundaries 10000 and 7500 also confirmed that the native ocean,
`getWaterLevel`, and `testLineAgainstWater` remain functional at Z=0.

### What the world and water patches do not lift

The supported XY range must not be described as every GTA map subsystem being
extended. This patch does not yet relocate or extend:

- native IPL loading boundaries or GTA's original map files;
- radar texture coverage;
- paths, traffic, zones, population, or ambient spawning;
- native building/object/model pool counts;
- the dimensions of the 16x16 repeat-sector ring.

Custom Lua-created geometry, vehicles, and objects can operate in the extended
area when their relevant MTA and network paths permit it, but empty space stays
empty until a resource creates terrain there.

### Changing the target again

The choice of +/-10000 is the smallest round target that contains the intended
Perry placement while keeping memory, patch values, and testing manageable. It
is not a fundamental maximum. A +/-20000 target would require 800x800 main
sectors and 200x200 LOD sectors: approximately 5.12 MB and 160 KB respectively,
plus adjusted constants, API validation, network bounds, and a new full test
pass. Quantized XY would need enough signed integer bits for that range; simply
changing `WORLD_MAP_SIZE` without updating every connected layer is invalid.

## Extended-world MTA pickup positions

MTA pickup elements now keep their visual object at the authoritative floating-
point position throughout Neon's supported world. GTA's native `CPickup`
structure remains unchanged at 0x20 bytes and the independent 620-entry pickup
capacity is not raised.

### Limit and affected layers

GTA stores each pickup coordinate as a signed 16-bit integer in eighth-units.
The exact native range is therefore `[-4096, +4095.875]` independently on X,
Y, and Z. The local GTA SA 1.0 US executable confirms that `CPickup::SetPosn`
at `0x454960` writes three words at offsets 0x10, 0x12, and 0x14, while
`CPickup::GiveUsAPickUpObject` at `0x4567E0` expands those words and initially
places the associated `CObject` from them.

The server element, client pickup streamer, MTA collision sphere, and entity-add
packet already retain the requested position. `SPositionSync` uses Neon's
version-gated 15-integer-bit XY encoding for capable peers and the original
14-integer-bit encoding for legacy peers. No pickup-specific network format or
compatibility change is needed. The visible wrap beginning at +4096 was solely
the native wrapper/object-creation boundary.

### MTA-integrated design

`CPickupSA` stores an additional MTA-side `CVector` beside its pointer to the
unchanged native slot. `SetPosition` updates that authoritative vector and
saturates the native eighth-unit fields, avoiding the undefined out-of-range
float-to-short conversion that previously wrapped on the target compiler.
In-range conversion and GTA's original structure layout remain unchanged.

After GTA creates an associated object, `GiveUsAPickUpObject` immediately moves
the `CObjectSA` wrapper to the authoritative vector when any component exceeds
the native range. In-range pickups retain GTA's original eighth-unit placement
and therefore preserve default San Andreas behavior. Repositioning happens before
`CPickupsSA::CreatePickup` adds the object to `CWorld`, so the object is linked
to the sector for its real position rather than to the saturated placeholder.
The same path covers a later object retry or recreation.

This deliberately does not relocate `CPickups::aPickUps` or patch every native
coordinate access. MTA disables GTA's `CPickups::Update` for the multiplayer
session and uses `CClientPickup`'s own collision sphere plus server-confirmed
hit handling. GTA therefore neither performs native collection nor resets the
visual object's transform from the compressed fields. Fastman92's broader IPL
pickup patch instead expands and relocates the structure and patches dozens of
native iteration and coordinate sites; that is appropriate when native GTA
pickup processing, save/load, or a raised pool capacity is required, but is
unnecessary risk for MTA-created pickups under the existing disabled-processing
contract.

### Reproducible validation

The opt-in resource is `test-resources/pickup-position-test`. It covers the
positive and negative native boundaries and extended-world positions with:

```text
/pickuppostest [x]                 create at a listed boundary
/pickupposmove [x]                 move the active pickup (client recreation)
/pickupposrecreate                 destroy and recreate the server elements
/pickupposstream                   force a player-away/player-back stream cycle
/pickupposcontext [dimension] [interior]
/pickupposwalk                     unfreeze nearby for hit/leave testing
/pickupposnative                   inspect the saturated native placeholder
/pickupposback                     clean up and restore player state
```

Recommended X values are `-9999`, `-8192`, `-4097`, `-4096`, `4095`, `4096`,
`8192`, `9500`, and `9999`. At every value the pickup core must remain inside
the red reference cylinder. For extended values, `/pickupposnative` must show
no duplicate or misplaced visual. Use `/pickupposwalk`, then walk through the
pickup to observe the server-side `onPickupHit` and `onPickupLeave` reports. Repeat after resource
restart and reconnect, and check crash artifacts before and after the run.

## Extended 3D markers and checkpoints

Neon expands the GTA storage used by scripted MTA cylinders, arrows,
checkpoints, and rings while retaining the existing global MTA marker-streaming
budget:

```text
GTA 3D markers             32 -> 4096 (0xA0 bytes each)
GTA checkpoints            32 -> 4096 (0x38 bytes each)
GTA direction arrows        5 -> 4096 (0x30 bytes each)
MTA streamed markers             4096 (unchanged by this patch)
```

The three replacements consume 1,081,344 bytes in total. They have process
lifetime because permanent GTA code pointers must remain valid across resource
stops, reconnects, marker reinitialization, and recreation of MTA wrappers.
The implementation is in `C3DMarkersSA.cpp`, `CCheckpointsSA.cpp`, and their
interface headers. Structure sizes and the fields used by native assembly are
protected with `static_assert` checks.

### Independent limits and marker-type interactions

MTA coronas use the separately relocated corona pool. MTA cylinders and arrows
call `C3dMarkers::PlaceMarker` directly every pulse. Checkpoints and rings first
occupy `CCheckpoint` entries, then `CCheckpoint::Render` calls
`C3dMarkers::PlaceMarker` every frame, so they also consume the 3D marker pool.
A normal checkpoint with an arrow icon additionally calls
`C3dMarkers::DirectionArrowSet`; GTA originally kept only five transient
direction arrows. Raising only either of the two visible 32-entry wrapper
constants would therefore leave independent 3D-marker and direction-arrow
ceilings in place.

The MTA streamer still applies one 4096-entry budget to all marker types. The
native pools use the same capacity so a worst-case homogeneous set, or any
mixture selected by the distance-sorted streamer, cannot exceed its specialized
storage solely because coronas previously raised the global budget. The old
five-entry `ms_user3dMarkers` save-game array is unrelated to MTA marker
elements and remains unchanged.

### Relocation and address inventory

The GTA SA 1.0 US HOODLUM arrays begin at `0xC7DD58` (3D markers),
`0xC7F158` (checkpoints), and `0xC802E8` (direction arrows). Neon initializes
the additional slots with GTA's native sentinel type 257 and default pulse,
size, color, map-height, and rotation values, then copies the already
initialized vanilla 32/32/5 entries before installing executable patches.

The 3D-marker operands are:

```text
Shutdown / update       0x722714 0x722756 0x7227BE 0x7227F9
Init / render           0x724E64 0x724ED9 0x72506D 0x7250FF
PlaceMarker             0x72518B 0x7251A1 0x7251DE 0x7251EB
                        0x7251FE 0x72520E 0x725234 0x725480
```

`PlaceMarker` has an eight-entry unrolled replacement scan. The capacity is
therefore deliberately a multiple of eight, and a compile-time assertion
protects that constraint.

The checkpoint operands are:

```text
Init                    0x722881 0x7228E1
UpdatePos               0x722907 0x72291A 0x72292C 0x722935
                        0x72293C 0x722948 0x722951 0x722961
SetHeading / update     0x722977 0x722989 0x7229A3 0x7229D7 0x722C28
PlaceMarker             0x722C82 0x722CA8 0x722CBC 0x722D7D
                        0x722D8D 0x722D9A 0x722DA7 0x722EF0
Delete / render         0x722FCB 0x722FEF 0x726062 0x726079
```

The direction-arrow operands are:

```text
Init / find / set       0x721101 0x72110C 0x721123 0x721132
                        0x721143 0x721151 0x72117B 0x721181
                        0x72118B 0x721195 0x72119F 0x7211A9
                        0x7211B3 0x7211BD 0x7211C7 0x7211D1
                        0x7211D7 0x7211DD
Draw / global reset     0x721218 0x7215F0 0x72691E 0x726928
```

This inventory was derived from the local GTA executable with SHA-256
`72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`
and checked against the corresponding `gta-reversed` functions. The local Open
Limit Adjuster and fastman92 trees do not implement these three limits, so they
provided no address list for this patch.

Two apparent references are intentionally excluded. `0x855321` and `0x856BDC`
are CRT constructor/destructor calls for the original 32-entry 3D-marker array,
not runtime iteration paths. Addresses at and after `0xC803D8` also hold
unrelated renderer globals immediately following the five direction arrows;
patching every occurrence of the old end address would corrupt those globals.

### Telemetry and reproducible test

The client-only `getMarkerLimitStats` function returns the current MTA streamed
marker count, allocated native 3D markers, active checkpoints, and all relevant
capacities. The opt-in resource is:

```text
test-resources/marker-limit-test
```

It provides `/markerlimittest [type] [count]`, `/markerlimitstats`, and
`/markerlimitclear`. Supported types are `cylinder`, `arrow`, `checkpoint`,
`checkpoint-arrow`, `ring`, and `mixed`. Boundary runs should use 31, 32, 33,
then 128 entries for every type before attempting higher performance tests.

### Build and validation status

The following projects build successfully as `Release|Win32` in the VM-local
copy:

```text
Game SA.vcxproj
Client Deathmatch.vcxproj
```

The address operands were also checked directly against the reference
executable before the build. Manual in-game tests crossed the old boundary with
31 and 32 cylinders, then exercised 128 cylinders, checkpoints, checkpoint
arrows, arrows, rings, and mixed markers. Once the distance streamer settled,
the mixed test reported 128 allocated 3D markers and 76 active checkpoints,
which is the expected three checkpoint-backed types out of the five-type test
cycle. Repeating the same test after reconnect produced the same 128/76 result.

Repeated `/markerlimitclear` runs destroyed all 128 test elements and returned
the native 3D-marker and checkpoint counts to zero. The global streamed-marker
count correctly included unrelated active marker resources: the post-reconnect
mixed run reported 248 streamed entries because the 120-corona test resource
was also active. Early automatic samples under-counted newly created markers
because the original test delay was only 100 ms; the resource now waits two
seconds for the streamer rather than describing that sample as a two-frame
measurement.

No marker-related warning or error appeared in the client or server logs, and
no new crash dump was created after the pre-test artifact baseline of
2026-07-12 15:45:26. The only current-session warnings were unrelated existing
test configuration issues: a duplicate `gp` command and the unset server owner
email. Resource hot-restart with active markers and marker-model
reinitialization remain useful follow-up coverage; creation, old-limit
crossing, clear/recreate, reconnect, and normal mixed rendering are validated.

The patch changes capacity, not recommended density. GTA performs linear marker
searches and allocates a RenderWare atomic for each active 3D marker, so very
large visible sets can be CPU/GPU expensive even though their storage is safe.
The marker streamer also keeps its 600-unit default range. Absolute addresses
remain GTA SA 1.0 US-specific and have the same executable-version-gating caveat
as the other Neon limit patches.

## Editable native CULL zones

Neon relocates GTA's three native CULL arrays and exposes their loaded IPL
entries to client Lua. The implemented capacities are:

```text
Attribute zones       1300 -> 4096 (0x12 bytes each)
Tunnel zones            40 -> 256  (0x12 bytes each)
Mirror zones            72 -> 256  (0x18 bytes each)
```

The implementation is in `Client/game_sa/CCullZonesSA.cpp`. It patches the 37
GTA SA 1.0 US operands used by the native lookup and loader functions, based on
Fastman92's `IPLsectionLimits::SetCull*AttributeZones` address inventory. The
native count globals remain authoritative because GTA's lookup loops already
use them rather than fixed immediate limits.

On first Lua access, Neon adopts every loaded vanilla entry into a stable-ID
catalog. Native arrays remain packed, but Lua IDs do not move when another zone
is removed. Custom entries belong to the resource that created them. Stopping
that resource deletes its custom entries and restores any vanilla entries it
edited or disabled. A vanilla entry can be claimed by only one resource at a
time, preventing two resources from leaving conflicting restoration state.

The client functions are:

```text
engineGetCullZones([type])
engineCreateCullZone(type, x, y, z, width, depth, height, flags,
                     [rotation=0, mirrorV=0, normalX=0, normalY=0, normalZ=1])
engineSetCullZone(id, type, x, y, z, width, depth, height, flags,
                  [rotation=0, mirrorV=0, normalX=0, normalY=0, normalZ=1])
engineSetCullZoneEnabled(id, enabled)
engineRemoveCullZone(id)
engineRestoreCullZone(id)
```

Types are `attribute`, `tunnel`, and `mirror`. `engineGetCullZones` returns the
stable ID, type, compressed native geometry expressed as center/size/rotation,
raw flag mask, enabled/original state, and mirror-plane fields. Raw flags are
retained because stock data uses behavior bits not fully named by the current
`gta-reversed` enum.

Geometry intentionally retains GTA's signed 16-bit whole-unit representation.
The supported extended-world range fits it, but fractional inputs are truncated
and coordinates or derived vector components outside `[-32768,32767]` are
rejected. Attribute and tunnel overlap combines flags; mirror lookup preserves
native first-match ordering.

MTA's minimize-time mirror toggle now delegates to the CULL manager rather than
temporarily saving the count itself. This keeps mirror additions and deletions
made while minimized from being lost when rendering resumes.

Two runtime integration details are required for the API to work reliably:

- MTA can disable mirrors before GTA has loaded the IPL CULL sections. The
  mirror toggle therefore preserves the native count without initializing the
  stable-ID catalog. Catalog adoption is deferred until the loaded vanilla
  entries are available; otherwise the first early toggle permanently adopts
  an empty catalog.
- MTA's multiplayer processing path does not reach GTA's later
  `CCullZones::Update` call before weather processing. The pre-weather handler
  explicitly refreshes the current player and camera attribute masks through
  `UpdateCullZoneFlags`. Without this update, custom entries are present in the
  relocated array but effects such as `NO_RAIN` and `CAM_CLOSE_IN` do not
  activate.

The opt-in resource `test-resources/cull-zone-test` provides CRUD, original-zone
restore, resource cleanup, old-capacity boundary commands, and a 3D visualizer.
The visualizer can draw custom zones alone or nearby custom and vanilla zones;
it labels their stable IDs, types, dimensions, enabled/original state, raw and
decoded flags, and highlights the zone containing the player.

`Game SA`, `Multiplayer SA`, and `Client Deathmatch` build successfully as
`Release|Win32`. Live testing against MTA:SA Server 1.7 on Windows reported
1180 attribute, 36 tunnel, and 65 mirror vanilla zones. The next custom entry
received ID 1282, matching the 1281 adopted originals. The following behavior
was verified in-game:

- create, move/edit, disable, re-enable, and delete a custom attribute zone;
- automatic custom-zone cleanup across resource restarts;
- `NO_RAIN` with rainy weather and `CAM_CLOSE_IN` while inside their volumes;
- custom and nearby-vanilla 3D visualization with decoded flags; and
- reconnecting after rebuilding and restarting the client resource.

Dedicated follow-up validation is still appropriate for the 39/40/41 tunnel
and 71/72/73 mirror capacity boundaries, visible mirror-plane behavior,
vanilla disable/restore, and mirror off/on transitions. These remain test cases,
not confirmed runtime results.

## FileID relocation prerequisite (no native patch yet)

Neon now captures GTA SA's stock FileID layout through
`Client/game_sa/CFileIDRuntimeSA.*` before any native-world feature can mutate
the executable. Ten manifested HOODLUM instructions supply the TXD, COL, IPL,
DAT, IFP, RRR and SCM bases, the streaming-table begin/end, and the global
model-pointer array. Exact bytes, operands, PE32 headers, partition ordering,
sentinels, table stride/count and mapped readability are validated read-only.

This removes MTA-owned static captures and direct `20000/25000` partition
arithmetic from Game SA, Multiplayer SA, Client Core and Client Deathmatch,
including naked ASM hooks. It deliberately leaves the stock partition and
26,316-entry table untouched. The successful diagnostic ends in
`nativeWrites=no`; the off-game mirror validator is
`utils/extended-world/validate_native_file_id_runtime.py`.

The future 44,325-entry stock-only checkpoint must update this single runtime
layout after its transactional relocation. It must not introduce new private
copies of table pointers or partition constants. DAT, streamed SCM, IFP/RRR and
paths/nodes are represented only to preserve the contiguous namespace and
remain outside the current activation scope.

The user-run baseline gate completed on 2026-07-18 with format-1 ticket
`7a1a461a`. Both the initial stock process and the authorized replacement
process captured the exact stock partitions and 26,316-entry table with
`nativeWrites=no`. Bullworth then registered archive 6, 952 models, 166 TXDs,
collision slot 252 and seven IPL slots. An exact reconnect preserved the
process lease, the same ticket, the 4008-block streaming floor and unchanged
model-store occupancy; `/nativebw` remained usable afterwards. The client and
server logs contained no FileID, preflight, capacity, exception or fatal
diagnostic, and no new crash dump was created. The expected duplicate transport
refusal retained `existing-native-world=preserved`.

## Candidate areas for future work

Radar, paths, zones/population, native IPL support, object/model pools,
streaming lists, and renderer effects each need their own investigation. Locate
the corresponding Fastman92 module first, trace the matching GTA reversed
functions, and inspect `librw` only for RenderWare-facing behavior. Do not
assume these candidates have the same risk or implementation size as the corona
or world-sector patches.

For the existing WOSA Perry resource, translating the main `novailhb` zone by
+8409.370117 places `perry_land_22` at X=9000 and keeps the whole zone within
approximately X=7842..9517. This is a safer first integration than translating
the complete resource by +9000, whose original maximum X would exceed the new
world boundary.
