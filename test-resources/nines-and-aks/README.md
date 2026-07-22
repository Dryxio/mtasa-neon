# Nines and AK's

This resource reproduces the playable graph of GTA San Andreas mission `sweet2`, displayed as **Nines and AK's**.

## Canonical references

- Script: `SCRIPT_NAME sweet2` in `main.scm`.
- Decompiled block: `mission_start_sweet2` through `mission_cleanup_sweet2`.
- Mission text block: `SWEET2`.
- File cutscenes: `SWEET3A` and `SWEET3B`.
- Mission audio family: `SWE3_*`, plus `MSWE07*`, `SMOX_*` reminders and `MOBRING`.
- Mission pass identifier: `SWEET_2`.
- Installed `main.scm` SHA-256 used for the independent audit: `601def3baae766ce6a23e2f0b9b48f6b33c9a64e2fc32eb4f22ddea8b868b0fa`.

The raw bytecode was used to resolve ambiguous decompiler symbols. In particular, the three contextual shooting helps are `HOOD2A`, `HOOD2B`, and `HOOD2F`, and the installed bytecode's bottle Z values supersede several incorrect floating-point values in the decompile.

## Start

The resource requires the Neon client runtime and `native-task-runtime`.

```text
start native-task-runtime
start nines-and-aks
```

Start the mission with either command:

```text
/nines
/ninesandaks
```

Only one mission session may run at a time. The test owner is placed in dimension `4103`. The resource does not launch or control a client process.

## Reachable mission graph

| Phase | SCM behavior covered |
| --- | --- |
| Intro | Load `SWEET2`, play native `SWEET3A`, restore the world, CJ appearance and Grove Street staging |
| Drive to Emmet | Spawn the `_A2TMFK_` Glendale and Smoke, native passenger entry, player/vehicle navigation swap, delayed nine-line `SWE3_A*` dialogue, Smoke/Glendale/all-wheels arrival gate |
| Arrival | Scripted arrival transition followed by native `SWEET3B` |
| Range setup | Exact Emmet special-model mapping, locked/proofed `_FELTCH_` Tampa, Colt 45 loadout and actor staging |
| Bottle round 1 | Smoke demonstrates one shot with all three native camera cuts, CJ destroys one bottle, `HOOD2A`, `SWE2_G`, `SWE3_BC`, `SWE3_DC` |
| Bottle round 2 | Smoke demonstrates three shots under the native 8000 ms camera move/track, CJ destroys three bottles, `HOOD2B`, `SWE2_H`, `SWE3_BA`, `SWE3_DA` |
| Bottle round 3 | Smoke demonstrates five shots with the Smoke cut and five progress-driven close-ups, CJ destroys five bottles, `HOOD2F`, `SWE2_I`, `SWE3_BD/BE/BF/BG/BB/ZZ`, `SWE3_DG` |
| Gas tank | Scripted Tampa camera, `SWE2_F`, `HOOD2C`, remove Tampa proofs, require its destruction |
| Leave Emmet | Exact actor staging, camera lease, all 14 `SWE3_G*` lines, skip input, warp CJ and Smoke into the Glendale |
| Return drive | Smoke/Glendale/all-wheels arrival gate, delayed seven-line `SWE3_H*` dialogue, return reminders |
| Smoke goodbye | Exact actor staging and camera, `SWE3_JA`, `SWE3_JB`, then remove Smoke |
| Phone | `MOBRING`, phone animation, all ten `MSWE07*` lines and early release to the Binco objective |
| Binco | Outside arrival camera and `S2HELP1`, area 15 interior staging, `S2HELP2` and `HELWARD`, required return to area 0 before pass |
| Pass/fail | Mission pass presentation, Colt 45 net gain, Smoke/Emmet/CJ/Glendale failures and deterministic cleanup |

## Runtime ownership

The server owns the mission state, actors, vehicles, authoritative stage changes, player snapshot, pass/fail decision and cleanup. The mission client owns native cutscene/audio/camera leases, native ped tasks for the local syncer, local target bottles, their damage evidence, navigation and the Binco tutorial presentation.

The resource waits for managed file-cutscene release before asking the server to create model 302. Neon maps that playable slot to `EMMET`; creating the ped before `SWEET3B` teardown would race the temporary CUTOBJ model mapping.

## Exact, adapted and excluded behavior

### Phase coverage matrix

