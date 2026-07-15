# UG Liberty City IMG streaming prototype

This resource tests the lower-risk Eagle-style architecture before adding
native GTA IPL registration in C++.

The 3,019 DFFs and 398 TXDs are packed into standard unencrypted IMG VER2
archives. The client allocates the custom model and TXD slots, links those
slots to the archives, loads collisions, and creates all 9,824 city placements
progressively while the player remains in San Andreas. GTA then streams the
linked RenderWare data according to camera position.

All placements and their 1,957 high/low-detail relationships remain registered
for the resource lifetime. The earlier spatial prototype destroyed buildings
outside a Lua retention ring; real driving tests showed that this raced GTA's
own streamer and repeatedly toggled buildings between HQ and LOD.

`/lctest` is enabled only after background registration finishes. It fades the
screen, asks Neon to preload the native scene around Portland, and lets the
server teleport the player after that request returns. Returning to LC does not
rebuild the city or reload its archives.

The radar catalog has a separate lifetime: its 81 tiles are registered when
the resource starts and remain available in the shared F11 atlas even while
Liberty City's 3D models are not the locally resident city.

When `city-residency-coordinator` is running, approaching Liberty City requests
this same client loader automatically. `/lctest` remains a manual debugging
override and no longer represents the normal arrival path.

## Commands

- `/lctest` preloads Portland and teleports to Liberty City.
- `/lcback` returns to San Andreas while keeping LC registered.
- `/lcstreamstats` reports registration, placement and GTA streaming-memory state.
- `/lcinspect [radius]` prints nearby source placements.
- `/lcinspectaim` identifies the Liberty City element under the crosshair.
- `/lcradarstats` reports the Neon radar tile state.

## Current limits

- Collision files are still loaded individually during background registration.
- Model/TXD linking validates each IMG entry once, but does not eagerly create
  the RenderWare model or texture dictionary.
- The placements are MTA buildings rather than native `CIplStore` instances.
  If stable placements plus IMG streaming still exhibit GTA-side LOD faults,
  native binary IPL registration is the next implementation stage.
