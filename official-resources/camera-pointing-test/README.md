# Camera pointing

Copy `camera-pointing-test` into `mods/deathmatch/resources/`, then run
`refresh` and `start camera-pointing-test` in the server console. The original
directory name is retained so existing project and server settings keep working.

Hold `E` while on foot to procedurally straighten the right arm toward the
local camera direction. No ped animation is started or replaced: the resource
drives GTA:SA's native `CTaskSimpleIKPointArm` chain while the ped's normal
movement continues.

The owning client sends only a normalized direction at a bounded rate. Every
client applies and interpolates the presentation locally, including for remote
players. Pointing stops when `E` is released, the player enters a vehicle, dies,
or opens an input UI.
