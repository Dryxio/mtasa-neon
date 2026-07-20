# Drive-Thru story checkpoint

This resource ports the validated opening and the next testable slice of `SCRIPT_NAME SWEET3` from the installed GTA:SA `main.scm`. It enters the restaurant transition, plays native file cutscene `SWEET2B`, reconstructs the mission world, runs the Ballas chase, and stops after both Ballas are dead immediately before the return-to-Grove stage.

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
- The reconstruction barrier requires all nearby vehicles and actors to be streamed and owned by the leader client and validates coordinates, headings, seats, health, and native policies for three stable samples. MTA's initial 12-bit vehicle-health field saturates the Voodoo at `2047.5`; after that streamed state is acknowledged, the server reapplies the full `2700` through the ordinary float health RPC and the route barrier observes it before assigning the driver task.
- While the screen remains black, the Ballas driver receives the exact eight-point `PERFORM_SEQUENCE_TASK`. Its native sequence index must become observable before the passenger is created, preserving SCM order.
- The Ballas passenger is then created in MTA seat `1`. Its drive-by targets the Greenwood with abort range `5000`, right-side mode and frequency `60`. Ryder and Sweet target the Voodoo from the right and left with frequency `99`. All three native `TASK_SIMPLE_GANG_DRIVEBY` instances must be observed active before the one-second fade-in.
- The two Grove support actors use FAM2/FAM3 models `106/107`, the installed SCM coordinates, health `30`, TEC9s, and paired native chat tasks once both distant peds stream in. Their stream state is deliberately independent from the restaurant barrier.
- Voodoo destruction or health at or below `250` assigns native `TASK_COMPLEX_KILL_PED_ON_FOOT` to each surviving Ballas. Ryder and Sweet retarget the surviving driver and passenger with frequencies `70`. The same transition occurs when either Ballas dies before the Voodoo.
- The chase ends only when both Ballas are dead. CJ, Sweet, Ryder, Smoke, Greenwood, and Grove-support deaths retain their mission-failure conditions. Reaching the final `8 x 8 x 5` Grove box with both Ballas alive also fails the checkpoint.
- Chase dialogue reproduces `SWE2_CA`, `SWE2_CC`, and `SWE2_GA..HE` with the original speakers and native mission audio. The two health-loss dialogue groups `SWE2_DA..DD` and `SWE2_EA..EE` arm after successive 60-health drops, matching the SCM ordering.

## Explicitly pending

- The three SCM `DM_PED_MISSION_EMPTY` assignments have no current public primitive. Native mission-actor classification and story protection are applied, but the empty decision-maker policy remains explicit pending work.
- The vanilla low-health path uses native immediate-exit and smart-flee tasks which are not exposed yet. The checkpoint reports mission failure and cleans up, but does not substitute those tasks in Lua.
- Cooperative support-vehicle policy is not active in this first slice. `/drivethru` currently assigns one mission leader so the canonical Greenwood retains CJ, Ryder, Smoke, and Sweet in its four seats.
- The Grove support actors' `SET_CHAR_NEVER_TARGETTED` and `SET_CHAR_STAY_IN_SAME_PLACE` flags still have no independent public primitive. They remain ordinary mission peds in the isolated dimension and receive GTA's native paired chat tasks without broader protagonist protection.
- If the Voodoo reaches Grove with both Ballas alive, the exact locate condition and mission failure are implemented. The intervening vanilla slow-motion execution scene uses `TASK_DIE`, which is not exposed and is not approximated with an immediate Lua kill in this checkpoint.
- Return scenes, Sweet/Ryder and Smoke drop-offs, reward, and mission-passed tune belong to the final checkpoint.

No SCM bytecode is executed. Lua remains the server-authoritative checkpoint orchestration while the resource consumes Neon's existing generic native cutscene, GXT, audio, actor-policy, vehicle-policy, and arrival primitives.

## Vehicle coordinate evidence

