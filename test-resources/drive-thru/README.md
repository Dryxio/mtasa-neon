# Drive-Thru story checkpoint

This resource ports the validated opening and the next testable slice of `SCRIPT_NAME SWEET3` from the installed GTA:SA `main.scm`. It now enters the restaurant transition, plays native file cutscene `SWEET2B`, reconstructs the mission world, and stops on the exact black-screen boundary immediately before the first pursuit task assignment.

## Commands

- `/drivethru` starts the checkpoint from the native `SWEET2A` file cutscene.
- `/drivethruskip` requests the server-authorized native cutscene skip. The leader's original GTA skip input is also consumed through the same server path.
- `/drivethruabort` destroys mission entities and restores the leader's original position, dimension, appearance, health, armour, and weapons.

## Implemented vanilla path

- The mission acquires GXT block `SWEET3`. Despite the dialogue keys using the `SWE2_` prefix, the installed SCM does not load block `SWEET2` for Drive-Thru.
- The leader receives fresh-game CJ model and clothes before `SWEET2A`; cleanup restores the previous appearance.
- The Greenwood is created under the post-cutscene black screen at the installed coordinates with plate `GROVE4L`, palette colours 59 and 34 converted to light crystal blue and arctic white RGB entries, unlocked doors, Bounce FM, and the native tyre-only no-burst policy. Deferring the synchronized element until native cutscene teardown is invisible to players and prevents GTA from invalidating MTA's vehicle instance.
- Smoke, Sweet, and Ryder are created after the cutscene with the SCM `CREATE_CHAR` Z conversion, their motion groups, TEC9, 500 health, mission-actor classification, story protection, and native passenger-entry tasks. SCM passenger indices `1`, `2`, and `0` map to MTA seats `2`, `3`, and `1` respectively. Task dispatch waits for the Greenwood and every actor to be streamed, syncer-owned, policy-ready, and position-stable. Mission progression separately waits for all three authoritative seat transitions.
- `SWE2_AA` loads and finishes before `TWAR2_A` is printed. The driving conversation preserves events `37601..37612`, keys `SWE2_BA..SWE2_BM`, the original speakers, native facial talk, four-second initial delay, and 200 ms inter-line gate.
- Navigation swaps from the friendly blue car blip to the default cream destination blip when the leader drives. Leaving the car restores the car blip and `TW2_X` instruction.
- The terminal checkpoint requires the server-observed Greenwood inside the original `4 x 4 x 4` box, the leader in driver seat, and a positive syncer-side `isVehicleOnAllWheels` result. A Lua return value alone cannot pass it.
- Passing that gate acquires the reusable native script-camera lease, enables widescreen, installs the exact fixed camera at `(2405.9749, -1882.4752, 15.2036)`, points it at `(2405.5693, -1883.3892, 15.2158)`, and completes the native one-second fade to black before world teardown.
- CJ is removed to the SCM staging coordinate, the original Greenwood and all three protagonists are destroyed, and `LOAD_SCENE_IN_DIRECTION 2397.6443 -1880.2762 23.8686 160.0` is reproduced by the existing directional preload primitive. The black fade is preserved across camera ownership transfer into native `SWEET2B`.
- Managed file cutscenes now preserve the vanilla `SWITCH_STREAMING OFF` loading window. Native `CCutsceneMgr` still owns player safety, playback, model instances, and the matching streaming restore during postload or teardown.
- After `SWEET2B` releases, the server reconstructs the Greenwood at SCM `(2396.36, -1917.96, 12.38)` and MTA centre Z `12.88`, with heading `267.3`, plate `GROVE4L`, palette colours 59 and 34, fire-only proof, unlocked doors, and non-bursting tyres. CJ is in the driver seat, with Ryder, Smoke, and Sweet in their exact SCM passenger mappings.
- The Voodoo is reconstructed at SCM `(2411.3098, -1928.8369, 12.3906)` and MTA centre Z `12.9405315`, heading `178.7106`, palette colour 22 on both channels, health `2700`, lock mode `3`, and non-bursting tyres. Its red threat blip and Ballas2 driver are present; the driver has TEC9, current-weapon selection, accuracy `40`, mission classification, and only the exact no-critical-hits flag.
- The terminal barrier requires all reconstructed vehicles and actors to be streamed and owned by the leader client, validates coordinates, headings, seats, health, and native policies for three stable samples, then stops without calling any pursuit task API. The screen intentionally remains black because the next SCM instruction is `PERFORM_SEQUENCE_TASK` for the Ballas driver.

## Explicitly pending

