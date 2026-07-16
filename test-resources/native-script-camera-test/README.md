# Native script camera test

This isolated resource exercises Neon's resource-owned GTA script-camera service without depending on a story mission. It covers the primitives needed by `SWEET1`: fixed position and point-at, native vector movement and tracking, persistence, fades, widescreen, script near clip, explicit abort, and automatic cleanup when the owning resource restarts.

The camera is local to the player running the command. No mission state or network synchronization is involved.

## Commands

- `/nativecam` runs the complete fixed → move+track → fade-out → fade-in → release sequence.
- `/nativecamfixed` holds a fixed camera until `/nativecamabort` is used.
- `/nativecamabort` releases the active lease explicitly.
- `/nativecamrestart` acquires and modifies the camera, then restarts this resource without calling `releaseScriptCamera`.
- `/nativecamstatus` prints the lease and native move/track/fade observations.

## Manual validation

Start `native-script-camera-test`, approve its narrowly scoped restart request once with
`aclrequest allow native-script-camera-test function.restartResource` in the server console,
stand in a streamed outdoor area, and run `/nativecam`.

Expected sequence:

1. The camera cuts to a fixed elevated view aimed at the player for two seconds.
2. Position and target travel simultaneously for about four seconds with the native eased interpolation.
3. The screen fades to black for one second, remains black briefly, then fades back in for one second.
4. The harness prints `PASS` and restores the original gameplay camera, near clip, widescreen state, and controls.

Run `/nativecam` again and use `/nativecamabort` during the four-second travelling shot. Movement must stop immediately and every captured state must be restored. Starting a new run after that must still work.

Run `/nativecamrestart`. The camera is deliberately left leased while the resource restarts. The native service—not this Lua script—must revoke the lease and restore the gameplay camera, near clip, widescreen state, and controls. After the `ready` line returns, `/nativecam` must work again. As a second cleanup variant, an administrator can run `restart native-script-camera-test` from the server console during any active phase.

Report the visual result together with every `[native camera]` line from the client log. A completed timer alone is not a pass: fixed framing, eased movement, target tracking, black fade, controls and every restoration path must all be observed in game.

## API contract exercised

```lua
local token = acquireScriptCamera([inhibitControls = true])
releaseScriptCamera(token)
setScriptCameraFixed(token, position, target [, upOffset = Vector3(0, 0, 0), jumpCut = true])
moveScriptCamera(token, from, to, durationMs [, ease = true])
trackScriptCamera(token, from, to, durationMs [, ease = true])
setScriptCameraPersist(token, position, target)
resetScriptCamera(token)
fadeScriptCamera(token, fadeIn, durationSeconds [, red = 0, green = 0, blue = 0])
isScriptCameraFading(token)
isScriptCameraMoveRunning(token)
isScriptCameraTrackRunning(token)
setScriptCameraWidescreen(token, enabled)
setScriptCameraNearClip(token, distance | false)
```

Every call after acquisition requires both the calling resource's implicit ownership and its generation token. An old timer from an earlier mission run therefore cannot mutate a newer lease held by the same resource. The small `API_NAMES` table and wrappers at the top of `client.lua` isolate this provisional surface from the scenario logic.
