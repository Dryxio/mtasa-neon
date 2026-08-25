# Checkpoint H black-box evaluation

- Date: 2026-08-25
- Base commit: `7c203145b`
- Final verdict: PASS
- Auditor: fresh independent `gpt-5.6-sol` agent
- Audit mode: read-only source review, hostile local transports, EOF-surviving
  process fixtures, macOS execution, and isolated Windows VM execution

Checkpoint H adds bounded, explicitly enabled resource lifecycle mutations and
runtime scenario execution. It does not add shell access, arbitrary MTA console
input, client launch, build execution, nested scenarios, automatic retries, or
runtime evidence labels.

## Final gates

```text
macOS: ./neon harness --json
PASS: 194 tests, 0 errors, 2 expected Windows-only skips

Python 3.10 targeted compatibility suite
PASS: 7/7 tests in 15.471 s

Windows portable Python 3.13 targeted suite
PASS: 7/7 tests in 20.895 s

./neon check --json
PASS

./neon catalogue verify --source-ref HEAD --json
PASS: no source, semantic, registration, event, or runtime-inventory drift

python3 -m py_compile Tools/neon-api/neon.py Tools/neon-api/neonlib/*.py
PASS

git diff --check
PASS
```

The Windows tests used a dedicated compiled stdio fixture whose process remains
alive after stdin EOF, matching the relevant MTA server behavior. They did not
modify or terminate the existing VM MTA server. After the audit, the only MTA
server process remaining was the pre-existing `MTA Server64.exe` PID 1748 under
`C:\dev\mtasa-vm-custom`.

## Permission and mutation boundaries

- Mutation capabilities are absent by default and fixed when the session is
  created. `resource.lifecycle` and `scenario.execute` must be named explicitly.
- Lifecycle access also requires an explicit server root containing only an
  approved executable name whose SHA-256 is pinned into the session contract.
- Only `start`, `stop`, and `restart` are accepted, and only for resources
  declared by the pinned project. Resource names use a bounded console-safe
  grammar; no arbitrary argument, shell, console line, or newline can pass.
- The MTA stdin adapter attempts exactly one non-blocking write. It never retries.
  Zero-byte backpressure is definite; a partial write or lost acknowledgement
  closes the session and reports `MUTATION_OUTCOME_UNKNOWN`.
- A timeout before the first request byte is a definite timeout. Any transport
  or authenticated-response failure after sending may be ambiguous, revokes the
  session, and explicitly forbids automatic retry.
- Success claims only `command-submitted` and grants zero evidence labels. It
  never claims that MTA processed the line or reached the requested state.
- `scenario.execute` has a distinct result command and run identity. Offline
  verification rejects a runtime result relabelled as `scenario.run`.
- A private guardian terminates the approved driver if the supervisor dies.
  The supervisor retains an identity-bound process reference and terminates the
  same driver if the guardian dies. Both adversarial death directions passed on
  macOS and Windows with an EOF-surviving fixture.

## Defects found and closed during independent review

The independent audit found and drove regression coverage for:

- post-send malformed or forged mutation responses initially returning a
  generic error instead of an unknown outcome and session revocation;
- Windows pipe non-blocking support incorrectly depending on Python 3.12 when
  the documented minimum is Python 3.10;
- a timeout race marking a request ambiguous before `sendall` began;
- `scenario execute` results initially identifying themselves as
  `scenario.run`;
- supervisor death leaving an EOF-tolerant MTA process orphaned;
- the first guardian design transferring that orphan risk to guardian death;
  and
- a double-newline framing error on the persistent guardian protocol.

Every blocker was fixed, reproduced independently, and covered by the closed
harness. The final auditor reported zero material blockers and made no source
edits.

## Frozen implementation hashes

```text
README.md                                      42f3bc1d3cc65ed982cfa0b27b2ae8897ca20dea97ff3c62ed5391dde1236590
neon.py                                       2574ad8fa3531282b51e19550425bc5d3e7209deb76830bb3c13349f087b7b8a
neonlib/mutation.py                           7fc80d3e25a49a82a8a1043562aff6bd8b627c5062694e37db64d50533106230
neonlib/scenario.py                           09d7b28b54f824dce450d51347406b7ea7d70018193fb30d7b995ff6e0308861
neonlib/supervisor.py                         c9477fce63ea27b6743f143b5d0c9c5a9e45fb5cff7d4dfb90282033ad132a02
neonlib/winfs.py                              caa8915640a965528c9bbf5aa1b8725826e5458d7fab1a64d4811a71ce121fd7
schemas/neon-evidence.schema.json             3fa526724a101069f433e449fc83fe5ac35061561b1c999544c2f1324173a3df
schemas/neon-mutation-result.schema.json      c887d0ff7b0b5215412659a86e3157544f8cca0e9eb4848ac085531637c569e8
schemas/neon-supervisor-session.schema.json   d0187c131331bf3edf89cb126e26fb9ffcb2182439e79ac3868f6f5de94f9922
schemas/neon-test-result.schema.json          7d84527366e1c826c9db7e0b65c35a9b5d3d09f9f6bbd31bafdddb179be25050
tests/test_neon_api.py                        6ded8353fbe53d482e540611d393c61a9423112d10a856cc8e68015ec204fd2a
tests/fixtures/windows-mta-stdio-driver.cs     c3808f36f087832bbd49a968e8ba6ca7913c7382f38787edafefaed117e3340d
```

No GitHub release, upload, push, or deployment was performed.
