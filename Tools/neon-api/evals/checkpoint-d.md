# Checkpoint D black-box evaluation

Date: 2026-08-25  
Evaluator: independent `gpt-5.6-sol` agent, no conversation context  
Workspace: `/tmp/mtasa-neon-checkpoint-d-audit.dtTocx`; repository read-only

## Scenario

The evaluator discovered the CLI and schemas, then authored a disposable
gamemode containing a typed inventory resource, a consuming resource, and an
approved native module represented by a harmless placeholder binary. It first
introduced an export-side error, a remote-event mismatch, and a missing ACL
request, corrected the project, resolved the project-local API twice, and
independently checked all manifest and binary hashes.

No MTA process, Windows VM, or native module was loaded. Resource symbols are
supported by matching manifests, `meta.xml`, and inspected Lua only. The module
therefore correctly remains `documented-only`.

## Negative and corrected checks

The deliberately broken strict project produced exactly three byte-stable
errors and no warnings:

```text
RESOURCE_ACL_MISSING
RESOURCE_EVENT_REMOTE_MISMATCH
RESOURCE_EXPORT_WRONG_SIDE
```

After correction, two checks were byte-identical and passed with two resources,
three Lua files, zero errors, and zero warnings.

Two `project resolve` runs were byte-identical and passed the shipped
`neon-project-api` schema. They contained three components and these three
stable symbols:

```text
module:audit-native:function:nativeInventoryHealth   documented-only
resource:inventory:event:inventoryChanged            verified
resource:inventory:server-export:takeItem             verified
```

The 32-byte placeholder binary was content-addressed with SHA-256
`112425f4d14ac2ef61f411208b3aca5af7fed195392829d28bfd73cdb976ab7d`.
Its independently calculated hash matched the resolved record.

## Opaque contract gate

A separate unmanifested legacy resource passed permissive mode with exactly
two warnings and resolved one opaque component plus two opaque symbols. Both
symbols set `signatureKnown` to `false` and omitted `parameters` and `returns`,
so an unknown signature cannot be interpreted as verified zero arity. Strict
mode promoted the same `RESOURCE_EVENT_OPAQUE` and `RESOURCE_EXPORT_OPAQUE`
diagnostics to errors.

## Closed gates

```text
harness:                 PASS, 79 tests, 0 skipped, 0 errors
catalogue verify HEAD:   PASS, all drift/divergence counters 0
check result schemas:    PASS, including deliberate failures
resolve schemas:         PASS
resolve byte stability:  PASS
repository read-only:    PASS
```

The evaluator reproduced no defect, false positive, false negative,
nondeterminism, internal error, or runtime-evidence overclaim.
