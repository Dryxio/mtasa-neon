# Managed object fracture showcase

Run:

```text
start break-showcase
/breakshow
```

Use `/breakshow stop` to abort and restore the player immediately.

The sequence is intentionally UI-free: it stages several ordinary GTA object models, fractures them in a travelling wave with different fragment counts/impulses, then destroys a denser mixed-scale finale while the camera moves around the debris.

## Interactive playground

Spawn any GTA object in front of you and arm it with a managed damage profile:

```text
/breakspawn <model> [key=value ...]
/breakhp
/breaknow
/breakclear
```

Example:

```text
/breakspawn 1337 health=250 native=true fragments=24 force=7 randomness=1.4 bounce=.4 lifetime=12000 scale=1.2
```

Then shoot the object or hit it with normal GTA damage. The managed profile consumes the native per-impact object damage and creates the generic fracture effect when its durability reaches zero or a native-style single-hit threshold is exceeded. `/breakhp` prints the remaining managed health and `/breaknow` remains a manual bypass.

Durability keys: `health`, `native`, `damageMultiplier`, `instantBreakThreshold`.

Fracture keys: `fragments`, `force`, `randomness`, `lifetime`, `gravity`, `bounce`, `drag`, `renderDistance`, `seed`, `scale`, `vx`, `vy`, `vz`, `hideOriginal`, `disableOriginalCollision`.

No custom DFF, TXD, shader or fracture metadata is required. Every fragment is generated from the object's live RenderWare geometry, keeps the source UV/material texture data, and is simulated by Neon's managed break-effect system rather than GTA SA's fixed native breakable pool.
