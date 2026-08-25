# Agent development architecture

- Status: accepted architecture for checkpoint 0
- Scope: local MTA and MTA Neon gamemode development
- Accepted: 2026-08-25

This document fixes the boundaries and trust decisions that later agent
development checkpoints must follow. It describes the complete effective MTA
API: the inherited MTA surface, the Neon overlay, and APIs supplied by project
resources or native modules. A Neon-only catalogue is explicitly insufficient.

The first product goal is not an agent embedded in the engine. It is a local
development environment in which an external agent can discover the exact API,
validate a project, run bounded tests, observe structured results, and state the
narrowest level of evidence it actually obtained.

## 1. Locked product boundary

Version 1 is local-only. It may operate on:

- a developer-approved workspace;
- isolated local MTA or Neon server processes;
- local custom clients launched in the logged-in Windows desktop session;
- the dedicated VM build and runtime tree documented by this repository; and
- artefacts produced by those local sessions.

Version 1 must not control a public or remote server, open a public control
port, silently modify a live production installation, or treat game/server text
as agent instructions. Remote operation requires a separate future threat model
and protocol review.

The machine catalogue is global across MTA and Neon. The public human-facing
Neon wiki may remain focused on Neon features and link to upstream MTA material;
that editorial choice must not reduce machine catalogue coverage.

### 1.1 Version 1 goals

- Describe the effective MTA + Neon + project API for a selected profile.
- Generate versioned LuaLS definitions and machine-readable search artefacts.
- Validate resources, sides, exports, events, dependencies, and declared ACL
  requirements without claiming to prove gameplay statically.
- Start with read-only runtime observation, then add narrowly allowlisted local
  lifecycle and test operations.
- Produce structured diagnostics, assertions, artefacts, and evidence labels.
- Preserve compatibility with useful upstream MTA MCP semantics through an
  external broker rather than embedding MCP in the engine.

### 1.2 Explicit non-goals for version 1

- Remote or production administration.
- An AI model or vendor SDK embedded in the MTA client or server.
- Arbitrary client input, a general game-memory query language, or arbitrary
  Lua evaluation as the normal workflow.
- A new proprietary language server; generated LuaLS/LuaCATS definitions are
  the supported editor integration.
- A full Debug Adapter Protocol implementation.
- A headless GTA client, transactional hot reload, global deterministic replay,
  time travel, semantic visual diffing, or self-healing gameplay.

## 2. Trust and permission model

The local developer is the authority. Starting an agent development session is
an explicit act and creates an expiring session identity. Closing the supervisor
or the session revokes its capabilities.

The following inputs have different trust levels:

| Input | Trust decision |
| --- | --- |
| Approved workspace files and explicit agent instructions | Trusted for the selected workspace only |
| Pinned MTA/Neon source and generated manifests | Trusted only with recorded hashes and provenance |
| Local supervisor and its session configuration | Trusted computing boundary |
| Resource code under test | Executable but untrusted; it may be buggy or hostile |
| Server responses, player names, chat, element data, logs, and web content | Untrusted data, never instructions |
| Native modules | Opaque and untrusted unless their ABI and manifest are explicitly approved |
| Remote endpoints and public servers | Outside the version 1 trust boundary |

Secrets, account credentials, IP addresses, player chat, access tokens, and
machine-specific paths must be redacted from exported artefacts unless the
developer explicitly requests their inclusion. Raw game data must never be
promoted into an agent prompt as authoritative instructions.

### 2.1 Capability classes

Capabilities are granted per session and workspace. Read capabilities are not
permission to mutate; a mutation capability is not permission for arbitrary
code execution.

