# Managed rope showcase

A short cinematic resource for recording the managed Rope API in action.

## Run

Start `rope-showcase`, join the server, then run:

```text
/ropeshow
```

Stop/reset early with:

```text
/ropeshow stop
```

The resource moves the player into an isolated dimension, hides the HUD/chat, runs the sequence, then restores the original player state automatically.

## Sequence

The shot is intentionally built around visible API behavior rather than debug text:

1. three managed ropes are created close together using native GTA rope types (`miniMagnet`, `wreckingBall`, `harness`);
2. a synchronized dynamic crate is assigned to the showcase player as its physics syncer;
3. the hero magnet rope explicitly picks the crate up with `attachElementToRope`;
4. `setRopeWinchHeight` lowers and raises the suspended load while the scripted top anchor moves sideways, producing native rope/cargo motion;
5. the two secondary rope types animate independently in the same shot;
6. `detachElementFromRope` releases the crate and native dynamic-object physics takes over for the drop;
7. the camera follows the release and pulls back for the final frame.

The cargo is server-owned and has `setObjectDynamicPhysics` enabled server-side. The sequence waits until the showcase player is the object's syncer before starting, so the Rope manager exercises the same sync-owner-safe physical path used in multiplayer rather than relying on a purely local fake.

## Recording intent

The full sequence is about 33 seconds. For a short README clip, the strongest section starts around the winch movement and runs through the crate release.

Before recording, run:

```text
/ropetest all
```

and confirm the Rope harness has no `FAIL` lines. If the showcase reports that cargo sync ownership was not assigned, stop/restart the resource and retry after the player is fully spawned.
