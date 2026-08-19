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
2. a client-local crate is created for deterministic native rope physics;
3. the hero magnet rope picks the crate up with `attachElementToRope`;
4. `setRopeWinchHeight` lowers and raises the suspended load while the scripted top anchor moves sideways, producing native rope/cargo motion;
5. the two secondary rope types animate independently in the same shot;
6. `detachElementFromRope` releases the crate and the native GTA object continues moving with the release velocity;
7. the camera follows the release and pulls back for the final frame.

The showcase intentionally uses a client-local cargo object. Local elements are authoritative by construction in the managed Rope runtime, and GTA's native `CRope::PickUpObject` promotes a picked-up object into the moving physical-object path. This keeps the recording resource deterministic and removes any dependency on server object-sync election.

Synchronized object/vehicle authority, leasing and multiplayer state are covered separately by `rope-test`; the showcase is only the visual demonstration resource.

## Recording intent

The full sequence is about 33 seconds. For a short README clip, the strongest section starts around the winch movement and runs through the crate release.

Before recording, run:

```text
/ropetest all
```

and confirm the Rope harness has no `FAIL` lines.
