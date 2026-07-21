# Story runtime handoff

This file is the durable operational handoff for continuing the GTA: San Andreas story-runtime work in Neon. It contains method, architecture, invariants, and collaboration rules only. Live implementation status belongs in source, Git history, tests, and the component documentation—not here.

## Resume protocol

A new primary agent should read, in order:

1. [`AGENTS.md`](./AGENTS.md) for the canonical worktree, VM, build, deployment, formatting, and verification rules.
2. This file completely.
3. [`STORY_RUNTIME.md`](./STORY_RUNTIME.md) for the architecture, reverse-engineering gate, opcode evidence, synchronization model, and roadmap.
4. [`test-resources/tagging-up-turf/README.md`](./test-resources/tagging-up-turf/README.md) for the exact state of the mission prototype.
5. The recent Git history and both worktree statuses before editing anything.
6. The user's current request. Do not infer the next slice from this handoff; establish it from the live code, documentation, logs, and conversation.

Suggested resume prompt:

> Read `AGENTS.md`, `STORY_HANDOFF.md`, and every directly referenced file required for the next slice. You are the primary orchestrator for the SCM story-runtime port. Preserve unrelated worktree changes, verify every relevant `gta-reversed-dryxio` reconstruction against the target GTA:SA assembly before implementing it in Neon, and stop for the user to perform every in-game test.

## Non-negotiable user constraints

- The agent must **never perform an in-game test**. Do not launch or control the graphical MTA/GTA client to exercise gameplay. Build, deploy, restart server resources, inspect logs, and prepare commands, then stop and ask the user to test in game.
- The user supplies the visual/gameplay feedback. After the user tests, inspect the VM client and server logs directly and correlate them with that feedback.
- Work as an orchestrator when the slice benefits from parallel research. Give agents bounded tasks such as ASM verification or MTA synchronization mapping. The primary agent owns API design, reviews all evidence, integrates changes, builds, and decides when the slice is ready for the user's test.
- Do not commit before the user confirms the in-game result when a gameplay test is required.
- Preserve unrelated dirty files. Both canonical repositories can contain concurrent work.
- Do not use the em dash character in user-facing prose or new documentation. The user considers that punctuation distracting and has explicitly asked agents to avoid it.

## Objective and architecture

The long-term objective is to reproduce GTA:SA missions, beginning with `SWEET1` / Tagging Up Turf, while building reusable Neon engine primitives rather than hardcoding one mission in C++.

Current parity work targets mission-visible behavior that remains meaningful in multiplayer. Do not port purely single-player campaign bookkeeping unless the user explicitly expands the scope. This currently excludes global gang-zone strength, story progression counters, respect, wanted-level cleanup, collectible or weapon pickups, campaign unlocks, and similar save-state side effects. Keep such SCM operations documented as intentionally out of scope rather than reporting them as missing multiplayer mission behavior.

The intended split is:

- server-authoritative mission/SCM orchestration, conditions, co-op policy, checkpoints, and recovery;
- generic C++ integrations for GTA-native tasks, vehicle recordings, camera/actor mechanics, tags, and other engine semantics;
- Lua resources as consumers and conformance harnesses, not as permanent reimplementations of GTA AI;
- ordinary MTA synchronization where it is sufficient, with explicit syncer ownership and migration policy where native client simulation is involved.

Do not build a client-local `main.scm` VM independently on every participant. The reasons and longer-term interpreter design are in [`STORY_RUNTIME.md`](./STORY_RUNTIME.md).

## Canonical repositories and evidence

| Purpose | Canonical location |
| --- | --- |
| Neon source | `/Users/salimtrouve/Documents/GitHub/mtasa-neon` |
| GTA reverse source | `/Users/salimtrouve/Documents/GitHub/gta-reversed-dryxio` |
| Local decompiled `main.scm` navigation aid | `/Users/salimtrouve/Documents/GitHub/gta-reversed-dryxio/reference/scripts/main-scm-decompiled/fakeMainOutputFile.sc` |
| Reverse verification tooling | `/Users/salimtrouve/Documents/GitHub/auto-re-agent` |
| Target GTA executable | `/Users/salimtrouve/Documents/GTA-SanAndreas/GTA_SA.EXE` |
| Reliable VM GTA installation | `C:\dev\GTA-SA` |
| Original/decompiled SCM reference | <https://github.com/x87/GTA_SA_SCRIPT> |
| Secondary leaked/decompiled reference | <https://gist.github.com/JuniorDjjr/2129e1e7640f7969acdfb1c56c263155> |

