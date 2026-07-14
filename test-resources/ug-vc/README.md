# UG Vice City IMG streaming prototype

This resource runs alongside `ug-lc` to validate San Andreas, Liberty City,
and Vice City in one server session.

The 3,497 DFFs and 654 TXDs are packed into standard unencrypted IMG VER2
archives. The client links dynamic model/TXD slots to those archives, loads
collisions, and registers all 8,588 exterior placements when Vice City becomes
the local resident city. Placements and their 1,081 LOD links then remain
stable until that client moves to Liberty City.

San Andreas leaves 5,136 free DFF slots, while LC and VC need 6,516 slots in
total. Both cities therefore remain active server-side, but only the city near
each local player owns dynamic slots on that player's client. `/vctest` fades
the screen, releases LC locally if necessary, registers VC with a bounded
per-frame budget, preloads Ocean Beach, and teleports only after completion.
Different players can be in LC and VC simultaneously because residency is
per-client rather than global.

The radar catalog has a separate lifetime: its 80 tiles are registered when
the resource starts and remain available in the shared F11 atlas even while
Vice City's 3D models are not the locally resident city.

## Commands

- `/vctest` preloads Ocean Beach and teleports to Vice City.
- `/vcback` returns to San Andreas while keeping VC locally resident.
- `/vcstreamstats` reports registration and GTA streaming-memory state.
- `/vcinspect [radius]` prints nearby source placements.
- `/vcinspectaim` identifies the Vice City element under the crosshair.
- `/vcradarstats` reports the Neon radar tile state.

Together, the resources expose 6,516 custom models, 1,052 TXDs, and 18,412
placements in addition to San Andreas without exceeding GTA's per-client DFF
slot range.