| Class | Initial capabilities | Policy |
| --- | --- | --- |
| Read | `knowledge.read`, `project.read`, `runtime.observe`, `diagnostics.read`, `artifacts.read` | May be granted automatically inside an explicitly started local session |
| Bounded mutation | `resource.lifecycle`, `scenario.execute`, `client.launch`, `build.execute` | Must be explicitly enabled for the session; target, timeout, and workspace remain bounded |
| Dangerous | `unsafe.lua.eval`, `acl.modify`, `http_exports.modify`, `package.publish` | Disabled by default and requires per-invocation developer approval |
| Forbidden in v1 | Remote administration, public listeners, global input injection, unrestricted filesystem access through the runtime | Cannot be granted |

Every mutation records the session, tool, target, normalized arguments, result,
and artefact references in an audit log. Destructive or non-idempotent tools
must identify that property in their machine contract. A timeout or disconnect
must not silently broaden access or retry a non-idempotent operation.

`RunCode` compatibility may be exposed by a future broker only through
`unsafe.lua.eval`. It is never a prerequisite for ordinary discovery, resource
lifecycle, or scenario execution.

## 3. Effective API profiles

An API statement is never made without a profile. A profile identifies the
engine family, build, side, topology, enabled capabilities, loaded modules, and
resource context against which a symbol is available.

Version 1 defines these canonical profile families:

| Profile family | Purpose |
| --- | --- |
| `mta-upstream` | Pinned inherited MTA API baseline |
| `neon-server` | Server-side API in a specific Neon build |
| `neon-client` | Client-side API in a specific Neon build |
| `neon-pair` | Matching Neon server and one real client |
| `neon-multiclient` | Matching Neon server and at least two real clients |

Profiles are data, not hand-written marketing names. Every resolved profile
must record at least:

- upstream engine commit;
- Neon engine commit;
- upstream wiki/data commit;
- API schema version and generated catalogue SHA-256;
- server and client build identifiers when applicable;
- side and runtime topology;
- feature/capability flags;
- loaded native modules and their manifest/ABI identity;
- active project resource revisions; and
- trust mode, which is `local-trusted` for version 1.

`neon-pair` and `neon-multiclient` require matching, compatible client/server
builds. A server-only check cannot be promoted to either profile. Standard MTA
and Neon remain separately queryable even when most symbols are inherited.

## 4. Authorities and conflict handling

There is no single file that is authoritative for existence, semantics, and
behavior simultaneously. The effective model is a provenance-preserving join
of several authorities.

| Source | Authority | Must not claim |
| --- | --- | --- |
| Pinned upstream wiki YAML | Documented names, signatures, descriptions, versions, examples | Presence in the selected compiled build |
| Compiled/runtime registration manifest | Actual symbol presence, side, restriction, capability | Rich lifecycle, ownership, examples, or behavioral correctness unless explicitly described |
| Static C++ registration extraction | Source-level registration candidates and divergence hints | Successful compilation or runtime availability |
| Neon overlay | Intentional additions, extensions, overrides, deprecations, and removals relative to the pinned baseline | Inherited MTA coverage outside the patch |
| Resource/module manifest | Declared exports, events, custom elements, dependencies, ABI, lifecycle, and security contract | Runtime presence when the resource/module is not loaded |
| Runtime probe | Capabilities, loaded resources/modules, exports, and symbols actually observable in one session | General support in other builds or behavioral correctness |
| Focused harness evidence | The exact behavior and topology exercised by that run | Broader stability, parity, or deterministic replay beyond the assertions |

### 4.1 Conflict rules

- Presence in a selected runtime is decided by a successful runtime probe when
  available, otherwise by the compiled manifest, then by source registration
  evidence. Documentation alone never proves presence.
- Semantics come from a reviewed upstream entry plus any explicit Neon or
  project overlay. Source extraction may identify a conflict but must not invent
  missing semantics.
- Behavior is supported only by the narrowest recorded test evidence.
- Conflicts are retained as first-class records. Generators must not silently
  choose one signature, side, version, or lifecycle description.
- Every imported record retains its origin commit and license provenance.
- Builds and CI use pinned local inputs. They must not depend on a live wiki or
  MCP service.

