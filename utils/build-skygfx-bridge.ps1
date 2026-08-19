[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $SourceRoot,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string] $ExpectedCommit,

    [string] $DestinationPath,

    [ValidatePattern('^v[0-9]{3}$')]
    [string] $PlatformToolset = 'v145'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedSourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$expectedDestination = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'Bin\MTA\skygfx_mta.dll'))
if ([string]::IsNullOrWhiteSpace($DestinationPath))
{
    $resolvedDestination = $expectedDestination
}
else
{
    $resolvedDestination = [System.IO.Path]::GetFullPath($DestinationPath)
}

# This helper deliberately owns one exact build output. Rejecting arbitrary
# destinations keeps an accidental CI argument from overwriting another DLL.
if (-not $resolvedDestination.Equals($expectedDestination, [System.StringComparison]::OrdinalIgnoreCase))
{
    throw "SkyGfx bridge destination must be: $expectedDestination"
}

$requiredSourceFiles = @(
    'premake5.exe',
    'premake5.lua',
    'src\mta\SkyGfxMTA.cpp',
    'src\mta\SkyGfxMTA.h',
    'src\mta\GradingPixelShader.h'
)
foreach ($relativePath in $requiredSourceFiles)
{
    $sourcePath = Join-Path $resolvedSourceRoot $relativePath
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf))
    {
        throw "Pinned SkyGfx checkout is missing: $relativePath"
    }
}

$vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe')
    (Join-Path $repositoryRoot 'utils\vswhere.exe')
)
$vswhere = $vswhereCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($vswhere))
{
    throw 'Visual Studio locator is missing from the installed Visual Studio tools and the repository fallback'
}
$visualStudioRoot = @(& $vswhere -latest -products '*' -property installationPath) | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($visualStudioRoot))
{
    throw 'Could not locate Visual Studio'
}

$gitCommand = Get-Command git.exe -ErrorAction SilentlyContinue
if ($gitCommand)
{
    $git = $gitCommand.Source
}
else
{
    $gitCandidates = @(
        (Join-Path $env:ProgramFiles 'Git\cmd\git.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Git\cmd\git.exe'),
        (Join-Path $visualStudioRoot 'Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe')
    )
    $git = $gitCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($git))
{
    throw 'Could not locate Git for Windows'
}

$sourceTopLevel = @(& $git -C $resolvedSourceRoot rev-parse --show-toplevel 2>&1)
if ($LASTEXITCODE -ne 0 -or $sourceTopLevel.Count -ne 1)
{
    throw "Could not validate the SkyGfx Git checkout: $($sourceTopLevel -join ' ')"
}
$resolvedTopLevel = (Resolve-Path -LiteralPath $sourceTopLevel[0]).Path
if (-not $resolvedTopLevel.Equals($resolvedSourceRoot, [System.StringComparison]::OrdinalIgnoreCase))
{
    throw "SkyGfx source root is not the checkout root: $resolvedSourceRoot"
}

$actualCommit = @(& $git -C $resolvedSourceRoot rev-parse HEAD 2>&1)
if ($LASTEXITCODE -ne 0 -or $actualCommit.Count -ne 1)
{
    throw "Could not read the SkyGfx source revision: $($actualCommit -join ' ')"
}
if (-not $actualCommit[0].Equals($ExpectedCommit, [System.StringComparison]::OrdinalIgnoreCase))
{
    throw "SkyGfx source revision is $($actualCommit[0]); expected $ExpectedCommit"
}
$trackedChanges = @(& $git -C $resolvedSourceRoot status --porcelain --untracked-files=no 2>&1)
if ($LASTEXITCODE -ne 0)
{
    throw "Could not inspect the SkyGfx checkout state: $($trackedChanges -join ' ')"
}
if ($trackedChanges.Count -ne 0)
{
    throw 'Pinned SkyGfx checkout has modified tracked files'
}

