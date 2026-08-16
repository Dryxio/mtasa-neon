# Deterministic native AI test harness

This resource turns native-AI regressions into short, repeatable two-client
scenarios. It fixes actor models, positions, dimension, owner, action and
relative timing, then records one correlated server timeline. It does not
replace GTA behavior with Lua animations or scripted health subtraction: the
melee scenario starts GTA's native `TASK_COMPLEX_KILL_PED_ON_FOOT`, observes
the original native damage attempt on the ped owner and invokes the native
damage event on the authoritative victim.

The resource is a test tool, not a public gameplay API. Only one run may be
active at a time and every run cleans up its actors and restores both players.

## Commands

Start the resource, connect two clients and run one of:

```text
/nativeai run melee
/nativeai run melee <owner> <victim>
/nativeai run handoff
/nativeai run handoff <owner> <next-owner>
/nativeai run rotation
/nativeai run rotation <owner> <observer>
/nativeai run rotation_handoff
/nativeai run rotation_handoff <owner> <next-owner>
/nativeai run gang_unarmed_flee
/nativeai run gang_armed_leader
/nativeai run gang_armed_member
/nativeai run gang_two_armed_two_unarmed_melee
/nativeai run gang_armed_handoff
/nativeai run gang_friendly_source
/nativeai status
/nativeai cleanup
```

Without names, the caller is the initial owner and the only other connected
player is the victim or next owner. With more than two connected players,
provide both names. Both players must be alive and on foot. The resource moves
them to isolated dimension `219`; neither player should move or attack during
the automatic scenario.

`remote-melee-group-v1` creates Ballas models `102` and `103`, assigns both to
one explicit client and acquires one GTA ambient group there. MTA normally
initializes script-ped wrappers with player fighting style `15`; the harness
pins the stock `CPed` style `4` before combat. This makes GTA select
`UNARMED_1`, whose gang factors are the validated `5/6/9/15/25`, rather than
the wrapper's `KICK_STD` factors `8/20`. After a 1500 ms settle interval, both
peds receive GTA's native kill-on-foot task against the remote
player. PASS requires an authenticated owner-side native damage attempt and an
accepted native replay which reduces health on the victim's own client.

`native-group-handoff-v1` creates Grove models `105` and `106`, releases the
native group on the first owner, advances the epoch, transfers both MTA
syncers, and acquires the group on the second client. PASS requires both peds
to share the expected owner after acquisition.

`isolated-ped-rotation-v2` deliberately leaves Grove actor `ped-2` outside a
native group, with no WanderGang/GangFollower task able to rewrite its heading.
Its explicit owner applies the deterministic native headings `45`, `135`, then
`225` degrees at predetermined 450 ms intervals. Dispatch never waits for
observer convergence: every target persists longer than the configured 400 ms
ped sync interval, while the close spacing keeps a one-snapshot interpolation
lag observable instead of allowing it to disappear before the next action.
The server-side harness requires the owner to hold each target for at least
120 ms before the action is attributable to networking, plus owner acceptance and
final convergence during a 1200 ms hold; its PASS means the trace capture is
complete, not that presentation latency is correct. The offline causal analyzer
owns that verdict.
`isolated-ped-rotation-handoff-v2` first transfers both ped syncers to the second
client, then runs the
same sequence under epoch 2. Run the non-handoff scenario first; use the
handoff variant only after its baseline is stable.

## Correlation and assertions

All actors receive synchronized element data before each action:

- `neon:nativeAIRunId`
- `neon:nativeAIScenarioId`
- `neon:nativeAIActorId`
- `neon:nativeAIActionId`
- `neon:nativeAIStep`

The native-AI telemetry writer copies these fields into its top-level `trace`
object. Server events and assertions are written to the deployed resource's
private file `harness-server.jsonl` (`@harness-server.jsonl` in the MTA file
API). Each run starts a fresh file with schema
`neon.native_ai.harness`, version `1`. Its stable correlation fields are
`run_id`, `scenario_id`, `action_id`, `actor_id`, `monotonic_ms` and
`relative_ms`; assertion records contain `name`, `passed`, `expected` and
`actual`.

The melee causal chain uses stable event names:

```text
action_dispatched
owner_native_damage_attempt
server_validated_forward
victim_injection_result
final_observation
```

