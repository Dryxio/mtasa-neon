# Managed object fracture showcase

Run:

```text
start break-showcase
/breakshow
```

Use `/breakshow stop` to abort and restore the player immediately.

## Runway video demo

`/breakshow` is a UI-free destruction-runway sequence on the abandoned-airport strip between:

```text
100.74450, 2501.11401, 16.48438
424.00473, 2503.03442, 16.48438
```

The hidden player is staged near the runway midpoint in a private dimension while the camera performs a continuous ~36 second travelling shot. Roughly thirty ordinary GTA object props are distributed along the strip, automatically grounded, varied in scale and offset, and fractured as camera progress reaches them rather than through fixed timers.

The sequence opens with a large model 1948 proof shot, accelerates into a mixed destruction gallery with different fragment counts/forces/bounce profiles, then ends with a compact six-object cascade. HUD/chat are hidden for capture and the player is restored automatically at the end.

## Interactive playground

Spawn any GTA object in front of you and arm it with a managed damage profile:

```text
/breakspawn <model> [key=value ...]
/breakobject <model> [key=value ...]
/breakhp
/breaknow
/breakclear
```

Example:

```text
/breakspawn 1948 health=500 fragments=24 force=4 randomness=.8
```

The object is automatically placed on the ground. Press `R` to raise the current playground object by `0.10m` per press when a model has an unusual visual base.

Then shoot the object or hit it with normal GTA damage. The managed profile consumes the native per-impact object damage and creates the generic fracture effect when its durability reaches zero or a native-style single-hit threshold is exceeded. `/breakhp` prints the remaining managed health and `/breaknow` remains a manual bypass.

Durability keys: `health`, `native`, `damageMultiplier`, `instantBreakThreshold`.

Fracture keys: `fragments`, `force`, `randomness`, `lifetime`, `gravity`, `bounce`, `drag`, `renderDistance`, `seed`, `vx`, `vy`, `vz`, `hideOriginal`, `disableOriginalCollision`.

Placement keys: `distance`, `scale`, `groundOffset`.

No custom DFF, TXD, shader or fracture metadata is required. Every fragment is generated from the object's live RenderWare geometry, keeps the source UV/material texture data, and is simulated by Neon's managed break-effect system rather than GTA SA's fixed native breakable pool.
