# OG Loc / SMOKE1

This resource reconstructs OG Loc as a synchronized co-op story checkpoint. It preserves `SMOKE1A`, `SMOKE1B`, the Grove/police/Freddy's-house sequence, the PCJ-600 chase recordings `30` through `40`, the ten recorded obstacle vehicles, Rockstar's exact distance-dependent playback-speed formula, the basketball-court fight, Burger Shot return, failure labels and five-respect reward. The natural path also uses the installed `SMOKE1` GXT block and mission-audio events `35000..35075` in the SCM-authored travel, house, chase, combat, death-scene and Burger Shot order.

The server owns mission state and predicates. Every player is an active rider. Exactly one client owns Freddy, his bike and every native recorded path; observers receive synchronized transforms and combat results. Playback is delegated to the transversal `native-task-runtime` API and never executed redundantly by observers.

- `/ogloc` starts the presentation path.
- `ogloctest` starts the bounded headless path from the server console.
- `natural.request`, `skip.request` and `headless.request` expose filesystem triggers for VM automation; a headless request waits for two clients.
- `oglocabort` restores players and removes all mission-owned state.

The headless verdict is terminal: `[og-loc] PASS` or `[og-loc] FAIL`, with each transition emitted as `[og-loc-jsonl]`. Headless mode still runs every native chase recording and validates the synchronized owner/result contract; it only auto-advances travel, presentation and combat gates.
