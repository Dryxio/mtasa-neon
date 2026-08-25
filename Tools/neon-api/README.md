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
./neon api stats --json
./neon catalogue verify --source-ref HEAD --json
./neon generate luals --json
./neon harness --json
python3 -m unittest discover -s Tools/neon-api/tests -v
```

On Windows, use `neon.cmd` instead of `./neon`.
Python 3.10 or newer is required; no third-party Python package is needed.

`neon check` validates `neon.project.json`, its selected catalogue, engine
compatibility, declared API requirements, resource directories, `meta.xml`
scripts, dependencies, and known wrong-side Lua calls. Set `unknownApis` to
`error` only for closed projects where every global callable is expected to be
catalogued; the default avoids misclassifying resource-local functions as
missing MTA APIs. When `--project` is omitted, the command uses
`./neon.project.json` from the current workspace and falls back to the repository
project only when the current directory has none.

## Catalogue generation

The checked-in catalogue is generated from Git objects, not uncommitted files:

```sh
./neon catalogue build \
  --neon-ref HEAD \
  --upstream-ref upstream/master \
  --engine-version 1.7.0 \
  --wiki-revision 39e80f8108fef8de0dfdf61876daf702d583243e
```

`snapshots/api-semantics.json` contains normalized data from the pinned official
MTA wiki and the pinned Neon wiki. Functions with matching documentation and
registrations are `verified`; documentation without a source registration is
`documented-only`; registrations without a semantic contract are
`runtime-only`; explicit side contradictions remain `conflict`. Evidence and
provenance are kept on every entry, so these labels do not imply an in-game
test. Unknown signatures remain `any` in LuaLS rather than being invented.

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

`catalogue verify` compares active client/server entries against source
registrations and confirms that both wiki revisions and the semantic digest
match the checked-in snapshot. With `--source-ref HEAD` it is reproducible and
ignores unrelated working-tree changes; without that option it deliberately
audits the working tree.

## Contract files

- `schemas/neon-api.schema.json`: canonical API catalogue.
- `schemas/neon-semantic-snapshot.schema.json`: strict normalized import.
- `schemas/neon-project.schema.json`: local project and resource selection.
- `schemas/neon-test.schema.json`: bounded scenario definition.
- `schemas/neon-assertion.schema.json`: assertion contract.
- `schemas/neon-artifact.schema.json`: content-addressed artefact record.
- `schemas/neon-check-result.schema.json`: stable `check --json` result.
- `generated/`: deterministic LuaLS/LuaCATS libraries and hashes.

The catalogue includes functions, events, elements, primitive types, side
contracts, parameters, return values, defaults, OOP metadata, versions,
descriptions, evidence, and source/license provenance. `api search` supports
tokenized discovery and deterministic filters; `api get` returns the complete
machine-readable entry.

All JSON readers reject duplicate keys, oversized documents, unknown schema
fields, unsupported schema majors, absolute paths, traversal, and symlink
escapes. XML resource metadata rejects DTD and entity declarations.
