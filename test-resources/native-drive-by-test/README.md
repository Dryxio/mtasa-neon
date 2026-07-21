# Native gang drive-by test

This resource isolates GTA's `TASK_DRIVE_BY` primitive before it is integrated
into SWEET3. It exercises vehicle, ped, and raw-coordinate targets without
running the Drive-Thru mission or approximating weapon fire in Lua.

## Protocol

1. Run `/nativedriveby` while on foot.
2. The harness moves the tester and a frozen Voodoo to an isolated dimension.
   The tester drives while Ballas2 occupies MTA seat `1`, owns a TEC9, and is
   synchronized by the tester.
3. Let the three automatic phases run. Each phase must independently report
   `ACCEPT`, observation of `TASK_SIMPLE_GANG_DRIVEBY`, at least 40 shots, and
   real target-health damage on both client and server. The harness keeps the
   native default ped accuracy of `60` and SWEET3-style frequency/rate `99`
   instead of masking dispersion defects with perfect accuracy. The vehicle
   target is placed on the passenger side; the ped and coordinate targets are
   placed across the cabin so GTA must select its native cross-vehicle animation.
   Rotate the gameplay camera throughout all three phases: the passenger's
   weapon and arms must remain aligned with the target instead of following
   the camera. Every phase also proves that the shooter's own Voodoo retained
   its health throughout the sustained firing window, rather than ending the
   phase after the first lucky target hit.
4. The vehicle phase queues cancellation after returning from the monitor
   callback, logs entry and return from `killPedTask`, then verifies that the
   native primary task is no longer active.
5. The ped phase destroys the targeted synchronized ped server-side while the
   task is active. The client waits another 500 ms, reports the surviving task
   state, and clears it if GTA did not finish it naturally.
6. The coordinate phase passes only a `Vector3` to the primitive. A collocated
   frozen vehicle is used solely as a broad collision witness for native
   bullets fired at that raw coordinate. This keeps the native accuracy of
   `60` without making the phase depend on a lucky hit against a ped-sized
   witness.
7. Confirm the terminal `COMPLETE` log, then run `/nativedrivebycleanup` to
   destroy the harness and restore the tester's original position.

Acceptance, task activity, shots, client damage, server damage, cancellation,
cross-vehicle targeting, shooter-vehicle health, target destruction, and
cleanup are deliberately separate observations. A successful Lua return alone
is not a pass.

## Native gate

The compact target executable has SHA-256
`72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`.
Opcode `0713` enters the `0x46D050` command group and dispatches at `0x46D3A7`.
It reads ten parameters, resolves an optional ped target and then an optional
vehicle target, allocates `0x44` bytes, and calls
`CTaskSimpleGangDriveBy::CTaskSimpleGangDriveBy` at `0x6217D0` with target,
coordinate, abort range, frequency, style, and side. It then sets the native
`m_bFromScriptCommand` byte at offset `0x0E` before scripted task dispatch.

The constructor registers a safe entity reference and copies the coordinate;
the destructor cleans that reference. The current `gta-reversed-dryxio`
constructor, clone, destructor, parameter types, offsets, and `0x44` size agree
with the target assembly, so this checkpoint requires no reverse correction.

MTA normally replaces animation `232` with `235` and `236` with `231` to keep
ordinary players vulnerable while leaning out of a vehicle. Neon now skips
that visual substitution only for a script-command task whose non-local ped
has already relinquished MTA player-weapon processing to GTA AI through the
mission-actor policy. Player drive-bys retain the established MTA correction,
while SCM-style actors keep GTA's matching cross-vehicle pose and task-owned
target.

The target executable selects the player or AI direction path at `0x62D58F`,
using `0x621960` or `0x627600`, before the required animation is consumed at
`0x62D5A7`. Five player tests in `ProcessPed` and the repeated tests at
`0x627D81` in the firing helper and `0x62838D` in the IK helper must all select
AI control. Otherwise `CWeapon::Fire` still receives the task target at
`0x628328`, but `CPedIK::PointGunInDirection` receives camera-derived angles
at `0x62876D`.

The integrated SWEET3 chase exposed one additional CPlayerPed mismatch. At
`0x73FF1E`, target-directed `CWeapon::FireInstantHit` loads
`CPed::m_pPlayerData` before the random player-spread branch. A vanilla NPC has
no player data and skips that branch, while every synchronized MTA ped is
CPlayerPed-backed and incorrectly took it even after the gang-drive-by task
selected AI control. Neon now presents a null player-data result only between
the audited firing-helper entry at `0x627D81` and its single epilogue at
`0x62833E`. It does not mutate the ped, change accuracy, alter weapon damage,
or affect ordinary player drive-bys.

The native occupied-vehicle ignore at `0x740697` is conditional on vehicle
flag `0x42D & 4`; it is not forced by Neon. Preventing the MTA-only player
spread preserves that original collision policy while removing rays that a
vanilla NPC would never generate. The current `gta-reversed-dryxio`
`CTaskSimpleGangDriveBy` and `CWeapon::FireInstantHit` control flow agrees with
all decisive instructions, so no reverse correction or GTA-reversed build is
required.
