# Canonical static-world v3 packs

Static-world v3 is the deterministic multi-IMG transport and admission format
for large native-world payloads. A child-pack resource is audited and published
into the content-addressed cache with `activation=no` and `lease=no`. It cannot
request a startup ticket or select a native registrar policy.

The separate `static-world-v3-set` coordinator may request a one-shot startup
ticket for exactly four immutable child identities. Its startup route locks and
re-audits all five cache objects and reruns the aggregate planner before any
native mutation. The authorized generation-1 registrar then keeps all five
leases for the process lifetime and activates the reviewed Bullworth and Carcer
working set. Vice City and Liberty City remain admitted logical packs, but are
not resident until their LOD entity-index bootstrap exists.

## Closed transport format

A v3 resource contains one descriptor without a `startup` attribute:

```xml
<native_world format="3"
              policy="static-world-v3"
              manifest="native/native-world.json" />
```

The exact tagged group is:

- `native-world.json`;
- `world.ide`;
- `world.lod`;
- one through 32 contiguous archives named `w000.img`, `w001.img`, and so on.

The manifest has an exact schema and ordered `files.images` array. Every file
name, byte length, and lowercase SHA-256 is bound before publication. The
content ID additionally binds the format, policy, pack ID, IDE and LOD
identities, and the ordered name/length/hash tuple of every IMG. Reordering
archives therefore creates a different semantic object.

Each IMG is a standard `VER2` archive capped at 131,072 sectors (256 MiB). The
payload cap is 8 GiB, with all aggregate arithmetic performed as checked
64-bit values. The transport cache retains at most eight v3 child objects under a
32 GiB cap and requires free space for the new object plus the greater of
512 MiB or 12.5 percent of that object.

Publication uses a private same-volume quarantine. Source files, quarantine
files, and the final object are opened as regular non-reparse files, checked by
length and SHA-256, and constrained to an exact file set. The semantic audit is
repeated inside the locked quarantine before its atomic rename.

## Deterministic native identities

Each pack receives a two-character lowercase namespace. Generated names are:

- models: `<ns>m` plus four base-36 digits;
- TXDs: `<ns>t` plus three base-36 digits;
- spatial COLs: `<ns>c` plus two base-36 digits;
- spatial IPLs: `<ns>i` plus two base-36 digits.

The builder checks GTA's uppercase key for every generated model name and
rejects collisions. Model IDs form one contiguous source-first range. A source
model used by more than one spatial IPL receives a stable primary ID first;
additional spatial variants are appended deterministically. This keeps each
collision record owned by exactly one streamed spatial group.

The runtime transport envelope derives the inventory from IDE and IMG bytes. It
checks the ID/name mapping, cross-IMG uniqueness, DFF/TXD RenderWare roots,
COL3 model mappings, paired spatial ordinals, and every binary IPL instance.
Stock placement IDs are allowed only below the custom range. Custom placement
IDs must exist in the IDE. Coordinates and quaternions are finite and bounded.
Generated models may belong to only one spatial IPL, and a supplied COL record
must belong to the paired IPL ordinal.

This transport envelope does not replace the full DFF/TXD/COL semantic audit
performed by the offline builder. A cached v3 child object is never directly
activable by itself. The exact four-pack set route repeats the complete payload
grammar, stock-key collision, aggregate capacity, pool, executable, and
native-state preflight while all five cache objects are locked.

Standalone streamed IPLs have no entry in GTA's static IPL entity-index array,
so every emitted binary IPL still carries `lodIndex = -1`. The exact
`world.lod` sidecar preserves the original cross-group child/anchor graph as
checked unsigned ordinals. It proves that VC needs 1,081 links/anchors and LC
needs 1,957, without asking GTA to resolve them yet. Native LOD linkage still
requires a later registrar-owned entity-index bootstrap.

Models explicitly lacking source collision keep no collision record; no
synthetic geometry is created. Models explicitly lacking a source TXD use one
builder-generated, shared, canonical empty dictionary. Both cases are recorded
in the validation report and are not generalized to arbitrary missing files.

## Conversion and admission boundary

`audit_native_world_v3_admission.py` scans the four local catalogs without
mutating them. `build_native_world_v3.py` applies only closed, reported
conversions:

- the single pinned Vice City RenderWare 3.4 DFF is deserialized and serialized
  through the pinned local librw null backend;
- two pinned malformed Carcer 2DFX extensions are reduced to empty extensions
  because their claimed 12 effects have no bytes before the next clump child;
