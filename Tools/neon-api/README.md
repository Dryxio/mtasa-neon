# MTA Neon agent contracts

This directory implements the local agent CLI, contracts, and semantic
catalogue for the full effective API: upstream MTA plus the Neon overlay. It
joins inspected Lua registrations with pinned MTA YAML documentation and pinned
Neon API data. Daily commands do not contact a remote service; mutations require
an explicit short-lived local supervisor capability.

## Commands

Run commands from the repository root:

```sh
./neon check --json
./neon api search "draw text" --side client --kind function --json
./neon api get createVehicle --profile neon-pair --json
./neon api get Ped --kind class --profile neon-pair --json
./neon api get element-type --kind enum --side server --json
./neon api stats --json
./neon project resolve --json
./neon generate project --json
./neon context verify --json
./neon scenario run Tools/neon-api/scenarios/static-smoke.json \
  --assertion Tools/neon-api/scenarios/assertions/static-search.json \
  --assertion Tools/neon-api/scenarios/assertions/static-check.json \
  --workspace . --output .neon-runs/static-smoke --json
./neon scenario verify .neon-runs/static-smoke --workspace . --json
./neon supervisor start --workspace . --snapshot .neon-runtime/runtime-snapshot.json --json
./neon supervisor status .neon-sessions/session-ID/session.json --workspace . --json
./neon runtime compare .neon-sessions/session-ID/session.json --workspace . --json
./neon supervisor stop .neon-sessions/session-ID/session.json --workspace . --json
./neon supervisor start --workspace . --enable resource.lifecycle \
  --enable scenario.execute --server-root /approved/mta-server --json
./neon resource restart .neon-sessions/session-ID/session.json inventory \
  --workspace . --json
./neon scenario execute .neon-sessions/session-ID/session.json scenario.json \
  --assertion assertion.json --workspace . --output .neon-runs/runtime --json
./neon runtime probe install --server-root /approved/mta-server --json
./neon supervisor start --workspace . --enable resource.lifecycle \
  --enable client.launch --server-root /approved/mta-server \
  --client-root /approved/mta-client --connect-port 22003 --json
./neon resource start .neon-sessions/session-ID/session.json neon-agent-probe \
  --workspace . --json
./neon client launch .neon-sessions/session-ID/session.json client-1 \
  --workspace . --json
./neon runtime prove .neon-sessions/session-ID/session.json \
  --workspace . --timeout-ms 120000 --json
./neon catalogue verify --source-ref HEAD --json
./neon generate luals --json
./neon harness --json
python3 -m unittest discover -s Tools/neon-api/tests -v
```

On Windows, use `neon.cmd` instead of `./neon`.
Python 3.10 or newer is required; no third-party Python package is needed.

`neon check` validates `neon.project.json`, its selected catalogue, engine
compatibility, declared API requirements, resource directories, `meta.xml`
scripts, dependencies, known wrong-side Lua calls, and literal built-in events
used on the wrong side. Static `Class.method(...)` calls and instance calls on
values assigned from a known `Class.create(...)` are checked against the same
profile, side, inheritance, and conflict contracts. Custom resource events remain valid. Set `unknownApis` to
`error` only for closed projects where every global callable is expected to be
catalogued; the default avoids misclassifying resource-local functions as
missing MTA APIs. When `--project` is omitted, the command uses
`./neon.project.json` from the current workspace and falls back to the repository
project only when the current directory has none.
Catalogue entries in `conflict` are never treated as active typed APIs:
requirements and literal Lua/event use emit `API_CONFLICT` or
`EVENT_CONFLICT`, and LuaLS omits the unsafe global signature while the API
index keeps the conflict discoverable. The conflict remains blocked when it is
referenced from the opposite side; any other catalogued symbol excluded by the
selected profile emits `API_UNAVAILABLE` or `EVENT_UNAVAILABLE` instead of
falling through to the permissive unknown-API policy.

## Project-local resource and module APIs

Checkpoint D adds opt-in contracts for APIs that do not belong to the engine:
resource exports, custom events/elements, native module functions, dependencies,
lifecycle, ACL requirements, and capabilities. A project selects a contract
with the resource or module `manifest` field. `neon check` then compares the
contract with `meta.xml` and Lua definitions; `neon project resolve --json`
emits deterministic stable IDs such as
`resource:inventory:server-export:takeItem` and
`module:example:function:exampleCall`.

