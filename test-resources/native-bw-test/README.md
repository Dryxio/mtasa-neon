# Native world streaming test

This resource contains no native-world assets and performs no model, TXD,
collision, IPL, or building registration. It only provides travel commands for
testing the process-global native registrar:

- `/nativevc` moves the player to Vice City in a test vehicle.
- `/nativelc` moves the player to Liberty City in a test vehicle.
- `/nativeback` returns the player to San Andreas.
- `/nativebw` and `/nativecc` report that those cities are deliberately
  non-resident during the VC/LC LOD checkpoint.

Do not run `ug-bw`, `carcer-city-test`, or `city-residency-coordinator` during
this test. Those resources own Lua-driven copies of the same cities and would
invalidate the native registrar result.

## VC/LC LOD bootstrap test order

Use a clean client process after the closed format-3 set has published its
startup ticket. Restart MTA, confirm the native-world log reports
`activation=yes lease=process`, `resident=vice-city,liberty-city`,
`lodArrays=2`, `lodAnchors=3038`, `lodLinks=3038`, and
`generation=1 recyclable=no`. The bootstrap line must include
`collisionTransfer=missing-anchor-only:2`; any other count means the frozen
VC/LC collision profile drifted. Then run:

1. `/nativevc`
2. `/nativeback`
3. `/nativelc`
4. `/nativeback`

At each city, drive toward and away from dense blocks so HD children unload and
the permanent distant anchors become visible. Check road/building collision and
one line-of-sight/model-ID query against a child if possible. The first city
load must also emit `lodBootstrap boundingCleanup=all-confirmed groups=21
counters=zero`, proving the final startup bounding-pass children were removed.

Repeat the sequence after disconnect/reconnect and a restart of this resource.
Minimize/restore once and include death/respawn before calling the checkpoint
complete. A full server restart remains a final lifecycle gate. Transport
publication is expected to be refused after activation because the
process-global registrar already owns the mutable descriptor;
`existing-native-world=preserved` is the required result.

The 2026-07-25 VC/LC gate completed without streaming, RenderWare, allocation
or crash diagnostics. It covered all 21 bounding-cleanup groups, both cities,
SA returns, minimize/restore, death/respawn, same-process reconnect, hot
resource restart and full server restart. Peak buildings were
`16,577/32,000`; ColModels reached `17,273/30,000`; TXD, COL, IPL and
QuadTreeNode occupancy remained `4,933/8,000`, `354/512`, `295/1,024` and
`264/2,048`.