- all 57 COL2 records are validated, converted to COL3, and revalidated;
- native COL1 records remain canonical `COLL` records: both the builder and the
  runtime dry-run validate their sequential counts, arrays, indices, finite
  bounds, record boundaries, model identity, and padding without a lossy
  geometry rewrite;
- TXD native-texture tuples, mip chains, anisotropy plugins, dimensions, and
  64-bit GPU/decoded budgets are checked;
- case-insensitive TXD duplicates use a deterministic first-wins policy and
  later unreachable entries are removed;
- known extractor defects in timed-object fields are repaired only by exact
  source fingerprint, prefix, source ID, and raw value tuples.
- 15 Liberty City definitions with source-specific high IDE metadata are
  projected onto GTA SA's observable `0x007fffff` flag domain only for the
  frozen source fingerprint and exact source-ID/raw-value tuples; the report
  retains raw, canonical and removed values.

COL admission validates complete record boundaries, counts, offsets, primitive
arrays, face groups, core and shadow indices, finite bounds, flags, and zero
padding before conversion, after conversion, and after model remap. Pack
verification re-reads the emitted DFF/TXD/COL/IPL members rather than trusting
source validation or manifest claims.

No generic "repair malformed data" mode exists. A source or converter identity
change fails closed and requires a reviewed new conversion vector.

## Reproduction

Build the local librw converter:

```sh
clang++ -std=c++17 \
  -I../librw -I../librw/src -DRW_NULL \
  utils/extended-world/librw_dff_upgrade.cpp \
  ../librw/lib/macos-arm64-null/Release/librw.a \
  -o /tmp/librw_dff_upgrade_v3
```

Audit all local source catalogs:

```sh
python3 utils/extended-world/audit_native_world_v3_admission.py \
  --librw-dff-upgrader /tmp/librw_dff_upgrade_v3 \
  --output /tmp/native-world-v3-admission.json
```

Build the Carcer proof into a new empty directory:

```sh
python3 utils/extended-world/build_native_world_v3.py \
  --resource test-resources/carcer-city-test \
  --output /tmp/carcer-v3 \
  --prefix CARCER_CITY \
  --pack-id carcer-city \
  --namespace cc \
  --model-id-start 26099 \
  --librw-dff-upgrader /tmp/librw_dff_upgrade_v3
```

Run the same command into a second empty directory and compare
`native-world.json`, `world.ide`, every IMG, and `validation.json` by SHA-256.
The deterministic proof is valid only when every digest matches.

## Carcer proof envelope

The reviewed Carcer input produces:

- 3,450 source models and 3,493 spatial model variants;
- model IDs 28,344 through 31,836 in the aggregate proof build;
- 106 TXDs;
- 12 COL/IPL spatial pairs;
- 12,475 placements, including 56 stock-model placements;
- four IMG archives;
- two pinned malformed-DFF repairs;
- four COL2-to-COL3 conversions;
- 70 removed later TXD duplicates.

The generated payload is 826,749,754 bytes including `world.ide` and
`world.lod`. Its first three archives are exactly 256 MiB and the fourth is
21,321,728 bytes. The current aggregate proof build produced:

| File | SHA-256 |
| --- | --- |
| `native-world.json` | `4632b31bf876d07987fe8fe310abf0e69744f52a4a484b3a2b3280c44b1618db` |
| `world.ide` | `4aa9a965d7b8835b41d4985c13da35bd9b11c4c737f046ded9d41c58d7fce181` |
| `world.lod` | `d67b97b52b5def0dfea6d41b628aed72c081c18229e88b8c9627fef20ff06fca` |
| `w000.img` | `5fa35e9cd436ba7abbb43095989b2f405aa35ad248b0748266d6b924fe77db7c` |
| `w001.img` | `a24248ce539348d8885fe25a83068dbd1f31e73fbd4ccaef125ee62fdda9bd05` |
| `w002.img` | `28dad0ea60acb9874594a17cc03230850e9fbb52bc2bb49df73b61a120d834b4` |
| `w003.img` | `6121af6fcffcf3d460dfcd9f9792ed910875805c94b34088d8bdef37f79c969a` |
| `validation.json` | `fa0cbcfd622e5aae47b7049360721951c6b913666dd43a6cf06d340b64d238b6` |

The exact hashes are also bound by the generated manifest and must be
reproduced from the current source fingerprints before each deployment.

`test-resources/native-world-v3-transport-test` contains only the tracked
descriptor. Deploy the generated payload separately into its runtime `native`
directory. A successful client gate must report:

- `format=3`, seven files for the Carcer proof;
- `audit=static-world-v3-transport-envelope-v1`;
- `publish=atomic`;
- first run `disposition=published`, second run `disposition=hit`;
- `activation=no`, `lease=no`, and no restart request.