Manifests use the dependency-free Neon YAML profile: a `.yaml` or `.yml` file
must contain strict JSON, which is a valid YAML 1.2 document. This deliberately
excludes YAML aliases, implicit scalar typing, tags, and duplicate keys from
daily agent tooling. Maintainers can therefore author YAML-compatible files
without adding a parser dependency or creating loader-dependent meanings.

```json
{
  "schemaVersion": "1.0.0",
  "kind": "resource",
  "name": "inventory",
  "version": "1.0.0",
  "lifecycle": {"start": "automatic", "stop": "clean", "reloadSafe": true, "persistentState": "none"},
  "dependencies": [],
  "exports": [{
    "name": "takeItem", "side": "server",
    "parameters": [{"name": "item", "type": "string", "optional": false, "description": "Item identifier."}],
    "returns": [{"name": "removed", "type": "boolean", "description": "Whether removal succeeded."}],
    "description": "Remove one item.", "http": false, "restricted": false
  }],
  "events": [],
  "elements": [],
  "acl": [],
  "capabilities": []
}
```

A native module uses the same strict component schema with `kind: "module"`.
Its callable surface is declared in `exports`; the project separately names
the manifest and binary so Neon can hash both without claiming the binary was
loaded:

```json
{
  "schemaVersion": "1.0.0",
  "kind": "module",
  "name": "native-status",
  "version": "1.0.0",
  "lifecycle": {"start": "automatic", "stop": "clean", "reloadSafe": false, "persistentState": "none"},
  "dependencies": [],
  "exports": [{
    "name": "getNativeStatus", "side": "server", "parameters": [],
    "returns": [{"name": "ready", "type": "boolean", "description": "Whether the module is ready."}],
    "description": "Read native module readiness.", "http": false, "restricted": false
  }],
  "events": [], "elements": [], "acl": [], "capabilities": []
}
```

```json
{"name":"native-status","path":"modules/native-status","manifest":"neon.module.yaml","binary":"native-status.dll"}
```

Without an approved manifest, public exports and literal `addEvent` definitions
remain visible as `opaque`: their parameters and returns are never guessed.
Resolved opaque callables set `signatureKnown` to `false` and omit parameter and
return arrays, so an unknown signature cannot be mistaken for verified zero
arity.
This is a warning under the default `unknownComponents: "allow-opaque"` policy
and an error under `unknownComponents: "error"`. An unmanifested native module
exposes no functions at all. Module records remain `documented-only` until a
later runtime checkpoint proves that the expected ABI was actually loaded.

## Catalogue generation

The checked-in catalogue is generated from Git objects, not uncommitted files:

```sh
./neon catalogue build \
  --neon-ref HEAD \
  --upstream-ref upstream/master \
  --engine-version 1.7.0 \
  --wiki-revision 39e80f8108fef8de0dfdf61876daf702d583243e
```

Engine provenance records the newest commit at the selected ref that actually
changed an inventoried C++ path. Tooling-only commits therefore do not create a
false catalogue revision drift; the archived source content still comes from
the exact requested ref and remains protected by its registration digest.

`snapshots/api-semantics.json` contains normalized data from the pinned official
MTA wiki and the pinned Neon wiki. Functions with matching documentation and
registrations are `verified`; documentation without a source registration is
`documented-only`; registrations without a semantic contract are
`runtime-only`; explicit side contradictions remain `conflict`. Evidence and
provenance are kept on every entry, so these labels do not imply an in-game
test. Unknown signatures remain `any` in LuaLS rather than being invented.

The same pass inventories literal engine event registrations, OOP classes,
methods and properties (including exact client/server bindings), enum string
maps, and the server element-type map. Current Neon definitions and pinned
upstream definitions stay separate, so additions, removals, side changes,
parent changes, and binding changes remain machine-visible.
These additive entity kinds use catalogue schema `1.1.0` and catalogue data
version `1.2.0`; existing project and result contracts remain on schema `1.0.0`.