$msbuildCandidates = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe')
$msbuild = $msbuildCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($msbuild))
{
    throw 'Could not locate MSBuild'
}

$dumpbinCandidates = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\**\bin\Hostx64\x86\dumpbin.exe')
$dumpbin = $dumpbinCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($dumpbin))
{
    throw 'Could not locate the x86 dumpbin utility'
}

# SkyGfx keeps this bridge isolated from its legacy ASI target. Generate only
# its project from the fully pinned checkout, then ask the current toolchain to
# build that project with the runner's installed platform toolset.
Push-Location $resolvedSourceRoot
try
{
    & (Join-Path $resolvedSourceRoot 'premake5.exe') vs2015
    if ($LASTEXITCODE -ne 0)
    {
        throw "SkyGfx Premake generation failed with exit code $LASTEXITCODE"
    }

    $projectPath = Join-Path $resolvedSourceRoot 'build\skygfx_mta.vcxproj'
    if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf))
    {
        throw 'SkyGfx Premake generation did not produce skygfx_mta.vcxproj'
    }

    & $msbuild $projectPath /m /t:Build /p:Configuration=Release /p:Platform=Win32 "/p:PlatformToolset=$PlatformToolset" /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0)
    {
        throw "SkyGfx bridge build failed with exit code $LASTEXITCODE"
    }
}
finally
{
    Pop-Location
}

$bridgeOutput = Join-Path $resolvedSourceRoot 'bin\Release\skygfx_mta.dll'
if (-not (Test-Path -LiteralPath $bridgeOutput -PathType Leaf))
{
    throw "SkyGfx bridge build output is missing: $bridgeOutput"
}
$bridgeFile = Get-Item -LiteralPath $bridgeOutput
if ($bridgeFile.Length -eq 0)
{
    throw 'SkyGfx bridge build output is empty'
}

$headers = @(& $dumpbin /headers $bridgeOutput)
if ($LASTEXITCODE -ne 0 -or ($headers -join "`n") -notmatch '(?im)^\s*14C machine \(x86\)')
{
    throw 'SkyGfx bridge is not a valid x86 DLL'
}

$exports = @(& $dumpbin /exports $bridgeOutput)
if ($LASTEXITCODE -ne 0)
{
    throw "Could not inspect SkyGfx bridge exports (exit code $LASTEXITCODE)"
}
$exportText = $exports -join "`n"
$requiredExports = @(
    'SkyGfxMTA_GetApiVersion',
    'SkyGfxMTA_GetCapabilities',
    'SkyGfxMTA_Initialize',
    'SkyGfxMTA_ApplyConfig',
    'SkyGfxMTA_RenderColorFilter',
    'SkyGfxMTA_RenderRadiosity',
    'SkyGfxMTA_RenderYCbCr',
    'SkyGfxMTA_InvalidateDeviceResources',
    'SkyGfxMTA_RestoreDeviceResources',
    'SkyGfxMTA_Shutdown'
)
foreach ($exportName in $requiredExports)
{
    if ($exportText -notmatch "(?m)\b$([regex]::Escape($exportName))\b")
    {
        throw "SkyGfx bridge is missing export: $exportName"
    }
}

$destinationDirectory = Split-Path -Parent $resolvedDestination
if (-not (Test-Path -LiteralPath $destinationDirectory -PathType Container))
{
    throw "MTA runtime directory is missing: $destinationDirectory"
}

Copy-Item -LiteralPath $bridgeOutput -Destination $resolvedDestination -Force
$sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $bridgeOutput).Hash
$destinationHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolvedDestination).Hash
if ($sourceHash -ne $destinationHash)
{
    throw 'Staged SkyGfx bridge does not match the build output'
}

Write-Host "SkyGfx bridge revision: $($actualCommit[0])"
Write-Host "SkyGfx bridge: $resolvedDestination"
Write-Host "Size: $($bridgeFile.Length) bytes"
Write-Host "SHA-256: $destinationHash"
Write-Output $resolvedDestination
