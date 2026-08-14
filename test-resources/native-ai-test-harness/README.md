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

The harness controls every input relevant to these two scenarios. It does not
claim to seed GTA's process-global random generator; future scenarios which
exercise a random decision must record the selected native branch or add a
dedicated engine seed/capture facility before claiming deterministic replay.

The intended debugging rule is to compare owner, server and observer/victim
timelines and identify the first divergent event before changing behavior.
Keep one implementation change per A/B run so a PASS or FAIL has one causal
interpretation.