Refreshing the snapshot is an explicit maintainer operation. It requires Node
22 or newer and local checkouts of both documentation repositories:

```sh
cd Tools/neon-api/importer
npm ci --ignore-scripts
cd ../../..
./neon catalogue import \
  --upstream-wiki /path/to/wiki.multitheftauto.com \
  --upstream-revision 39e80f8108fef8de0dfdf61876daf702d583243e \
  --neon-wiki /path/to/wiki.mtasa-neon.com \
  --neon-revision 4775aae5fa28346837b3735f04e26705cf7660b5 \
  --json
./neon catalogue build --neon-ref HEAD --upstream-ref upstream/master --json
./neon generate luals --json
```

The importer reads Git objects at the requested commits, never uncommitted wiki
files. It handles YAML aliases without recursively expanding malformed
self-merges, bundles the two Neon TypeScript data files in an isolated temporary
directory, evaluates the data in a sandbox without Node/process/filesystem
globals and with a timeout, normalizes all entities, and records a SHA-256 digest. Node packages
are only needed to refresh the snapshot; `check`, API discovery, catalogue
verification, LuaLS generation, and the Python harness remain dependency-free.

## Generated agent context

`neon generate project --json` first runs the complete static project check and
refuses to generate active contracts on any error. On success it writes `.neon`
beside the selected project (or `--output`) with:

- `agent-context.json`, containing the profile, engine/catalogue identities,
  content-addressed project files (including Lua, manifests, module binaries,
  and `file`/`map`/`config`/`html` payloads declared by `meta.xml`), APIs already
  used by the code, and safe discovery/validation guidance;
- `api-index.json`, a compact, deterministic index of the complete effective
  MTA + Neon surface filtered to the selected profile;
- `project-api.json`, the exact resolved resource/module contracts;
- separate client/server LuaLS libraries combining the shared/global API with
  exact side-specific OOP classes/methods/properties plus project module
  functions, exports, custom events, and elements;
- a side-specific `.luarc.json`; and
- `artifacts.json`, which hashes every generated payload artefact (the index
  itself is excluded to avoid a recursive hash).

The pack contains no absolute workspace path or timestamp. Repeated generation
from identical inputs is byte-for-byte stable. Runtime-only global functions
and opaque project callables use LuaLS `unknown`; they never receive a guessed
zero-argument or `nil`-return contract. `--profile mta-upstream` and the Neon
client/server profiles also filter generated global LuaLS definitions rather
than leaking APIs from the wrong engine overlay or side.

`neon context verify --json` is the read-only freshness gate. It validates the
schemas, checks every recorded payload hash without following paths outside the
pack, regenerates in an isolated temporary directory, and compares the complete
artifact index. A source, manifest, catalogue, generator, or generated-file
change therefore yields a machine-readable stale/tamper diagnostic. Unindexed
files are rejected as well; generation never deletes or silently adopts an
unexpected user-owned file already present in its output directory.

`catalogue verify` compares active client/server functions, events, OOP
bindings, classes, properties, and enums against source registrations and
confirms that both wiki revisions and the semantic digest match the checked-in
snapshot. With `--source-ref HEAD` it is reproducible and
ignores unrelated working-tree changes; without that option it deliberately
audits the working tree.

## Closed scenarios, assertions, and evidence

`neon scenario run` executes a bounded, sequential scenario inside one approved
workspace. The runtime-free command enables only `check`, `project.resolve`,
`generate.project`, `context.verify`, and `api.search`. Each action runs without
a shell in a child process under its declared timeout. Build, client launch,
and all runtime actions remain fail-closed in this command. Checkpoint H adds
the separate `scenario execute` command for explicitly authorized resource
lifecycle steps; it does not silently upgrade `scenario run`.

A scenario names every assertion document explicitly. Assertions can select a
step result with `step:id#/json/pointer`, select the last step with a plain JSON
pointer, or check a workspace-relative regular file. Supported predicates are
`equals`, `not-equals`, `truthy`, `falsy`, `contains`, `file-exists`, and
`diagnostic-absent`. `expectedStatus: "fail"` supports deliberate negative
checks, but infrastructure failures such as timeouts, invalid inputs, profile
mismatches, unavailable actions, malformed output, or exit/result
contradictions can never be converted into a passing scenario.