Target executable SHA-256:

```text
72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b
```

`main.scm` is authoritative for mission control flow, coordinates, conditions, and opcode parameters. Decompiled text is a navigation aid and can contain errors. The GTA executable is authoritative for the native behavior behind each opcode.

## Mission parity protocol

The phrase `1:1 with vanilla` means exhaustive classification of the reachable
mission graph, not a review of the happy path. Do not claim parity from a
successful end-to-end run alone. Before implementation, construct a coverage
map for the complete mission script, then keep it current as evidence changes.

The goal is to discover missing behavior before the first in-game run. Follow
this order:

1. Identify the exact mission script, entry and exit labels, subroutines,
   cutscene names, GXT blocks, audio tables, vehicle recordings, external
   scripts, and cleanup paths.
2. Build the control-flow graph for every reachable success, failure, skip,
   retry, timeout, actor-death, vehicle-damage, player-exit, and alternate-route
   branch. Include loops whose monitoring conditions change by phase.
3. Classify every reachable SCM opcode. Consecutive instructions may share one
   row only when they implement one indivisible effect and have no independent
   native state or failure behavior.
4. Establish the multiplayer scope before coding. Every excluded operation
   needs a precise category and reason. Never silently omit an opcode because
   it looks like bookkeeping or has no immediate visual effect.
5. Audit every native semantic not already proven for the exact target build.
   Search existing Neon primitives first, then the target assembly and
   `gta-reversed-dryxio`, before proposing new C++.
6. Design the smallest reusable primitive set and isolated harnesses needed by
   the whole mission. Front-load cross-cutting behavior such as ownership,
   streaming, task sequencing, collision policy, damage, camera, and cleanup.
7. Implement mission phases from the completed map. Treat each actor or vehicle
   creation and reconstruction as a distinct event, even when it reuses the
   same model or logical role.
8. Run a fresh parity audit after the full mission works. Start from the SCM
   graph and coverage table, not from the implementation, so existing code does
   not bias the review.

### Required coverage matrix

Keep the mission matrix in the resource README or a dedicated mission audit
document. Use exact SCM labels or bytecode offsets so another agent can resume
without reconstructing the mapping from prose.

| SCM label or offset | Entry condition and phase | Vanilla operation and exact parameters | Native effect or dependency | Multiplayer relevance | Neon implementation | Evidence | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `label / 0x...` | Branch, loop, or transition that reaches it | Opcode sequence, coordinates, models, seats, flags, timings, text and audio keys | Engine behavior, implicit state, streaming or task dependency | Required, adapted, or excluded with reason | API, resource function, harness, or explicit gap | SCM, ASM/reverse, static test, logs, visual test | `implemented`, `adapted`, `excluded`, `missing`, or `uncertain` |

The matrix must cover at least:

- every actor and vehicle creation, deletion, reconstruction, seat assignment,
  model, color, health, weapon, proof, door, tyre, collision, targeting, mission,
  and persistence property;
- every native task, sequence child, driving style, speed, coordinate, radius,
  timeout, repetition rule, cancellation, and transition between vehicle and
  on-foot behavior;
- every camera, fade, widescreen, cutscene, subtitle, objective, checkpoint,
  blip, audio event, conversation interruption, animation, look-at, and tune;
- every success, failure, low-health, death, explosion, player-exit,
  out-of-range, alternate-scene, skip, and cleanup branch;
- the exact phases during which each monitor is active. A condition that exists
  in one SCM loop must not remain globally active in later phases;
- all state intentionally retained after success or failure, including player
  position, vehicle, weapons, camera, actors, and mission-owned leases.

### Scope and exclusion ledger

Maintain a second, short table beside the coverage matrix. This prevents
multiplayer scope decisions from becoming accidental omissions.

| SCM operation | Classification | Reason | Visible consequence checked | Revisit trigger |
| --- | --- | --- | --- | --- |
| Exact opcode or block | `required`, `adapted multiplayer`, or `excluded solo bookkeeping` | Concrete architectural or user-scope reason | What was inspected to prove the exclusion is not visibly relevant | Condition that would make it necessary later |

The default solo-bookkeeping exclusions in this handoff remain valid, but each
mission must still list where they occur. An operation is not bookkeeping merely
because its effect is delayed or invisible near the player. Before excluding a
flag or property, find its native readers and check effects during streaming,
damage, destruction, task changes, syncer migration, and phase transitions.

