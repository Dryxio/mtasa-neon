# Reproduce a clean-PC loader failure without removing the developer's system DLL.
# Each probe is a fresh x86 process whose DLL search is restricted to the fixture.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RuntimeRoot,
    [Parameter(Mandatory = $true)][string]$Redistributable,
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe'
)
$ErrorActionPreference = 'Stop'
if ((Get-FileHash $Redistributable).Hash -ne '99dce3c841cc6028560830f7866c9ce2928c98cf3256892ef8e6cf755147b0d8') { throw 'Unverified redistributable' }
$scratch = Join-Path $env:TEMP ('neon-fmod-dependency-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
    $code = @'
using System;
using System.Runtime.InteropServices;
class LoaderProbe {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
    static int Main(string[] args) {
        if (IntPtr.Size != 4) return 2;
        IntPtr module = LoadLibraryExW(args[0], IntPtr.Zero, 0x100);
        int error = module == IntPtr.Zero ? Marshal.GetLastWin32Error() : 0;
        if (module != IntPtr.Zero) FreeLibrary(module);
        Console.WriteLine("x86 loader: " + System.IO.Path.GetFileName(args[0]) + " -> " + error);
        return error == int.Parse(args[1]) ? 0 : 1;
    }
}
'@
    $source = Join-Path $scratch 'probe.cs'
    $probe = Join-Path $scratch 'probe.exe'
    Set-Content $source $code
    & "$env:WINDIR\Microsoft.NET\Framework\v4.0.30319\csc.exe" /nologo /platform:x86 "/out:$probe" $source
    if ($LASTEXITCODE -ne 0) { throw 'Could not build the x86 loader probe' }
    & $SevenZip x -y "-o$scratch\redist" $Redistributable > (Join-Path $scratch 'extract.log')
    if ($LASTEXITCODE -gt 1) { throw 'Could not extract redistributable' }
    $cab = Get-ChildItem "$scratch\redist" -Recurse -Filter vc_red.cab | Select-Object -First 1
    if (-not $cab) { throw 'Missing runtime cabinet' }
    & $SevenZip x -y "-o$scratch\cab" $cab.FullName > (Join-Path $scratch 'cab.log')
    if ($LASTEXITCODE -ne 0) { throw 'Could not extract runtime cabinet' }
    $crt = Get-ChildItem "$scratch\cab" -Recurse -File | Where-Object { $_.Name -match 'msvcr100.*x86' } | Select-Object -First 1
    if (-not $crt) { throw 'No x86 MSVCR100 payload in the approved redistributable' }
    foreach ($name in @('fmod_distance_filter.dll', 'fmod_gain.dll')) {
        $directory = Join-Path $scratch ($name + '-fixture')
        New-Item -ItemType Directory $directory | Out-Null
        $fixture = Join-Path $directory $name
        Copy-Item (Join-Path $RuntimeRoot "plugins\$name") $fixture
        & $probe $fixture 126
        if ($LASTEXITCODE -ne 0) { throw "Missing-CRT reproduction failed for $name" }
        Copy-Item $crt.FullName (Join-Path $directory 'msvcr100.dll')
        & $probe $fixture 0
        if ($LASTEXITCODE -ne 0) { throw "Approved CRT did not repair $name" }
    }
    Write-Output 'Both FMOD plugins fail without the x86 CRT and load with the approved prerequisite.'
} finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
