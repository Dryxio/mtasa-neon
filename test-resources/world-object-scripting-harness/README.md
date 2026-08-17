# World object scripting harness

Client-side validation and showcase resource for native GTA dynamic world objects exposed as `worldobject` elements.

## Showcase flow

### Live physics object

1. Aim at a native physics object such as a Grove Street box and use `/wotarget`.
2. A blue halo, 3D `PHYSICS OBJECT` label, live coordinates, and a top banner follow the object.
3. Push it normally with the player or a vehicle. The visuals track the GTA-owned object in real time.
4. Shoot or break it. Damage flashes red with the loss value; destruction displays `OBJECT BROKEN`.

This makes the scripting bridge visible without moving the object from Lua: GTA still owns the physics, while Lua observes and reacts to it.

### Physics objective

1. Select a movable object with `/wotarget`.
2. Use `/woobjective`.
3. A green destination zone is placed roughly five metres beyond the object in the direction you are facing it.
4. Physically push the object into the zone.
5. The zone and HUD switch to `OBJECT DELIVERED!` when the live `worldobject` position enters the goal.

This is a tiny gameplay example driven entirely by the native object's GTA physics and its Lua-visible transform.

## Commands

- `/wotarget` — aim the camera at a native physics object and bind the closest `worldobject` proxy; also enables the showcase visuals.
- `/woobjective` — create the green push objective for the selected object.
- `/woshowcase` — toggle the halo, 3D labels, damage/break feedback, and showcase HUD.
- `/wolist` — list currently known proxies.
- `/wopull` — move the selected proxy two metres in front of the local player.
- `/worotate` — rotate the selected proxy 30 degrees through `getElementMatrix` / `setElementMatrix`.
- `/womatrix` — dump the selected proxy matrix.
- `/wodestroy` — verify scripts cannot destroy the GTA-owned native object/proxy.
- `/woclear` — clear the selected object and local showcase state.

Damage, break, stream-in and stream-out events are also logged to chat/debug output. For the lifetime test, select an object, move far enough away to stream it out, then return; the same Lua element should report `STREAM OUT ... [TARGET PRESERVED]` followed by `STREAM IN ... [SAME TARGET]`.

This resource is not auto-deployed. Copy it to the VM server resources directory before starting it.
