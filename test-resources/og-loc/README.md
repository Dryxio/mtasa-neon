# OG Loc / SMOKE1

This resource reconstructs OG Loc as a synchronized co-op story checkpoint. It preserves `SMOKE1A`, `SMOKE1B`, the Grove/police/Freddy's-house sequence, the PCJ-600 chase recordings `30` through `40`, the ten recorded obstacle vehicles, Rockstar's exact distance-dependent playback-speed formula, the basketball-court fight, Burger Shot return, failure labels and five-respect reward. The natural path also uses the installed `SMOKE1` GXT block and mission-audio events `35000..35075` in the SCM-authored travel, house, chase, combat, death-scene and Burger Shot order.

The server owns mission state and predicates. Every player is an active rider. Exactly one client owns Freddy, his bike and every native recorded path; observers receive synchronized transforms and combat results. Playback is delegated to the transversal `native-task-runtime` API and never executed redundantly by observers.

- `/ogloc` starts the presentation path.
- `ogloctest` starts the bounded structural headless path from the server console. It requires at least two connected clients.
- `ogloctransitionnatural [1|2]` starts the transition harness with every real file cutscene, mission-audio queue, script camera and world reconstruction. The optional argument is the exact connected-client count and defaults to the current count.
- `ogloctransitionskip [1|2]` runs the same harness, but broadcasts a synchronized skip only after every client has reported the current file cutscene `started`.
- `natural.request`, `skip.request` and `headless.request` retain the original filesystem entries. A headless request is consumed only after at least two clients are connected.
- `transition-natural-1.request`, `transition-natural-2.request`, `transition-skip-1.request` and `transition-skip-2.request` start the corresponding exact-count transition profile. A request remains pending until its exact client count is connected.
- `oglocabort` restores players and removes all mission-owned state.

The headless verdict is terminal: `[og-loc] PASS` or `[og-loc] FAIL`, with each transition emitted as `[og-loc-jsonl]`. Headless mode still runs every native chase recording and validates the synchronized owner/result contract; it only auto-advances travel, presentation and combat gates.

The transition harness deliberately does not use `setControlState` as evidence. At each drivable or on-foot release it emits `[og-loc-transition] READY` in the server log and `[og-loc-transition] INPUT_READY` in the target client's log. A VM runner must focus the named client window and hold physical `W`. The client requires a real `W` key event, at least three raw and post-filter analog frames above `0.8`, and more than `0.5 m` of real movement in the same bounded probe. This catches a cutscene or camera lease that leaves GTA input disabled; scripted control injection would bypass that failure. The runner should service every active client during `vehicle_all` and `foot_all` checkpoints. Each input checkpoint has a 60-second client timeout and a 70-second server timeout.

For automation, each client also emits `[og-loc-transition-jsonl]` records, and relays the same accepted results to `server.log`. `INPUT_READY` contains stable `player`, numeric `probeId`, `checkpoint`, `control` and `key` fields; the matching terminal record is `INPUT_PASS` or `INPUT_FAIL` and adds `reason`, `raw`, `processed`, `displacement` and `samples`. Clients that only supply camera, occupancy or streaming evidence emit `PROBE_PASS` or `PROBE_FAIL` with the same probe identity. Once all expected clients pass, the server emits `CHECKPOINT_PASS`; after cleanup it emits the aggregate mission `PASS` or `FAIL`. A runner must match both `player` and `probeId`, because multiple windows can be waiting at the same checkpoint.

`utils/og-loc-transition-harness.sh` is the VM wrapper used from the canonical
macOS repository. Start it before the server command so it establishes fresh
log offsets, for example `./utils/og-loc-transition-harness.sh
EvasivePanpipe6_CL2 1200` for two clients or
`./utils/og-loc-transition-harness.sh - 1200` for solo, then issue the matching
`ogloctransition*` command in the server console. The wrapper focuses the exact
GTA process named by each probe and injects the physical key through Parallels.

Transition auto-advance begins only after those physical-control proofs and the corresponding camera/occupancy evidence. It drives the objective transitions and ends combat after native cohort activation, while leaving `SMOKE1A`, `SMOKE1B`, the house, doorbell, death and Burger Shot presentations real. Recording `30` exercises Rockstar's live distance-speed formula; recordings `31..40` run at the native playback maximum so the bounded harness needs no repeated scripted relocation during the chase. The final objective relocation explicitly releases the return cohort, re-establishes OG Loc's passenger occupancy through the shared world runtime, then creates a fresh policy cohort; this exercises the same-owner rapid-replacement path instead of retaining a stale sync lease. Every scene verifies its acquired camera matrix, completion of its full configured mission-audio queue without the natural path's text fallback, and its return to the local player. A separate 12-second P0 watchdog fails if either synchronized Glendale reconstruction never reaches `travelReleased`, independently of the diagnostic vehicle probe.

Before either native file cutscene starts, OG Loc acquires the transversal
player-model lease and temporarily maps every participant to model `0`. This
prevents multiplayer skins `300..312` from occupying GTA's reserved CUTOBJ
slots; the exact original model of every player is restored on success,
failure, abort, disconnect or dependency shutdown.

The terminal transition verdict is `[og-loc-transition] PASS profile=<profile> players=<count>` or `FAIL`. It is emitted only after every client has released its cutscene, camera, audio and objective state, restored the local camera target, and acknowledged cleanup; the server separately verifies participant position, dimension, interior, collision, frozen state and gameplay controls. Dependency lifecycle-stop events fail and clean the mission immediately rather than being misreported as an actor death.

The harness proves lifecycle, camera matrices, input admission, synchronized occupancy, native playback ownership and cleanup. Human or recorded-video review remains necessary for framing quality, one-frame flicker, audio mix, lip synchronization, animation nuance, chase difficulty and continuous visual comparison with vanilla GTA:SA.
