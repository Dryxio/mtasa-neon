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
   `ACCEPT`, observation of `TASK_SIMPLE_GANG_DRIVEBY`, an ammo decrease, and
   real target-health damage on both client and server.
4. The vehicle phase queues cancellation after returning from the monitor
   callback, logs entry and return from `killPedTask`, then verifies that the
   native primary task is no longer active.
5. The ped phase destroys the targeted synchronized ped server-side while the
   task is active. The client waits another 500 ms, reports the surviving task
   state, and clears it if GTA did not finish it naturally.
6. The coordinate phase passes only a `Vector3` to the primitive. A collocated
   frozen ped is used solely to prove that native bullets fired at the raw
   coordinate cause world damage.
7. Confirm the terminal `COMPLETE` log, then run `/nativedrivebycleanup` to
   destroy the harness and restore the tester's original position.

Acceptance, task activity, shots, client damage, server damage, cancellation,
target destruction, and cleanup are deliberately separate observations. A
successful Lua return alone is not a pass.

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