The initial implementation may bootstrap presence by statically extracting
existing registrations. Structured descriptors may later be added near C++
registrations where extraction cannot represent overloads or conditions. The
catalogue and generated editor artefacts remain outside the runtime.

## 5. Evidence taxonomy

Evidence labels describe separate claims and are not silently transitive. For
example, a successful build does not imply an in-game check, and an in-game
single-client check does not imply multiplayer behavior.

| Label | Required evidence |
| --- | --- |
| `documented` | A schema-valid catalogue or manifest entry exists |
| `source-inspected` | Final implementation and registrations were inspected at recorded commits |
| `static-checked` | Named static validators completed successfully against recorded inputs |
| `built` | Named projects compiled successfully in the stated configuration |
| `server-checked` | A real server executed the named scenario and assertions |
| `client-checked` | A real client executed the named scenario and assertions |
| `in-game-checked` | The stated behavior was observed after reaching GTA |
| `multiplayer-checked` | The exact scenario ran with the stated number and roles of real clients |
| `visual-checked` | A named visual assertion or reviewed capture supports the claim |
| `native-trace-checked` | A bounded native trace supports the named causal claim |
| `performance-checked` | A recorded workload met explicitly stated budgets |

Each evidence record includes profile ID, source/build hashes, scenario ID,
wall-clock time, monotonic run identifiers where available, assertions,
artefact hashes, and the exact scope of the claim. `PASS` without one or more
evidence labels and named assertions is not a publishable proof.

The existing wiki editorial meanings of implemented, statically checked,
built, in-game checked, and multiplayer checked remain authoritative. This
taxonomy makes those distinctions machine-readable; it does not weaken them.

## 6. Formats, identity, and versioning

### 6.1 Authoring and generated formats

- Human-authored baseline locks, overlays, resource/module manifests, and
  scenario manifests use YAML encoded as UTF-8. Dependency-free daily tooling
  initially accepts the strict JSON-compatible YAML 1.2 profile only; aliases,
  tags, implicit scalar typing, and duplicate keys are outside that profile.
- Every authored document is normalized to the common data model and validated
  with JSON Schema draft 2020-12 before use.
- Generated catalogues, profiles, results, assertions, and artefact indexes use
  deterministic UTF-8 JSON.
- Streaming diagnostics and event records use bounded JSONL with one complete
  object per line.
- Generated collections have deterministic ordering, normalized path
  separators, stable line endings, and a SHA-256 content hash.
- Markdown is presentation only and is never parsed as the contract source.

### 6.2 Stable identity

Names alone are not globally unique. Stable IDs use a namespace and kind, for
example:

```text
mta:function:createVehicle
neon:function:createRope
mta:event:onPlayerJoin
mta:element:vehicle
resource:inventory:server-export:takeItem
resource:inventory:event:inventoryChanged
module:example:function:exampleCall
```

Renames create aliases or deprecations; they do not reuse an existing stable
ID for unrelated behavior. OOP aliases point to the underlying global API ID
when they are the same operation and use a distinct ID when they are not.

### 6.3 Version rules

- Every schema has a semantic `schemaVersion`.
- A major change removes or changes existing meaning and requires an explicit
  migration.
- A minor change adds optional fields or new entity kinds without changing
  existing meaning.
- A patch change clarifies validation or fixes generation without changing the
  represented contract.
- Consumers reject unknown major versions and ignore unknown optional fields
  within a supported major version.
- Human-authored manifests are validated against the exact schema version they
  declare; unknown authored fields are errors so typos cannot become contracts.
- Engine/wiki/API semantic versions are not substitutes for commit and content
  hashes. Resolved profiles always retain both.

## 7. Unknown, dynamic, and conflicting APIs

The system must prefer visible uncertainty over a plausible hallucination.