- The three SCM `DM_PED_MISSION_EMPTY` assignments have no current public primitive. Native mission-actor classification and story protection are applied, but the empty decision-maker policy remains explicit pending work.
- The vanilla low-health path uses native immediate-exit and smart-flee tasks which are not exposed yet. The checkpoint reports mission failure and cleans up, but does not substitute those tasks in Lua.
- Cooperative support-vehicle policy is not active in this first slice. `/drivethru` currently assigns one mission leader so the canonical Greenwood retains CJ, Ryder, Smoke, and Sweet in its four seats.
- The Ballas passenger is created only after the driver's first `PERFORM_SEQUENCE_TASK` in the installed SCM. It is therefore intentionally absent at this exact checkpoint rather than being created early.
- Pursuit sequence assignment, the Ballas passenger, Grove support actors, drive-by tasks, combat, return scenes, and mission reward belong to later checkpoints.

No SCM bytecode is executed. Lua remains the server-authoritative checkpoint orchestration while the resource consumes Neon's existing generic native cutscene, GXT, audio, actor-policy, vehicle-policy, and arrival primitives.

## Vehicle coordinate evidence

The compact GTA SA 1.0 target, SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`, implements `CCarCtrl::CreateCarForScript` at `0x431F80`. Both its automobile and boat paths call `CEntity::GetDistanceFromCentreOfMassToBaseOfModel` at `0x536BE0` and add that result to the script Z before writing the entity matrix. The current `gta-reversed-dryxio` bodies match the decisive target instructions. The installed `greenwoo.dff` embeds `greenwoo_col` as COL3 with bounding-box minimum Z `-0.5`, so SCM `CREATE_CAR ... 12.3` becomes MTA centre Z `12.8`. Passing raw `12.3` to `createVehicle` places the Greenwood half a metre too low.

The installed `voodoo.dff` embeds `voodoo_col` with bounding-box minimum Z `-0.549931526`. Its raw pursuit coordinate therefore becomes centre Z `12.9405315`. Car colour table entry 22 is RGB `(105,30,59)`, while ped model 103 is the installed `BALLAS2` entry.

## Native property evidence

`SET_CAR_PROOFS` opcode `02AC` enters the target handler at `0x47F908`, resolves the vehicle, then writes the five independent physical flags through the shared block at `0x47F8AA`. In script argument order these are bullet `0x40000`, fire `0x80000`, explosion `0x800000`, collision `0x100000`, and melee `0x200000` in `CPhysical+0x40`. The current `gta-reversed-dryxio` handler matches those writes. `setVehiclePhysicalProofs` persists the exact tuple across local native vehicle recreation instead of using MTA's broader damage-proof switch.

`SET_CHAR_SUFFERS_CRITICAL_HITS` remains the previously verified inverse write at `0x48A023` to `CPed+0x470` bit `0x1000`. `setPedSuffersCriticalHits` exposes that one flag independently so the enemy driver does not inherit the protagonists' unrelated target and vehicle-jacking protections.

`SWITCH_STREAMING OFF` reaches `0x48478E`; its decisive write at `0x4847A1` stores the inverse script argument in global `0x9654B0`. Native cutscene postload and deletion restore false at `0x5AFBE8` and `0x5AFF90`. The managed file-cutscene path now sets the same flag immediately before native load and retains native restoration.

## Validated checkpoint

The complete first checkpoint was validated in game on 20 July 2026. `SWEET2A` finished naturally after `31453 ms` without a crash or model `304` load dialog. Client and server both observed the Greenwood at `(2508.700, -1671.700, 12.800)` with RGB colours `(78,104,129)/(100,100,100)` and plate `GROVE4L`. The streaming barrier accepted all three native entry tasks, the server confirmed Ryder, Smoke, and Sweet in MTA seats `1`, `2`, and `3`, and `SWE2_AA` completed before the leader entered as driver. The terminal `09D0` gate passed at `(2407.68, -1888.30, 13.16)` with the leader driving and GTA reporting all four wheels in contact. `/drivethruabort` then restored the mission state cleanly. No Drive-Thru client or server error was logged during the run.

The first restaurant test confirmed that native `SWEET2B` loaded and played, but exposed a watchdog error before reconstruction: the generic `60000 ms` finish timeout fired exactly one minute after playback started. The installed `sweet2b.cut` still contains a text cue ending at `82680 ms`, so the mission now retains the 60-second default for `SWEET2A` and applies a `120000 ms` safety timeout only to `SWEET2B`. The restaurant and pre-pursuit reconstruction checkpoint remains pending a clean gameplay rerun that confirms the native completion signal, the intentional black-screen stop, and the reconstruction logs; it must not be described as gameplay-validated before that evidence exists.