`action_scheduled` announces the future action, then actors remain labelled
`prepare/settle` for 1500 ms so initial streaming convergence cannot be
misclassified as an action transport loss. The action label changes at actual
dispatch. Planned and actual relative milliseconds are both logged. Missing
stages become explicit failed assertions at the verdict or timeout; the first
server validation rejection ends the run immediately with a `run_end` record.

Rotation runs emit `rotation_action_dispatched` and
`rotation_schedule_complete`; an action may also emit
`rotation_action_converged` when both clients converge before the next fixed
turn. The C++ telemetry supplies the corresponding owner `packet_serialize`,
observer `packet_receive`, and `rotation_post_process` samples under the same
action and actor IDs. The analyzer starts presentation timing at the exact
observer receive, reports network transit separately, and fails if the rendered
matrix remains outside two degrees after `spatialSyncRate + 100 ms`. The extra
100 ms covers the 50 ms post-render sampling cadence plus one frame/scheduling
margin. It also fails a sustained one-snapshot lag when the first post-render
sample at least 50 ms after receipt remains within 15 degrees of the previous
target and at least 45 degrees from the newly decoded target for both
consecutive turns. The arbitrary first sample (0–50 ms after receipt) remains
in the evidence but cannot trigger this verdict: a healthy 100 ms interpolation
may still be near its old target in its first frame. At the 50 ms checkpoint a
400 ms, 90-degree interpolation has advanced about 11.25 degrees, while a
healthy 100 ms interpolation has advanced about 45 degrees. The 45-degree new
target bound also excludes small animation-facing corrections.

The harness controls every input relevant to these scenarios. It does not
claim to seed GTA's process-global random generator; future scenarios which
exercise a random decision must record the selected native branch or add a
dedicated engine seed/capture facility before claiming deterministic replay.

The ordinary combat scenarios use three fixed Ballas actors. The
`gang_two_armed_two_unarmed_melee` fixture creates four Ballas with exactly two
Colts and two unarmed members, then injects one melee-source damage event. It
does not depend on ambient spawn or weapon RNG. Retail should assign KILL to
all four members; `group_member_allocator_assignment` is the authoritative
oracle when an individual DUCK temporarily hides that group primary task. The
classification case uses two separate Ballas groups (three target members plus
two source members).
Every actor uses the exact ambient CPed
weapon state: total ammo `25001`, STD skill thresholds, and full clips. They
inject one real native damage-response event into GTA's group instead of
forcing `setPedKillOnFoot`. The firearm source is frozen for the capture and
its exact previous frozen state is restored during cleanup, so a human input
cannot turn an aim or impact check into a different run.

The decision oracle is branch-aware because GTA's weighted group decision uses
the process-global RNG. A selected `TASK_GROUP_FLEE_THREAT` must allocate flee
tasks. Against a firearm, a selected `TASK_GROUP_KILL_THREATS_BASIC` makes armed
members attack and assigns `TASK_SEEK_COVER_UNTIL_TARGET_DEAD` to participating
unarmed members. Against a melee source, all members receive a kill task;
an all-unarmed firearm response is converted to flee by GTA's appropriateness
check. The analyzer correlates the selected C++ task with the allocation seen
by the harness. Only an actually selected fight branch requires the owner →
server → authoritative-victim damage chain. `gang_armed_handoff` repeats the
collective decision after epoch 2 acquisition. `gang_friendly_source` uses a
managed group member as the event source; the analyzer owns the strict
`event_source_type=2` verdict from C++ group-decision telemetry.

For armed fight branches, owner telemetry emits
`native_weapon_instant_hit_resolved` immediately after the standard on-foot
primary `FireInstantHit` line-of-sight callsite. The copied record contains the
ray, weapon and LOS result. Target and hit identities/models are included only
when MTA resolves them through its entity pools; raw GTA entity fields and the
temporary collision point are deliberately excluded. This distinguishes a
genuine world miss from a safely resolved hit that failed to reach the damage
bridge without changing GTA's aim, impact or damage behavior.

The intended debugging rule is to compare owner, server and observer/victim
timelines and identify the first divergent event before changing behavior.
Keep one implementation change per A/B run so a PASS or FAIL has one causal
interpretation.
