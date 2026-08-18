# Managed object fracture showcase

Run:

```text
start break-showcase
/breakshow
```

Use `/breakshow stop` to abort and restore the player immediately.

The sequence is intentionally UI-free: it stages several ordinary GTA object models, fractures them in a travelling wave with different fragment counts/impulses, then destroys a denser mixed-scale finale while the camera moves around the debris.

## Interactive playground

Spawn any GTA object in front of you and configure its fracture settings with `key=value` options:

```text
/breakspawn <model> [key=value ...]
/breaknow
/breakclear
```

Example:

```text
/breakspawn 1337 fragments=24 force=7 randomness=1.4 bounce=.4 lifetime=12000 scale=1.2
/breaknow
```

Supported keys: `fragments`, `force`, `randomness`, `lifetime`, `gravity`, `bounce`, `drag`, `renderDistance`, `seed`, `scale`, `vx`, `vy`, `vz`, `hideOriginal`, `disableOriginalCollision`.

`/breaknow` uses the player's current position as the impact point so the debris impulse naturally travels away from the player.

No custom DFF, TXD, shader or fracture metadata is required. Every fragment is generated from the object's live RenderWare geometry, keeps the source UV/material texture data, and is simulated by Neon's managed break-effect system rather than GTA SA's fixed native breakable pool.
