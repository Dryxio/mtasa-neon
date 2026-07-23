# Canonical static-world v3 packs

Static-world v3 is the deterministic multi-IMG transport and admission format
for large native-world payloads. A child-pack resource is audited and published
into the content-addressed cache with `activation=no` and `lease=no`. It cannot
request a startup ticket or select a native registrar policy.

The separate `static-world-v3-set` coordinator may request a one-shot startup
ticket for exactly four immutable child identities. Its startup route locks and
re-audits all five cache objects, reruns the aggregate planner, releases every
lease and deliberately continues with stock GTA. It still cannot register an
IMG or mutate a GTA store.

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
performed by the offline builder. In particular, a cached v3 object is not
directly activable by a future registrar. Activation must repeat the complete
payload grammar, stock-key collision, pool, and native-state preflight under
an activation lease.

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

The game must remain stock. Seeing Carcer in GTA during either the child
transport gate or the aggregate dry-run would indicate an architectural
violation, not success.

## Closed aggregate startup dry-run

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
store/pool/archive/handle capacity. All child leases and then the set lease are
released before startup returns `aggregate-dry-run` and starts stock GTA.

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

The remaining activation blockers are concrete runtime mechanisms: exclude
the arena from every MTA allocator and script mutation, remap IPL/COL buffers
before GTA consumes them, own two reusable VC/LC LOD entity-index arrays,
prove building/QuadTree overlap high-water, and measure RenderWare residency.
The v3 cache now has an eight-object double bank for one complete rollover;
safe reclamation of later inactive generations remains to implement. The
native streaming floor covers both channel halves in source, pending build and
runtime validation. These are activation requirements, not reasons to inflate
constants inside the planner.