| Phase | Exact/runtime-equivalent | Adapted | Excluded |
| --- | --- | --- | --- |
| `SWEET3A` | File cutscene, area-visible 1 lease, GXT lease, CJ model/clothes, fade and release barrier | Mission dimension isolates ambient world | Campaign mission-given counter |
| Glendale outbound | Model, plate, COL base Z, colors, Smoke passenger seat, Bounce FM, friendly/destination blip swap, all-wheels/seat/position gate, nine audio lines | `DM_PED_MISSION_EMPTY` and SCM-only ped flags unavailable | Global car generator and zone density bookkeeping |
| Emmet arrival | Fixed camera, controls off, Smoke native leave plus two native go-to children, CJ delayed leave/go-to, black fade, `SWEET3B` | Native go-to children replace GTA's route buffer | Global gang-size limit |
| Range setup | Emmet slot 302, Tampa model/plate/base Z, actors and Colt 45 ammo | MTA model bootstrap supplies the special character | None in reachable gameplay |
| Round 1 | Raw-bytecode demo/player bottle Z, centre offset, native Smoke shot, all fixed cuts and break timing, `SWE3_BC/DC`, `HOOD2A`, 1-hit server gate | Object-damage event replaces SCM polling | Object lock-on flag unavailable |
| Round 2 | Raw-bytecode three-bottle layout, Colt animation, native shot sequence, 8000 ms native camera move/track, progress-timed breaks and `SWE3_BA/DA`, `HOOD2B`, 3-hit server gate | Repeating post-shot idle sequence omitted | Exact nested idle sequence tree |
| Round 3 | Raw-bytecode five-bottle layout, roll animation, native shot sequence, Smoke cut plus five progress-driven close-ups/breaks, all six demo lines, `SWE3_DG`, `HOOD2F`, 5-hit server gate | Native duck toggle and nested post-shot idle sequence simplified | Exact nested duck/idle task tree |
| Range boundary | `SWE2_J`, range blip, pistol ammo 10 outside and 30000 on return throughout all three rounds and the Tampa objective | Server applies ammo after client boundary evidence | SCM conditional-help internal flag storage |
| Tampa | Exact actor/vehicle staging, petrol-cap weakpoint opt-in, two camera shots, controls, delayed proof removal and blown-state server gate | Ped explosion proofs unavailable | None beyond unavailable proof primitive |
| Leave Emmet | Two-second post-explosion hold, black staging/preload, exact headings, four cameras, 14 lines with 200 ms gates, mandatory first line, front-edge skip, walks, look-at, native entries and seat barrier | Server warp is the 50-second deterministic fallback and skip path | Exact nested dialogue animation microtiming |
| Return drive | Blip swap, reminders `SMOX_AA..AE`, `SWE2_L`, `HELP53/HOOD2D/HOOD2E`, `EMMET_G`, seven-line dialogue and arrival gates | Dialogue restarts after a vehicle reminder | Persistent global Emmet shop unlock |
| Smoke goodbye | Exact staging, three cameras, fades, native walks and `SWE3_JA/JB` | Small wait differences remain resource-owned | None in reachable gameplay |
| Phone | `MOBRING`, 1500 ms lead, phone animation, 1800 ms lead, ten `MSWE07*` lines, Binco objective after first line | Animation replaces native phone task | Native phone task internals |
| Binco | Exterior camera/help, explicit entry input, area 15 tutorial coordinates, 1500+4000 ms tutorial, explicit exit, server-validated area 0 pass | Resource teleports across the entry-exit boundary | Clothing purchase UI, which vanilla does not gate |
| Result | Failure reason plus big fail, big pass with respect 4, passed tune, pistol old ammo +60 | Test session restores the original player snapshot on teardown | Respect, progress, contact, save and shop-blip persistence |

### Exact or runtime-equivalent

- Native file cutscenes, mission GXT, mission audio IDs and scripted camera ownership.
- Smoke model 269 and Neon special-model slot 302 for Emmet.
- Glendale/Tampa models, plates, key positions, headings and mission dimensions.
- One, three and five target layouts using `DYN_WINE_BIG` model 1551.
- Colt 45 target damage, Tampa destruction, actor/vehicle death gates and all-wheels arrival gates.
- All reachable dialogue arrays and required Binco entry/interior/exit topology.
- Player appearance, weapons, health, armor, position and dimension are snapshotted for deterministic failure/resource-stop restoration.

### Adapted with existing public APIs