When `--output` names a new or empty directory inside the workspace, the runner
writes the exact scenario and assertion inputs, canonical per-step JSON,
deterministic JSONL events, a validated artifact index, `evidence.json`, and
`result.json`. Payloads are content addressed, repeated static inputs retain the
same run identity, and the evidence records the actual wall-clock observation
and duration separately. Output paths cannot traverse or follow symlinks,
overlap generated project context, or adopt an existing user-owned directory.
Scenario and assertion documents must also be regular files inside the approved
workspace. Purely static runs grant only `static-checked`. A run containing a
runtime action grants no label merely because its bounded command was submitted.

`neon scenario verify` reopens a saved run without executing it. It validates
all contracts, recomputes every size and SHA-256, reproduces the run identity,
cross-checks result/evidence summaries, and rejects altered, unindexed, escaped,
or symbolic-link files.

## Local runtime supervisor

Without an `--enable` option, `neon supervisor start` explicitly opens an
expiring read-only local observation session.
It starts a dependency-free supervisor on an ephemeral `127.0.0.1` port, writes
its token only to a mode-`0600` local session file, and grants exactly the five
read capabilities fixed by the architecture. The public command result contains
the workspace-relative session path but never the token. Each start creates a
new random child under `.neon-sessions`; it never adopts an existing session.
The token is never transmitted: a fresh server challenge and client nonce bind
HMAC-authenticated requests and responses, so a redirected loopback connection
cannot forge a PASS, recover the bearer, or replay a captured command.

The session pins the project, catalogue, and resolved transitive component
contract by SHA-256 and fixes one workspace-relative runtime snapshot path.
`neon runtime compare SESSION` asks the supervisor to reopen that one snapshot
without following symbolic links and compare its observations with the selected
MTA/Neon profile and freshly resolved project contracts. It checks:

- topology, side, engine version, and matching build IDs;
- registered functions, restrictions, events, and remote-trigger policy;
- running project resources and their declared exports;
- native module version, ABI, manifest, binary identity, and exports;
- duplicate, missing, unexpected, wrong-side, and uncatalogued entries; and
- session, profile, project, catalogue, timestamp, and snapshot identity.

A `complete` observation must use the exact single-server/single-client topology
for `neon-pair` and may prove that an expected entry is absent. A `partial`
observation produces visible warnings and never turns an unobserved function,
event, resource, module, or export into an absence claim. Runtime strings and
diagnostic counts are bounded contract data, not instructions. Project,
component, or catalogue drift, symlink replacement and races, traversal,
malformed or unauthorized requests, stale sessions, and non-loopback transports
fail closed. Session state and audit writes stay anchored to the directory that
was approved at startup, even if its parent path is later replaced.
POSIX uses directory-relative descriptors; Windows uses native handle-relative
opens and rejects reparse points. Platforms providing neither safe primitive
reject supervisor startup rather than falling back to pathname check/reopen.
Transitive validation never reopens live component paths: manifests, meta
files, scripts, assets, and module binaries are copied from anchored reads into
a private ephemeral contract tree before the general project resolver runs.
Positive observations of undeclared resources, modules, or exports remain
errors even in a partial inventory, as do uncatalogued functions and events;
partial only weakens claims about absence. Resource and module exports are
compared against the side represented by each observation. Resource sides come
from their script declarations, so even a client resource with no public API
must appear in a complete client inventory.

`neon runtime compare` does not create the runtime snapshot. A developer or
bounded probe supplies it at the configured path with the session ID and pinned
hashes. Consequently comparison results declare `scope: "observation-only"`
and always grant zero evidence labels; reading a submitted snapshot is not
`server-checked`, `client-checked`, or `in-game-checked` proof.

### Explicitly authorized resource operations

Checkpoint H adds two opt-in capabilities. They must be named at session start;
they are absent by default and cannot be added to an active session:

- `resource.lifecycle` permits only `start`, `stop`, and `restart` for a resource
  declared by the pinned `neon.project.json`. It also requires an explicit
  `--server-root` containing an approved MTA server executable name.
