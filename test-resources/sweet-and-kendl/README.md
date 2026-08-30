# Sweet & Kendl / INTRO1

This resource reconstructs the first San Andreas mission around Neon's reusable story services. `/sweetandkendl` runs the synchronized `INTRO1A` and `INTRO1B` file cutscenes, Smoke's in-world Peren scene, the funeral drive-by, the first three-bike ride, the Ballas chase, the split, the Ryder/Smoke return ride, the recorded-car finale, the save-house tutorial, failure paths and the three-respect completion.

Every connected player is an active co-op cyclist during gameplay and receives a separate BMX. The server owns phase changes, objective predicates, pass/fail and cleanup. Exactly one client owns the gang and Ballas native AI through `native-task-runtime` cohorts; other clients receive only synchronized movement, combat and presentation. File cutscene, mission audio, camera and help presentation run locally behind a party barrier.

Commands:

- `/sweetandkendl` starts the complete presentation path.
- `sweetandkendlnatural` from the server console starts that same presentation
  path on the first connected player; `sweetandkendlskip` synchronizes the
  current file-cutscene skip for non-interactive validation.
- `sweetandkendltest` from the server console starts the bounded headless path. It creates the same actors and native cohorts, advances both objective gates automatically, and must terminate with `[sweet-and-kendl] PASS` plus `[sweet-and-kendl-jsonl]` stage evidence.
- `sweetandkendltransitionnatural 1|2` and
  `sweetandkendltransitionskip 1|2` run the bounded presentation profiles for
  solo or co-op. Both real file cutscenes and every in-world scene execute
  through their ordinary party barriers. The skip profile waits for every
  client to report that the native cutscene started before broadcasting its
  synchronized skip.
- Creating `headless.request` in the deployed server resource starts that same path as soon as two clients are present, then atomically consumes the request. This is the non-interactive VM harness entry point.
- `transition-natural-1.request`, `transition-natural-2.request`,
  `transition-skip-1.request` and `transition-skip-2.request` are the equivalent
  filesystem entry points for the four presentation profiles.
- `natural.request` and `skip.request` provide the equivalent filesystem entry
  points for presentation-path start and synchronized file-cutscene skip.
- `/sweetandkendlabort` restores all enrolled players and removes mission-owned entities.

Run `./utils/sweet-and-kendl-transition-harness.sh EvasivePanpipe6_CL2 1200`
before a two-client profile, or pass `-` instead of the secondary player for a
solo profile. The runner watches only newly appended server/client log lines,
maps each player to its GTA process, injects a real physical `W` press whenever
the resource emits `INPUT_READY`, and exits only on a terminal
`[sweet-and-kendl-transition] PASS/FAIL`. Each ride release must prove the
gameplay camera, driver occupancy, streaming, raw and processed acceleration,
and physical displacement for every participant. Only after that proof does
the resource relocate the bikes to exercise the split, return-Ballas and Grove
objective predicates. Cutscenes, scene playback and cleanup are never
short-circuited; the terminal verdict is emitted only after every client
acknowledges restored camera/control state and the server verifies the original
player transform.

The checkpoint preserves the original route coordinates and speeds, actor/vehicle models, health, tyre policy, Ballas `MISSION_ESCORT_LEFT` and drive-by parameters, split staging, recordings 201/205/206, GXT keys, mission-audio event order, cameras, failure labels and reward. Multiplayer policy intentionally replaces the single-CJ predicates with a shared party requirement. Ballas rubber-banding is server-visible and conservative because an authoritative server has no native `IS_CAR_ON_SCREEN` result; it acts only beyond 90 metres.

Validation must include all four transition profiles, the headless two-client
PASS, task-cohort ownership on both ride phases, resource restart cleanup and
inspection of both client script logs for errors. The harness exposes stage,
barrier, occupancy and cleanup evidence. The current implementation was also
compared directly with the installed `main.scm` for command order, coordinates,
timings, cameras, task composition and mission-audio order. Successful cleanup
preserves each player's BMX or on-foot state, returns every participant to the
exterior world (`interior=0`, `dimension=0`) and restores controls.

Two primitive-fidelity gaps remain explicit: the funeral reaction approximates
SCM's `TASK_DIVE_AND_GET_UP`, and the original `SET_CAR_FORWARD_SPEED` impulses
of `10` and `5` are not yet exposed as a synchronized story primitive. Campaign
respect/progress/save persistence also remains outside this isolated mission
resource; the visible respect reward and mission-passed presentation are
implemented. These limits do not bypass any stage, party barrier or cleanup
proof in the validation harness.
