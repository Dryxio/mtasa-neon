# Change pedestrian paths on custom maps

This checkpoint adds a client resource API for moving, adding and disabling
pedestrian nodes in GTA's exterior path network. Native wandering and
`setPedNavigateTo` read the modified network. Vehicle nodes and their links are
not editable through this format.

```lua
local ok, revisionOrReason = engineLoadPedPathPatch("detour.npp")
local revision, reason = engineGetPedPathPatchRevision()
local graphRevision, reason = engineGetPedPathGraphRevision()
local removed = engineUnloadPedPathPatch()
```

Declare the `.npp` file in `meta.xml`. The loading resource owns one patch;
resource stop restores its base graph automatically. All four functions are
client-side. Paths must belong to the calling resource. Load returns
`true, sha256` or `false, reason`; revision returns `sha256` or `false, reason`;
unload returns whether a patch was removed. SHA-256 covers the exact compiled
patch, including its base graph fingerprints. The graph query is accessible from any resource and hashes the sorted set of
active patch revisions. It returns `false, reason` if any lease is incompatible
or no patch is active. Compare this aggregate revision, not just one resource's
file revision, before selecting an NPC syncer. A resource must request fresh
revisions through its server before granting AI authority; streaming can invalidate
a lease after an earlier readiness report. The engine does not automatically
modify existing traffic resources' syncer policy.

## Compile an edit

`compile_patch.py` consumes the author's verified `NODES0.DAT` through
`NODES63.DAT` and a JSON edit specification. Use a trusted local export of the
same base graph as the client; loose data files and IMG entries can differ.

```
python3 utils/ped-path-patches/compile_patch.py --paths /path/to/paths \
  --spec test-resources/ped-path-patch-test/detour.json --output detour.npp
```

Each `areas` entry has an `area` (0–63) and `nodes`. Each node edit specifies
`id`, `position` (three world coordinates), `width` (native units of 1/16),
`flood` (native connectivity group), optional `enabled` (default true), and
`links` (`area`, `node`, `length` in native graph-cost units, optional `flags`
0–3). Existing pedestrian IDs stay unchanged. Added IDs must start at the
original node count and be contiguous. There are at most 15 outgoing links.
Disabled nodes have no links; the compiler also removes their incoming links
and includes all 64 regions for verification. Editing a vehicle node is rejected.
Keep the same flood group across connected nodes; this version does not merge
previously separate native flood groups.

The binary format starts with `NPEDPAT1`, little-endian area count (exactly 64), then sorted
area records: five uint32 fields (area, original node count, vehicle count,
address count, car-link count), 32-byte normalized base SHA-256, uint32 edit
count, and edits. An edit is uint16 ID, three int16 positions scaled by eight,
four bytes (width, flood, enabled, link count), followed by links of uint16
area, uint16 node, uint8 length, uint8 flags. Trailing data is rejected.
Only edits and fingerprints are distributed, not the original graph files.

## Lifecycle and limits

- The graph is GTA-global per client, not isolated by dimension or interior.
  This checkpoint targets the original 64 exterior regions, not map-grid
  expansion or custom interiors.
- All 64 base regions are requested and verified synchronously before publication,
  including every incoming edge. Only edited regions are reserved: two patches
  cannot edit the same region. The effective pedestrian graph must have equal-cost
  reciprocal links, as required by GTA's path reconstruction.
- Streaming reloads reapply the patch and recheck the base fingerprint.
  A rejected reload invalidates the lease. Previously published node addresses
  remain backed by storage even if the replacement graph is shorter; changing
  that base requires a client restart before patches can be admitted again.
- Added node storage and IDs remain reserved for the process lifetime. Unload
  turns them into non-spawnable, disconnected tombstones. The identical patch
  can be restarted, but a different patch adding nodes to the same region
  requires restarting the client. This prevents old task addresses being
  silently recycled for unrelated paths. Existing tasks are not forcibly
  restarted; they may finish their current segment before using restored paths.
- Fixed caps: 4 MiB file, 4096 nodes and 32768 link slots per region, including
  192 slots reserved for native dynamic links. Slabs are bounded and retained;
  this is not an unbounded history of every patch revision.
- Active interior link rewrites are refused during patch admission. While a patch
  owns a region, new native interior connections involving that region are
  suppressed on both endpoints. Exterior-to-interior navigation through that
  region is therefore unavailable until the patch is unloaded. The loader
  requires the verified US 1.0 native entrypoints and fails on competing detours.
- Patches do not fix unrelated event-profile, collision-support or synchronization
  problems, and do not provide a server-side route calculator.

## Tester handoff

Use `test-resources/ped-path-patch-test`. Its synthetic Idlewood detour is a
reproducible fixture, not the author's unavailable Seven/Alhambra assets.

1. Install the matching complete CI client artifact; keep modules from the same
   build together. Start the resource on the test server and connect two clients.
2. Both clients must report the same revision as `fixture.lua`. A mismatched
   base graph must report an error, not partial success.
3. Both players run `/pathpatch visit` to stream the fixture location. Wait for
   `building collision ready: true` on both clients. Each client creates a building collision barrier and checks that it blocks
   the direct segment while leaving the detour clear. Readiness stays false if
   this collision check fails. A frozen object would not suffice: GTA ignores
   CObjects in the native short-route shortcut.
4. `/pathpatch` spawns a shared ped and dispatches native navigation near
   X=1954, Y=-1820. Observe the detour through X=1948.5 and return to vanilla.
   The test reports task acceptance, not arrival success: visually verify the
   safe obstacle avoidance and compare owner/observer positions. GTA may shorten
   clear portions of the path; exact traversal of every node is not required.
5. `/pathpatch handoff` transfers to another nearby ready client. Repeat while
   the pedestrian is on an added node. `/pathpatch wander` exercises wandering.
6. Travel far enough to unload the region, return with `/pathpatch visit`, wait
   for collision readiness, then run `/pathpatchstatus` and navigation again. Restart the resource and repeat.
7. `/pathpatchunload` during navigation must not crash; `/pathpatchload` reloads
   the identical patch. Restart/disconnect/reconnect and repeat several times.
8. Run vanilla vehicle traffic through the region; check driving, lanes and
   traffic lights. Exercise a malformed/truncated file and overlapping resources:
   refusal must leave the previous graph untouched.

No local C++ compilation or runtime test was requested. Static checks and the
independent reviews are separate from CI build results and tester observations.

## Implementation references

Lua registration: `CLuaWorldDefs.cpp`; resource cleanup: `CResource.cpp`;
engine: `CPedPathPatchesSA.cpp`; bounded decoder: `PedPathPatchFormat.h`.
Native reference: compact US 1.0 SHA-256
`72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`.
Load entry `0x4529F0`, unload entry `0x44D0F0`, CPathFind `0x96F050`.