The game must remain stock during the child transport gate. Seeing Carcer
before a closed set authorization has been selected and committed would
indicate an architectural violation, not success.

## Closed aggregate startup and generation-1 registrar

`native_world_v3_set.py` emits one canonical ASCII
`static-world-v3-set.json`. It contains exactly Bullworth, Vice City, Liberty
City and Carcer City in that order, each as an exact `(pack_id, content_id)`
pair. The domain-separated set ID covers the format, policy, order and all
eight identity strings. The frozen checkpoint-8 envelope has set ID
`04547ff361e98e97b42badfde3a85c58f6c7a8cbb1eb83e2dbcdec69247b3afb`.

Child packs use the format-3 LOD transport capability and remain publish-only.
The coordinator uses a later, independent format-3 startup-authorization
capability; a server refuses to start it while an incapable client is joined
and refuses a newly joining incapable client while it is running. There is no
silent downgrade to an ordinary resource.

On publication and again at authorized startup, the coordinator locks its
envelope plus all four exact child cache objects. Every child manifest,
IDE/LOD file and IMG is rehashed and semantically audited while locked. The
planner then proves the canonical namespaces and ID ranges, cross-pack member,
model and GTA uppercase-key uniqueness, and every compiled
store/pool/archive/handle capacity without native writes.

After that read-only boundary passes, generation 1 prepares a single global
transaction. It admits all four logical pack identities, but makes only
Bullworth and Carcer resident because neither requires the deferred LOD
bootstrap. It plans every physical model ID, TXD/COL/IPL slot, IMG handle and
streaming binding before mutation; opens seven IMG archives; allocates 1,325
TXD, 19 COL and 19 IPL slots; creates 4,547 ModelInfos; and installs 5,910
direct streaming bindings. COL and binary IPL model IDs are remapped in their
owned buffers immediately before the native loaders consume them, then
restored in the cache buffer. The streaming-buffer floor is derived from the
largest entry across the locked set and covers both GTA channel halves.

Pool and archive changes before the first ModelInfo are journaled and rolled
back globally on failure. The first ModelInfo is the explicit irreversible
barrier: failures after it terminate the process rather than expose a partial
world. A successful commit marks all five leases as the exact authorized
ticket and keeps them locked for the process lifetime. Generation 1 is
deliberately logged as `recyclable=no`; runtime pack replacement and physical
slot reuse remain out of scope until a generation fence exists.

The set cache uses the same closed-directory security boundary as child packs:
all ancestors remain locked for the lease lifetime, interrupted private
siblings are collected only through verified no-reparse handles, and an exact
one-file canonical object with a corrupt hash is removed safely so a later
publication can recover. An explicit ResourceStop revocation that cannot be
terminalized immediately is retained as a value-only manager job and retried
against the exact durable identity. Generic client teardown deliberately does
not revoke, because `nativeworldauth restart` must preserve the pending ticket
for the next process.

## Aggregate planning handoff

The next read-only gate is documented in
`utils/extended-world/NATIVE_WORLD_PLANNER.md`.
`plan_native_world_v3.py` derives all four city remaps together, scans stock
IDs/names/GTA uppercase keys, proves FileID boundaries, calculates
store/pool/memory/streaming/cache budgets and emits the complete VC/LC LOD
dependency graph. It never builds or publishes a pack and never mutates GTA.

The permanent contiguous plan is intentionally blocked: its Carcer tail enters
MTA's logical model namespace at 30,000, and 11,837 variants cannot retain the
required future reserve. Activation instead uses typed pack-local logical
identities and a generation-fenced physical arena at 20,000..29,999. The worst
current two-city transition leaves 2,705 physical slots; the largest current
city plus a 4,096-variant future working set leaves 2,102.

The first three activation blockers are now implemented in source: the
physical arena is excluded from MTA model allocation/script mutation, owned
IPL/COL buffers are remapped before GTA consumes them, and the VC/LC working
set owns two native LOD entity-index arrays. The remaining mechanisms are
generation-fenced recycling, broader building/QuadTree overlap high-water, and
RenderWare residency measurement.
The v3 cache now has an eight-object double bank for one complete rollover;
safe reclamation of later inactive generations remains to implement. The
native streaming floor covers both channel halves and was live validated at
50,120 blocks. These are activation requirements, not reasons to inflate
constants inside the planner.

## Generation-1 live gate

The 2026-07-25 gate used the frozen set ID above after a complete server and
`native-bw-test` resource restart. Startup re-proved 11,837 logical models and
33,849 placements with `nativeWrites=0`, then committed ticket `09478331` with
`activation=yes lease=process`. The registrar reported:

