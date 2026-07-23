# Aggregate native-world v3 planner

`plan_native_world_v3.py` is the read-only gate between canonical v3 transport
and native multi-city activation. It does not write a pack, register an IMG,
allocate a GTA store entry, patch an executable, publish cache content, or
request an authorization ticket.

The planner binds the frozen `map_data.lua` inputs for Bullworth, Vice City,
Liberty City and Carcer City. In canonical order it derives the same
source-first spatial variants and short namespaces as the v3 builder,
diagnoses the former contiguous aggregate DFF range, checks stock and
generated identities, and reports every capacity and budget with its
authority.

## Current result

The deterministic allocation is:

| City | Namespace | Source models | Spatial variants | Model IDs |
| --- | --- | ---: | ---: | --- |
| Bullworth | `bw` | 952 | 1,054 | 20,000..21,053 |
| Vice City | `vc` | 3,497 | 3,802 | 21,054..24,855 |
| Liberty City | `lc` | 3,019 | 3,488 | 24,856..28,343 |
| Carcer City | `cc` | 3,450 | 3,493 | 28,344..31,836 |

The four cities require 11,837 model variants, including 919 cross-spatial
duplicates. A permanent one-ID-per-variant assignment is invalid for two
independent reasons: only 163 IDs remain through 31,999, and Carcer's
30,000..31,836 tail collides with MTA's logical server-model namespace and the
30,000..30,151 clothes pseudo-model range.

Activation therefore separates logical identity from GTA residency. A model
is identified by `(content ID, pack ID, pack-local model ID)` and is bound for
one generation into the physical arena 20,000..29,999. The arena contains
10,000 slots and is never exposed as MTA logical IDs. The worst current
two-city transition is Vice City plus Carcer: 7,295 slots, leaving 2,705. The
largest current city plus a 4,096-variant future working set needs 7,898
slots, leaving 2,102.

A same-city old/new rollover of two maximum future generations needs 8,192
slots and leaves 1,808. The registrar admits at most two concurrent working
sets: a city transition and a generation rollover are mutually exclusive, and
a third set is refused before mutation.

This is a residency reserve, not permission to recycle IDs by distance alone.
An old physical slot remains owned until its IPL entities and LOD links are
gone, streaming channels and request lists no longer reference it, its
RenderWare/COL/TXD references are released, and a generation fence completes.

The stock installation contains 5,168 IDE-free IDs below 20,000. Once the
native arena is active, MTA's dynamic allocator is confined to that pre-arena
space. The count is observational only: high-water tests must prove that
resources and server-defined models retain sufficient headroom. Every
allocator and script-facing model mutation must also consult the central
native-slot predicate; merely changing `GetFirstFreeModelID()` is
insufficient.

The generated set contains 13,404 global identities: 11,837 DFF names, 1,325
TXD names, 121 COL names and 121 IPL names. The frozen namespaces have no
literal or GTA uppercase-key collision with each other or with the scanned
stock installation, including loose case-insensitive DFF/TXD files and stock
COL/IPL archive/directive names. The stock proof is tied to an inventory digest
in the baseline. Each semantic-admission budget is
also tied to the complete DFF/TXD/COL asset fingerprint, not only to
`map_data.lua`. Archive filenames such as `w000.img` are pack-scoped; IMG
member names remain globally namespaced.

## Capacity proof

The installed stores and file-backed pools fit the aggregate definitions:

| Capacity | Projected | Limit | Remaining |
| --- | ---: | ---: | ---: |
| Atomic model store | 25,201 | 32,000 | 6,799 |
| DamageAtomic store | 205 | 512 | 307 |
| Time store | 644 | 1,024 | 380 |
| TXD slots | 4,933 | 8,000 | 3,067 |
| COL slots | 373 | 512 | 139 |
| IPL slots | 312 | 1,024 | 712 |
| ColModels | about 21,815 | 30,000 | about 8,185 |
| IMG archive IDs | 13 | 245 | 232 |
| Stream handles | 23 | 255 | 232 |

ColModel and building baselines are derived from the validated Bullworth
high-water by removing Bullworth's known additions; they are planning values,
not new executable constants.

Buildings do not yet have an activation proof. Stock plus every city resident
at once is 43,015, above the 32,000 pool. Stock plus only the largest imported
city is 21,641 and fits, but the future registrar must prove translated spatial
bounds, overlap sets and unload ordering before relying on mutual exclusion.
QuadTreeNode demand has no safe formula from placement totals; its 225/2,048
Bullworth high-water is retained as telemetry, not extrapolated into a claim.

