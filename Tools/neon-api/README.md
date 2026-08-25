# MTA Neon agent contracts

This directory implements the runtime-free contract and semantic catalogue for
the full effective API: upstream MTA plus the Neon overlay. It joins inspected
Lua registrations with pinned MTA YAML documentation and pinned Neon API data.
Daily commands are local, deterministic, and do not contact a remote service.

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
used on the wrong side. Custom resource events remain valid. Set `unknownApis` to
`error` only for closed projects where every global callable is expected to be
catalogued; the default avoids misclassifying resource-local functions as
missing MTA APIs. When `--project` is omitted, the command uses
`./neon.project.json` from the current workspace and falls back to the repository
project only when the current directory has none.

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

`catalogue verify` compares active client/server functions, events, OOP
bindings, classes, properties, and enums against source registrations and
confirms that both wiki revisions and the semantic digest match the checked-in
snapshot. With `--source-ref HEAD` it is reproducible and
ignores unrelated working-tree changes; without that option it deliberately
audits the working tree.

## Contract files

- `schemas/neon-api.schema.json`: canonical API catalogue.
- `schemas/neon-semantic-snapshot.schema.json`: strict normalized import.
- `schemas/neon-project.schema.json`: local project and resource selection.
- `schemas/neon-component.schema.json`: resource/module semantic manifest.
- `schemas/neon-project-api.schema.json`: resolved project-local API result.
- `schemas/neon-test.schema.json`: bounded scenario definition.
- `schemas/neon-assertion.schema.json`: assertion contract.
- `schemas/neon-artifact.schema.json`: content-addressed artefact record.
- `schemas/neon-check-result.schema.json`: stable `check --json` result.
- `generated/`: deterministic LuaLS/LuaCATS libraries and hashes.

The catalogue includes functions, registered events, elements, primitive
types, runtime OOP classes, enum value maps, side contracts, parameters,
return values, defaults, OOP metadata, versions,
descriptions, evidence, and source/license provenance. `api search` supports
tokenized discovery and deterministic filters; `api get` returns the complete
machine-readable entry.

All JSON readers reject duplicate keys, oversized documents, unknown schema
fields, unsupported schema majors, absolute paths, traversal, and symlink
escapes. XML resource metadata rejects DTD and entity declarations.