- four logical packs, with Bullworth and Carcer resident;
- 4,547 physical models, seven archives and 5,910 streaming bindings;
- 1,325 TXDs, 19 COLs and 19 IPLs;
- `barrier=first-ModelInfo`, `commit=global`, `generation=1 recyclable=no`.

Repeated San Andreas/Bullworth/Carcer transitions, minimize/restore,
death/respawn, disconnect/reconnect in one process, a resource restart and a
full server restart remained stable. After the final cross-city pass, peak
buildings were `18,352/32,000`; ColModels reached `14,522/30,000`; TXD, COL,
IPL and QuadTreeNode occupancy stayed at `4,933/8,000`, `271/512`,
`210/1,024` and `238/2,048`. No streaming, RenderWare, allocation or crash
diagnostic appeared. Transport republication refusals after activation are
expected: they preserve the already committed process-global generation.

## VC/LC LOD bootstrap candidate

The next generation-1 working set switches residency from Bullworth/Carcer to
Vice City/Liberty City while retaining all four exact logical packs and cache
leases. Its Win32 `Game SA` build and user-run live gate are complete.

Before stock `LoadScene` allocates its 30 arrays, the registrar reserves GTA
IPL entity-index arrays 0 and 1 through native `0x404780`. The first owned IPL
callback after stock scene loading requires the native table to contain exactly
32 non-null entries at `0x8E3F08` with a count of 32 at `0x8E3F00`. It then
constructs 1,081 VC and 1,957 LC permanent anchors from the locked audited IPL
instances. Each anchor follows GTA's native transition from the temporary
`lod=-1` union value to a null pointer, runs `SetupBigBuilding` before `Add`,
and belongs to one disabled hidden IPL owner. Retail `0x533150` clears
collision and sets the entity `BIGBuilding` and `DontCastShadowsOn` flags plus
the model-info collision-ownership flag. It deliberately does not set
`StreamingDontDelete`; anchor lifetime is owned by the hidden IPL instead.
Both hidden owners are allocated after all 102 spatial IPL slots so GTA's
ascending shutdown order deletes every child before either owner.

The frozen sidecars require exact scratch profiles `2162/4096` for VC and
`3914/4096` for LC. Their 3,038 one-to-one links occupy 9 VC and 12 LC child
groups. During the initial flipped-rectangle pass, the loader keeps temporary
anchor records so native spatial bounds remain complete; normal streaming
compacts those duplicates and remaps each child `lodIndex` to the permanent
city array. Exact source bytes are restored after every synchronous native
load. Counter gates require zero before load, exactly one after load and zero
again after the bounding-pass child IPL is removed.

The canonical VC inventory has exactly two visual-only LOD anchors with no
direct COL record; LC has none. GTA permits those entities to be constructed,
but `SetupMapEntityVisibility` unconditionally reads their ModelInfo
`pColModel`, so leaving either pointer null crashes at retail `0x553F71`.
Admission therefore proves that every missing-COL placement is a unique
one-to-one anchor whose child has collision. Before constructing those two
anchors, generation 1 applies the narrow native `_LinkLods` rule through
`CBaseModelInfo::SetColModel`: the anchor borrows its child's stable ColModel
and clears bit `0x80` (`bIsColLoaded` in MTA, the native allocation/deletion
ownership bit) while the child remains the sole owner. `SetupBigBuilding` keeps
the separate bit `0x20` set on the anchor. COL unload preserves the shared
bounds and pointer and removes only collision volumes; reload updates the same
child object in place.

The general `_LinkLods` ColModel-pointer transfer remains omitted. Nearly every
other link crosses independently streamed COL groups and anchor model variants
are reused, so transferring all child pointers into LOD ModelInfos would make
COL load/unload ownership asymmetric. The runtime first proves that every
supplied placement COL materialized, freezes the exception profile at VC
`2` / LC `0`, and reports
`collisionTransfer=missing-anchor-only:2`. A future canonical-pack revision
should synthesize bounds-only COL records for visual-only models, after which
even this narrow runtime exception can disappear. The live gate must verify
child collision and line-of-sight behavior as well as visual LOD transitions.

The candidate plans 7,290 physical models, 1,325 TXDs, 102 COLs, 104 total IPL
slots including the two hidden owners, seven archives and 8,819 direct
streaming bindings. The conservative startup building budget is
`9,166 + 18,412 + 3,038 = 30,616/32,000`. Generation 1 remains
`recyclable=no`: final owner deletion/reuse belongs to the later generation
fence even though process shutdown is structurally child-first.

