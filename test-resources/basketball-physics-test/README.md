# Basketball physics test

Playable vertical slice for validating dynamic-object basketball behavior with no custom assets.

It uses the stock basketball model, replaces its collision with one runtime sphere, creates invisible runtime collision carriers for a 20-sphere rim and box backboard, and exposes a simple aim/charge/launch loop.

## Start

1. Start the resource.
2. Run `/baskettest` while standing on flat ground and facing the direction where the hoop should be placed.
3. The ball starts attached to the player.

## Controls

- Hold RMB: aim state.
- Hold LMB while aiming: charge power.
- Release LMB: launch the ball using the ballistic solver and backspin.
- `E`: pick up a free nearby ball.
- `/basketshot [0..1]`: deterministic quick shot without charging.
- `/basketreset`: return the ball to your hands.
- `/baskettest`: rebuild the court in front of you.

## What to inspect

- The ball should detach cleanly, preserve initial linear/angular velocity, arc under gravity and bounce on world geometry.
- Backboard impacts should reflect naturally.
- Rim contacts should react against the generated ring rather than a visual-only trigger.
- The overlay should show live linear velocity and spin.
- A score is emitted only after the ball crosses an upper and lower plane downward inside the rim.
- Pick up and shoot repeatedly to exercise dynamic-physics enable/disable transitions.
- With two clients, watch shots through syncer changes and confirm no velocity/spin reset.

This is deliberately a test resource: final animation, camera polish, art and gameplay tuning are outside this harness. Run it independently from `dynamic-object-physics-harness`; both intentionally replace model 2114 collision.