### Three independent levels of proof

Record these separately. One level must never stand in for another:

| Proof level | Question answered | Typical evidence |
| --- | --- | --- |
| SCM intent | What does the mission request, and on which branch? | Raw bytecode, verified decompile, labels, parameters and offsets |
| Native semantics | What does the target GTA build actually do with that request? | Target assembly, layout checks, gta-reversed comparison and native call graph |
| Runtime observation | Did Neon reproduce the intended world result and lifecycle? | Acceptance plus task observation, authoritative state, logs and the user's visual test |

Decompiled SCM is never sufficient native-semantics evidence. A plausible
`gta-reversed-dryxio` implementation is never sufficient target evidence. A Lua
return value is never sufficient runtime evidence for a native task.

### Harness and checkpoint rules

Each checkpoint should validate one independently diagnosable capability and
leave the mission runnable. Its PASS criteria must measure the intended effect
directly and include negative guards against plausible false positives. For
example, route simulation requires horizontal movement, valid world height,
task progress, retained ownership, and no unexpected stream-out. Raw 3D
distance alone can incorrectly count a vehicle falling through unloaded world
collision as successful driving.

Before asking the user to test, provide:

- exact commands and ordering;
- expected actors, vehicles, seats, colors, positions, camera, text, audio and
  approximate timing;
- the expected task, ownership, health and lifecycle log evidence;
- explicit failure signs and which log lines distinguish their likely causes;
- cleanup or retry commands that do not require restarting unrelated work.

After the test, correlate visual feedback with client and server logs. A PASS is
valid only when all required predicates agree. If a metric was capable of a
false positive, invalidate that result, repair the harness, and repeat it.

### Parity completion gate

Do not write `1:1`, `complete`, or `no missing behavior` unless all of the
following are true:

- the reachable SCM graph has no unclassified block;
- the coverage matrix has no `missing` or unexplained `uncertain` row;
- every exclusion appears in the scope ledger with a concrete reason;
- all creations and reconstructions were compared independently;
- every required native semantic has target evidence or a previously verified
  reusable primitive with matching parameters;
- success, failure, alternate, skip, interruption and cleanup paths are covered;
- static evidence and runtime evidence are clearly distinguished;
- the final fresh audit found no behavior only inferred from existing Neon code.

If in-game validation is still pending, say `statically mapped and awaiting
in-game validation`, never `1:1 validated`.

## Mandatory reverse-engineering gate

Before implementing or exposing any native behavior:

1. Read the local instructions in `/Users/salimtrouve/Documents/GitHub/auto-re-agent`.
2. Identify the exact opcode handler, constructors, clone/destructor, process/control methods, allocations, layouts, sentinels, and parameter conversions relevant to the slice.
3. Compare `gta-reversed-dryxio` against the target executable's assembly. Do not accept plausible reconstructed C++ as ground truth.
4. Record addresses, target hash, verified behavior, discrepancies, and remaining uncertainty in [`STORY_RUNTIME.md`](./STORY_RUNTIME.md) or the relevant harness README.
5. Correct every proven discrepancy in the canonical `gta-reversed-dryxio` worktree. Add size/offset assertions where layout is involved.
6. Keep reverse corrections in a separate, narrowly staged commit. The reverse worktree is heavily dirty; never stage whole files merely because the desired hunk is inside them.
7. Only then implement the Neon wrapper and Lua surface.

When delegating, one agent should normally own the ASM/reverse audit and another can map the existing MTA abstraction, ownership, and synchronization path. They should report evidence before the primary agent finalizes the API.

## Reconstruct the live state

Do not copy a status snapshot into this file. At the beginning of a new conversation, reconstruct the current state from the canonical sources:

1. Run `git status --short`, `git log --oneline --decorate -30`, and `git diff --stat` in both Neon and `gta-reversed-dryxio`.
2. Read the native story API table in the root [`README.md`](./README.md).
3. Read the current implementation/evidence sections of [`STORY_RUNTIME.md`](./STORY_RUNTIME.md).
4. Read the relevant resource README, especially [`test-resources/tagging-up-turf/README.md`](./test-resources/tagging-up-turf/README.md).
5. Inspect the resource stage machine, isolated conformance harnesses, and latest client/server logs rather than trusting prose alone.
6. Ask the user only if the desired next outcome remains ambiguous after those checks.