- `scenario.execute` permits `scenario execute` to dispatch only those same
  resource operations. It does not enable build, shell, arbitrary console,
  nested scenario, or client-launch actions. A runtime scenario needs both
  capabilities to change resource lifecycle.

The supervisor launches exactly that executable with no caller-controlled
arguments, pins its SHA-256 in the immutable session, and submits strict ASCII
MTA console lines through a private guardian channel and pipe. The guardian
terminates MTA if the supervisor disappears abruptly; the supervisor retains
an identity-bound process handle as the inverse fallback if the guardian dies.
Normal stop and expiration also terminate the exact child. Resource names are
both contract-declared and limited to a
bounded safe character set, so they cannot become console syntax. The approved
server directory is a local trust boundary: the user selecting it is approving
the executable and adjacent MTA runtime files in that directory.

The pipe is unbuffered and non-blocking. Neon attempts one write only; it never
retries a lifecycle command. Backpressure proves that zero bytes were accepted
and returns a visible failure. A theoretically partial write closes the session
and reports an unknown outcome because a truncated console line must not be
continued by a later request.

A successful lifecycle result means only `scope: "command-submitted"`: Neon
wrote the bounded line to the approved server input. It never claims MTA
processed it or that the resource reached a requested state, and it grants zero
evidence labels. Runtime state must be established independently through a
fresh observation. `scenario execute` preserves the same truth boundary, so a
successful runtime scenario also has an empty evidence-label list while its
assertions and content-addressed artifacts remain verifiable offline.

Every runtime step applies its declared timeout to the authenticated request.
There is no automatic retry. If transport fails after any part of a mutation
request may have been sent, Neon reports `MUTATION_OUTCOME_UNKNOWN`, revokes the
session, and tells the caller not to retry automatically. This prevents a lost
response from turning a non-idempotent restart into an unnoticed duplicate.

### Authenticated one-client and multi-client proof

Checkpoint I adds Windows-only `client.launch` for `neon-pair` and `neon-multiclient`
profiles. It is opt-in, requires `resource.lifecycle`, and requires exact local
server and client roots. The CLI accepts no executable name, remote host, shell
fragment, or arbitrary client argument. It pins the approved client executable
hash, launches `client-1` with only the loopback MTA URI, and launches the
second role with only `-cl2` plus that URI. A role can be launched once per
session. The executable path is hashed before launch and again immediately
after process creation; Windows creates it suspended, attaches it to a
kill-on-close Job Object, and resumes it only after both checks pass. This
prevents child-process escape, but it is not cryptographic image attestation
against an administrator concurrently replacing files inside the approved
client root. Any ambiguous post-send failure revokes the session and is never
retried automatically.

`runtime probe install` copies three byte-verified bundled resource files to
`neon-agent-probe`. Every resource-path traversal, read, delete, and atomic
write is relative to anchored directory handles and rejects symlinks and
Windows reparse points. At session start, the supervisor re-verifies those files,
writes a private expiring challenge bound to the session/project/catalogue,
and removes any stale report. Start the probe resource before launching the
clients. Its client script can report only after the real MTA client has loaded
the resource; the server script counts distinct remote client elements, derives
their versions from `getPlayerVersion` rather than trusting event payloads, and
writes an HMAC-authenticated report atomically. The report is invalidated when
a contributing player leaves. The secret is never included in the public
session or audit log.

Launching a process still grants zero evidence labels. `runtime prove` grants
`server-checked`, `client-checked`, and `in-game-checked` only after the signed
report, exact topology, matching engine versions and client build IDs, live
supervisor-launched roles, a live supervisor-owned server, a report heartbeat
no older than ten seconds, the session time window, and pinned runtime contracts
all pass. `neon-multiclient` additionally requires exactly two distinct clients and
grants `multiplayer-checked`. A submitted
snapshot used by `runtime compare` remains observation-only and can never gain
those labels.

