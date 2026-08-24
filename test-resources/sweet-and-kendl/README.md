# Sweet & Kendl / INTRO1

This resource reconstructs the first San Andreas mission around Neon's reusable story services. `/sweetandkendl` runs the synchronized `INTRO1A` and `INTRO1B` file cutscenes, Smoke's in-world Peren scene, the funeral drive-by, the first three-bike ride, the Ballas chase, the split, the Ryder/Smoke return ride, the recorded-car finale, the save-house tutorial, failure paths and the three-respect completion.

Every connected player is an active co-op cyclist during gameplay and receives a separate BMX. The server owns phase changes, objective predicates, pass/fail and cleanup. Exactly one client owns the gang and Ballas native AI through `native-task-runtime` cohorts; other clients receive only synchronized movement, combat and presentation. File cutscene, mission audio, camera and help presentation run locally behind a party barrier.

Commands:

- `/sweetandkendl` starts the complete presentation path.
- `sweetandkendlnatural` from the server console starts that same presentation
  path on the first connected player; `sweetandkendlskip` synchronizes the
  current file-cutscene skip for non-interactive validation.
- `sweetandkendltest` from the server console starts the bounded headless path. It creates the same actors and native cohorts, advances both objective gates automatically, and must terminate with `[sweet-and-kendl] PASS` plus `[sweet-and-kendl-jsonl]` stage evidence.
- Creating `headless.request` in the deployed server resource starts that same path as soon as two clients are present, then atomically consumes the request. This is the non-interactive VM harness entry point.
- `natural.request` and `skip.request` provide the equivalent filesystem entry
  points for presentation-path start and synchronized file-cutscene skip.
- `/sweetandkendlabort` restores all enrolled players and removes mission-owned entities.

The checkpoint preserves the original route coordinates and speeds, actor/vehicle models, health, tyre policy, Ballas `MISSION_ESCORT_LEFT` and drive-by parameters, split staging, recordings 201/205/206, GXT keys, mission-audio event order, cameras, failure labels and reward. Multiplayer policy intentionally replaces the single-CJ predicates with a shared party requirement. Ballas rubber-banding is server-visible and conservative because an authoritative server has no native `IS_CAR_ON_SCREEN` result; it acts only beyond 90 metres.

Validation must include the headless two-client PASS, a natural two-client file-cutscene start/skip, task-cohort ownership on both ride phases, resource restart cleanup and inspection of both client script logs for errors. Visual camera/audio fidelity still requires human review even when every state and native-task gate passes.
