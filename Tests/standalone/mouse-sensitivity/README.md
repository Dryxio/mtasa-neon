# Precise mouse sensitivity (issue #76)

The web Controls settings provide sliders and four-decimal numeric input. Legacy
settings also have a Mouse tab with precise numeric fields. Apply/OK saves the
values; Cancel discards the draft. Mouse-tab defaults are also a draft.

Horizontal and vertical sliders now cover raw coefficients 0.000001–0.004688 and
0.000001–0.002688. Existing raw settings and raw defaults (0.0025 / 0.0015) are
preserved, so an old percentage is displayed differently without changing its
camera response. Vertical configuration uses round-trip float text instead of
six decimal places. The game already stores horizontal sensitivity as a float.
Mouse steering retains its historical normalized scale; settings below the old
camera minimum therefore give zero mouse steering, as the old minimum did.

Aiming and sniper multipliers range from 0.01 to 2, default 1. They are selected
by camera mode, never multiplied together. They affect both mouse axes while
preserving the existing XY-link option, separate vertical setting, and inversion.

- Third-person aiming: native modes 53, 55 and 65 (including vehicle/attached aim).
- Sniper multiplier: modes 7 and 34; runabout modes 39 and 42.
- Other modes, including the camera item, helicannon and rocket launcher, retain
  the general sensitivity without either aiming multiplier.
- Joypad calculations, native zoom/FOV rules and scripted camera positioning are
  not patched by these multipliers.

## Native evidence and implementation

Reviewed executable SHA-256:
`72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`.

- `0x522263`: mouse FOV coefficient in Process_AimWeapon; redirect its operand
  to stable storage holding `0.0125 * aimingMultiplier`.
- `0x510C10`: mouse FOV coefficient in Process_M16_1stPerson.
- `0x50F030`: mouse FOV coefficient in Process_1rstPersonPedOnPC.
- The latter two replace one six-byte FMUL with CALL + NOP. The helper selects
  the scoped multiplier by CCam mode at offset 0xC, preserves GPRs/EFLAGS, and
  reproduces exactly one FMUL without changing x87 stack depth.
- Do not modify `0x85A5C4`, or the adjacent joypad coefficient operands
  (`0x522329`, `0x510C83`). They are not mouse-only settings.

## Automated checks

```
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  Tests/standalone/mouse-sensitivity/settings.cpp -o /tmp/mouse-settings
/tmp/mouse-settings
npm run build --prefix Tools/server-browser-prototype
```

The standalone test covers invalid values, default/quarter-speed coefficients,
old raw-setting migration and persistence of nearby values below the old minimum.
It does not validate native gameplay. Compile Game SA and Client Core plus SDK
consumers Multiplayer SA and Client Deathmatch for Release|Win32.

## Tester checklist (not yet executed in game)

1. Record the current sensitivity, update the client, and compare the same mouse
   movement at multipliers 1. Camera response should stay unchanged.
2. Set close values (e.g. 0.5000 and 0.5125 percent), Apply, reopen settings and
   restart the client. Check preservation including the vertical axis.
3. Compare equal movements at aiming 1 and 0.25 with the same FOV; repeat for
   sniper at multiple zoom levels. Camera outside aim should stay unchanged.
4. Test XY linked/unlinked and inverted input, AK/M4/rifle/sniper, drive-by,
   attached aim, rockets, camera item, joypad and scripted cameras. Only the
   documented modes should receive an aim multiplier.
5. Check invalid/empty numeric input, defaults, Apply/OK and Cancel in both UIs;
   applying an unrelated setting must preserve precise sensitivity.
6. Enter/leave vehicles, respawn, reconnect and restart resources. Check that
   switching modes never leaves the previous multiplier active.
