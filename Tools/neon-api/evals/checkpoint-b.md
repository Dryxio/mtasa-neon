# Checkpoint B black-box evaluation

Date: 2026-08-25  
Evaluator: independent `gpt-5.6-sol` agent, no conversation context  
Workspace policy: disposable `/tmp` project; repository read-only

## Scenario

The evaluator had to discover the repository tooling, query MTA and Neon API
contracts, create a client/server resource, use one Neon-only function, render a
client HUD, and validate the project with `unknownApis: "error"`. It also had to
place `dxDrawText` deliberately in server code, observe the structured
wrong-side failure, remove it, and obtain a passing JSON result.

The resource used anonymous callback syntax and a parenthesized Lua keyword:

```lua
addEventHandler("onClientRender", root, function()
    if (true) then
        dxDrawText("NEON", 0, 0)
    end
end)
```

## First run and correction

The first independent run successfully discovered and used `api search`,
`api get`, and `check`. Its negative side test produced `API_WRONG_SIDE` with
the exact symbol, side, file, and line, and its corrected resource passed.

It also exposed a real false positive: strict unknown-API scanning treated the
anonymous `function()` keyword as a global call. The scanner now explicitly
excludes Lua keywords, and the closed harness contains the exact regression.

## Fresh final run

A fresh independent `gpt-5.6-sol` agent repeated the scenario in another
disposable workspace. It queried provenance, state, side, parameters, and
returns for MTA functions, a documented event, an element, and the Neon-only
`getWorldSeaBedOuterBoundary` function.

The deliberate invalid server call returned exit code 1:

```json
{"code":"API_WRONG_SIDE","line":4,"message":"dxDrawText is unavailable on server","path":"resources/regression/server.lua","severity":"error","side":"server","symbol":"dxDrawText"}
```

After correction, the final validation returned exit code 0 with no false
positive for `function` or `if`:

```json
{"command":"check","diagnostics":[],"schemaVersion":"1.0.0","status":"pass","summary":{"apiRequirements":5,"errors":0,"files":2,"resources":1,"warnings":0}}
```

The evaluator then identified that an omitted `--project` selected the
repository project instead of the current workspace. The CLI default was
changed to prefer `./neon.project.json`; the same independent evaluator reran
the command without `--project` or `--catalogue` and obtained the same passing
summary and exit code 0.

## Remaining evidence limits

- Some pinned upstream wiki contracts carry `requiresReview: true`, missing
  descriptions, or incomplete optionality. The catalogue preserves these facts
  rather than guessing.
- Events are first-class documented entities in this checkpoint, but do not yet
  carry source-registration or runtime-probe evidence.
- This checkpoint is static and runtime-free. It makes no build, in-game, or
  multiplayer claim.
