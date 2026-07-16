<#
.SYNOPSIS
Synchronizes an explicit checkpoint file set into the Windows VM build tree and
builds only the selected MTA projects.

.DESCRIPTION
Run this script with Windows PowerShell 5.1 or newer from the Parallels shared
canonical tree. It never mirrors the repository, never deletes source files,
and never selects files from Git automatically. Deleted checkpoint files are
moved into a VM-local quarantine when explicitly listed. The default is a
read-only plan; pass -Execute to mutate the VM or build.

.EXAMPLE
& 'C:\Mac\Home\Documents\GitHub\mtasa-neon\utils\vm-build.ps1' `
    -Files @('Client\game_sa\CGameSA.cpp', 'Client\game_sa\CGameSA.h') `
    -ClientProjects @('Game SA') `
    -Execute

.EXAMPLE
& 'C:\Mac\Home\Documents\GitHub\mtasa-neon\utils\vm-build.ps1' `
    -Files @('Shared\sdk\net\bitstream.h') `
    -ClientProjects @('Client Deathmatch') `
    -ServerProjects @('Deathmatch')
#>

[CmdletBinding()]
param(
    [string[]] $Files = @(),
    [string[]] $DeleteFiles = @(),

    [ValidateSet(
        'Game SA',
        'Client Core',
        'Client Deathmatch',
        'Client Launcher',
        'Client Webbrowser',
        'Multiplayer SA',
        'GUI',
        'Loader',
        'Loader Proxy',
        'CEF',
        'CEFLauncher',
        'CEFLauncher DLL'
    )]
    [string[]] $ClientProjects = @(),

    [ValidateSet('Deathmatch', 'Core', 'Launcher', 'Dbconmy', 'Server SDK', 'XML')]
    [string[]] $ServerProjects = @(),

    [switch] $Regenerate,
    [switch] $BuildOnly,
    [switch] $BootstrapDependencies,
    [switch] $Execute
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$DryRun = -not $Execute
$script:PlanBlockers = New-Object 'System.Collections.Generic.List[string]'

function Write-Step {
    param([Parameter(Mandatory = $true)] [string] $Message)
    Write-Host "[vm-build] $Message" -ForegroundColor Cyan
}

function Add-PlanBlocker {
    param([Parameter(Mandatory = $true)] [string] $Message)

    if (-not $DryRun) {
        throw $Message
    }
    if (-not $script:PlanBlockers.Contains($Message)) {
        $script:PlanBlockers.Add($Message)
        Write-Warning "BLOCKED: $Message"
    }
}

function Get-NormalizedRoot {
    param([Parameter(Mandatory = $true)] [string] $Path)

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    return [System.IO.Path]::GetFullPath($resolved).TrimEnd('\')
}

function Get-SafeRelativePath {
    param([Parameter(Mandatory = $true)] [string] $RelativePath)

    if ([string]::IsNullOrWhiteSpace($RelativePath) -or [System.IO.Path]::IsPathRooted($RelativePath)) {
        throw "Checkpoint path must be a non-empty relative path: '$RelativePath'"
    }

    $normalized = $RelativePath.Replace('/', '\')
    if ($normalized -match '[:*?"<>|]') {
        throw "Checkpoint path contains a wildcard, stream, or invalid Windows character: '$RelativePath'"
    }
    $segments = $normalized.Split([char]'\')
    if ($segments.Count -eq 0 -or $segments -contains '' -or $segments -contains '..' -or $segments -contains '.') {
        throw "Checkpoint path contains an unsafe segment: '$RelativePath'"
    }
    foreach ($segment in $segments) {
        if ($segment.EndsWith('.') -or $segment.EndsWith(' ') -or
            $segment -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?$') {
            throw "Checkpoint path contains an unsafe Windows name: '$RelativePath'"
        }
    }

    $blockedPrefixes = @(
        '.git\',
        'Build\',
        'Bin\',
        '.vs\',
        'vendor\cef3\cef\',
        'vendor\discord-rpc\discord\'
    )
    foreach ($prefix in $blockedPrefixes) {
        $directoryName = $prefix.TrimEnd('\')
        if ($normalized.Equals($directoryName, [System.StringComparison]::OrdinalIgnoreCase) -or
            $normalized.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Checkpoint path targets protected VM-local state: '$RelativePath'"
        }
    }
    if ($normalized.Equals('Shared\data\MTA San Andreas\MTA\cgui\unifont.ttf', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Checkpoint path targets generated VM-local Unifont data: '$RelativePath'"
    }
    foreach ($bootstrapInput in @(
        'utils\premake5.exe',
        'utils\buildactions\install_discord.lua',
        'utils\buildactions\install_cef.lua'
    )) {
        if ($normalized.Equals($bootstrapInput, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Checkpoint path targets a dependency bootstrap input: '$RelativePath'. Use the intentional dependency-upgrade/full-setup workflow."
        }
    }
    foreach ($protectedArchive in @(
        'vendor\cef3\temp.tar.bz2',
        'vendor\discord-rpc\discord-rpc.zip',
        'vendor\discord-rpc\rapidjson.zip'
    )) {
        if ($normalized.Equals($protectedArchive, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Checkpoint path targets a generated VM-local dependency archive: '$RelativePath'"
        }
    }

    return $normalized
}

function Get-ContainedPath {
    param(
        [Parameter(Mandatory = $true)] [string] $Root,
        [Parameter(Mandatory = $true)] [string] $RelativePath
    )

    $candidate = [System.IO.Path]::GetFullPath((Join-Path $Root $RelativePath))
    $prefix = $Root.TrimEnd('\') + '\'
    if (-not $candidate.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path escapes its root: '$RelativePath'"
    }
    return $candidate
}

function Assert-NoReparseDescendant {
    param(
        [Parameter(Mandatory = $true)] [string] $Root,
        [Parameter(Mandatory = $true)] [string] $Path
    )

    $current = $Path
    while (-not $current.Equals($Root, [System.StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Path crosses a reparse point below '$Root': '$current'"
            }
        }
        $parent = Split-Path -Parent $current
        if ([string]::IsNullOrWhiteSpace($parent) -or $parent.Equals($current, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Could not walk path back to its root: '$Path'"
        }
        $current = $parent
    }
}

function Assert-DirectoryNotReparse {
    param([Parameter(Mandatory = $true)] [string] $Path)

    $item = Get-Item -LiteralPath $Path -Force
    if (-not $item.PSIsContainer -or ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Expected a regular directory, not a file or reparse point: '$Path'"
    }
}

function Test-GeneratedProjectMembership {
    param([Parameter(Mandatory = $true)] [string] $RelativePath)

    $needle = '..\' + $RelativePath
    $projectRoot = Join-Path $script:TargetRootNormalized 'Build'
    foreach ($projectFile in @(Get-ChildItem -LiteralPath $projectRoot -Filter '*.vcxproj' -File -ErrorAction Stop)) {
        $contents = [System.IO.File]::ReadAllText($projectFile.FullName)
        if ($contents.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            return $true
        }
    }
    return $false
}

function Invoke-CheckedTool {
    param(
        [Parameter(Mandatory = $true)] [string] $Executable,
        [Parameter(Mandatory = $true)] [string[]] $Arguments,
        [Parameter(Mandatory = $true)] [string] $Description
    )

    if ($DryRun) {
        Write-Step "DRY RUN: $Description"
        Write-Host "  $Executable $($Arguments -join ' ')"
        return
    }

    Write-Step $Description
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
}

function Test-AllMarkers {
    param([Parameter(Mandatory = $true)] [string[]] $Markers)

    foreach ($marker in $Markers) {
        if (-not (Test-Path -LiteralPath (Join-Path $script:TargetRootNormalized $marker) -PathType Leaf)) {
            return $false
        }
    }
    return $true
}

function Assert-BootstrapInputMatchesCanonical {
    param([Parameter(Mandatory = $true)] [string] $RelativePath)

    $sourcePath = Get-ContainedPath -Root $script:SourceRootNormalized -RelativePath $RelativePath
    $targetPath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath $RelativePath
    Assert-NoReparseDescendant -Root $script:SourceRootNormalized -Path $sourcePath
    Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path $targetPath
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $targetPath -PathType Leaf)) {
        throw "Dependency bootstrap input is missing canonically or in the VM: '$RelativePath'"
    }
    $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    $targetHash = (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash
    if ($sourceHash -ne $targetHash) {
        throw "Dependency bootstrap input differs between canonical and VM trees: '$RelativePath'. Use the intentional dependency-upgrade/full-setup workflow."
    }
}

function Ensure-GeneratedDependency {
    param(
        [Parameter(Mandatory = $true)] [string] $Name,
        [Parameter(Mandatory = $true)] [string] $Directory,
        [Parameter(Mandatory = $true)] [string[]] $Markers,
        [Parameter(Mandatory = $true)] [string] $InstallAction
    )

    if (Test-AllMarkers -Markers $Markers) {
        Write-Step "$Name dependency markers are present"
        return
    }

    if (-not $BootstrapDependencies) {
        Add-PlanBlocker "$Name dependency is missing. Re-run with -BootstrapDependencies to invoke pinned action '$InstallAction'."
        return
    }

    $dependencyDirectory = Join-Path $script:TargetRootNormalized $Directory
    if (Test-Path -LiteralPath $dependencyDirectory) {
        Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path $dependencyDirectory
        Assert-DirectoryNotReparse -Path $dependencyDirectory
        $backupRelative = 'Build\dependency-quarantine\{0}-{1}\{2}' -f (
            (Get-Date -Format 'yyyyMMdd-HHmmss'),
            [Guid]::NewGuid().ToString('N'),
            [System.IO.Path]::GetFileName($Directory)
        )
        $backupPath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath $backupRelative
        Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path $backupPath
        if ($DryRun) {
            Write-Step "DRY RUN: quarantine partial $Name dependency at '$backupPath'"
        }
        else {
            Write-Step "Quarantining partial $Name dependency at '$backupPath'"
            $backupParent = Split-Path -Parent $backupPath
            New-Item -ItemType Directory -Force -Path $backupParent | Out-Null
            Move-Item -LiteralPath $dependencyDirectory -Destination $backupPath
        }
    }

    $premake = Join-Path $script:TargetRootNormalized 'utils\premake5.exe'
    if (-not (Test-Path -LiteralPath $premake -PathType Leaf)) {
        throw "Premake is missing from the VM build tree: '$premake'"
    }
    Invoke-CheckedTool -Executable $premake -Arguments @($InstallAction) -Description "Installing pinned $Name dependency"

    if (-not $DryRun -and -not (Test-AllMarkers -Markers $Markers)) {
        throw "$Name dependency markers are still missing after installation"
    }
}

function Assert-ProcessesClosed {
    param(
        [Parameter(Mandatory = $true)] [string[]] $Names,
        [Parameter(Mandatory = $true)] [string] $Purpose
    )

    $running = @()
    foreach ($name in $Names) {
        $running += @(Get-Process -Name $name -ErrorAction SilentlyContinue)
    }
    if ($running.Count -gt 0) {
        $descriptions = ($running | ForEach-Object { "$($_.ProcessName)($($_.Id))" }) -join ', '
        $message = "Close $Purpose before building; output files may be locked: $descriptions"
        Add-PlanBlocker $message
    }
}

function Assert-OutputUnlocked {
    param([Parameter(Mandatory = $true)] [string] $Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return
    }
    try {
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
        $stream.Dispose()
    }
    catch {
        Add-PlanBlocker "Build output is locked: '$Path'. Close the owning process before synchronization/build."
    }
}

if ($env:OS -ne 'Windows_NT') {
    throw 'utils/vm-build.ps1 must run inside Windows, not on the macOS host'
}

$expectedSourceRoot = 'C:\Mac\Home\Documents\GitHub\mtasa-neon'
$expectedTargetRoot = 'C:\dev\mtasa-vm-custom'
$script:SourceRootNormalized = Get-NormalizedRoot -Path (Split-Path -Parent $PSScriptRoot)
$script:TargetRootNormalized = Get-NormalizedRoot -Path $expectedTargetRoot

if (-not $script:SourceRootNormalized.Equals($expectedSourceRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Run the canonical shared script at '$expectedSourceRoot\utils\vm-build.ps1'; actual root is '$script:SourceRootNormalized'"
}
foreach ($marker in @('AGENTS.md', 'premake5.lua', '.git')) {
    if (-not (Test-Path -LiteralPath (Join-Path $script:SourceRootNormalized $marker))) {
        throw "Canonical source marker is missing: '$marker'"
    }
}
foreach ($marker in @('AGENTS.md', 'premake5.lua', 'utils\premake5.exe', 'Build')) {
    if (-not (Test-Path -LiteralPath (Join-Path $script:TargetRootNormalized $marker))) {
        throw "VM build-tree marker is missing: '$marker'"
    }
}

Assert-DirectoryNotReparse -Path $script:SourceRootNormalized
Assert-DirectoryNotReparse -Path $script:TargetRootNormalized
Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path (Join-Path $script:TargetRootNormalized 'Build')

# A global mutex keeps the plan, synchronization, regeneration, and selected
# builds in one exclusive transaction, including invocations from other shells.
$runMutex = New-Object System.Threading.Mutex($false, 'Global\MTASANeonVmBuild')
$runMutexAcquired = $false
try {
    try {
        $runMutexAcquired = $runMutex.WaitOne(0)
    }
    catch [System.Threading.AbandonedMutexException] {
        $runMutexAcquired = $true
    }
    if (-not $runMutexAcquired) {
        throw 'Another utils\vm-build.ps1 transaction is already running. Wait for it to finish before retrying.'
    }

if ($Files.Count -eq 0 -and $DeleteFiles.Count -eq 0 -and $ClientProjects.Count -eq 0 -and $ServerProjects.Count -eq 0 -and -not $Regenerate) {
    throw 'No action requested. Specify explicit files, projects, or -Regenerate.'
}

$hasProjects = $ClientProjects.Count -gt 0 -or $ServerProjects.Count -gt 0
if ($BuildOnly -and ($Files.Count -gt 0 -or $DeleteFiles.Count -gt 0 -or $Regenerate)) {
    throw '-BuildOnly cannot be combined with file synchronization, deletion, or regeneration.'
}
if ($hasProjects -and $Files.Count -eq 0 -and $DeleteFiles.Count -eq 0 -and -not $Regenerate -and -not $BuildOnly) {
    throw 'A project-only retry requires explicit -BuildOnly.'
}

if ($ClientProjects.Count -gt 0) {
    Assert-ProcessesClosed -Names @('gta_sa', 'Multi Theft Auto') -Purpose 'MTA and GTA'
}
if ($ServerProjects.Count -gt 0) {
    Assert-ProcessesClosed -Names @('MTA Server64') -Purpose 'the MTA server'
}

$clientOutputMap = @{
    'Game SA' = 'Bin\MTA\game_sa.dll'
    'Client Core' = 'Bin\MTA\core.dll'
    'Client Deathmatch' = 'Bin\mods\deathmatch\client.dll'
    'Client Launcher' = 'Bin\Multi Theft Auto.exe'
    'Client Webbrowser' = 'Bin\MTA\cefweb.dll'
    'Multiplayer SA' = 'Bin\MTA\multiplayer_sa.dll'
}
$serverOutputMap = @{
    'Deathmatch' = 'Bin\server\x64\deathmatch.dll'
    'Core' = 'Bin\server\x64\core.dll'
    'Launcher' = 'Bin\server\MTA Server64.exe'
    'XML' = 'Bin\server\x64\xmll.dll'
}
$discordMarkers = @(
    'vendor\discord-rpc\discord\include\discord_rpc.h',
    'vendor\discord-rpc\discord\src\discord_rpc.cpp',
    'vendor\discord-rpc\discord\src\rpc_connection.cpp',
    'vendor\discord-rpc\discord\src\serialization.cpp',
    'vendor\discord-rpc\discord\src\connection_win.cpp',
    'vendor\discord-rpc\discord\src\discord_register_win.cpp',
    'vendor\discord-rpc\discord\thirdparty\rapidjson\include\rapidjson\document.h'
)
$cefMarkers = @(
    'vendor\cef3\cef\include\cef_version.h',
    'vendor\cef3\cef\libcef_dll\wrapper\cef_helpers.cc',
    'vendor\cef3\cef\Release\libcef.lib',
    'vendor\cef3\cef\Resources\icudtl.dat'
)
$cefProjects = @('CEF', 'CEFLauncher', 'CEFLauncher DLL', 'Client Webbrowser')
$needsDiscord = $Regenerate -or ($ClientProjects -contains 'Client Core')
$needsCef = $Regenerate
foreach ($project in $ClientProjects) {
    if ($cefProjects -contains $project) {
        $needsCef = $true
    }
}
foreach ($project in $ClientProjects) {
    if ($clientOutputMap.ContainsKey($project)) {
        Assert-OutputUnlocked -Path (Join-Path $script:TargetRootNormalized $clientOutputMap[$project])
    }
}
foreach ($project in $ServerProjects) {
    if ($serverOutputMap.ContainsKey($project)) {
        Assert-OutputUnlocked -Path (Join-Path $script:TargetRootNormalized $serverOutputMap[$project])
    }
}

$copyPaths = @{}
foreach ($relative in $Files) {
    $safe = Get-SafeRelativePath -RelativePath $relative
    if ($copyPaths.ContainsKey($safe)) {
        throw "Duplicate checkpoint copy path: '$safe'"
    }
    $copyPaths[$safe] = $true
}

$deletePaths = @{}
foreach ($relative in $DeleteFiles) {
    $safe = Get-SafeRelativePath -RelativePath $relative
    if ($deletePaths.ContainsKey($safe)) {
        throw "Duplicate checkpoint deletion path: '$safe'"
    }
    if ($copyPaths.ContainsKey($safe)) {
        throw "Path cannot be copied and deleted in one invocation: '$safe'"
    }
    $deletePaths[$safe] = $true
}

$compiledSourceExtensions = @('.c', '.cc', '.cpp', '.cxx', '.asm', '.rc')
$regenerationReasons = @()
$plannedSourceHashes = @{}
foreach ($relative in $copyPaths.Keys) {
    $sourcePath = Get-ContainedPath -Root $script:SourceRootNormalized -RelativePath $relative
    $targetPath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath $relative
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Canonical checkpoint file is missing: '$relative'"
    }

    Assert-NoReparseDescendant -Root $script:SourceRootNormalized -Path $sourcePath
    Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path $targetPath
    $sourceItem = Get-Item -LiteralPath $sourcePath -Force
    if (($sourceItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Canonical checkpoint file is a reparse point: '$relative'"
    }
    if (Test-Path -LiteralPath $targetPath) {
        if (-not (Test-Path -LiteralPath $targetPath -PathType Leaf)) {
            throw "VM checkpoint target is not a regular file: '$relative'"
        }
        $targetItem = Get-Item -LiteralPath $targetPath -Force
        if (($targetItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "VM checkpoint target is a reparse point: '$relative'"
        }
    }

    $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    $plannedSourceHashes[$relative] = $sourceHash
    $targetExists = Test-Path -LiteralPath $targetPath -PathType Leaf
    $contentDiffers = -not $targetExists
    if ($targetExists) {
        $contentDiffers = $sourceHash -ne (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash
    }
    if ($contentDiffers -and [System.IO.Path]::GetFileName($relative).Equals('premake5.lua', [System.StringComparison]::OrdinalIgnoreCase)) {
        $regenerationReasons += "changed build definition '$relative'"
    }
    if ($compiledSourceExtensions -contains [System.IO.Path]::GetExtension($relative).ToLowerInvariant() -and
        -not (Test-GeneratedProjectMembership -RelativePath $relative)) {
        $regenerationReasons += "compiled source is absent from generated projects '$relative'"
    }
}
foreach ($relative in $deletePaths.Keys) {
    $sourcePath = Get-ContainedPath -Root $script:SourceRootNormalized -RelativePath $relative
    $targetPath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath $relative
    Assert-NoReparseDescendant -Root $script:SourceRootNormalized -Path $sourcePath
    Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path $targetPath
    if (Test-Path -LiteralPath $sourcePath) {
        throw "DeleteFiles path still exists in the canonical tree: '$relative'"
    }
    if ((Test-Path -LiteralPath $targetPath) -and -not (Test-Path -LiteralPath $targetPath -PathType Leaf)) {
        throw "DeleteFiles accepts files only, not directories: '$relative'"
    }
    if ($compiledSourceExtensions -contains [System.IO.Path]::GetExtension($relative).ToLowerInvariant()) {
        $regenerationReasons += "deleted compiled source '$relative'"
    }
    if ([System.IO.Path]::GetFileName($relative).Equals('premake5.lua', [System.StringComparison]::OrdinalIgnoreCase)) {
        $regenerationReasons += "deleted build definition '$relative'"
    }
}
if ($regenerationReasons.Count -gt 0 -and -not $Regenerate) {
    throw "Project regeneration is required: $($regenerationReasons -join '; '). Re-run with -Regenerate."
}

$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
if ($hasProjects -and -not (Test-Path -LiteralPath $msbuild -PathType Leaf)) {
    $vswhere = Join-Path $script:TargetRootNormalized 'utils\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'MSBuild and the repository vswhere utility are both unavailable'
    }
    $msbuild = @(& $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1)[0]
    if ([string]::IsNullOrWhiteSpace($msbuild) -or -not (Test-Path -LiteralPath $msbuild -PathType Leaf)) {
        throw 'Could not locate MSBuild'
    }
}
if (-not $Regenerate) {
    foreach ($project in @($ClientProjects) + @($ServerProjects)) {
        $projectPath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath ("Build\$project.vcxproj")
        if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
            Add-PlanBlocker "Generated project is missing: '$projectPath'. Re-run with -Regenerate."
        }
    }
}

# Dependency installation is an explicit, pinned bootstrap step. Do it before
# checkpoint source mutation so an unavailable prerequisite fails early.
Push-Location $script:TargetRootNormalized
try {
    if ($needsDiscord) {
        if ($BootstrapDependencies -and -not (Test-AllMarkers -Markers $discordMarkers)) {
            Assert-BootstrapInputMatchesCanonical -RelativePath 'utils\premake5.exe'
            Assert-BootstrapInputMatchesCanonical -RelativePath 'utils\buildactions\install_discord.lua'
        }
        Ensure-GeneratedDependency -Name 'Discord/RapidJSON' -Directory 'vendor\discord-rpc\discord' -Markers $discordMarkers -InstallAction 'install_discord'
    }
    if ($needsCef) {
        if ($BootstrapDependencies -and -not (Test-AllMarkers -Markers $cefMarkers)) {
            Assert-BootstrapInputMatchesCanonical -RelativePath 'utils\premake5.exe'
            Assert-BootstrapInputMatchesCanonical -RelativePath 'utils\buildactions\install_cef.lua'
        }
        Ensure-GeneratedDependency -Name 'CEF' -Directory 'vendor\cef3\cef' -Markers $cefMarkers -InstallAction 'install_cef'
    }
}
finally {
    Pop-Location
}

foreach ($relative in $copyPaths.Keys) {
    $sourcePath = Get-ContainedPath -Root $script:SourceRootNormalized -RelativePath $relative
    $targetPath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath $relative
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Canonical checkpoint file is missing: '$relative'"
    }

    Assert-NoReparseDescendant -Root $script:SourceRootNormalized -Path $sourcePath
    Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path $targetPath
    $sourceItem = Get-Item -LiteralPath $sourcePath
    if (($sourceItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Canonical checkpoint file is a reparse point: '$relative'"
    }

    $sourceHashBefore = $plannedSourceHashes[$relative]
    $currentSourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    if ($currentSourceHash -ne $sourceHashBefore) {
        throw "Canonical checkpoint source changed after planning: '$relative'"
    }
    if (Test-Path -LiteralPath $targetPath -PathType Leaf) {
        $existingHash = (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash
        if ($sourceHashBefore -eq $existingHash) {
            Write-Step "Already synchronized: '$relative'"
            continue
        }
    }

    if ($DryRun) {
        Write-Step "PLAN: copy and verify '$relative' (SHA256 $sourceHashBefore)"
        continue
    }

    $targetDirectory = Split-Path -Parent $targetPath
    if (-not (Test-Path -LiteralPath $targetDirectory)) {
        New-Item -ItemType Directory -Force -Path $targetDirectory | Out-Null
    }
    $temporaryPath = $targetPath + '.vm-build.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    $replacementBackup = $null
    try {
        Copy-Item -LiteralPath $sourcePath -Destination $temporaryPath
        $temporaryHash = (Get-FileHash -LiteralPath $temporaryPath -Algorithm SHA256).Hash
        $sourceHashAfter = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
        if ($sourceHashBefore -ne $sourceHashAfter -or $sourceHashBefore -ne $temporaryHash) {
            throw "Canonical source changed or copied inconsistently during synchronization: '$relative'"
        }

        if (Test-Path -LiteralPath $targetPath) {
            if (-not (Test-Path -LiteralPath $targetPath -PathType Leaf)) {
                throw "VM checkpoint target is not a regular file: '$relative'"
            }
            $targetItem = Get-Item -LiteralPath $targetPath -Force
            if (($targetItem.Attributes -band [System.IO.FileAttributes]::ReadOnly) -ne 0 -or
                ($targetItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "VM checkpoint target is read-only or a reparse point: '$relative'"
            }
            $replacementBackup = $targetPath + '.vm-build-backup.' + [Guid]::NewGuid().ToString('N') + '.tmp'
            [System.IO.File]::Replace($temporaryPath, $targetPath, $replacementBackup, $true)
        }
        else {
            [System.IO.File]::Move($temporaryPath, $targetPath)
        }
        $targetHash = (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash
        if ($sourceHashBefore -ne $targetHash) {
            throw "VM copy differs from canonical source: '$relative'"
        }
        if ($replacementBackup -and (Test-Path -LiteralPath $replacementBackup)) {
            Remove-Item -LiteralPath $replacementBackup -Force
            $replacementBackup = $null
        }
    }
    finally {
        if (Test-Path -LiteralPath $temporaryPath) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
        if ($replacementBackup -and (Test-Path -LiteralPath $replacementBackup)) {
            Write-Warning "Replacement backup retained after a failed copy: '$replacementBackup'"
        }
    }

    # Parallels may expose a source timestamp older than an existing object.
    # Touch only the explicitly selected, hash-verified target file.
    (Get-Item -LiteralPath $targetPath).LastWriteTimeUtc = [DateTime]::UtcNow
    Write-Step "Copied and verified '$relative'"

    if ($relative.StartsWith('test-resources\', [System.StringComparison]::OrdinalIgnoreCase)) {
        Write-Warning "'$relative' was copied into VM source only; runtime resource deployment is intentionally separate."
    }
}

if ($deletePaths.Count -gt 0) {
    $quarantineRelativeRoot = 'Build\sync-quarantine\{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), [Guid]::NewGuid().ToString('N')
    $quarantineRoot = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath $quarantineRelativeRoot
    Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path $quarantineRoot
    foreach ($relative in $deletePaths.Keys) {
        $targetPath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath $relative
        Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path $targetPath
        if (-not (Test-Path -LiteralPath $targetPath)) {
            Write-Step "Deletion target is already absent: '$relative'"
            continue
        }
        if (-not (Test-Path -LiteralPath $targetPath -PathType Leaf)) {
            throw "DeleteFiles accepts files only, not directories: '$relative'"
        }
        $originalHash = (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash

        $quarantinePath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath ($quarantineRelativeRoot + '\' + $relative)
        Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path $quarantinePath
        if ($DryRun) {
            Write-Step "PLAN: move '$relative' to '$quarantinePath'"
            continue
        }

        $quarantineDirectory = Split-Path -Parent $quarantinePath
        if (-not (Test-Path -LiteralPath $quarantineDirectory)) {
            New-Item -ItemType Directory -Force -Path $quarantineDirectory | Out-Null
        }
        Move-Item -LiteralPath $targetPath -Destination $quarantinePath
        $quarantineHash = (Get-FileHash -LiteralPath $quarantinePath -Algorithm SHA256).Hash
        if ($originalHash -ne $quarantineHash) {
            throw "Quarantined file hash differs from its original: '$relative'"
        }
        Write-Step "Moved deleted checkpoint file '$relative' to quarantine '$quarantinePath' (SHA256 $originalHash)"
    }
}

# Revalidate the entire explicit checkpoint immediately before regeneration or
# build. This prevents one transaction from compiling a mixed source snapshot.
foreach ($relative in $copyPaths.Keys) {
    $sourcePath = Get-ContainedPath -Root $script:SourceRootNormalized -RelativePath $relative
    Assert-NoReparseDescendant -Root $script:SourceRootNormalized -Path $sourcePath
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf) -or
        (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -ne $plannedSourceHashes[$relative]) {
        throw "Canonical checkpoint source changed before regeneration/build: '$relative'"
    }
    if (-not $DryRun) {
        $targetPath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath $relative
        Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path $targetPath
        if (-not (Test-Path -LiteralPath $targetPath -PathType Leaf) -or
            (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash -ne $plannedSourceHashes[$relative]) {
            throw "VM checkpoint target changed before regeneration/build: '$relative'"
        }
    }
}
if (-not $DryRun) {
    foreach ($relative in $deletePaths.Keys) {
        $sourcePath = Get-ContainedPath -Root $script:SourceRootNormalized -RelativePath $relative
        $targetPath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath $relative
        Assert-NoReparseDescendant -Root $script:SourceRootNormalized -Path $sourcePath
        Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path $targetPath
        if ((Test-Path -LiteralPath $sourcePath) -or (Test-Path -LiteralPath $targetPath)) {
            throw "Deleted checkpoint path reappeared before regeneration/build: '$relative'"
        }
    }
}

Push-Location $script:TargetRootNormalized
try {
    if ($Regenerate) {
        $premake = Join-Path $script:TargetRootNormalized 'utils\premake5.exe'
        Invoke-CheckedTool -Executable $premake -Arguments @('vs2026') -Description 'Regenerating Visual Studio projects'
    }

    if ($ClientProjects.Count -gt 0) {
        Assert-ProcessesClosed -Names @('gta_sa', 'Multi Theft Auto') -Purpose 'MTA and GTA'
    }
    if ($ServerProjects.Count -gt 0) {
        Assert-ProcessesClosed -Names @('MTA Server64') -Purpose 'the MTA server'
    }

    foreach ($project in $ClientProjects) {
        $projectPath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath ("Build\$project.vcxproj")
        if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf) -and -not ($DryRun -and $Regenerate)) {
            throw "Generated client project is missing: '$projectPath'"
        }
        if ($clientOutputMap.ContainsKey($project)) {
            Assert-OutputUnlocked -Path (Join-Path $script:TargetRootNormalized $clientOutputMap[$project])
        }
        Invoke-CheckedTool -Executable $msbuild -Arguments @($projectPath, '/t:Build', '/m', '/p:Configuration=Release', '/p:Platform=Win32') -Description "Building client project '$project' (Release|Win32)"
        if (-not $DryRun -and $clientOutputMap.ContainsKey($project)) {
            $outputPath = Join-Path $script:TargetRootNormalized $clientOutputMap[$project]
            if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
                throw "Expected client output is missing after a successful build: '$outputPath'"
            }
            Write-Step "Output: '$outputPath' ($((Get-Item -LiteralPath $outputPath).LastWriteTime.ToString('s')))"
        }
    }

    foreach ($project in $ServerProjects) {
        $projectPath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath ("Build\$project.vcxproj")
        if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf) -and -not ($DryRun -and $Regenerate)) {
            throw "Generated server project is missing: '$projectPath'"
        }
        if ($serverOutputMap.ContainsKey($project)) {
            Assert-OutputUnlocked -Path (Join-Path $script:TargetRootNormalized $serverOutputMap[$project])
        }
        Invoke-CheckedTool -Executable $msbuild -Arguments @($projectPath, '/t:Build', '/m', '/p:Configuration=Release', '/p:Platform=x64') -Description "Building server project '$project' (Release|x64)"
        if (-not $DryRun -and $serverOutputMap.ContainsKey($project)) {
            $outputPath = Join-Path $script:TargetRootNormalized $serverOutputMap[$project]
            if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
                throw "Expected server output is missing after a successful build: '$outputPath'"
            }
            Write-Step "Output: '$outputPath' ($((Get-Item -LiteralPath $outputPath).LastWriteTime.ToString('s')))"
        }
    }
}
finally {
    Pop-Location
}

# External editors are not covered by the VM transaction mutex. Recheck after
# the potentially long build so success never describes a stale checkpoint.
if (-not $DryRun) {
    foreach ($relative in $copyPaths.Keys) {
        $sourcePath = Get-ContainedPath -Root $script:SourceRootNormalized -RelativePath $relative
        $targetPath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath $relative
        Assert-NoReparseDescendant -Root $script:SourceRootNormalized -Path $sourcePath
        Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path $targetPath
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf) -or
            (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -ne $plannedSourceHashes[$relative] -or
            -not (Test-Path -LiteralPath $targetPath -PathType Leaf) -or
            (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash -ne $plannedSourceHashes[$relative]) {
            throw "Transaction completed against a stale checkpoint: '$relative' changed during regeneration/build."
        }
    }
    foreach ($relative in $deletePaths.Keys) {
        $sourcePath = Get-ContainedPath -Root $script:SourceRootNormalized -RelativePath $relative
        $targetPath = Get-ContainedPath -Root $script:TargetRootNormalized -RelativePath $relative
        Assert-NoReparseDescendant -Root $script:SourceRootNormalized -Path $sourcePath
        Assert-NoReparseDescendant -Root $script:TargetRootNormalized -Path $targetPath
        if ((Test-Path -LiteralPath $sourcePath) -or (Test-Path -LiteralPath $targetPath)) {
            throw "Transaction completed against a stale checkpoint: deleted path '$relative' reappeared during regeneration/build."
        }
    }
}

if ($DryRun -and $script:PlanBlockers.Count -gt 0) {
    Write-Host ''
    Write-Warning "Plan is blocked by $($script:PlanBlockers.Count) prerequisite(s):"
    foreach ($blocker in $script:PlanBlockers) {
        Write-Host "  - $blocker"
    }
    throw 'Plan is blocked; resolve the listed prerequisites before using -Execute.'
}
elseif ($DryRun) {
    Write-Step 'Plan completed successfully; review it, then rerun with -Execute'
}
else {
    Write-Step 'Completed successfully'
}
}
finally {
    if ($runMutexAcquired) {
        $runMutex.ReleaseMutex()
    }
    $runMutex.Dispose()
}
