## Dev environment tips

Run `./utils/clang-format.ps1` after making changes to C++ files, to ensure that
the changes are correctly formatted.

## Canonical repository and remotes

The canonical working tree is on the macOS host:

```text
/Users/salimtrouve/Documents/GitHub/mtasa-neon
```

Make, review, commit, and push all source changes from this working tree. Its
remotes are:

```text
origin   https://github.com/Dryxio/mtasa-neon.git
upstream https://github.com/multitheftauto/mtasa-blue.git
```

The same working tree is visible inside the Parallels VM at:

```text
C:\Mac\Home\Documents\GitHub\mtasa-neon
```

Do not build directly on the Parallels shared folder. It is slower than the
VM-local disk and has caused inconsistent file access. Do not make source
changes only in the VM build copy because it has no Git metadata and those
changes will not be versioned.

## Windows VM build workflow

The Parallels VM is named `Windows 11`. The VM-local source/build copy is:

```text
C:\dev\mtasa-vm-custom
```

Use this workflow:

1. Modify the canonical working tree on macOS.
2. Review and name only the files owned by the current checkpoint.
3. Use `utils/vm-build.ps1` to plan their VM synchronization and the smallest
   affected project build.
4. Review the plan, then rerun with `-Execute`.
5. Test the produced binaries in the VM.
6. Run the broader build appropriate to the checkpoint before calling it
   complete.
7. Commit and push from the canonical macOS working tree only.

The generated solution is:

```text
C:\dev\mtasa-vm-custom\Build\MTASA.sln
```

The installed Visual Studio/MSBuild toolchain is under:

```text
C:\Program Files\Microsoft Visual Studio\18\Community
```

The DirectX SDK June 2010 is installed in the VM.

### VM build helper

Run the canonical `utils/vm-build.ps1` through Windows PowerShell 5.1. It uses
an explicit file/project checkpoint and defaults to a read-only plan. Review
that plan before adding `-Execute`:

```powershell
$vmBuild = 'C:\Mac\Home\Documents\GitHub\mtasa-neon\utils\vm-build.ps1'
$files = @('Client\game_sa\CGameSA.cpp', 'Client\game_sa\CGameSA.h')

& $vmBuild -Files $files -ClientProjects @('Game SA')
& $vmBuild -Files $files -ClientProjects @('Game SA') -Execute
```

The script owns the detailed safety policy: exact-path SHA-256 synchronization,
generated-dependency preservation/bootstrap, regeneration checks, process and
output-lock checks, project/platform selection, output verification, and an
exclusive transaction lock. Important modes are:

- `-BuildOnly` to retry selected projects without synchronization;
- `-DeleteFiles` for checkpoint-owned files already absent canonically (the VM
  copy is quarantined under `Build`, not deleted);
- `-Regenerate` after compiled-source or Premake/build-definition changes; and
- `-BootstrapDependencies` only when a reviewed plan reports a missing pinned
  dependency.

Changes to Premake/Discord/CEF bootstrap tools use the intentional full-setup
workflow; the incremental helper refuses those paths.

Never use plain `robocopy /MIR` or `/PURGE` on the established VM tree: the
canonical tree omits VM-generated CEF, Discord/RapidJSON, cached archives, and
Unifont. Do not use `win-build.bat` for ordinary iteration; it runs every
dependency installer, regenerates, and builds the full solution. Reserve it for
initial setup or an intentional full build.

Common project mappings are:

| Changed area | Iteration project |
| --- | --- |
| `Client/game_sa` | `Game SA`, `Release|Win32` |
| `Client/core` | `Client Core`, `Release|Win32` |
| `Client/multiplayer_sa` | `Multiplayer SA`, `Release|Win32` |
| `Client/mods/deathmatch` | `Client Deathmatch`, `Release|Win32` |
| `Server/mods/deathmatch` | `Deathmatch`, `Release|x64` |
| `Server/core` | `Core`, `Release|x64` |
| `Server/launcher` | `Launcher`, `Release|x64` |
| Lua/resource data only | No C++ build; deploy/restart the resource separately |

For SDK, shared header, serialization, protocol, or ABI changes, trace and
build every affected producer and consumer. For native-world client/server
work the usual mixed set is `Game SA` and `Client Deathmatch` as client
projects plus `Deathmatch` as a server project. Add broader projects only when
their sources or consumed interfaces changed.

The helper deliberately does not deploy `test-resources` into `Bin`; deploy and
restart the exact runtime resource separately.

### Manual fallback

If the helper itself is being repaired, copy only reviewed files with verified
hashes, preserve VM-generated state, regenerate only when required, and invoke
the affected `.vcxproj` directly. Do not use timestamp-only copy logic such as
`/XO`; a newer VM timestamp can hide canonical content changes.

