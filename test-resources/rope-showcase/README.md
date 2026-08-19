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
2. three invisible client-local objects act as the required physical holders for those native winch/crane rope types;
3. a client-local crate is created for deterministic native rope physics;
4. the hero magnet rope picks the crate up with `attachElementToRope`;
5. `setRopeWinchHeight` lowers and raises the suspended load while the hero holder moves sideways, producing native rope/cargo motion;
6. the two secondary holder/rope pairs animate independently in the same shot;
7. `detachElementFromRope` releases the crate and the native GTA object continues moving with the release velocity;
8. the camera follows the release and pulls back for the final frame.

The showcase intentionally uses client-local holder and cargo objects. Local elements are authoritative by construction in the managed Rope runtime, and GTA's native `CRope::PickUpObject` promotes a picked-up object into the moving physical-object path. This keeps the recording resource deterministic and removes any dependency on server object-sync election.

Native GTA rope types 1 through 7 require a valid physical holder: `CRope::Update` dereferences that holder during its weight/force pass. Managed ropes of those types remain logical but are not native-leased until an authoritative holder is available. `swat` is the free world-anchored type.

Synchronized object/vehicle authority, leasing and multiplayer state are covered separately by `rope-test`; the showcase is only the visual demonstration resource.

## Recording intent

The full sequence is about 33 seconds. For a short README clip, the strongest section starts around the winch movement and runs through the crate release.

Before recording, run:

```text
/ropetest all
```

and confirm the Rope harness has no `FAIL` lines.
