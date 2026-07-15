# Native Bullworth streaming test

This resource contains no Bullworth assets and performs no model, TXD,
collision, IPL, or building registration. It only provides travel commands for
testing the process-global native Bullworth registrar:

- `/nativebw` moves the player to the academy in a test vehicle.
- `/nativeback` returns the player to San Andreas.

Do not run the legacy `ug-bw` resource during this test. That resource owns a
second, Lua-driven copy of the same city and would invalidate the result.
