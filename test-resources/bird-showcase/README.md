# Scriptable bird showcase

A short cinematic demo for Neon's managed `bird` element API.

## Run

```text
start bird-showcase
/birdshow
```

Use `/birdshow stop` to abort and restore the player immediately. `/birdshow shoot` starts a separate short interactive shot-event demo at the player's current position.

The main sequence is automatic and lasts about 30 seconds:

1. **96 Lua-controlled birds** sweep toward the camera in one flock — immediately proving the feature exceeds GTA SA's six native ambient-bird slots.
2. The flock splits into two coordinated vortices using live target-velocity changes.
3. One persistent bird breaks formation, turns red, grows, moves to camera and freezes while every other bird keeps flying.
4. All 96 birds converge into a large aerial **NEON** formation.
5. The camera pulls back on the final capability card: position, flight, size, color and shot events.

## Recording notes

- Record 16:9. The camera and captions are authored for a widescreen frame.
- No custom DFF, texture, shader or effect is used; the visual is entirely the managed bird renderer and Lua control API.
- Start recording just before `/birdshow`; trim the staging frame if desired.
- The broad-user claim should be readable without code knowledge: this is not merely "more ambient birds" — every visible bird is an individually addressable Lua element whose flight and appearance can change live.

## Shot-event clip

Run `/birdshow shoot`, aim at the generated flock and fire. Each hit goes through `processBirdGunShot` and emits cancellable `onClientBirdShot`; the default behavior destroys the exact bird element that was hit.
