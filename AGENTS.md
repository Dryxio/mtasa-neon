## Dev environment tips

Run ./utils/clang-format.ps1 after making changes to C++ files, to ensure that the changes are correctly formatted.

## Canonical repository and remotes

The canonical working tree is on the macOS host:

```text
/Users/salimtrouve/Documents/GitHub/mtasa-neon
```

Make, review, commit, and push all source changes from this working tree. Its remotes are:

```text
origin   https://github.com/Dryxio/mtasa-neon.git
upstream https://github.com/multitheftauto/mtasa-blue.git
```

The same working tree is visible inside the Parallels VM at:

```text
C:\Mac\Home\Documents\GitHub\mtasa-neon
```

Do not build directly on the Parallels shared folder. It is slower than the VM-local disk and has caused inconsistent file access. Do not make source changes only in the VM build copy because it has no Git metadata and those changes will not be versioned.

## Windows VM build workflow

The Parallels VM is named `Windows 11`. The VM-local source/build copy is:

```text
C:\dev\mtasa-vm-custom
```

Use this workflow:

1. Modify the canonical `mtasa-neon` working tree on macOS.
2. Synchronize it from `C:\Mac\Home\Documents\GitHub\mtasa-neon` to `C:\dev\mtasa-vm-custom`, preserving the VM-local `Build` and `Bin` directories and excluding `.git`.
3. Regenerate Visual Studio projects when build definitions, dependencies, or source-file lists change.
4. Build the client as `Release|Win32` and the server as `Release|x64`.
5. Test the produced binaries in the VM.
6. Commit and push from the canonical macOS working tree only.

The generated solution is:

```text
C:\dev\mtasa-vm-custom\Build\MTASA.sln
```

The installed Visual Studio/MSBuild toolchain is under:

```text
C:\Program Files\Microsoft Visual Studio\18\Community
```

The DirectX SDK June 2010 is installed in the VM.

## Client build and runtime paths

Build the client with `Release|Win32`. Important outputs are:

```text
C:\dev\mtasa-vm-custom\Bin\Multi Theft Auto.exe
C:\dev\mtasa-vm-custom\Bin\MTA\core.dll
C:\dev\mtasa-vm-custom\Bin\MTA\netc.dll
C:\dev\mtasa-vm-custom\Bin\mods\deathmatch\client.dll
```

The current CUSTOM `netc.dll` works in the Parallels VM. Keep it paired with the current source ABI; do not replace it with the old MTA 1.6 module.

## Server build and runtime paths

Build the current Windows server with `Release|x64`. Building the solution as `Release|Win32` excludes the server launcher, core, and deathmatch projects. Important x64 outputs are:

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

Run the server from `C:\dev\mtasa-vm-custom\Bin\server` so its relative paths resolve correctly. The default tested endpoints are:

```text
22003/UDP  game server
22005/TCP  HTTP resource server
```

`win-install-data.bat` installs shared client/server data. Answer yes to its resources prompt, or otherwise ensure the resources directory above is installed before testing the server.

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

Prefer `C:\dev\GTA-SA` for runtime tests. Loading GTA directly from the shared Mac path previously produced false missing-file errors even when Windows command-line tools could see the files.

## Reference installations

Keep the official/older installations isolated from the custom build:

```text
C:\Program Files (x86)\MTA San Andreas 1.6
C:\Program Files (x86)\MTA San Andreas 1.7
C:\dev\mtasa-vm-custom
```

The MTA 1.6 `netc.dll` uses an older client net module ABI and is not a drop-in replacement for current source builds.

## Verification expectations

After relevant changes, verify the smallest applicable set of the following:

- The requested configuration compiles successfully.
- The client reaches GTA and can connect to the local custom server.
- The server starts the configured resources without fatal errors.
- The server listens on `22003/UDP` and, when enabled, `22005/TCP`.
- Reconnection, resource restart, spawn/respawn, and a short gameplay session remain stable for networking or lifecycle changes.

## Code comments

When making code changes, explain *why* the code you've written should exist and the motivation behind the changes. This ensures that future engineers don't have to read between the lines.

## Steer the user towards writing clear commit messages

Tell the user to include details about the prompt / goals / motivation / reasoning / how they tested their changes.

Don't assume the user will include this information in the commit messages; proactively tell them to include this information in their commit messages.
