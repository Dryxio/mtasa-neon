# Official Neon resources

Reusable, ready-to-use resources maintained by Neon for server owners. These
use Neon APIs and require compatible Neon client and server builds; they are
not resources for an unmodified MTA:SA installation.

| Resource | What it adds |
| --- | --- |
| [native-ped-traffic](native-ped-traffic/README.md) | Shared ambient pedestrians using GTA's native walking and reaction AI. |
| [native-vehicle-traffic](native-vehicle-traffic/README.md) | Shared civilian road traffic with native driving, occupants, and ownership handoff. |
| [camera-pointing-test](camera-pointing-test/README.md) | Hold E to point toward the camera direction, visible to other players. The existing resource name is retained for compatibility. |
| [fps-counter](fps-counter/README.md) | An optional local FPS display toggled with `/fps`. |
| [samp-map-loader](samp-map-loader/README.md) | Load SA-MP/Pawn maps through client exports, with the model payload included. Supply your own map. |
| [native-task-runtime](native-task-runtime/README.md) | Server exports for native driving routes, actor cohorts, and recorded vehicle playback. |
| [story-world-runtime](story-world-runtime/README.md) | Shared mission-world services for vehicle placement, cutscenes, relocation, and cleanup. |
| [story-entry-exit-runtime](story-entry-exit-runtime/README.md) | Server-owned interior transitions with entry/exit leases and cleanup. |

The last four are libraries for gamemode authors: start them before their
consuming resources and call their documented exports. Starting a library alone
does not create a map or launch a mission.

## Install

1. Use resources from the same source revision as your Neon build.
2. Copy each desired resource directory into your server's
   `mods/deathmatch/resources/` directory, keeping its name unchanged.
3. In the server console, run:

   ```text
   refresh
   start native-ped-traffic
   start native-vehicle-traffic
   ```

   Start only the resources you installed. They can run individually; running
   both provides the combined pedestrian and road population.
   For another resource, use `start <resource-name>` in the same console.
4. To start them automatically on subsequent server launches, add the desired
   entries inside the existing `<config>` element in `mtaserver.conf`:

   ```xml
   <resource src="native-ped-traffic" startup="1" protected="0" />
   <resource src="native-vehicle-traffic" startup="1" protected="0" />
   ```

   Use the same entry format with the directory name of any other installed
   resource. Place libraries before resources that consume their exports.

Traffic starts automatically when each resource starts, for eligible players
in dimension/interior 0. Read each resource's README for controls, population
budgets, cleanup, and current limitations. The vehicle system covers civilian
road traffic; boats, aircraft, and emergency response are outside its scope.
This classification does not imply full single-player behavior parity.

## Updating an existing installation

These resources previously lived under `test-resources/` in the source tree.
Only their repository location changed: deployed directory names, resource
names, commands, and exports remain the same. Stop the resource, update its
files from this directory, and start it again. Keep a single installed copy of
each resource; existing startup entries do not need renaming.

## Development resources

[test-resources/](../test-resources/README.md) contains standalone regression
harnesses, demos, and experimental scenarios. Tests kept inside an official
resource's `tests/` directory exercise that resource's implementation and are
not loaded by its `meta.xml`.

Standalone test harnesses remain in `test-resources/` even when they exercise
one of these libraries. Mission reconstructions remain development scenarios.
City loaders and radar resources requiring separately generated assets also
remain there. The radio carousel still bundles test-vehicle spawning commands;
it needs that fixture separated before being offered as a server resource.
