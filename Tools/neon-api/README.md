# MTA Neon agent contracts

This directory implements the runtime-free contract checkpoint for the full
effective MTA API: inherited upstream MTA registrations, the Neon overlay, and
project resource requirements. It intentionally does not start MTA or contact a
remote service.

## Commands

Run commands from the repository root:

```sh
./neon check --json
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
missing MTA APIs.

## Catalogue generation

The checked-in catalogue is generated from Git objects, not uncommitted files:

```sh
./neon catalogue build \
  --neon-ref HEAD \
  --upstream-ref upstream/master \
  --engine-version 1.7.0 \
  --wiki-revision 39e80f8108fef8de0dfdf61876daf702d583243e
```

Function signatures that have not yet been joined with reviewed wiki data are
represented as `unknown`. The generator never invents parameter or return
types. `sources.upstreamWiki.imported` remains `false` until that semantic join
is implemented and reviewed.

`catalogue verify` compares active client/server entries against source
registrations. With `--source-ref HEAD` it is reproducible and ignores unrelated
working-tree changes; without that option it deliberately audits the working
tree.

## Contract files

- `schemas/neon-api.schema.json`: canonical API catalogue.
- `schemas/neon-project.schema.json`: local project and resource selection.
- `schemas/neon-test.schema.json`: bounded scenario definition.
- `schemas/neon-assertion.schema.json`: assertion contract.
- `schemas/neon-artifact.schema.json`: content-addressed artefact record.
- `schemas/neon-check-result.schema.json`: stable `check --json` result.
- `generated/`: deterministic LuaLS/LuaCATS libraries and hashes.

All JSON readers reject duplicate keys, oversized documents, unknown schema
fields, unsupported schema majors, absolute paths, traversal, and symlink
escapes. XML resource metadata rejects DTD and entity declarations.
