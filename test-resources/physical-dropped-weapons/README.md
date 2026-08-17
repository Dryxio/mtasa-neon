# Physical dropped weapons test resource

Validation/showcase resource for Neon dynamic object physics and object velocity sync.

## Setup

Copy `physical-dropped-weapons` into the server resources directory and start it on a Neon client/server build containing this checkpoint.

## Commands

- `/physdrop` — drops the currently selected supported weapon; falls back to an AK-47 when unarmed/unsupported.
- `/physdrop <weaponId> <ammo>` — drops a chosen weapon model and stores its test ammo server-side.
- `/physpickup` — picks up the closest dropped weapon within 2.5 metres.
- `E` — same as `/physpickup`.
- `/physclear` — removes all test drops.

The test command intentionally does not remove the weapon from the player, so the same drop can be spawned repeatedly while tuning physics.

## Acceptance checks

1. Stand on flat ground and run `/physdrop 30 90`. The AK should leave the player with forward/up velocity, rotate, collide, bounce slightly and settle.
2. Drop from a roof or stairs. The final server position should follow the actual physics result rather than the creation point.
3. Push a settled weapon with a player or vehicle. Object sync should resume while it moves and become quiet again once velocity/turn velocity reach zero.
4. Connect a second client. Both clients should see the same final transform; the server-selected object syncer sends position, rotation, linear velocity and angular velocity.
5. Disconnect the current nearby syncer while a weapon is moving. The replacement syncer receives the last server velocity/turn velocity in `CObjectStartSyncPacket` and continues from that state.
6. Pick the weapon up with `E`. The server validates element ownership, dimension/interior and distance, removes the authoritative drop before granting the weapon, then destroys the physical object.

## Notes

`setObjectDynamicPhysics(object, true)` is server-side and opt-in. Normal MTA objects retain the old ObjectSync behaviour and do not send velocity fields. The physical state is reapplied when GTA recreates the streamed `CObject`, which matters for weapon models that otherwise receive the 99999-mass/no-gravity fallback from `object.dat` handling.