| State | Meaning | Tooling behavior |
| --- | --- | --- |
| `verified` | Description and selected-profile presence agree | Generate the normal typed contract |
| `documented-only` | Documented but not present in the selected profile | Exclude from active stubs; retain as an unavailable search result |
| `runtime-only` | Present but lacks a reviewed semantic contract | Expose name, side, restriction, and provenance as `opaque` |
| `opaque` | Dynamic resource/module API has no semantic manifest | Never infer parameters or returns; LuaLS uses `unknown`, not `any` |
| `conflict` | Authorities disagree on side, signature, version, or identity | Emit a blocking diagnostic for generated active contracts |
| `unavailable` | Explicitly absent because of build, side, capability, or removal | Explain the failed availability predicate |

Additional locked policies:

- A Neon overlay targeting no pinned base ID is a schema error unless it is an
  explicit `add` operation.
- A built-in registered symbol without a catalogue classification is reported
  during bootstrap and becomes a release-blocking error once checkpoint 2
  establishes the inventory baseline.
- A documented symbol absent from a selected profile is not a global error; it
  is `documented-only` or `unavailable` for that profile.
- Public resource exports and dynamic events may operate without a semantic
  manifest, but remain `opaque` and cannot receive an `AI-ready` contract label.
- Native modules without an approved manifest expose no guessed signatures.
- Source-discovered dynamic events remain local project observations until a
  manifest or runtime probe confirms them.
- Conflicting entries are never collapsed merely to make LuaLS generation pass.

## 8. Metrics and release gates

Metrics compare the current agent-assisted workflow with the future closed
loop. Raw counts of Neon functions are informational only and are never a
success metric.

### 8.1 Catalogue metrics

- registered built-in symbols classified, by kind and side;
- documented symbols resolved for each canonical profile;
- OOP methods/properties linked to stable API IDs;
- engine events, element types, and enums classified;
- unresolved conflicts and runtime-only opaque symbols;
- public resource/module APIs with reviewed manifests;
- deterministic regeneration and content-hash stability.

### 8.2 Agent workflow metrics

- elapsed time from a fixed prompt to the narrowest verified result;
- number of human interventions and manual VM/game actions;
- invalid API, wrong-side, manifest, ACL, and dependency errors caught before
  runtime;
- edit/restart/test cycles before PASS;
- scenario repeatability across clean runs;
- regressions and security violations detected by negative tests; and
- context volume used to discover the effective contract.

Checkpoint 1 records catalogue baselines. Checkpoint 5 records the current
manual workflow on representative ordinary MTA scenarios. Later checkpoints
must compare against those baselines rather than claim improvement from feature
count alone.

### 8.3 Locked gates

- Generated artefacts must be reproducible byte-for-byte from pinned inputs.
- Every exposed mutation must have success, rejection, timeout, and scope-limit
  tests before it is enabled.
- Active typed contracts contain no unresolved `conflict` records.
- `opaque` is acceptable for dynamic third-party APIs but cannot be promoted to
  an `AI-ready` public contract.
- No evidence claim may exceed the topology and assertions recorded by its run.

## 9. Consequences for later checkpoints

Later work must follow this order:

1. import the pinned MTA baseline and apply the Neon overlay;
2. inventory registrations, OOP, events, enums, and element types;
3. model resource/module contracts and project-local APIs;
4. generate global LuaLS artefacts and static validation;
5. standardize scenarios, assertions, diagnostics, and evidence;
6. add a local read-only supervisor and runtime comparison;
7. expose external CLI/MCP adapters and bounded test mutations;
8. add one-client and then real multi-client proof; and
9. integrate native build, protocol, security, and release gates.

No later checkpoint may narrow the product back to Neon-only functions, use
documentation as proof of runtime presence, or require arbitrary evaluation for
ordinary operation.

## 10. Checkpoint 0 acceptance

Checkpoint 0 is complete when:

- this specification is reviewed as the governing decision record;
- version 1 remains local-only and read-only by default;
- the profile, authority, evidence, format, version, metric, and unknown-API
  policies above have no unresolved architectural alternative;
- no runtime, protocol, wiki, or VM state was changed; and
- downstream implementation starts from the full MTA + Neon + project surface.
