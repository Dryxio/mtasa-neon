# Managed fire test harness

This resource exercises the managed fire runtime without depending on a gameplay resource.

## Automated run

Start `fire-test`, join the server, then run:

```text
/firetest all
```

The server and client print explicit lines such as:

```text
[FIRETEST] PASS duration.get: 5000
[FIRETEST] PASS client.sync.element: fire element received
[FIRETEST] FAIL damage.cancel-event: true
```

The automated run covers managed element creation, duration/remaining time, live strength changes, damage target masks, source/target setters, client synchronization, server expiry, one-generation spread, more than 60 simultaneous managed fires, local damage-mask behavior and cancellable `onClientFireDamage` behavior.

Other commands:

```text
/firetest basic
/firetest late
/firetest status
/firetest reset
```

`/firetest late` creates a 30-second synchronized fire for manual late-join/stream validation. Join with another client while it is active and verify the second client receives the same `fire` element with a reduced remaining lifetime rather than a restarted lifetime.

## API shape exercised

Server and managed client-local fires use the options-table form:

```lua
local fire = createFire(x, y, z, {
    duration = 10000,
    strength = 1.5,
    damage = true,
    damageTargets = {
        players = true,
        peds = true,
        vehicles = false,
        objects = false,
    },
    spread = false,
    maxGenerations = 0,
    source = localPlayer,
    target = nil,
})
```

The legacy client call `createFire(x, y, z [, size])` remains the native GTA fire path and still returns a boolean.

## Manual checks

After `/firetest all`, additionally verify:

- `strength` values around `1`, `2` and `3` visibly select small, medium and large GTA fire FX.
- moving a target element makes a targeted managed fire follow it.
- moving the local player to another dimension/interior hides a managed fire until the contexts match again.
- normal GTA/Molotov fires still use the native fire runtime and are unaffected by managed-fire settings.
- stopping `fire-test` removes every fire owned by the resource and leaves no visible FX behind.