For a large branch switch or cleanup, prepare a fresh VM-local source copy
rather than risking the established build/runtime tree with an unreviewed
mirror.

## Client build and runtime paths

Build the client with `Release|Win32`. Important outputs are:

```text
C:\dev\mtasa-vm-custom\Bin\Multi Theft Auto.exe
C:\dev\mtasa-vm-custom\Bin\MTA\core.dll
C:\dev\mtasa-vm-custom\Bin\MTA\netc.dll
C:\dev\mtasa-vm-custom\Bin\mods\deathmatch\client.dll
```

The current CUSTOM `netc.dll` works in the Parallels VM. Keep it paired with the
current source ABI; do not replace it with the old MTA 1.6 module.

### Launching the graphical client from macOS

GUI applications must be started in the logged-in Windows desktop session.
Always pass `--current-user` to `prlctl exec`; without it, `cmd.exe` or
PowerShell can return success while no visible MTA process is created.

Launch the custom client and connect directly to the local server with:

```sh
prlctl exec "Windows 11" --current-user powershell.exe -NoProfile -Command \
  "Start-Process -FilePath 'C:\dev\mtasa-vm-custom\Bin\Multi Theft Auto.exe' -ArgumentList 'mtasa://127.0.0.1:22003' -WorkingDirectory 'C:\dev\mtasa-vm-custom\Bin'"
```

The equivalent `cmd.exe` form is:

```sh
prlctl exec "Windows 11" --current-user cmd.exe /c start "" \
  "C:\dev\mtasa-vm-custom\Bin\Multi Theft Auto.exe" \
  "mtasa://127.0.0.1:22003"
```

## Server build and runtime paths

Build the current Windows server with `Release|x64`. Building the solution as
`Release|Win32` excludes the server launcher, core, and deathmatch projects.
Important x64 outputs are:

```text
C:\dev\mtasa-vm-custom\Bin\server\MTA Server64.exe
C:\dev\mtasa-vm-custom\Bin\server\x64\core.dll
C:\dev\mtasa-vm-custom\Bin\server\x64\net.dll
C:\dev\mtasa-vm-custom\Bin\server\x64\deathmatch.dll
C:\dev\mtasa-vm-custom\Bin\server\x64\lua5.1.dll
C:\dev\mtasa-vm-custom\Bin\server\x64\xmll.dll
```

Server configuration and resources are stored at:

```text
C:\dev\mtasa-vm-custom\Bin\server\mods\deathmatch\mtaserver.conf
C:\dev\mtasa-vm-custom\Bin\server\mods\deathmatch\acl.xml
C:\dev\mtasa-vm-custom\Bin\server\mods\deathmatch\resources
```

Run the server from `C:\dev\mtasa-vm-custom\Bin\server` so its relative paths
resolve correctly. The default tested endpoints are:

```text
22003/UDP  game server
22005/TCP  HTTP resource server
```

`win-install-data.bat` installs shared client/server data. Answer yes to its
resources prompt, or otherwise ensure the resources directory above is
installed before testing the server.

## GTA: San Andreas test paths

The reliable VM-local GTA copy used for testing is:

```text
C:\dev\GTA-SA
```

The host copy and its Parallels shared path are:

```text
/Users/salimtrouve/Documents/GTA-SanAndreas
C:\Mac\Home\Documents\GTA-SanAndreas
```

Prefer `C:\dev\GTA-SA` for runtime tests. Loading GTA directly from the shared
Mac path previously produced false missing-file errors even when Windows
command-line tools could see the files.

## Reference installations

Keep the official/older installations isolated from the custom build:

```text
C:\Program Files (x86)\MTA San Andreas 1.6
C:\Program Files (x86)\MTA San Andreas 1.7
C:\dev\mtasa-vm-custom
```

The MTA 1.6 `netc.dll` uses an older client net module ABI and is not a drop-in
replacement for current source builds.

## Verification expectations

After relevant changes, verify the smallest applicable set of the following:

- The requested configuration compiles successfully.
- The client reaches GTA and can connect to the local custom server.
- The server starts the configured resources without fatal errors.
- The server listens on `22003/UDP` and, when enabled, `22005/TCP`.
- Reconnection, resource restart, spawn/respawn, and a short gameplay session
  remain stable for networking or lifecycle changes.

## Code comments

When making code changes, explain *why* the code should exist and the motivation
behind it. Future engineers should not have to read between the lines.

## Steer the user towards writing clear commit messages

Tell the user to include details about the prompt, goals, motivation, reasoning,
and how they tested their changes. Do not assume they will include this
information; proactively tell them to put it in commit messages.
