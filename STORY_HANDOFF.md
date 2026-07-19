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
