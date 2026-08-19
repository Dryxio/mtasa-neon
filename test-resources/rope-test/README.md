# Managed rope test harness

This resource exercises the managed Rope API independently from a gameplay resource.

## Automated run

Start `rope-test`, join the server, then run:

```text
/ropetest all
```

Both sides report explicit results:

```text
[ROPETEST] PASS basic.create: userdata
[ROPETEST] PASS client.native.active: true
[ROPETEST] PASS client.leasing.native-cap: active=8 logical=12
[ROPETEST] FAIL client.holder.follow: error=...
```

The automated run covers:

- synchronized `rope` element creation and destruction;
- type, duration, remaining time, fixed-node, ground, winch-height and velocity state;
- client reception of authoritative rope custom data;
- native GTA rope activation and `getRopePositionAt` interpolation;
- holder + local-offset tracking;
- object and vehicle carried-element state;
- client-local rope creation and mutation;
- server expiry;
- twelve logical ropes with no more than eight native leases;
- coexistence with the legacy `createSWATRope` path.

Individual cases:

```text
/ropetest basic
/ropetest holder
/ropetest pickup
/ropetest leasing
/ropetest local
/ropetest legacy
/ropetest late
/ropetest status
/ropetest reset
```

## Late join

`/ropetest late` creates a synchronized rope for 30 seconds. Join with another client while it is alive and verify that the second client receives the existing `rope` element and a reduced remaining lifetime rather than a restarted timer.

## API shape exercised

```lua
local rope = createRope(x, y, z, {
    type = "miniMagnet",
    holder = vehicle,
    holderOffset = {0, 0, -1},
    duration = 10000,
    fixedNode = 0,
    sitOnGround = false,
    winchHeight = 0.5,
    topVelocity = {0, 0, 0},
    carriedElement = object,
    physics = true,
})

setRopeTopVelocity(rope, Vector3(0.001, 0, 0))
setRopeWinchHeight(rope, 0.35)
attachElementToRope(rope, object)

-- Client-only native solver inspection:
local position, velocity = getRopePositionAt(rope, 0.5)
local active = isRopeActive(rope)
```

Synchronized ropes are authoritative on the server. Clients may inspect them but mutation functions return `false`; client-created ropes are locally mutable.

## Manual physics checks

The automated suite intentionally avoids asserting exact physical trajectories because object/vehicle sync ownership can migrate. Run `/ropetest pickup` and visually verify the following on the current sync owner:

- the `miniMagnet` rope obtains a native hook and picks up the scripted object when that client owns object sync;
- a non-syncing client sees the same logical carried element but does not fight the networked object's transform;
- changing object/vehicle sync owner transfers physical authority without deleting the `rope` element;
- `detachElementFromRope` releases the native carried entity on the owning client;
- destroying or restarting `rope-test` releases managed native leases and leaves no hook/carried object stuck in GTA's rope pool.

For a two-client authority test, keep `/ropetest pickup` active while causing the carried object's sync owner to change, then use `/ropetest status` and the visible object motion to check that only the current sync owner applies rope physics.

## Native-pool behavior

GTA:SA still owns a fixed eight-entry `CRopes` pool. Managed ropes do not patch or steal active stock slots. More than eight logical `rope` elements are allowed; the client leases free native slots to relevant nearby ropes and leaves the others logical-only until a slot becomes available.

`/ropetest leasing` is the regression test for that contract.
