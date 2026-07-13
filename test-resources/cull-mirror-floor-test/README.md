# Native CULL mirror floor control

This resource isolates native mirror behavior with one custom floor zone and a
stock GTA barbershop control.

The custom zone uses mirror flag `1` and normal `(0,0,1)`. For this visual
test, its mirror plane is slightly above the platform so the reflection
boundary remains easy to see. The stock barbershop uses
normal `(-1,0,0)` and `mirrorV=-415.64`, producing the wall plane `x=415.64`.
The custom platform runs in interior `1` and dimension `4243`, isolating its
reflected scene from the exterior San Andreas map below it.

Commands:

- `/mirrorfloor` teleports to the custom single-floor test.
- `/mirrorvanilla` teleports to the stock GTA barbershop mirror.
- `/mirrorfloorinfo` reports the nearest mirror definition and plane.
- `/mirrorfloortoggle [on|off]` provides a custom-zone A/B comparison.
- `/mirrorfloordebug [on|off]` draws the custom volume and floor plane.
- `/mirrorfloorleave` restores the previous position.