The 2026-07-25 gate validated the exact transfer count `2`, cleanup of all 21
child groups, repeated VC/LC/SA transitions, collisions and visible LOD
changes, minimize/restore, death/respawn, same-process reconnect, hot resource
restart and full server restart. Ticket `b7bef51c` and all process leases
survived the lifecycle tests without re-registering the physical arena.
High-water was Atomic `20,833/32,000`, DamageAtomic `89/512`, Time
`581/1,024`, TXD `4,933/8,000`, COL `354/512`, IPL `295/1,024`, buildings
`16,577/32,000`, ColModels `17,273/30,000` and QuadTreeNodes `264/2,048`.
No crash, streaming, RenderWare or allocation diagnostic appeared after the
missing-anchor fix.

## Four-city generation-fenced candidate

The next candidate keeps all four immutable pack identities, seven archives,
1,325 TXD slots, all COL/IPL definitions, and all 11,837 append-only
ModelInfos in one process generation. It deliberately does **not** bind all
11,837 models at once: the protected physical arena contains 10,000 slots, and
the canonical Carcer tail overlaps MTA's logical/clothes namespace above
30,000.

During the startup-only `CFileLoader::LoadLevel` boundary, the catalog is
temporarily addressable at its canonical IDs so GTA can compute every COL/IPL
bounding box. The owned IPL hook flattens only that temporary pass to
`lod=-1`; no persistent anchor exists yet. The validated tail jump at
`0x5B9321` calls `CColStore::RemoveAllCollision`, proves every catalog
ColModel was removed, clears all 11,837 canonical model pointers, and only then
publishes `bootstrap=spatial-ready`. Thus the temporary 30,000..31,836 overlap
cannot escape into gameplay or MTA APIs.

Runtime residency uses two alternating 4,096-slot banks,
`20,000..24,095` and `24,096..28,191`. All four cities are available, but a
strict spatial-exclusion coordinator materializes one city's child IPLs at a
time. A transition disables outgoing child IPLs, clears retail `CCover`'s
processed-building cache while its raw entity pointers are still valid, then
removes those IPLs, proves every LOD counter is zero, removes the hidden anchor
owner, flushes both streaming channels, unloads collision while the old
pointers still exist, removes every DFF/request binding, clears the old bank,
rebuilds all IMG chains, and advances the generation before the new bank is
published. Timed-model peer IDs and COL first/last model ranges are repatched
for every bank assignment. VC/LC anchors are rebuilt only for the active city
in the corresponding reusable 4,096-entry array.

The cover purge is a required generation sub-fence, not optional cleanup.
Retail `CCover::m_ListOfProcessedBuildings` stores raw building pointers and
can revisit them from `CCover::Update` after an IPL deletes the entities. The
first Bullworth-to-SA live retirement demonstrated that failure at
`CEntity::GetBoundCentre` (`0x53425B`) with bank model ID `20043`. Native
`CCover::Init` (`0x698710`) now runs after dynamic streaming is disabled and
before the first outgoing IPL is removed.

The all-city building sum remains invalid (`43,015/32,000`). The coordinator's
hard rule is therefore one materialized city. The true maximum is stock
`9,166` plus Carcer's `12,475` placements, or `21,641/32,000`; LC's own
placements plus its `1,957` anchors remain lower at `20,947/32,000`. Anchors
from one city are never counted beside another city's child set because the
generation fence removes them first. The five cache leases stay
process-lifetime, so no old cache object is reclaimed merely because a
physical bank became reusable.

The 2026-07-26 live gate completed generations 2..29 across all four imported
cities and San Andreas. Both banks were repeatedly reused; direct Carcer-to-VC
and VC-to-LC transitions, death/respawn, same-process reconnect, hot resource
restart and full server restart remained stable. VC rebuilt all `1,081` links
with its exact two borrowed-COL exceptions, and LC rebuilt all `1,957` links
with none. An initial VC fail-stop exposed an inverted validation predicate:
retail `SetupBigBuilding` always sets ModelInfo bit `0x20`, while
`SetColModel(..., false)` changes only bit `0x80` for borrowed collision. The
runtime now proves those independent postconditions instead of coupling bit
`0x20` to the exception state.

Observed peaks were buildings `21,500/32,000`, ColModels `21,819/30,000`, TXD
`4,933/8,000`, COL `373/512`, IPL `314/1,024` and QuadTreeNodes `280/2,048`.
No fatal, streaming, RenderWare, allocation or crash diagnostic appeared after
the fix. Borderless Alt-Tab did not cause `OnInvalidate`/`OnRestore`; a true
device reset remains deliberately pending for the streaming/render/memory
gate rather than being reported as tested here.
