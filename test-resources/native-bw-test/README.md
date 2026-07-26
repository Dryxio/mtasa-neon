# Native world streaming test

This resource contains no native-world assets and performs no model, TXD,
collision, IPL, or building registration. It only provides travel commands for
testing the process-global native registrar:

- `/nativevc` moves the player to Vice City in a test vehicle.
- `/nativelc` moves the player to Liberty City in a test vehicle.
- `/nativebw` moves the player to Bullworth.
- `/nativecc` moves the player to Carcer City.
- `/nativeback` returns the player to San Andreas.

Do not run `ug-bw`, `carcer-city-test`, or `city-residency-coordinator` during
this test. Those resources own Lua-driven copies of the same cities and would
invalidate the native registrar result.

## Simultaneous-catalog transition test order

Use a clean client process after the closed format-3 set has published its
startup ticket. Restart MTA, confirm the native-world log reports
`activation=yes lease=process`, `catalogModels=11837`, `lodArrays=2`, and
`startupMapping=canonical-until-bounds`. Before gameplay it must also report
`bootstrap=spatial-ready`, `canonicalPointersCleared=11837`,
`banks=2x4096`, and `active=none`. Then run:

1. `/nativebw`
2. `/nativeback`
3. `/nativevc`
4. `/nativeback`
5. `/nativelc`
6. `/nativeback`
7. `/nativecc`
8. `/nativeback`

Every city entry must emit `transition=active`; every return or direct
city-to-city jump must first emit `transition=retired` with
`fence=cover-ipl-anchors-channels-col-dff reusable=yes`. Banks must alternate
without an inactive city reaching the IPL/COL loaders. At VC and LC, drive
toward and away from dense blocks so HD children unload and distant anchors
become visible. VC must report `collisionTransfer=missing-anchor-only:2`, LC
`0`. Check road/building collision in all four cities.

Repeat direct hostile jumps (`/nativecc`, `/nativelc`, `/nativebw`,
`/nativevc`) without intermediate SA returns, then repeat after
disconnect/reconnect and a restart of this resource. Minimize/restore in Carcer
and VC, include death/respawn, and finish with a full server restart. Transport
publication is expected to be refused after activation because the process
global registrar already owns the immutable catalog and five cache leases;
`existing-native-world=preserved` is the required result.

The previous 2026-07-25 VC/LC checkpoint remains the baseline: peak buildings
were `16,577/32,000`, ColModels `17,273/30,000`, TXD `4,933/8,000`, COL
`354/512`, IPL `295/1,024`, and QuadTreeNodes `264/2,048`. Record new
high-water values for the four-city transition gate rather than carrying
those numbers forward.

The 2026-07-26 four-city gate completed generations 2..29, including direct
Carcer-to-VC and VC-to-LC transitions, repeated reuse of both banks,
death/respawn, same-process reconnect, resource restart and full server
restart. Peak buildings were `21,500/32,000`; ColModels reached
`21,819/30,000`; TXD, COL, IPL and QuadTreeNodes reached `4,933/8,000`,
`373/512`, `314/1,024` and `280/2,048`. The enabled Superman test resource
briefly rewrote the player position during two teleports and therefore caused
extra, fully fenced generations; disable position-owning gameplay resources
when measuring transition count or latency. Borderless Alt-Tab did not produce
a D3D device reset, so that case remains part of the later render/memory gate.
