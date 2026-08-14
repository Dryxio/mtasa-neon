# Native AI telemetry

Native AI telemetry is an opt-in, bounded JSONL diagnostic stream for tracing a
ped sample from its syncer to an observing client. It is intended for native
traffic, Story Runtime and future vehicle-AI investigations; it is not a public
scripting API or part of the network protocol.

## Enabling a trace

Before starting the client, create one of these empty marker files in the
client's `mta/logs` directory:

- `native-ai-telemetry.enable` enables every category.
- `native-ai-telemetry-network.enable` enables owner serialization, packet
  receipt and observer application.
- `native-ai-telemetry-ownership.enable` enables syncer acquire/release events.
- `native-ai-telemetry-group.enable` enables native group-response decisions.
- `native-ai-telemetry-task.enable` and
  `native-ai-telemetry-presentation.enable` reserve the task/presentation
  categories for focused producers added by later checkpoints.

Enablement is cached for the process lifetime, so restart the client after
adding or removing a marker. Output is written to
`native-ai-telemetry-primary.jsonl` and `native-ai-telemetry-cl2.jsonl`. Each
file is capped at 32 MiB with three rotated backups; lines are capped at 16 KiB
and the writer accepts at most 4096 events per second. A later accepted record
reports any dropped count as `dropped_before`.

## Correlating records

The schema is `neon.native_ai.telemetry`, version 1. Use:

- `(client_identity, packet.local_sequence)` to group records produced inside
  one client process;
- `packet.sample_key` to match bit-identical records, especially fast animation,
  and to bind each local `receive` to its `apply` record;
- `ped.mta_element_id`, `ped.traffic_id`, group fields, and
  `packet.sync_context` to disambiguate lifecycle or ownership changes.

`sample_key` fingerprints the lane and exact serialized record bit range,
including the element ID, but it is deliberately not sent on the wire. Fast
animation records currently remain bit-exact across the server relay. Spatial
records do not: the relay decodes and re-encodes them, which can re-quantize
position, heading, velocity, controller values, and flags. A different spatial
key therefore proves a relay transformation, not a dropped or misapplied
sample. Correlate spatial records by ped identity, owner epoch, lane, sequence,
and time until the relay carries or records a stable trace ID. A
locomotion-burst sender reports its producer lane as `locomotion_burst` while
hashing the canonical `ped_spatial` wire lane used by the receiver, but the
same spatial relay limitation still applies.

The legacy fire/water/reload tail also uses a different field order across the
current relay. The server edge is not instrumented in this checkpoint, so the
files prove local serialization, receipt, and observer application, but they
do not by themselves prove exact end-to-end spatial payload identity or expose
server processing timestamps.

Each ped record also includes the current task leaf and bounded parent
ancestry, animation/locomotion data when present, position, heading, velocity,
model, syncer state, and optional namespaced native-AI identity fields. Raw
engine pointers are intentionally excluded.

The deterministic test harness publishes five synchronized, namespaced fields
on its actors: run, scenario, actor, action and step. The writer copies them to
the top-level `trace` object. They are stable causal identifiers rather than
engine or network sequence numbers, so the same scripted action can be joined
across the owner, server, victim and observer logs.

Group-decision records are captured at GTA 1.0 US's stock
`CPedGroupIntelligence::AddEvent` call into the group decision maker, after the
response task has been selected. Their `group_decision` object contains the
native group slot, event and source classification, selected task ID,
threatened/friendly branch, representative member, and source ped identity.
The representative `ped` object supplies the corresponding traffic ID,
server-group ID, member index, role, owner, and epoch. This makes the chain
from an ambient group event to its exact vanilla response queryable without
logging raw pointers or player names.

## Deterministic workflow

Use `native-ai-test-harness` for regressions that cross AI, network ownership
and presentation. It creates an isolated, repeatable scenario and records each
causal stage under one run/action identity. Analyze a completed run with:

```sh
python3 utils/native-ai-trace-analyzer.py \
  --primary native-ai-telemetry-primary.jsonl \
  --cl2 native-ai-telemetry-cl2.jsonl \
  --server /path/to/deployed/native-ai-test-harness/@harness-server.jsonl
```

The server file is generated inside the deployed resource, not the canonical
`test-resources` source directory. Copy the three completed logs to one machine
before invoking the analyzer.

The analyzer checks writer integrity, ownership and handoff invariants, local
receive/apply pairing, fast-animation delivery, bounded semantic spatial
differences and harness assertions. It prints the earliest failing stage as
`FIRST_DIVERGENCE`. A clean report only means that the current invariants found
no divergence; it is not a general proof that the run matches vanilla.

For native-AI regressions, identify that first divergence before changing the
runtime. Record the actor, owner, action, task ancestry, observed transition and
the matching vanilla callsite. Make one causal correction per build whenever
possible, then rerun the same scenario as an A/B comparison. This prevents a
visual symptom from being patched independently in AI, transport and
presentation layers.

## Privacy and runtime cost

The writer records only typed engine/sync state and the whitelisted ambient-ped
identity keys already used by the traffic resource. It does not record player
names, chat, IP addresses, resource paths, or arbitrary element data. Enabling
telemetry does not publish element data or add network traffic.

When disabled, sync paths only read a cached category boolean; the group hook
also performs one handler-presence check before calling GTA directly. When
enabled, JSON escaping, finite-number checks, rate limiting, line limits, file
rotation, and exception containment keep the diagnostic path bounded. This is
still a high-detail debugging mode and should not be left enabled for normal
play.
