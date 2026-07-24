# Native Bullworth streaming test

This resource contains no Bullworth assets and performs no model, TXD,
collision, IPL, or building registration. It only provides travel commands for
testing the process-global native Bullworth registrar:

- `/nativebw` moves the player to the academy in a test vehicle.
- `/nativecc` moves the player to Carcer City without owning or replacing any
  native-world asset.
- `/nativeback` returns the player to San Andreas.

Do not run `ug-bw`, `carcer-city-test`, or `city-residency-coordinator` during
this test. Those resources own Lua-driven copies of the same cities and would
invalidate the native registrar result.

## Generation-1 test order

Use a clean client process after the closed format-3 set has published its
startup ticket. Restart MTA, confirm the native-world log reports
`activation=yes lease=process` and `generation=1 recyclable=no`, then run:

1. `/nativebw`
2. `/nativeback`
3. `/nativecc`
4. `/nativeback`

Repeat the sequence after disconnect/reconnect, a restart of this resource and
a full server restart. Minimize/restore once and include death/respawn before
calling the checkpoint complete. Transport publication is expected to be
refused after activation because the process-global registrar already owns the
mutable descriptor; `existing-native-world=preserved` is the required result.

The 2026-07-25 server/resource restart gate completed without streaming,
RenderWare, allocation or crash diagnostics. Peak buildings were
`18,352/32,000`; ColModels reached `14,522/30,000`; TXD, COL, IPL and
QuadTreeNode occupancy remained `4,933/8,000`, `271/512`, `210/1,024` and
`238/2,048`.
