# Native cargo adapter

A client can make its local player, a local ped, or a ped it currently synchronizes
hold an MTA object using GTA's actual secondary HoldEntity task. Walking remains
available. Put-down uses GTA's primary script-command task and adopts the secondary
hold, including put-down initiated by the player's native exit-vehicle button.

This first preset begins holding immediately: it does not approach the crate or
play a ground-pickup sequence. `box` accepts model 1271 only, within 3 metres of a
living, grounded, on-foot, streamed holder in the same interior and dimension.
The object must be streamed, unattached and at its original unit scale. Remote players, non-owner synced
peds, occupied secondary partial-animation slots, duplicate holders/objects,
and unknown presets are rejected. Preset suitability still needs visual testing.

## Client API

```lua
local accepted = setPedCarryObject(ped, object, "box")
local objectOrFalse, state = getPedCarriedObject(ped)
local accepted = putDownPedObject(ped)
local cancelled = cancelPedCarryObject(ped)
```

`state` is `starting`, `holding`, `putting_down`, or `released`. Successful start
means native submission, not observed animation. `onClientPedCarryStateChange`
has the holder as source and arguments `(object, state, reason)`. Holding requires
a processed native task with an animation association. Released means that the
native task no longer retains the object, or the adapter explicitly released it.
It does not mean the crate reached a delivery destination.

Only the submitting Lua VM can request put-down or cancellation. Resource stop
also releases objects created by other resources. Death, damage, ownership loss,
vehicle entry, water, a sustained fall, conflicting attachment/interior/dimension,
and native task replacement release the lease. Startup and put-down have bounded
5/8-second recovery. These deadlines only recover failure; they never manufacture
an animation-completion acknowledgement.

Model teardown, object destruction and resource stop release synchronously before
GTA cleanup, without invoking reentrant Lua events during destruction. Consumers
must also observe element/resource lifecycle events or query the carried object.
Streaming back in never silently reapplies a cancelled hold.

The adapter restores collision, native visibility, streaming protection, physical
attachment, mission ownership and frozen state, preserving the final native
transform in MTA. Forced interruption recovers to the last reachable pose; native
put-down preserves the observed drop pose. Retail deleting destructors are guarded for leased MTA objects:
mission classification alone does not prevent GTA's destructor from marking an
unreleased object for removal. No raw pointers, opcodes or animation IDs are exposed.

## Server reservation example

Start this resource on a development server, then use:

- `/cargospawn`: spawn at Grove Street on an otherwise empty test server.
- `/cargotest`: create a crate for the local player.
- `/cargocarry`: request the fixture's reservation and native hold.
- `/cargodrop`: request native put-down; `/cargocancel`: release immediately.
- `/cargorejections`: exercise rejected arguments on the running client.
- `/cargonpc`: replace the fixture with a synced NPC and crate; then `/cargocarry`.

This resource intentionally offers test-fixture commands to connected players;
it is not a production mission or a reward API.

The server owns the original object and chooses one executor. It validates
identity, sync ownership, proximity, dimension, phase and monotonically changing
reservation tokens. Stale/observer reports cannot take over or cancel another
reservation. Failed execution and put-down expire; disconnect and ownership
transfer release instead of retrying invisibly. Late observers request a snapshot.
The server derives recovery positions from observed holder positions, never from
client-supplied coordinates. No `delivered` message grants money or progress.

The executor carries a client-local MTA proxy with the native task. Observers use
an explicitly approximate hand-position proxy from the accepted server state;
remote peds receive no native task submissions. Thus the example does not claim
native pickup/put-down animation replication fidelity. Client object transforms
are not themselves a synchronized cargo relationship. The server object reappears
at the server's bounded recovery point after release.

## Verification

```sh
lua test-resources/native-cargo/tests/state_spec.lua
lua test-resources/native-cargo/tests/server_spec.lua
luac -p test-resources/native-cargo/state.lua test-resources/native-cargo/server.lua test-resources/native-cargo/client.lua
xmllint --noout test-resources/native-cargo/meta.xml
```

The read-only `tests/retail_layout.py /path/to/gta_sa.exe` additionally verifies
the retail vtables and constructor call sites on disk.

The state spec covers contention, spoofed executors, stale tokens, phase skipping,
unknown delivery claims, timeouts and lease reuse. It cannot prove GTA behavior.
The C++ build asserts the 0x3C retail task layout; runtime setup checks all three
cargo vtables' abort/process/position entries before installing the ownership guard.

Gameplay acceptance remains a manual gate: local player and synced NPC, second
observer and late join, hold/walk/native-button/API drop, combat interruption,
vehicle attempt, death, water/fall, resource restart, stream-out, object destruction
and model replacement. Verify collision/visibility and that dropped crates remain
reachable. Also try restarting the submitting resource while holding an object
created by another resource, then picking the same object up again.

The engine catalogue is pinned to committed source snapshots. These new
registrations require a catalogue rebuild against the engine checkpoint;
do not treat the existing catalogue's missing symbols as evidence of missing code.


Verified checkpoint (2026-09-06): Game SA and Client Deathmatch compile in
Release|Win32; the Game SA hook check passes. The 12 canonical/VM source hashes
match. The Lua specs pass 39 assertions; Lua syntax, XML, client registrations and
retail executable checks pass. An isolated Windows x64 server starts, stops and
restarts the resource and listens on UDP 22003 / HTTP 22005. The established
runtime did not finish startup in the smoke-test window; the isolated runtime
uses the same server binaries with only this resource. No client gameplay or
second-observer animation validation has been performed.
