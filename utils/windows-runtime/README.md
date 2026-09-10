# Windows runtime prerequisite checks

Neon's client installer includes the official Visual C++ 2010 SP1 **x86**
redistributable required by the pinned FMOD DSP plugins. It runs from the
required client-core section on fresh installs and in-place updates. Existing
loadable x86 runtimes are reused; setup checks installer failure/reboot codes
and verifies the DLL afterward. It does not depend on the player's network or
on a Visual Studio installation. It never removes the shared Microsoft runtime
when Neon is uninstalled.

Before compiling `Shared/installer/nightly.nsi` with `MTA_NEON`, run:

```powershell
.\utils\windows-runtime\prepare-vc2010.ps1
```

The script pins the official Microsoft download by SHA-256 and places it in
`Build/runtime-prerequisites/vcredist_x86.exe`. For a separate build tree, pass
`/DNEON_VC2010_INSTALLER=<absolute path>` to NSIS. Do not commit this binary.
The normal Windows CI executes the preparation automatically.

## Regression checks

```powershell
python -m unittest discover -s utils/windows-runtime -p 'test_*.py' -v
python utils/windows-runtime/audit-imports.py InstallFiles --vc2010-installer Build/runtime-prerequisites/vcredist_x86.exe --output Build/runtime-import-audit.json
.\utils\windows-runtime\test-fmod-dependency.ps1 -RuntimeRoot InstallFiles/MTA/vehicle-sounds/runtime -Redistributable Build/runtime-prerequisites/vcredist_x86.exe
```

The loader regression uses fresh **x86** child processes and an isolated DLL
search directory. Both DSP plugins must fail with Windows error 126 without
the CRT and load with the x86 CRT extracted from the approved redistributable.
It never renames or removes a system DLL. This reproduces the dependency issue
on a developer machine which already has VC++ installed.

The Python audit walks normal and delayed PE imports, checks architecture,
and rejects dependencies absent from the package and reviewed OS/prerequisite
policy. It must not use the developer machine's installed DLL list as an
implicit allowlist. The XInput filename aliases are backed by
`Client/loader/MainFunctions.cpp::InitLocalization`.

This audit inventories package contents; it does not prove every DLL search
path at runtime, Windows N optional media features, graphics driver support,
or arbitrary dynamically constructed `LoadLibrary` calls. New OS allowlist
entries require review; not every DLL found on a build agent ships with Windows.

## Vehicle audio readiness contract

`engineLoadVehicleAudioConfig(path)` now returns `true, "ready"` only after
FMOD Core/Studio, required DSP plugins, the audio device and base banks have
initialized. Failure returns `false, diagnostic` and releases the lease.
Per-vehicle banks, events and samples remain lazy: the marker does not prove
that every vehicle profile can play, or monitor later audio-device loss.

Release 186 returned only a parsing result. Resource integrations must require
the second return value before acknowledging backend readiness, display the
failure reason, and preserve the player's chosen HD preference. Retry failed
initialization at a bounded rate. This also distinguishes older clients
without introducing a network ABI change.

## Release 186 dependency audit (2026-09-10)

The public installer was extracted and its 58 PE files inspected, including
installer plugins, FMOD, BASS, CEF, SkyGfx and the native client modules.
Both FMOD DSP plugins import `MSVCR100.dll`; the old installer omitted that
prerequisite. All four FMOD payload hashes match the pinned CI expectations.

A second risk remains: `core.dll` and `cgui.dll` import `D3DX9_43.dll`.
The installer supplies an optional **web** DirectX installer and ignores its
exit status. A clean offline installation, or a failed/skipped DirectX setup,
can therefore lack this dependency. The audit reports this existing weakness
as a warning; the audio fix does not silently claim to repair DirectX.
A follow-up should package/verify the required legacy DirectX component and
make prerequisite failure actionable. The suffixed XInput libraries already
have a loader fallback and are not an equivalent omission.

No other unresolved non-OS imports were found in this client package under the
reviewed Windows 10+ policy. Media Foundation imports belong to Windows media
features; Windows N/KN requires a separate feature-pack check rather than an
arbitrary DLL download.

The release 186 Windows x64 server archive was also inspected (16 PE files).
Its MySQL/OpenSSL, Lua, PCRE and pthread imports have matching packaged DLLs;
no additional missing non-OS imports were found under the same policy.
