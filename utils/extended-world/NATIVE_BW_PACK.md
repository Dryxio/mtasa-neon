# Bullworth native pack tooling

`build_native_bw_pack.py` converts the generated `ug-bw` test resource into
ordinary GTA SA streaming artifacts without changing the running client or the
existing Lua city loader. It assigns Bullworth's 952 source models to the exact
compact range `18631..19582`, emits one IDE, one IMG VER2 archive, one merged
COL archive, and seven district-preserving binary IPLs.

The stock GTA root is mandatory because the generator refuses a model-ID
collision and records TXD/COL/IPL slot budgets. Standalone stock archive
inventory accounts for 3607 TXDs, while the audited initialized MTA runtime
pool has 3608 occupied slots because it also registers the stock cutscene TXD
`preq_cargo.txd`. The
manifest records the MTA snapshot plan (`3608..3773`) for review. At runtime
the registrar selects an identity-specific exact pool profile, inventories the
actual occupancy and cursor, then deterministically simulates native
`CPool::Allocate`. `mta-programdata` requires capacity 5000, occupied slots
`0..3607`, cursor 3607, and a reviewed fingerprint for the extra registered
stock cutscene TXD in slot 3607;
`hoodlum-raw` requires occupied slots `0..3606` and cursor 3606. Any holes or
other layout refuse the feature before mutation.

The reviewed `preq_cargo.txd` slot 3607 fingerprint is pool flag `0x01`, null
RenderWare dictionary, zero usages, parent `0xFFFF`, key `0xEA5A8E45`, and streaming entry
`archive=4, offset=13153, size=5` with all three links `0xFFFF`, flags zero,
and state not loaded. Matching only the occupied count is insufficient: a mod
that happens to consume the same slot is rejected.

Generate into a new or empty ignored/temporary directory:

```sh
python3 utils/extended-world/build_native_bw_pack.py \
    --resource test-resources/ug-bw \
    --stock-gta /Users/salimtrouve/Documents/GTA-SanAndreas \
    --output /tmp/mtasa-neon-bw-native
```

The generator parses all emitted artifacts back before reporting success.
`manifest.json` contains source/native IDs, model metadata, TXD slot/name plans,
and IMG offsets. `validation.json` and `validation.txt` contain counts, bounds,
cross-reference results, archive sector data, pool budgets, and required model
store capacities. They also enforce the 327,680-byte native collision read
buffer contract against every individual COL record (Bullworth's current
maximum is 256,716 bytes).

## Experimental native runtime

The native runtime is disabled by default and currently supports only two
exact GTA SA 1.0 US identities: the raw HOODLUM executable and MTA's audited
ProgramData runtime copy derived from it. It relocates the three IDE model
stores, raises the native
single-COL read buffer to 327,680 bytes, and registers the generated Bullworth
pack through GTA's own startup streaming path. Install these two runtime files:

```text
MTA\data\extended-world\bullworth\bw.ide
MTA\data\extended-world\bullworth\bw.img
```

`bw.col` and the seven binary IPLs are already entries inside `bw.img`; the
runtime does not need the JSON or text validation reports. Paths passed to the
native loaders must currently be ASCII and shorter than `MAX_PATH`.

The switch is read once, before GTA populates its model stores. In the Windows
VM, set it in the process-start environment and then launch the client from the
same PowerShell process:

```powershell
$env:MTA_NATIVE_BW_MODEL_STORES = '1'
Start-Process -FilePath 'C:\dev\mtasa-vm-custom\Bin\Multi Theft Auto.exe' `
  -ArgumentList 'mtasa://127.0.0.1:22003' `
  -WorkingDirectory 'C:\dev\mtasa-vm-custom\Bin'
```

Rollback is a clean restart with the variable removed (or set to anything
other than the exact value `1`):

```powershell
Remove-Item Env:MTA_NATIVE_BW_MODEL_STORES -ErrorAction SilentlyContinue
```

The startup hook always lets GTA load its stock CD directories first. It then
parses the complete IDE and IMG directory and verifies the executable patch,
stock occupancies, free IDs, pool capacities, planned streaming slots, all 166
new TXD names, archive availability, IMG bounds, entry names, and entry sizes
before changing native state. It also verifies the exact reviewed SHA-256 of
both runtime files, including every streamed payload sector. Registration is
process-global and deliberately survives resource stops, reconnects, and
streaming reinitialization. The largest native IMG entry is `bw.col` at 4,007
sectors, so GTA initially rounds its split streaming buffer to 4,008 sectors.
MTA's script-managed IMG inventory cannot see this native archive; while the
registrar is active, its central buffer setter therefore preserves that 4,008
sector minimum across disconnect cleanup and logs any smaller request as
`[NativeBW] streamingBuffer=request-clamped`. Do not run
the old `ug-bw` custom-streaming resource in the same process because it owns
the same models and placements through a different lifecycle.

