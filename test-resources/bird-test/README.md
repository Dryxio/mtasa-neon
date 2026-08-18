# Managed bird test harness

Client-side regression harness for Neon's managed `bird` elements.

## Automated run

```text
start bird-test
/birdtest all
```

The resource prints explicit `PASS` / `FAIL` lines. The main run covers creation/type identity, property round-trips, movement/freeze behavior, invalid inputs and **128 simultaneous managed birds**, well beyond GTA SA's six native ambient-bird slots.

Useful commands:

```text
/birdtest basic
/birdtest stress [count]
/birdtest shoot
/birdtest shootcancel
/birdtest status
/birdtest reset
```

`/birdtest shoot` creates one large red stationary bird in front of the player. Shoot it and verify `onClientBirdShot` fires and the element is destroyed. `/birdtest shootcancel` repeats the check with a blue bird and cancels the event, so the same element must survive.

The harness forwards `onClientPlayerWeaponFire` traces through `processBirdGunShot`. That helper is also useful for custom weapon/bullet systems that want to participate in managed-bird hit testing.

## Manual checks

- `normal`, `water` and `desert` presets look visibly distinct in size and flight cadence.
- birds fade near their configured render distance instead of being destroyed.
- dimension/interior mismatch hides birds while preserving their element identity.
- stopping/restarting the resource removes all owned birds.
- GTA's ordinary ambient birds continue to spawn and behave independently.