This keeps exposed APIs, completed slices, pending substitutes, test evidence, and commit anchors in their canonical locations. A new agent should summarize the reconstructed state before proposing or implementing the next slice.

## Durable engineering invariants

- Never insert virtual methods between existing methods in a cross-module MTA interface. Append them or design a non-breaking extension, and rebuild every affected module. A shifted `CPed` vtable previously made `core.dll` call the wrong `GetPedIntelligence()` slot.
- Native client simulation requires an explicit owner. Establish which client owns the ped/vehicle, how normal MTA sync propagates the result, and what happens on migration, disconnect, stream-out, abort, and resource stop.
- A synchronized mission must not rely on a client-local actor policy being coincidentally present. Replicate desired policy to every potential syncer and restore state when relinquishing a surviving entity.
- Treat native task acceptance, task observation, and authoritative world-state completion as separate facts. Harnesses should prove the relevant combination rather than passing on a single Lua return value.
- Preserve GTA constructor, vtable, destructor, and layout semantics where possible. Prefer calling verified original engine routines over copying incomplete reconstructed C++.
- Keep generic engine behavior in reusable Neon APIs. Mission-specific coordinates, sequence order, dialogue choices, co-op conditions, and failure policy belong in the story resource/runtime.
- Treat every SCM opcode as a semantic adapter, not as a raw argument copy into an MTA function. Audit and document handler-side conversions such as `009A CREATE_CHAR` adding `1.0` to script Z, then assert the converted native state before dependent tasks begin.
- Keep every temporary substitute explicit in code, documentation, and the instruction trace. Never label a Lua approximation as a native opcode implementation.
- Keep the mission playable after each slice, with cleanup and diagnostics for every refusal or premature lifecycle transition.

## Build and deployment quick reference

The canonical source is edited only on macOS. The VM-local build copy is `C:\dev\mtasa-vm-custom`. Never synchronize it with plain `robocopy /MIR` or `/PURGE`: the canonical tree omits generated dependencies and cached VM state which a mirror can destroy.

Use the canonical `utils/vm-build.ps1` helper through Windows PowerShell 5.1. Name only the files owned by the current checkpoint, review its read-only plan, then rerun the identical command with `-Execute`:

```powershell
$vmBuild = 'C:\Mac\Home\Documents\GitHub\mtasa-neon\utils\vm-build.ps1'
$files = @(
    'Client\game_sa\CTasksSA.cpp',
    'Client\mods\deathmatch\logic\luadefs\CLuaPedDefs.cpp'
)

& $vmBuild -Files $files -ClientProjects @('Game SA', 'Client Deathmatch')
& $vmBuild -Files $files -ClientProjects @('Game SA', 'Client Deathmatch') -Execute
```

Select the smallest affected project set from `AGENTS.md`. For the usual client-native story task spanning `Client/game_sa`, the client Lua surface, and their SDK interfaces, build `Game SA` and `Client Deathmatch` in `Release|Win32`. Use `-BuildOnly` only to retry an already synchronized project. Use `-Regenerate` only when compiled-source membership or build definitions changed. The helper owns hash-verified synchronization, dependency preservation, output-lock checks, and output verification.

Outputs:

```text
C:\dev\mtasa-vm-custom\Bin\mta\game_sa.dll
C:\dev\mtasa-vm-custom\Bin\mods\deathmatch\client.dll
```

Copy changed test resources into:

```text
C:\dev\mtasa-vm-custom\Bin\server\mods\deathmatch\resources
```

Useful runtime logs:

```text
C:\dev\mtasa-vm-custom\Bin\MTA\logs\console.log
C:\dev\mtasa-vm-custom\Bin\MTA\logs\clientscript.log
C:\dev\mtasa-vm-custom\Bin\server\mods\deathmatch\logs\server.log
```

Before asking for the in-game test:

- format every changed C++ file;
- run `git diff --check`;
- run `luac -p` on every changed Lua file;
- build the smallest affected Win32 projects;
- deploy the DLLs/resources;
- restart the affected server resources;
- tell the user that a complete client restart is required when DLLs changed;
- provide exact commands, expected observations, timing, and failure evidence to report.

Keep the mission playable after each slice. Temporary substitutes must remain explicitly labelled in the instruction trace and documentation until replaced. The primary agent should derive priorities from the user's current goal rather than maintaining them in this handoff.
