# Native ped go-to test

This resource exercises the first Neon story primitive without involving the
Tagging Up Turf state machine.

## API under test

```text
setPedGoTo(ped, Vector3 target [, string movement = "walk", float radius = 0.5, float slowdownRadius = 2.0, int timeout = -2])
```

The client call returns a boolean and only accepts a living, streamed ped simulated by that client: the local player, a client-local ped, or a server ped for which the client is the current syncer. Movement is `walk`, `run`, or `sprint`; the radius must be positive and the slowdown radius must be at least as large. Timeout `-2` creates the untimed task, `-1` selects the SCM-compatible 20-second timeout, and non-negative values create a timed task. The call replaces the ped's primary task. This harness cancels it through the existing `killPedTask(ped, "primary", 3, false)` API because native task handles are not implemented yet.

## Commands

- `/nativegoto [walk|run|sprint]` creates Sweet near the player and assigns the
  player as his syncer. Sweet should travel ten metres using GTA pathfinding.
- `/nativegotocancel` removes the primary task while it is running.
- `/nativegotocleanup` destroys the test ped.

The result is reported as `arrived`, `timeout_relocated`, `cancelled`,
`ended_outside_radius`, `destroyed`, or `refused`, with horizontal distance,
vertical delta, 3D distance, and elapsed-time diagnostics when applicable.

`ended_outside_radius` is deliberately not called an interruption. GTA may end
the underlying go-to task after detecting that the ped overshot or circled the
target. Until the synchronized task manager retains the native success flag,
task disappearance alone cannot distinguish that condition from replacement by
another primary task.

## Assembly gate

The implementation was checked against the local compact GTA SA 1.0 executable
(SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`).
Opcode `05D3` is handled at `0x4907CE`. Its timed path constructs
`CTaskComplexGoToPointAndStandStillTimed` at `0x6685E0` with a `0.5` target
radius and `2.0` movement-state radius. A finite timeout relocates the ped to
the target when it expires; it is not an error result in the original game.

## Manual protocol

1. Start this resource on the local test server.
2. Run `/nativegoto walk` on open, level ground.
3. Verify that Sweet walks rather than sliding or teleporting, stops within
   roughly `0.75 m`, and produces exactly one `arrived` result.
4. Repeat with `run` and `sprint`.
5. Start another walk and run `/nativegotocancel` before arrival. Sweet must stop
   and the resource must report `cancelled` once.
6. Run `/nativegotocleanup` and confirm no ped remains.

Gameplay testing is intentionally manual; Codex only builds and deploys the
resource, then asks the user to perform this protocol in-game.
