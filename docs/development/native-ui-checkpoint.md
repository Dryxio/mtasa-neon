# Native interfaces checkpoint: issues 109–112

Implemented a client resource-owned API for custom mission messages, GTA mission
clocks/counters/gauges, native list/color menus, and positioned native
text/windows/rectangles/blackjack cards. Resource authors should start with the
[API guide](../client/native-ui.md) and the
[test resource](../../test-resources/native-ui-test/README.md).

## Verification performed

- Portable ownership/model tests pass on macOS with ASan and UBSan.
- The same assertion suite passes as an MSVC x64 console executable in the VM.
- Lab server protocol tests pass for client/source validation, sequence numbers,
  disconnect cleanup and timer wrap. All lab Lua files parse.
- `Game SA`, `Client Deathmatch`, `Client Core`, and `Multiplayer SA` compile as
  `Release|Win32`. The final vm-build transaction reports success and checks that
  the canonical checkpoint did not change during the build.
- Catalogue generation, Lua definition generation and catalogue verification
  pass: seven client functions plus `onClientNativeUI`, no inventory divergence.
- Both test resources are deployed to the VM server resources directory with
  exact file hashes checked. Neither a graphical client nor a test resource was
  started by the implementation workflow.

The user's VM session on 2026-09-06 at 16:04–16:06 records two complete
13-assertion selftests, two passing late-clock-correction tests, an accepted list
selection (row 3) and an accepted grid selection (cell 9/color 8). Both resources
started successfully; no Native UI error appears in the reviewed session logs.
The text, taxi, blackjack, fade and stress scenes were invoked, but invocation
alone does not certify their full duration or visual result.

Visual comparison, complete client/server lifecycle, input blocking/rebinding,
grid positions beyond 12, resource restart while rendering and multi-client
behavior remain manual validation items. The test matrix covers
all four issue families, including two-resource contention. No issue closure or
visual parity claim follows from these builds.

## Source and binary provenance

The registration/cleanup boundary is `CLuaNativeUIDefs.cpp`, with the service in
`CNativeUISA.cpp` and its portable bounds/ownership model in `NativeUIModel.h`.
`CGame::GetNativeUI` is appended to the existing interface; existing virtual
slots are preserved. No server C++ interface or wire packet changed. Server
corrections are ordinary resource events, and client completion never awards a
reward.

The on-disk PE audit was run against these SHA-256 values:

- Reference: `72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b`.
- VM: `a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26`.

The timer renderer is identical in the compared range. The VM executable
relocates its SCM clock/counter allocator routines; this implementation supplies
owned display buffers and does not invoke those allocators. Its standard-menu
render detour reproduces the displaced instruction and returns to the original
continuation. The reversed source is a symbol guide, not a binary contract:
the double counter and grid accept path required direct disassembly checks.

Compilation and headless checks used the canonical working tree with its
pre-existing changes. The published source checkpoint isolates only Native UI
changes. The catalogue is regenerated against that isolated source commit;
its new records remain `runtime-only` in the CLI's source/documentation
classification, which is not an in-game validation claim. The API guide is local
to the engine repository; no wiki publication was performed.

Regenerate the catalogue against the eventual reviewed source commit when
publishing this checkpoint. Its commit message should retain the requested
scope, ownership/lifetime motivation, native adaptations, performed headless and
build checks, and the outstanding manual validation.
