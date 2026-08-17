# Runtime collision wall demo

Interactive Grove Street-friendly demo for runtime-generated model collision.

The visible object has collisions disabled. A second invisible object uses the same temporary model ID with an `EngineCOL` created entirely from a Lua table, so every physical interaction comes from the generated collision rather than from non-uniform object scaling.

## Run

Deploy this resource to the server resource directory and start `runtime-collision-wall-demo`.

Use:

- `/wall` - start a new wall and replace the previous demo wall
- left click - place the first endpoint, then lock the second endpoint
- hold `E` / `A` - extend / shorten the locked wall while continuing to move normally
- hold `U` / `I` - raise / lower the locked wall
- `P` or `/wallramp` - toggle the locked wall between a vertical wall and a sloped ramp
- `/wallwire` - toggle the generated collision outline
- `/wallreset` - remove the wall
- `/wallcar` - spawn an Infernus and warp into it for an impact test
- `/wallcarclear` - remove the test vehicle

## Suggested video flow

1. Start at Grove Street on foot and run `/wall`.
2. Draw a short, low wall and lock it.
3. Jump onto it.
4. Hold `E` while standing/walking on it so the wall extends after creation.
5. Hold `U` briefly so the top rises while the collision updates.
6. Press `P` to turn the same runtime model into a small ramp.
7. Walk down the ramp.
8. Toggle `/wallwire` briefly to show the actual generated collision, then toggle it off.

There is no presentation/marketing overlay in the resource; only normal debug chat/control hints are emitted.
