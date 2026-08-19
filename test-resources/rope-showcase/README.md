# Managed rope showcase

A cinematic visual demo for the managed Rope API.

## Run

Start `rope-showcase`, join the server, then run:

```text
/ropeshow
```

Stop/reset early with:

```text
/ropeshow stop
```

The resource moves the player into an isolated dimension, hides the normal HUD/chat, runs the sequence, then restores the original player state automatically.

## Location

The scene uses the same clear Las Venturas runway coordinates as `2dfx-showcase`:

```text
center ~= 262.375, 2502.074, 16.484
```

It remains in its own showcase dimension, so the Rope and 2DFX resources do not share runtime entities.

## What the shot shows

The three native rope types are separated into clear left/center/right demonstrations with world-space labels:

1. **Left — Wrecking Ball**: a `wreckingBall` rope uses GTA's native weighted ball hook. The invisible physical holder moves laterally so the ball visibly swings. Its rope length is explicitly shortened so the native ball remains well above the runway instead of initializing below terrain.
2. **Center — Mini Magnet**: a `miniMagnet` rope explicitly picks up a local crate. The rope shortens to hoist it, the holder translates while the crate follows through native rope physics, and the crate is released near the final wide shot.
3. **Right — Harness**: a `harness` rope explicitly carries a local Bobcat vehicle. During the right-side close-up the rope shortens and the vehicle is visibly lifted off the runway.

The camera first establishes all three setups, then gives each one its own close-up before returning to a final wide shot. Labels remain attached to the relevant payload so the differences are obvious while recording.

## Native-holder safety

Native GTA rope types 1 through 7 require a valid physical holder: `CRope::Update` dereferences `m_pRopeHolder` during its force pass. The showcase therefore uses three invisible client-local physical holder objects. `swat` remains the only free world-anchored native type.

Synchronized object/vehicle authority, leasing and multiplayer behavior are covered separately by `rope-test`; this resource is deliberately a deterministic visual demo.

## Before recording

After rebuilding the Rope safety fix, run:

```text
/ropetest all
```

and confirm there are no `FAIL` lines, especially the missing-holder regression and local physical-pickup checks.