- `TASK_FOLLOW_POINT_ROUTE` is represented by the scripted Emmet-arrival transition. The current public sequence surface has native point-to-point movement, but not SCM route-buffer ownership.
- Smoke's shooting demonstrations use native `shoot_at` sequence children, stock Colt/roll animations and progress-driven native camera/audio timing. The exact nested repeat, duck toggle, post-shot idle and achieve-heading children are not public yet.
- `TASK_USE_MOBILE_PHONE` uses the stock phone animation while the exact native phone task is not exposed.
- Binco uses the original area 15 coordinates and entry/exit gates without opening the GTA clothing purchase menu. Vanilla `sweet2` does not inspect a purchased clothing item, so this preserves its actual pass condition.
- Local bottle damage events replace the SCM `HAS_OBJECT_BEEN_DAMAGED` polling flag.

### Known native-surface limits

- MTA does not currently expose `MAKE_OBJECT_TARGETTABLE` or `IS_PLAYER_TARGETTING_OBJECT`. Bottles are breakable and valid only when damaged by the mission player, but controller lock-on to an object is not asserted by this resource.
- `DM_PED_MISSION_EMPTY` has no public runtime lease.
- Smoke's SCM-only `ONLY_DAMAGED_BY_PLAYER`, `CANT_BE_DRAGGED_OUT`, `STAY_IN_CAR_WHEN_JACKED`, and `GET_OUT_UPSIDE_DOWN` combination is not fully public. The resource intentionally avoids applying the broad story-protection preset because that preset prevents upside-down exits, opposite the mission script.
- Temporary ped explosion proofs around the Tampa sequence do not have a public ped-physical-proof API.
- Gang-zone strength bookkeeping, contact unlocks, global shop blips, persistent Emmet pickup, respect/progress counters and save-game mission statistics are solo campaign bookkeeping and remain outside Story Runtime scope.

## Failure and cleanup matrix

| Condition | Result |
| --- | --- |
| CJ dies | Mission failed, session restored |
| Smoke dies | `SWE2_B`, mission failed, session restored |
| Glendale explodes before Smoke's final arrival gate | `SWE2_C`, mission failed, session restored |
| Emmet dies while present | Emmet failure, mission failed, session restored |
| Tampa explodes during its objective | Advance once to the departure scene |
| File cutscene request/load/start/release fails or times out | Mission failed instead of silently advancing |
| Player disconnects | Destroy session entities without attempting player restoration |
| Resource stops | Release client leases, destroy local bottles/navigation, restore the player and destroy server entities |

The client rejects bottle damage unless the source is a current player-round bottle and the attacker is the mission player. Repeated Tampa notifications are stage-gated on the server.

## Validation state

The SCM graph, raw bottle coordinates, camera vectors, audio order, stage gates and cleanup paths are statically mapped. User testing reached the Emmet range and exposed the file-cutscene crash, area visibility and initial camera-sequencing defects. The corrected managed-cutscene path was rebuilt and the resource passes Lua syntax validation, but the latest camera timing, bottle-height, Tampa petrol-cap and departure corrections still require a fresh complete in-game run. This checkpoint does not claim full 1:1 runtime validation.

## Validation checklist

Static validation:

```sh
luac -p test-resources/nines-and-aks/shared.lua \
  test-resources/nines-and-aks/server.lua \
  test-resources/nines-and-aks/client.lua
git diff --check
```

In-game validation remains manual:

1. Play both native file cutscenes to completion and repeat with skip input.
2. Verify Smoke enters MTA passenger seat 1 and both arrival gates refuse an upside-down Glendale.
3. Leave and re-enter the Glendale on both drives and verify navigation/reminders swap correctly.
4. Complete the 1/3/5 bottle rounds and verify only CJ damage advances them.
5. Walk outside the range and return during every round.
6. Shoot the Tampa petrol cap after control returns, verify the weakpoint explosion, and verify the departure scene starts exactly once.
7. Complete and skip the Emmet departure scene.
8. Complete the Smoke goodbye and phone dialogue, then enter and exit Binco.
9. Kill Smoke, destroy the Glendale, kill Emmet and die as CJ in separate runs.
10. Restart the resource during a cutscene, a bottle round, the Tampa objective, a drive and the Binco interior.

Do not commit a mission checkpoint until this in-game matrix has been validated. The commit message should record the source mission, fidelity goals, design reasoning and exact tests performed.