Failures before the IDE commit are fail-soft: the registrar either has not
mutated state or rolls back its archive and empty TXD slots, logs
`[NativeBW] registrar=refused`, and continues with the stock world. Once GTA has
constructed the IDE model entries, there is no safe complete inverse. A failed
postcondition therefore logs `[NativeBW] registrar=fatal` and terminates that
opt-in process instead of continuing with partially registered native state.
Success logs `[NativeBW] registrar=active` with archive 6, 952 models, 166 TXDs,
the actual planned TXD range and span holes, the exact planned COL slot, the
ordered IPL slot list, and 1,126 verified directory entries. The preceding
pool diagnostics give
the selected profile, capacity, occupied/free counts, allocation cursor,
highest occupied slot, holes below that high-water mark, and the simulated
plans. The strict stock layouts currently plan COL slot 252 and IPL slots
`191,192,193,194,195,196,197`; registration refuses any reordered or holey
layout instead of assuming those IDs. The audited current TXD runtime reports
capacity 5000, occupied 3608,
cursor/highest 3607, zero holes, and therefore allocates Bullworth at
`3608..3773`.

GTA creates every IMG-backed IPL slot with dynamic streaming disabled. Stock
`*_stream` IPLs are enabled when their parent text IPL is linked, but these
seven standalone district IPLs intentionally have no text parent. The
registrar therefore enables dynamic streaming for exactly its seven owned
slots after directory validation. It leaves their rectangles in the native
flipped state so the later stock `LoadAllRemainingIpls` startup pass still
calculates their real bounding boxes, inserts them into GTA's IPL quadtree, and
unloads them for normal position-driven streaming.

### In-game checkpoint

Use a clean process with the old `ug-bw` resource stopped. With the switch
enabled, confirm an `active` registrar line before connecting, then test spawn,
teleport or flight to Bullworth, collision in all seven districts, district
transitions, leaving and re-entering the city, one resource restart, and two
disconnect/reconnect cycles. Finish with a clean restart with the switch off
and verify normal San Andreas. The current generated IPLs intentionally have
all `lod_index` values set to `-1`; this checkpoint validates native spatial
streaming and collision, not full long-distance LOD equivalence with GTA UG.

When enabled, the patch refuses unsupported or modified executables before its
first allocation or executable write. The relocated allocations intentionally
remain alive for the process because MTA attaches after GTA's CRT static model
arrays have already been constructed.

The executable allowlist is exact, not signature-only:

| Identity | SHA-256 | Image size | Timestamp | Checksum |
| --- | --- | ---: | ---: | ---: |
| `hoodlum-raw` | `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b` | `0x008B1000` | `0x427101CA` | `0x00DC5BEA` |
| `mta-programdata` | `77485627b4ef17f92819318050d501e171c7ab84ceffe5091b973b9e29f9cc98` | `0x01177000` | `0x437101CA` | `0x00DC29E6` |

Both also require PE32/i386, image base `0x00400000`, and every constructor,
CRT wrapper, pointer, grower, and collision-site byte from the manifest. The
ProgramData variant's appended `.text`, `.init`, `.data`, and `.HOODLUM`
sections do not change those audited sites. Preflight refusal reasons are
written to MTA's debug-event logfile as well as the debugger output.

### Why the FLA CRT count patches are validation-only

FLA also changes sites that OLA does not: Atomic's vector count immediates at
`0x4C5CBB+1` and `0x4C5845+1`, redirects DamageAtomic's constructor/destructor
at `0x4C5D10` and `0x4C5860`, and changes Time's count immediates at
`0x4C5DDB+1` and `0x4C58A5+1`. That is necessary for an ASI loaded before the
GTA CRT initializes the inline stores.

MTA's `game_sa` module is created from the `Direct3DCreate9` hook, after those
CRT constructors ran. The HOODLUM wrappers at `0x84BBF0`, `0x84BC10`, and
`0x84BC50` call the three constructor routines once and register shutdown
wrappers at `0x856230`, `0x856240`, and `0x856260`; those wrappers branch to the
three destructors only during CRT termination. The runtime therefore manually
constructs every new slot and keeps the relocated arrays process-lifetime,
while leaving the already-registered destructors aimed at the original static
arrays. Repointing only those late destructor wrappers would instead make them
destroy storage that the CRT never constructed.

The checked-in manifest validates 16 bytes at all six FLA-only routines and the
complete 10-byte `mov ecx` plus `call`/`jmp` sequences at all six CRT wrappers.
Any changed target refuses the feature before allocation or executable writes.

The validator also inventories every executable immediate equal to each store
base, array base, and Atomic's special `+0x1C` field. Atomic's non-aliased
one-past address (`0xB1BF54`) has no executable references. DamageAtomic and
Time end at `0xB1C934` and `0xB1E128`, which are the bases of adjacent stock
stores; their add paths are count-driven, so those aliased addresses are not
repointed. MTA's later `CModelInfoSA::StaticSetHooks` only hooks
`NodeNameStreamRead` and does not overlap any manifest instruction.

Run the focused round-trip test with:

```sh
python3 -m unittest utils/extended-world/tests/test_build_native_bw_pack.py
```

The test skips when the ignored local Bullworth assets or stock GTA root are
not present.
