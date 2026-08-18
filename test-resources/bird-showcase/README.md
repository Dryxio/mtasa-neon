# Scriptable bird showcase

A short cinematic demo for Neon's managed `bird` element API.

## Run

```text
start bird-showcase
/birdshow
```

Use `/birdshow stop` to abort and restore the player immediately. `/birdshow shoot` starts a separate short interactive shot-event demo at the player's current position.

The main sequence is automatic and lasts about 30 seconds:

1. 96 Lua-controlled birds sweep toward the camera in one flock.
2. The flock splits into two coordinated vortices using live target-velocity changes.
3. One persistent bird breaks formation, changes appearance, moves to camera and freezes while every other bird keeps flying.
4. All 96 birds converge into a large aerial **NEON** formation.
5. The camera pulls back for the final clean shot.

## Recording notes

- Record 16:9.
- The showcase deliberately contains no captions, capability cards, debug text or explanatory overlays; the footage is gameplay-only.
- HUD and chat are hidden during the cinematic and restored afterward.
- No custom DFF, texture, shader or effect is used; the visual is entirely the managed bird renderer and Lua control API.
- Start recording just before `/birdshow`; trim the staging frame if desired.

## Shot-event clip

Run `/birdshow shoot`, aim at the generated flock and fire. The clip intentionally shows only the gameplay result of shooting the managed birds, without explanatory UI.
