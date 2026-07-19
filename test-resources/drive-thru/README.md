# Drive-Thru story checkpoint

This resource ports the first testable slice of `SCRIPT_NAME SWEET3` from the installed GTA:SA `main.scm`. It stops after the exact Cluckin' Bell arrival predicate and deliberately does not enter `SWEET2B` or the Ballas pursuit yet.

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

## Explicitly pending

- `SET_CAR_PROOFS FALSE TRUE FALSE FALSE FALSE` cannot be represented by MTA's all-or-nothing `setVehicleDamageProof`; this checkpoint leaves body damage ordinary rather than faking the individual fire-proof flag.
- The three SCM `DM_PED_MISSION_EMPTY` assignments have no current public primitive. Native mission-actor classification and story protection are applied, but the empty decision-maker policy remains explicit pending work.
- The vanilla low-health path uses native immediate-exit and smart-flee tasks which are not exposed yet. The checkpoint reports mission failure and cleans up, but does not substitute those tasks in Lua.
- Cooperative support-vehicle policy is not active in this first slice. `/drivethru` currently assigns one mission leader so the canonical Greenwood retains CJ, Ryder, Smoke, and Sweet in its four seats.
- `SWEET2B`, pursuit driving, drive-by tasks, combat, return scenes, and mission reward belong to later checkpoints.

No SCM bytecode is executed. Lua remains the server-authoritative checkpoint orchestration while the resource consumes Neon's existing generic native cutscene, GXT, audio, actor-policy, vehicle-policy, and arrival primitives.

## Vehicle coordinate evidence

The compact GTA SA 1.0 target, SHA-256 `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`, implements `CCarCtrl::CreateCarForScript` at `0x431F80`. Both its automobile and boat paths call `CEntity::GetDistanceFromCentreOfMassToBaseOfModel` at `0x536BE0` and add that result to the script Z before writing the entity matrix. The current `gta-reversed-dryxio` bodies match the decisive target instructions. The installed `greenwoo.dff` embeds `greenwoo_col` as COL3 with bounding-box minimum Z `-0.5`, so SCM `CREATE_CAR ... 12.3` becomes MTA centre Z `12.8`. Passing raw `12.3` to `createVehicle` places the Greenwood half a metre too low.

## Validated checkpoint

The complete first checkpoint was validated in game on 20 July 2026. `SWEET2A` finished naturally after `31453 ms` without a crash or model `304` load dialog. Client and server both observed the Greenwood at `(2508.700, -1671.700, 12.800)` with RGB colours `(78,104,129)/(100,100,100)` and plate `GROVE4L`. The streaming barrier accepted all three native entry tasks, the server confirmed Ryder, Smoke, and Sweet in MTA seats `1`, `2`, and `3`, and `SWE2_AA` completed before the leader entered as driver. The terminal `09D0` gate passed at `(2407.68, -1888.30, 13.16)` with the leader driving and GTA reporting all four wheels in contact. `/drivethruabort` then restored the mission state cleanly. No Drive-Thru client or server error was logged during the run.
