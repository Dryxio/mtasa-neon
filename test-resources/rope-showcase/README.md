# Managed rope showcase

A broad-audience cinematic demo for the managed Rope API, built to show not only the native GTA rope types but how Neon's engine-level systems compose with each other.

## Run

```text
start rope-showcase
/ropeshow
```

Stop early with:

```text
/ropeshow stop
```

The resource stages the hidden player in its own dimension on the same clear Verdant Meadows runway used by the 2DFX and fracture showcases, switches the local scene to night, hides HUD/chat, then restores everything when the sequence ends.

## Design rule

The Rope API stays the subject of every shot. Secondary Neon APIs are only used as visible consequences or environmental dressing.

Only **one native rope is active at a time**. Each act creates its holder, rope and payload immediately before the shot, waits for the native lease to become active, then destroys the whole act behind a short black cut before creating the next one.

Holder translation uses MTA's `moveObject` rather than per-frame teleports. Rope length remains script-controlled, while GTA's native `CRope` solver is left to produce the suspended motion.

## Act 1 — Mini Magnet / control

A crate is clearly resting on the runway with the mini-magnet several metres above it.

1. the rope lengthens vertically until the magnet reaches the crate;
2. `attachElementToRope` gives the crate to GTA's native rope physics;
3. the rope shortens and hoists it;
4. the holder performs one smooth `moveObject` translation so the suspended crate follows and swings;
5. once the pickup is established, a **client-local managed fire** is created with the crate as its target, so the same moving payload visibly burns without enabling damage or spread;
6. `detachElementFromRope` releases the still-burning crate.

The shot demonstrates pickup/release, live rope length, moving holders, carried-object physics and managed-fire targeting without changing the main Rope story.

## Act 2 — Wrecking Ball / destruction

A native wrecking-ball hook faces GTA model **3175**, the same air-stream/caravan-style model used by `break-showcase`. A small flock of managed birds circles the target.

1. the ball is held still long enough to identify the native weighted hook;
2. the holder pulls back, then performs one fast `moveObject` launch across the target;
3. the script samples the real rope-end position with `getRopePositionAt`;
4. when the ball reaches the target radius, `createObjectBreakEffect` fractures the caravan from its live RenderWare geometry with 56 fragments;
5. the nearby managed birds immediately accelerate outward/upward from the impact.

This is the hero shot: **native rope physics -> visible impact -> runtime fracture -> reactive flock**. A late fallback impact exists only so a recording cannot end with no break if the native swing varies slightly on a machine.

## Act 3 — Harness / scale

A Bobcat is grounded from its real bounding box and parked under the harness. Two real GTA lamp posts frame the scene.

1. the harness starts clearly above the vehicle;
2. the rope descends vertically;
3. the Bobcat is unfrozen only immediately before `attachElementToRope`;
4. the rope shortens and visibly lifts the vehicle off its wheels;
5. the holder then performs one smooth sideways `moveObject` transport while the vehicle hangs;
6. the lamp posts use their **native model 2DFX light**, recolored to an amber `warnlight` with larger corona/range for subtle construction-site dressing.

The final card summarizes the composition shown in the clip: objects, vehicles, fracture, fire, birds and native 2DFX around the Rope API.

## Safety / cleanup

Native GTA rope types 1 through 7 require a valid physical holder because `CRope::Update` dereferences `m_pRopeHolder` during its force pass. Every act uses an invisible client-local physical holder. `swat` remains the free world-anchored native type.

All fires, birds, break effects, ropes, payloads and local scenery are resource-owned and destroyed between acts. The 1226 2DFX mutations are explicitly reset during cleanup, and the player's original local time is restored at the end.

Synchronized authority, leasing, lifetime and multiplayer correctness remain the responsibility of `rope-test`; this resource is the visual composition demo.

## Before recording

Use a client containing the merged Rope holder-safety fix, then run:

```text
/ropetest all
```

and confirm there are no `FAIL` lines before recording `/ropeshow`.
