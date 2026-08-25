# Checkpoint F black-box evaluation

- Date: 2026-08-25
- Base commit: `8b49d7541b2cc8749cb13cd3c11a5bb6db7776fa`
- Final verdict: PASS
- Auditor: fresh `gpt-5.6-sol` agent with no inherited conversation context
- Audit mode: read-only repository inspection and disposable external workspaces

Checkpoint F standardizes bounded scenarios, assertions, diagnostics, artifacts,
and evidence without enabling runtime mutation. The final audit independently
discovered the CLI, built its own projects and control documents, and attacked
the saved-run verifier rather than relying only on the repository tests.

## Final gates

```text
./neon harness --json
PASS: 143 tests, 0 errors, 0 warnings, 0 skipped

./neon catalogue verify --source-ref HEAD --json
PASS: no source, semantic, registration, event, or runtime-inventory drift

./neon check --json
PASS

./neon context verify --json
PASS

git diff --check
PASS
```

The bundled `static-smoke` scenario passed. Repeated independent runs produced
the same run ID, artifact index, and artifact payload bytes. Every contract,
size, SHA-256, JSONL sequence, input snapshot, step result, assertion outcome,
and evidence reference was verified.

## Hostile acceptance cases

The final implementation rejected all of the following:

- timeout or reserved-runtime failures rewritten as PASS after removing both
  root and child diagnostics, rewriting summaries, reindexing the step,
  updating evidence, and recomputing the run ID;
- a failed equality assertion rewritten as PASS;
- a failed assertion retained as failed while its required root diagnostic was
  removed and the overall result was rewritten as PASS;
- an absent file rewritten to a passing `file-exists` result while the file
  remained absent, including coherently updated events, index, and evidence;
- a rehashed and reindexed step artifact contradicting `result.json`;
- raw artifact, result, evidence, and unindexed-file tampering;
- output traversal, outside-workspace output, occupied output, and output
  symlinks;
- scenario and assertion symlinks, including `/var` and `/private/var` path
  aliases on macOS;
- nested-project default context output overlapping the scenario run output;
- profile mismatches, unknown actions, shell metacharacters, and unsupported
  schema major versions.

JSON assertion equality keeps booleans distinct from numbers, so `false` does
not equal `0`. Genuine reserved-action, timeout, invalid-input, assertion, and
expected-negative failures remain integrity-verifiable; verification success
means the saved failure is internally authentic, not that its scenario passed.
Only `static-checked` evidence can be granted by this checkpoint.

## Defects found and closed during independent review

Three successive fresh audits found and drove fixes for:

- symlink controls being resolved before validation;
- nested project default-output collision;
- assertion and step artifacts not being semantically recomputed;
- infrastructure diagnostics being removable from saved evidence;
- Python boolean/number equality leaking into JSON assertions;
- `file-exists` trusting a recorded boolean;
- early failure paths omitting their status-mismatch diagnostic; and
- inconsistent `/var` versus `/private/var` handling in saved-run verification.

Every defect received a closed regression test before the final clean audit.
No arbitrary command execution, user-file overwrite, workspace escape, false
evidence label, or remaining release blocker was found.

## Frozen implementation hashes

```text
neon.py                              4fba4e0441463e41a2aa314dbe4816811569a8c8b75705639600b5fdd6d45bb4
neonlib/scenario.py                  ab814808162fd02a60e3f7682c1c850d6d3564bb252b89c0a1c7b87e849477af
tests/test_neon_api.py               57e470b08614b88581900a71ea1d113bd9b8b97aba5220436f08b26986e8884c
schemas/neon-test.schema.json        8d3304ec4d30c60fa8d3e7cc0a45de6e509f3a2a7678a1a38545ccfcd667c9b1
schemas/neon-evidence.schema.json    959f6543781da9748ad6e73b80febfd67f68a1248efc905171821866e88f3295
schemas/neon-test-result.schema.json 8b67149138f00d450370e4ad48f8c62b5e2706fa5e95a6caac4eca49f9870c34
```
