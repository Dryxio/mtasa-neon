# Dynamic object physics harness

Low-level validation resource for opt-in native physics on MTA-owned objects.

The resource replaces model 2114 collision with one explicit sphere, then tunes mass, turn mass, air resistance and elasticity through the existing object property API. Dynamic physics itself does not generate fallback collision.

## Commands

- `/dophys` - spawn/reset a physical ball in front of you.
- `/dothrow [speed] [up]` - relaunch it with linear and angular velocity.
- `/dofreeze` - toggle frozen state.
- `/dostatus` - print server-side physics and velocity state.
- `/doreset` - recreate your ball.
- `/doclear` - remove all harness objects.

## Expected checks

1. `/dophys`: the ball falls under gravity, collides with the world, rebounds and settles.
2. `/dothrow`: linear and angular velocity remain live instead of being overwritten by object sync.
3. Walk/drive into the object: native collision should react physically.
4. Move far enough to stream the object out and back in: position, velocity and dynamic-physics state should survive.
5. With two clients, watch the same object while the syncer changes. The new syncer should receive position, rotation, linear velocity and angular velocity without a visible reset.
6. `/dostatus` and the client debug overlay should agree closely on motion.

Run this resource independently from `basketball-physics-test`; both intentionally replace the stock basketball model collision for validation.