# Checkpoint C black-box evaluation

Date: 2026-08-25  
Evaluator: independent `gpt-5.6-sol` agent, no conversation context  
Workspace: `/tmp/neon-checkpoint-c-release.Tb0tyf`; repository read-only

## Scenario

The evaluator had to discover the local CLI, inspect current and inherited
event/OOP/enum contracts, create a strict client/server resource with OOP
enabled, and use discovered OOP bindings without repository guidance. It then
placed `onClientRender` deliberately in server code beside a custom event,
corrected the error, and proved deterministic catalogue regeneration.

No MTA process, Windows VM, or native build was used because this checkpoint
makes source-inventory and static-validation claims only.

## Discovery results

- Catalogue schema `1.1.0`, data version `1.2.0`, engine `1.7.0`.
- 2,335 entities: 1,842 functions, 241 events, 71 elements, 9 types,
  75 runtime OOP classes, and 97 enums.
- `Ped` exposed 121 methods and 38 properties with exact current/inherited
  client/server bindings, including native targets where no global function
  name exists.
- `element-type` exposed separate current/inherited server definitions and 22
  exact string values.
- `onClientClick` explicitly retained a harmless parameter-name difference
  while remaining `verified`.
- `onClientPlayerDamage` explicitly retained a material parameter-count
  difference and remained `conflict`.

## Wrong-side regression and correction

From the disposable project directory, without `--project` or `--catalogue`:

```sh
/Users/salimtrouve/Documents/GitHub/mtasa-neon/neon check --json
```

The deliberate failure exited 1 with exactly one diagnostic:

```json
{"code":"EVENT_WRONG_SIDE","side":"server","symbol":"onClientRender"}
```

The custom event produced no diagnostic. After removing only the invalid
handler, the same command exited 0 with `status: pass`, zero diagnostics, one
resource, and two checked files.

## Closed gates

```text
harness:                 PASS, 57 tests, 0 skipped, 0 errors
catalogue verify HEAD:   PASS, all source/semantic/runtime drift counters 0
catalogue build A:       PASS, 2,335 symbols
catalogue build B:       PASS, 2,335 symbols
cmp A B:                 PASS
cmp A checked-in:        PASS
cmp B checked-in:        PASS
```

The final checked catalogue was 9,996,620 bytes with SHA-256
`3d58f37cb3bfaf8d18e60c2f9bfefdd1b20ab6eac53650e5eccd2ebc731820d4`.
The evaluator reproduced no defect.

