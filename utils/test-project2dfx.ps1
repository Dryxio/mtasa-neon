# Build on VM-local storage; the shared canonical tree is read-only input.
param([string]$BuildRoot = 'C:\dev\neon-project2dfx-tests')
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
if ($BuildRoot -notmatch '^[A-Za-z]:\\' -or $BuildRoot -like 'C:\Mac\*') { throw 'Use a VM-local build directory.' }
$files = @('Client\game_sa\CDistantLightNativeTransitionsSA.h', 'Client\sdk\game\SDistantLightSettings.h', 'Client\game_sa\CDistantLightsSA.h', 'Tests\standalone\Project2DFX_Tests.cpp', 'Shared\data\MTA San Andreas\MTA\data\SALodLights.dat')
foreach ($relative in $files) {
    $source = Join-Path $sourceRoot $relative
    $target = Join-Path $BuildRoot $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
    if ((Get-FileHash $source).Hash -ne (Get-FileHash $target).Hash) { throw "Hash mismatch: $relative" }
}
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat'
if (!(Test-Path $vcvars)) { throw 'Visual Studio C++ toolchain missing.' }
$command = '@echo off' + "`r`n" + 'call "' + $vcvars + '" x86 >nul' + "`r`n" +
    'if errorlevel 1 exit /b 1' + "`r`n" + 'cl /nologo /EHsc /std:c++17 /W4 /WX /Od /RTC1 Tests\standalone\Project2DFX_Tests.cpp /Fe:project2dfx-tests.exe' + "`r`n" +
    'if errorlevel 1 exit /b 1' + "`r`n" + 'project2dfx-tests.exe "Shared\data\MTA San Andreas\MTA\data\SALodLights.dat"' + "`r`n" + 'exit /b %errorlevel%'
[IO.File]::WriteAllText((Join-Path $BuildRoot 'run.cmd'), $command, [Text.Encoding]::ASCII)
Push-Location $BuildRoot
try {
    & cmd.exe /d /c run.cmd
    if ($LASTEXITCODE -ne 0) { throw "Project2DFX Win32 harness failed ($LASTEXITCODE)." }
} finally { Pop-Location }
