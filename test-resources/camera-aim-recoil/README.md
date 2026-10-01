# Scripted camera recoil (issue #121)

Client scripts can read and set the local player's gameplay camera heading and
pitch while keeping native mouse input and camera-driven aiming.

```lua
local yaw, pitch = getCameraAimDirection()
if yaw ~= false then
    setCameraAimDirection(yaw + 0.15, pitch + 1.0)
end
```

`getCameraAimDirection()` returns two numbers, or `false` when unavailable.
`setCameraAimDirection(yaw, pitch)` returns a boolean. Both angles are degrees:
heading 0 faces +Y, 90 faces -X; positive pitch looks up. The getter reports
heading in [0, 360). The setter wraps heading, clamps pitch to the active native
limits, and rejects non-finite inputs or magnitudes above 360,000 degrees without
changing either angle. Readback uses the same angle state as the setter, so
multiple read/add/write calls in one frame accumulate.

Supported modes are free third-person weapon aim (`MODE_AIMWEAPON`), vehicle
weapon aim (`MODE_AIMWEAPON_FROMCAR`), sniper/M16 first person, and their sniper/M16
runabout variants. The Country Rifle uses whichever camera mode is active; its
name does not imply a first-person camera. Default pitch limits are:

| Native context | Minimum | Maximum |
| --- | ---: | ---: |
| Third-person weapon aim on foot | -89° | +45° |
| Vehicle / jetpack weapon aim | -70° | +35° |
| Non-attached sniper/M16 first person | approximately -68.755° | +60° |
| Sniper/M16 runabout | -89.5° | +60° |

The functions return `false` during camera initialization/transitions, while
spectating another entity, with a fixed/script camera or an active script-camera
lease, for a dead/unavailable local player, target lock, physical attachments,
unsupported modes, melee orbit cameras, and attached/vehicle first-person aiming. They do not change
camera ownership, switch modes, clear target lock, or disable mouse controls.
Leaving aim or switching weapons may legitimately change the native camera state.

The setter clears residual angular input velocity and the native angular bump
oscillator. It does not disable future weapon effects or global visual shake;
a recoil script can call `resetShakeCamera()` after each accepted write if it
also wants to remove that visual effect. It does not remove weapon spread.

These are native camera orientation angles, not a replacement for the engine's
crosshair/FOV/muzzle calculations. Native camera processing rebuilds its vectors
on its next update. A call from `onClientPlayerWeaponFire` applies recoil after
the shot that triggered the event; it does not change that shot retroactively.
The legacy `getPedCameraRotation`/`setPedCameraRotation` API is unchanged.

## Sending the test build

Install `MTA-Neon-Setup.exe` from the `MTA-Neon-Windows-Installer` Actions artifact
for the issue-121 commit. Use the full installer so all client modules match.
This is a test build, not a declaration of in-game validation.

Copy this entire resource directory into the server's resources directory and
start `camera-aim-recoil`. It needs no server-side C++ change or automatic weapon
spawning. Use your existing admin/gameplay tools to obtain weapons and vehicles.

1. Run `/aimrecoil 1 0.15`, hold aim with an M4 or AK, and shoot at a wall. Verify
   upward cumulative aim movement, no automatic return while holding aim, normal
   mouse compensation, and subsequent impacts following the new aim.
2. Repeat with Country Rifle and Sniper, including zoom changes. Stop moving the
   mouse before firing to distinguish recoil from remaining input.
3. Repeat as passenger and driver during drive-by, with the vehicle stopped and
   moving. Report the seat, vehicle, weapon and whether the HUD reports refusal.
4. Press F7 while holding aim (or run `/aimcheck`). It checks invalid-input rejection and
   unchanged readback. Deliberate invalid arguments may appear in the debug log.
5. Check limits by repeatedly firing up/down; test both signs using
   `/aimrecoil -1 -0.15`. Compare with native mouse limits.
6. Exit/re-enter aim, switch weapons, die/respawn, reconnect and restart this
   resource. A refusal during a transition is expected; stable aim must recover.
7. With recoil off, verify the existing horizontal-only `setPedCameraRotation`
   behavior in your script. Verify the new functions return `false` under your
   fixed/script camera and while spectating someone else.

Send the Actions run/commit, weapon/seat, reproduction steps, HUD counters and
`clientscript.log`, ideally with a short recording. Offline tests and compilation
cannot establish persistence, input feel and shot alignment in the running game.

## Offline checks and native provenance

Run from the repository root:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -IClient/game_sa \
  Tests/standalone/camera-aim/math.cpp -o /tmp/camera-aim-test
/tmp/camera-aim-test
luac -p test-resources/camera-aim-recoil/client.lua
```

The implementation was checked against the retail compact 1.0 executable with
SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`:
`CCam::Process_AimWeapon` at `0x521500`, `Process_M16_1stPerson` at `0x5105C0`,
`Process_1rstPersonPedOnPC` at `0x50EB70`, mode dispatch at `0x526FC0`, and
`Find3rdPersonCamTargetVector` at `0x514970`. The two angular velocities are at
`CCam+0xB0/0xC0`; bump state is at `+0x118/+0x11C/+0x120`.
The existing Neon driver-camera patch at `0x522423` is preserved.