The compact GTA SA 1.0 target, SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`, implements `CCarCtrl::CreateCarForScript` at `0x431F80`. Both its automobile and boat paths call `CEntity::GetDistanceFromCentreOfMassToBaseOfModel` at `0x536BE0` and add that result to the script Z before writing the entity matrix. The current `gta-reversed-dryxio` bodies match the decisive target instructions. The installed `greenwoo.dff` embeds `greenwoo_col` as COL3 with bounding-box minimum Z `-0.5`, so SCM `CREATE_CAR ... 12.3` becomes MTA centre Z `12.8`. Passing raw `12.3` to `createVehicle` places the Greenwood half a metre too low.

The installed `voodoo.dff` embeds `voodoo_col` with bounding-box minimum Z `-0.549931526`. Its raw pursuit coordinate therefore becomes centre Z `12.9405315`. Car colour table entry 22 is RGB `(105,30,59)`, while ped model 103 is the installed `BALLAS2` entry.

The installed `main.scm`, SHA-256 `601def3baae766ce6a23e2f0b9b48f6b33c9a64e2fc32eb4f22ddea8b868b0fa`, stores the first unique route coordinate at offset `0x826A5`. The eight decoded points and speeds match the local SWEET3 source exactly. The route uses model `412`, normal mode, and `DRIVINGMODE_AVOIDCARS`; it is consumed through the already audited generic `drive_to` sequence descriptor.

MTA's `SVehicleHealthSync` stores vehicle health in 12 bits at `0.5` precision, giving an explicit maximum of `2047.5`. The entity-add packet uses that compact field, while the later `SET_ELEMENT_HEALTH` RPC writes a full float and `CClientVehicle::SetHealth` forwards it to the native vehicle. The post-stream rearm preserves SWEET3's `2700` locally without changing the network protocol or adding a mission-specific C++ primitive.

## Native property evidence

`SET_CAR_PROOFS` opcode `02AC` enters the target handler at `0x47F908`, resolves the vehicle, then writes the five independent physical flags through the shared block at `0x47F8AA`. In script argument order these are bullet `0x40000`, fire `0x80000`, explosion `0x800000`, collision `0x100000`, and melee `0x200000` in `CPhysical+0x40`. The current `gta-reversed-dryxio` handler matches those writes. `setVehiclePhysicalProofs` persists the exact tuple across local native vehicle recreation instead of using MTA's broader damage-proof switch.

`SET_CHAR_SUFFERS_CRITICAL_HITS` remains the previously verified inverse write at `0x48A023` to `CPed+0x470` bit `0x1000`. `setPedSuffersCriticalHits` exposes that one flag independently so the enemy driver does not inherit the protagonists' unrelated target and vehicle-jacking protections.

`SWITCH_STREAMING OFF` reaches `0x48478E`; its decisive write at `0x4847A1` stores the inverse script argument in global `0x9654B0`. Native cutscene postload and deletion restore false at `0x5AFBE8` and `0x5AFF90`. The managed file-cutscene path now sets the same flag immediately before native load and retains native restoration.

## Validated checkpoint

The complete first checkpoint was validated in game on 20 July 2026. `SWEET2A` finished naturally after `31453 ms` without a crash or model `304` load dialog. Client and server both observed the Greenwood at `(2508.700, -1671.700, 12.800)` with RGB colours `(78,104,129)/(100,100,100)` and plate `GROVE4L`. The streaming barrier accepted all three native entry tasks, the server confirmed Ryder, Smoke, and Sweet in MTA seats `1`, `2`, and `3`, and `SWE2_AA` completed before the leader entered as driver. The terminal `09D0` gate passed at `(2407.68, -1888.30, 13.16)` with the leader driving and GTA reporting all four wheels in contact. `/drivethruabort` then restored the mission state cleanly. No Drive-Thru client or server error was logged during the run.

Native `SWEET2B` was validated through its natural completion after `83313 ms`. Its first reconstruction exposed MTA's entity-add health saturation: the streamed Voodoo arrived at the documented `2047.5` network maximum instead of SWEET3's `2700`, so the exact-health barrier correctly refused progression. The corrected path acknowledges that compact initial value, reapplies `2700` through the full-float health RPC, and requires the leader client to observe the vanilla value before route assignment. Two following runs passed reconstruction in less than one second without remaining on the black screen.

The integrated chase was validated twice in game on 20 July 2026. Both runs observed the driver's native route before creating the passenger, then observed all three native drive-by tasks before the visible fade-in. Real fire reduced the Voodoo to the vanilla `250` threshold, both surviving Ballas accepted native `TASK_COMPLEX_KILL_PED_ON_FOOT`, and the checkpoint passed only after both were dead. The server recorded the two foot-combat transitions at `14:18:20` and `14:19:54`, followed by terminal passes at `14:18:30` and `14:20:06`. The tester observed one Ballas engage CJ while the other left the vehicle but died in its explosion, which is compatible with GTA's kill-on-foot task beginning with its own leave-car subtask. No Drive-Thru error was logged in either chase run.
