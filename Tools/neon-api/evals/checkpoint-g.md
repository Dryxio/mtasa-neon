# Checkpoint G black-box evaluation

- Date: 2026-08-25
- Source commit: `1f1dc78e98520cb673e5f90dc793aa588138eb92`
- Final verdict: PASS
- Auditor: fresh `gpt-5.6-sol` agent with no inherited conversation context
- Audit mode: read-only inspection, hostile disposable workspaces, real loopback daemons, and Windows VM execution

Checkpoint G adds an explicitly started, expiring, read-only local supervisor and
compares bounded runtime observations with the pinned MTA + Neon + project
contract. It does not launch MTA, mutate resources, execute Lua, or grant a
runtime evidence label.

## Final gates

```text
macOS: ./neon harness --json
PASS: 184 tests, 0 errors, 1 expected Windows-only skip

Windows portable Python, full pre-final-metadata harness
PASS: 180 tests, 0 errors, 17 expected platform/tool skips

Windows final targeted supervisor/security suite
PASS: 34 tests, 7 expected POSIX-only skips

./neon catalogue verify --source-ref HEAD --json
PASS: no source, semantic, registration, event, or runtime-inventory drift

./neon check --json
PASS

python3 -m py_compile Tools/neon-api/neon.py Tools/neon-api/neonlib/*.py
PASS

git diff --check
PASS
```

Python 3.13.15's official embeddable Windows runtime was used from
`C:\dev\python313-neon-audit\python.exe`; the VM has no system Python
installation. A real Windows session completed start, status, missing-snapshot
comparison, and stop. The native junction and parent-replacement regression
proved that writes remained on the retained directory handle and created
nothing outside the workspace.

## Security and truthfulness properties

- The listener binds only to an ephemeral `127.0.0.1` port.
- The mode-`0600` token remains in the session record and is never sent over
  the wire. Fresh server challenges and client nonces HMAC-bind both directions.
- Captured requests cannot be replayed against a new challenge. Forged,
  malformed, oversized, redirected, truncated, or stale exchanges fail closed.
- The complete active session document is immutable. Any changed capability,
  input path/hash, transport field, timestamp, process ID, or bearer revokes the
  session and removes its token.
- Revocation and monotonic expiration are checked around accept, receive,
  authentication, action, and response, including preaccepted connections.
- POSIX uses component-by-component `dir_fd` opens with `O_NOFOLLOW`. Windows
  uses native root-handle-relative `NtCreateFile` and rejects reparse points.
  Platforms with neither primitive reject startup.
- Project, catalogue, manifest, meta, script, asset, and module-binary bytes are
  read through the retained workspace handle. General project validation runs
  only against a private ephemeral shadow populated from those anchored reads.
- Inputs, diagnostics, results, identifiers, protocol documents, snapshot size,
  reference count, and the 256 KiB audit are bounded. Audit truncation is an
  explicit artifact.
- Partial observations weaken only absence claims. Positively observed rogue,
  unavailable, conflicted, wrong-side, or identity-mismatched APIs and
  components remain blocking errors.
- Resource presence and exports are side-aware even when a client or shared
  resource has no public symbols; resolved component sides come from `meta.xml`
  script declarations.
- Successful comparison remains `scope: observation-only` with no granted
  evidence labels.

## Defects found and closed during independent review

Successive fresh audits found and drove regression coverage for:

- graceful EOF, stale-bearer, preaccept, expiration, and revocation races;
- forged response and captured-request replay;
- unbounded audit growth, diagnostics, observation IDs, and semvers;
- session-root creation, project/catalogue selection, snapshot reading, and
  revocation parent-replacement races;
- the insecure non-`dir_fd` pathname fallback on Windows, replaced by native
  handle-relative operations;
- an active record that initially bound only session ID and bearer;
- uncatalogued functions/events and rogue client resources false-passing;
- resource/module export comparisons mixing client and server contracts;
- conflicted catalogue symbols being treated as active;
- client resources missing from complete observations, wrong-side resources,
  and symbol-free client resources;
- pathname-based transitive component resolution reading a transient forged
  project or component outside the approved workspace; and
- source-catalogue drift after the concurrent Neon identity commit advanced
  `HEAD`.

The final independent audit reproduced the high-risk cases and reported zero
material release blockers. It made no repository edits.

## Frozen implementation hashes

```text
neon.py                                      7cb53fb912e58a50b5952f732407d99c0179c788c1c57c54e3cfda27d6d15e59
neonlib/project.py                           b5b50124564fdfd17cf8c1ae05527b4473418ee356b8fdffca88829775ce2480
neonlib/runtime.py                           c7223c60701a856c4fb432165e13b56657399807ea277ff0ff1317bc4ea3ef1f
neonlib/supervisor.py                        d9bcc8caad867d75a359b42624fe7735c5f49dd2d79d86607ad48da1b1b79081
neonlib/winfs.py                             4c27a6901b5c3a5e8659f71367b66fbeaedc25a60940d8a65009fb4c74e7ea85
tests/test_neon_api.py                       393a66e97ef68b17864e3966d7a5306996a3cc6b9130e2618abed6ec9e6a2a35
schemas/neon-project-api.schema.json         29e34e043c1f7370456f3fd2eb8134de52ca554101ea8694d650e07ef9f170a3
schemas/neon-project.schema.json             73a5512293b7035f87cbd1a6772c5b59dd40254cb2444e08d46538b6b1194a96
schemas/neon-runtime-compare-result.schema.json c6d27a7e5812582c77822ffbca0260b9d66e9e631666df4f8b319971573dda2e
schemas/neon-runtime-snapshot.schema.json    f4b80c749a23313abee4614921b39f341b241f044183d1479b1abe5dd8b84594
schemas/neon-supervisor-result.schema.json   283f07689f6d4c5fdf8e204304083f40058f49ecfb442e4d9e1bb21ed0dd59e9
schemas/neon-supervisor-session.schema.json  3298460cca3523607d204b7057ec2127e2a25bf43bcd7044b25056376c25cacd
```