The planner explicitly proves the partition ownership at 31,999/32,000,
39,999/40,000 and 40,511/40,512. It also proves every adjacent partition, the
terminal ID 42,340 and exclusive end/count 42,341, so the named samples cannot
hide a later gap or overlap. DAT, IFP, RRR and SCM keep their stock spans;
paths/nodes, DAT expansion, streamed SCM, new IFP/RRR and population remain
outside this checkpoint.

## LOD and generation blocker

Vice City has 1,081 non-negative LOD links and Liberty City has 1,957. All
Vice City links and 1,956 Liberty City links cross IPL groups; one Liberty City
link remains within its group. The report emits every group dependency edge.

This is not an index-width limit. A standalone streamed IPL currently has
`staticIdx = -1`, while GTA's LOD resolver expects a registrar-owned entity
index array and a valid static IPL index. Vice City needs 1,081 anchors and
Liberty City needs 1,957.

The registrar will allocate one entity-index array for each of those cities
once per process, then reuse its contents by generation. Anchors are loaded
before children and removed after children; they are not permanently resident
when their city is inactive. The current stock load uses 30 of the 40 pointer
slots, so the runtime must prove that two more remain. The scratch loader must
also prove `anchors + linked children <= 4,096`: Vice City needs 2,162 and
Liberty City needs 3,914. Until the bootstrap, buffer remap, and child-first
unload ordering exist, the planner rejects activation instead of rewriting or
discarding the 3,038 links.

## Memory, streaming and disk budgets

The frozen semantic admission profile contains 31,330 textures:
928,578,564 bytes in serialized GPU format and 6,191,178,736 decoded RGBA
bytes. Those figures remain under the compiled 4 GiB and 16 GiB aggregate
admission ceilings; they are worst-case corpus measurements, not a promise
that every texture can be simultaneously resident.

After spatial duplication the aggregate collision payload is 11,835 records
and 50,150,532 serialized bytes. The planner also reports fixed relocated
table/store/pool bytes as a known lower bound, exact binary IPL record bytes,
largest source-derived IMG member, required double streaming-buffer size and
cache transaction headroom. It emits every source city/group bound and
pairwise gap; activation must repeat that analysis after final world
translation.

`SetStreamingBufferSize()` treats its argument as the total allocation and
splits it between two channels. The activation work now returns both channel
halves: for the largest current Carcer member, each channel needs 25,060
blocks and the total floor is 50,120 blocks (102,645,760 bytes). The source
contract test pins the unit and overflow checks; this still needs a client
build and runtime high-water validation.

RenderWare object graphs, allocator overhead and simultaneous CPU/GPU residency
cannot be proven from serialized corpus bytes. They remain an explicit
high-water blocker, not hidden inside the approximately 6.6 MiB known fixed
native allocation lower bound.

IMG payload sizing is conservative and source-derived. It includes spatial DFF
and COL duplication, COL2 header growth and exact IPL sizes, but does not claim
to replace canonical TXD duplicate removal or the pinned librw serialization.
Emitted v3 manifests remain the disk and archive authority for activation.

The activation work raises only the v3 child-cache object limit from four to eight,
providing an active four-city bank plus a four-city replacement bank. The
independent 32 GiB byte cap is unchanged. Aggregate set envelopes use a
separate eight-object metadata bank and never consume a payload-object slot.
Eight child objects allow one complete rollover only: safe reclamation of an
older, unlocked generation is still a blocker before continuous pack rotation
can be claimed.

## Reproduction

Run the planner against the reviewed stock GTA copy:

```sh
python3 utils/extended-world/plan_native_world_v3.py \
  --gta-root /Users/salimtrouve/Documents/GTA-SanAndreas \
  --output /tmp/native-world-aggregate-plan.json
```

Verify source, stock identity and conclusions against the reviewed baseline:

```sh
python3 utils/extended-world/plan_native_world_v3.py \
  --gta-root /Users/salimtrouve/Documents/GTA-SanAndreas \
  --verify utils/extended-world/native_world_aggregate_plan_baseline.json \
  --output /tmp/native-world-aggregate-plan.json
```

`--require-activable` is intended for the later activation pipeline. It exits
nonzero while any blocker remains. A normal planning run exits successfully
with `status=blocked`, because reporting a proved blocker is a successful
read-only plan.

Run the offline tests with:

```sh
python3 -m unittest utils/extended-world/tests/test_plan_native_world_v3.py
```

No VM synchronization, C++ build or game launch is part of this planner
checkpoint. Its exact logic is now rerun against locked child cache bytes by
the later `static-world-v3-set` authorization dry-run before GTA stock starts.
