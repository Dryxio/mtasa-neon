# Local multi-client testing

Neon custom, unstable, and untested client builds can run one primary MTA
client and one secondary client on the same Windows desktop. This is intended
for local development of resources that need two simultaneous players; it is
not enabled in release builds.

## Runtime behavior

Start the normal client first, then start the second client with `-cl2`:

```powershell
Start-Process -FilePath 'C:\dev\mtasa-vm-custom\Bin\Multi Theft Auto.exe' `
  -ArgumentList 'mtasa://127.0.0.1:22003' `
  -WorkingDirectory 'C:\dev\mtasa-vm-custom\Bin'

Start-Process -FilePath 'C:\dev\mtasa-vm-custom\Bin\Multi Theft Auto.exe' `
  -ArgumentList '-cl2 mtasa://127.0.0.1:22003' `
  -WorkingDirectory 'C:\dev\mtasa-vm-custom\Bin'
```

The secondary client uses its own loader mutex, window title, configuration,
console and client-script logs, server-browser cache, and downloaded resource
cache. Its visible game window is named `MTA: San Andreas [CL2]`.

The local server must permit two connections that share a serial. Set the
runtime server configuration to:

```xml
<check_duplicate_serials>0</check_duplicate_serials>
```

Do this only on a trusted development server. Public servers should normally
keep duplicate-serial checking enabled. The two profiles also need distinct
nicknames.

## Side-by-side Windows layout

Both clients should use windowed mode when they share one display. The local
development setup uses these values in both `coreconfig.xml` and
`coreconfig-cl2.xml`:

```xml
<display_windowed>1</display_windowed>
<display_resolution>1440x900x32</display_resolution>
```

`utils/multi-client/DualMtaTile.cs` builds a small Windows helper that waits
for both game windows, minimizes the server console, and places the primary
client on the left and the `CL2` client on the right. Build it in the VM with:

```powershell
Add-Type `
  -Path 'C:\Mac\Home\Documents\GitHub\mtasa-neon\utils\multi-client\DualMtaTile.cs' `
  -OutputAssembly 'C:\dev\mtasa-vm-custom\Build\dual-mta-tile.exe' `
  -OutputType ConsoleApplication
```

## macOS launcher for Parallels

`utils/multi-client/MTA Neon Duo.applescript` is the maintained source for a
macOS applet. It verifies that no partial client session is running, prepares
the two windowed profiles, launches the primary client and `CL2`, and starts
the layout helper in the logged-in Windows desktop session.

Compile it with:

```sh
osacompile -o "$HOME/Applications/MTA Neon Duo.app" \
  "utils/multi-client/MTA Neon Duo.applescript"
```

The script expects the Parallels VM and paths documented in `AGENTS.md`. The
server must already be running and listening on `127.0.0.1:22003`.

### Optional local-server download reduction

Large native-world transport test packs are not required for ordinary
two-player script testing. On a local server, their `mtaserver.conf` entries
can use `startup="0"` so they remain available for explicit tests without being
sent on every connection. The current local setup disables every resource
whose name begins with `native-world`; restart the server after changing these
flags. This is a runtime-server choice rather than a client multi-process
requirement.

## Verification

A successful local test has all of the following:

- two `Multi Theft Auto.exe` and two `gta_sa.exe` processes;
- window titles `MTA: San Andreas` and `MTA: San Andreas [CL2]`;
- two different nicknames in the server log;
- both joins accepted even though the serial is the same;
- `coreconfig-cl2.xml`, `console-cl2.log`, and the secondary cache root created
  independently from the primary profile.
