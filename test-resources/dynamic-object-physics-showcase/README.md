# Dynamic object physics showcase

Clean basketball-throw showcase for recording Neon's generic dynamic-object physics without the harness debug gizmos.

## Controls

- `A`: toggle aim mode.
- Hold `E`: charge the throw. The reticle shrinks as power increases.
- Release `E`: spawn and throw a new basketball through the reticle.
- `/showcasethrow [0..1]`: throw immediately at a fixed power while using the current reticle direction.
- `/showcaseclear`: remove your spawned showcase balls.
- `/showcaseclearall`: remove all showcase balls.

Each throw creates a new dynamic object, so previous basketballs remain in the world and continue rolling, bouncing and colliding. Up to 150 balls per player are kept alive; the oldest are recycled beyond that limit.

## Aim presentation

The resource keeps GTA's normal third-person camera and mouse-look instead of replacing it with a scripted camera. The reticle is placed to the right of the player, in an over-the-shoulder screen position. `getWorldFromScreenPosition` converts that exact reticle position into the world-space throw direction, so the reticle indicates where the ball is actually aimed while keeping the character out of the center of the sight picture.

## Physics

The showcase uses the same runtime sphere collision and physical basketball properties validated by `dynamic-object-physics-harness`:

- radius `0.12`
- mass `0.62`
- solid-sphere turn mass `0.4 * mass * radius^2`
- air resistance `0.995`
- elasticity `0.72`

There is intentionally no velocity overlay, collision-axis gizmo, scoring, hoop logic or gameplay-specific assistance.
