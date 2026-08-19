# Managed rope showcase

A short, broad-audience cinematic demo for the managed Rope API.

## Run

Start `rope-showcase`, join the server, then run:

```text
/ropeshow
```

Stop early with:

```text
/ropeshow stop
```

The resource moves the player into an isolated dimension, hides the normal HUD/chat, runs the sequence, then restores the original player state automatically.

## Location

The scene uses the same clear Las Venturas runway coordinates as `2dfx-showcase`:

```text
center ~= 262.375, 2502.074, 16.484
```

It remains in its own dimension.

## Design

The demo deliberately uses **one native rope at a time**. Each act creates its holder, rope and payload immediately before its shot, waits for the native lease to become active, then cleans the entire act before moving on. This keeps the screen readable and avoids off-camera rope physics accumulating before the viewer sees them.

The choreography uses simple continuous phases rather than constantly moving every parameter at once. Payloads are frozen only while they are stationary props; they are unfrozen immediately before native pickup.

## Act 1 — Mini Magnet: pick up objects

A crate is visibly sitting on the runway while the mini-magnet hook hangs several metres above it.

1. the rope lengthens vertically until the magnet reaches the crate;
2. `attachElementToRope` picks the crate up;
3. the rope shortens and hoists the crate;
4. the physical holder translates sideways so the suspended crate follows and swings;
5. `detachElementFromRope` releases the crate and it falls back to the runway.

This demonstrates script-controlled rope length, object pickup/release, moving holders and native carried-object physics in one immediately readable shot.

## Act 2 — Wrecking Ball: native rope physics

A native wrecking-ball hook hangs beside a small stack of crates.

1. the ball is held still long enough to identify it;
2. the invisible physical holder moves laterally, launching a visible rope/weight swing;
3. the crate stack is kicked apart at the impact beat to make the collision moment readable on video.

The important part of the shot is the native weighted hook and rope reacting to holder movement. The prop burst is only presentation choreography.

## Act 3 — Harness: lift vehicles

A Bobcat is clearly parked on the runway and the harness hangs well above it.

1. the harness descends toward the vehicle;
2. `attachElementToRope` attaches the Bobcat only during this act;
3. the rope shortens and lifts the vehicle several metres off the ground;
4. the holder then moves sideways so the suspended vehicle follows the rope.

The final card summarizes the broader API surface: objects, vehicles, native hook types, moving holders and explicit pickup/release.

## Native-holder safety

Native GTA rope types 1 through 7 require a valid physical holder because `CRope::Update` dereferences `m_pRopeHolder` during its force pass. Every act therefore uses an invisible client-local physical holder. `swat` remains the free world-anchored native type.

Synchronized object/vehicle authority, leasing, lifetime and multiplayer behavior are covered separately by `rope-test`; this resource is deliberately a deterministic visual demo.

## Before recording

After rebuilding the Rope holder-safety fix, run:

```text
/ropetest all
```

and confirm there are no `FAIL` lines.