This is development-harness evidence, not anti-cheat or hostile-server
attestation. It protects the CLI from stale, edited, cross-session, and
wrong-topology report files and proves that the bundled probe observed real
remote player elements while the exact supervisor-launched client roles were
still alive on the explicitly approved local server. MTA does not expose a
cryptographic mapping between a remote player element and its originating OS
process, so these two observations are correlated by the isolated harness but
are not anti-cheat process attestation. A server
administrator, a native module, or another resource with permission to read or
replace the probe's private files is inside that local trust boundary and could
forge the observation. Run proof sessions on an isolated development server;
do not treat these labels as evidence against adversarial code already holding
server filesystem or administrator access. On Windows the private file inherits
the server directory's ACL, so that isolated directory must itself be restricted
to the developer identity running the harness.

`neon supervisor stop SESSION` closes the loopback listener, removes the token
from the retained session record, and makes further requests fail. Expiration
does the same using a monotonic deadline. If the daemon dies abruptly, the next
request reconciles the retained record to a token-free closed or expired state.
`audit.jsonl` records start, reads,
comparisons, rejected requests, stop, and expiration by session ID and result
hash without recording the token. It is capped at 256 KiB so unauthenticated
request floods cannot consume unbounded disk; `audit-truncated.json` makes any
records omitted after that cap explicit.

## Contract files

- `schemas/neon-api.schema.json`: canonical API catalogue.
- `schemas/neon-semantic-snapshot.schema.json`: strict normalized import.
- `schemas/neon-project.schema.json`: local project and resource selection.
- `schemas/neon-component.schema.json`: resource/module semantic manifest.
- `schemas/neon-project-api.schema.json`: resolved project-local API result.
- `schemas/neon-agent-context.schema.json`: compact project context pack.
- `schemas/neon-api-index.schema.json`: profile-filtered discovery index.
- `schemas/neon-test.schema.json`: bounded scenario definition.
- `schemas/neon-assertion.schema.json`: assertion contract.
- `schemas/neon-artifact.schema.json`: content-addressed artefact record.
- `schemas/neon-artifact-index.schema.json`: closed index of run artefacts.
- `schemas/neon-evidence.schema.json`: scoped test evidence and exact assertion outcomes.
- `schemas/neon-test-result.schema.json`: stable scenario step/assertion result.
- `schemas/neon-scenario-verify-result.schema.json`: saved-run integrity result.
- `schemas/neon-runtime-snapshot.schema.json`: bounded multi-runtime observation.
- `schemas/neon-runtime-compare-result.schema.json`: read-only divergence result.
- `schemas/neon-probe-config.schema.json`: private expiring probe challenge.
- `schemas/neon-probe-report.schema.json`: authenticated real-client report.
- `schemas/neon-proof-result.schema.json`: bounded runtime proof and evidence.
- `schemas/neon-probe-install-result.schema.json`: verified probe installation result.
- `schemas/neon-supervisor-session.schema.json`: local expiring session record.
- `schemas/neon-supervisor-result.schema.json`: supervisor lifecycle result.
- `schemas/neon-mutation-result.schema.json`: bounded operation and authorization result.
- `schemas/neon-check-result.schema.json`: stable `check --json` result.
- `generated/`: deterministic LuaLS/LuaCATS libraries and hashes.

The catalogue includes functions, registered events, elements, primitive
types, runtime OOP classes, enum value maps, side contracts, parameters,
return values, defaults, OOP metadata, versions,
descriptions, evidence, and source/license provenance. `api search` supports
intent-oriented local discovery and deterministic filters; `api get` returns
the complete machine-readable entry. Search splits camelCase names, indexes
OOP bindings, categories, descriptions, signatures, parameters, returns,
examples, enum values, and source categories, and applies a bounded MTA-aware
synonym vocabulary plus conservative plural, prefix, and single-typo matching.
This makes sparse runtime-only functions discoverable from tasks such as
`npc pathfinding` without inventing any API semantics. The generated compact
index also carries deterministic keywords for agents that inspect the context
pack without invoking the CLI. Search ranking is a navigation aid only: agents
must still use `api get` and respect the selected profile, side, state, and
evidence before calling a result.
Search returns compact ranked summaries by default to protect the agent context
window; `--full` is available for tooling that truly needs every matching
contract, while the recommended workflow is a compact search followed by one
exact `api get`.

All JSON readers reject duplicate keys, oversized documents, unknown schema
fields, unsupported schema majors, absolute paths, traversal, and symlink
escapes. XML resource metadata rejects DTD and entity declarations.
