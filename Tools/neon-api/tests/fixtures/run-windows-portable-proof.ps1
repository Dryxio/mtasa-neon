param(
    [Parameter(Mandatory = $true)]
    [string]$ZipPath,

    [string]$WorkRoot = 'C:\dev\Neon CLI Checkpoint J'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$resolvedWorkRoot = [IO.Path]::GetFullPath($WorkRoot).TrimEnd('\')
$approvedParent = [IO.Path]::GetFullPath('C:\dev').TrimEnd('\') + '\'
if (-not $resolvedWorkRoot.StartsWith($approvedParent, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'WorkRoot must be a dedicated child of C:\dev'
}
$WorkRoot = $resolvedWorkRoot

function Invoke-NeonJson {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Launcher,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $text = & $Launcher @Arguments 2>&1 | Out-String
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Neon command failed ($exitCode): $($Arguments -join ' ')`n$text"
    }
    return ($text | ConvertFrom-Json)
}

$resolvedZip = (Resolve-Path -LiteralPath $ZipPath).Path
$sidecarPath = "$resolvedZip.sha256"
if (-not (Test-Path -LiteralPath $sidecarPath -PathType Leaf)) {
    throw 'Portable ZIP checksum sidecar is missing'
}
$expectedHash = ((Get-Content -LiteralPath $sidecarPath -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
$actualHash = (Get-FileHash -LiteralPath $resolvedZip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $expectedHash) {
    throw 'Portable ZIP does not match its SHA-256 sidecar'
}
if (Test-Path -LiteralPath $WorkRoot) {
    Remove-Item -LiteralPath $WorkRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $WorkRoot | Out-Null
Expand-Archive -LiteralPath $resolvedZip -DestinationPath $WorkRoot

$packageRoot = Join-Path $WorkRoot 'neon-cli'
$launcher = Join-Path $packageRoot 'neon.cmd'
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    throw "Portable archive did not contain neon.cmd"
}

# Reproduce the reported machine shape: `python` is unusable, while the Windows
# Python launcher owns a compatible 3.x interpreter. neon.cmd must skip the bad
# PATH entry and select `py -3` before importing any Neon module.
$oldPython = Join-Path $WorkRoot 'old-python-first-on-path'
New-Item -ItemType Directory -Path $oldPython | Out-Null
Set-Content -LiteralPath (Join-Path $oldPython 'python.cmd') -Encoding Ascii -Value '@exit /b 37'
$previousPath = $env:PATH
$env:PATH = "$oldPython;$previousPath"
try {
    $version = Invoke-NeonJson -Launcher $launcher -Arguments @('version', '--json')
    if ($version.status -ne 'pass' -or $version.mode -ne 'portable') {
        throw 'Portable version contract did not pass'
    }
    $versionParts = @($version.pythonVersion -split '\.' | ForEach-Object { [int]$_ })
    if ($versionParts[0] -lt 3 -or ($versionParts[0] -eq 3 -and $versionParts[1] -lt 10)) {
        throw "neon.cmd selected an unsupported Python: $($version.pythonVersion)"
    }

    $negativeText = & $launcher api get definitelyMissingEntity --json 2>&1 | Out-String
    $negativeExitCode = $LASTEXITCODE
    $negative = $negativeText | ConvertFrom-Json
    if ($negativeExitCode -eq 0 -or $negative.status -ne 'fail') {
        throw 'neon.cmd did not preserve a failing CLI exit code'
    }

    $selfTest = Invoke-NeonJson -Launcher $launcher -Arguments @('self-test', '--json')
    $harness = Invoke-NeonJson -Launcher $launcher -Arguments @('harness', '--json')
    if ($selfTest.status -ne 'pass' -or $harness.status -ne 'pass') {
        throw 'Portable self-test or harness did not pass'
    }

    # A workspace-controlled junction must never redirect catalogue writes
    # outside the initialized gamemode.
    $junctionWorkspace = Join-Path $WorkRoot 'Junction Rejection'
    $junctionResource = Join-Path $junctionWorkspace 'resources\demo'
    $junctionOutside = Join-Path $WorkRoot 'Junction Outside Sentinel'
    New-Item -ItemType Directory -Path $junctionResource | Out-Null
    New-Item -ItemType Directory -Path $junctionOutside | Out-Null
    [IO.File]::WriteAllText((Join-Path $junctionResource 'meta.xml'), "<meta><script src=`"server.lua`" type=`"server`" /></meta>`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $junctionResource 'server.lua'), "local marker = getTickCount()`n", [Text.UTF8Encoding]::new($false))
    $junctionPath = Join-Path $junctionWorkspace '.neon-tooling'
    & cmd.exe /d /c mklink /J $junctionPath $junctionOutside | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not create the Windows junction regression fixture'
    }
    Push-Location $junctionWorkspace
    try {
        $junctionText = & $launcher init --json 2>&1 | Out-String
        $junctionExitCode = $LASTEXITCODE
        $junctionResult = $junctionText | ConvertFrom-Json
    }
    finally {
        Pop-Location
    }
    if ($junctionExitCode -eq 0 -or $junctionResult.status -ne 'fail') {
        throw 'init accepted a Windows junction as its tooling directory'
    }
    if (Test-Path -LiteralPath (Join-Path $junctionOutside 'neon-api.json')) {
        throw 'init wrote its catalogue through a Windows junction'
    }
    & cmd.exe /d /c rmdir $junctionPath | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not remove the Windows junction regression fixture'
    }

    $workspace = Join-Path $WorkRoot 'Gamemode With Spaces'
    $resource = Join-Path $workspace 'resources\demo'
    New-Item -ItemType Directory -Path $resource | Out-Null
    [IO.File]::WriteAllText((Join-Path $resource 'meta.xml'), "<meta><script src=`"server.lua`" type=`"server`" /></meta>`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $resource 'server.lua'), "local vehicle = createVehicle(411, 0, 0, 3)`n", [Text.UTF8Encoding]::new($false))

    Push-Location $workspace
    try {
        $initialized = Invoke-NeonJson -Launcher $launcher -Arguments @('init', '--json')
        $checked = Invoke-NeonJson -Launcher $launcher -Arguments @('check', '--json')
        $generated = Invoke-NeonJson -Launcher $launcher -Arguments @('generate', 'project', '--json')
        $verified = Invoke-NeonJson -Launcher $launcher -Arguments @('context', 'verify', '--json')
        $search = Invoke-NeonJson -Launcher $launcher -Arguments @('api', 'search', 'spawn a car', '--side', 'server', '--kind', 'function', '--json')
    }
    finally {
        Pop-Location
    }
    foreach ($document in @($initialized, $checked, $generated, $verified, $search)) {
        if ($document.status -ne 'pass') {
            throw 'One clean extracted-workspace command did not pass'
        }
    }
    if ($checked.summary.resources -ne 1 -or $checked.summary.files -ne 1) {
        throw 'Initialized project did not index its real resource and Lua file'
    }
    if (-not ($search.symbols | Where-Object { $_.name -eq 'createVehicle' })) {
        throw 'Portable semantic discovery could not find createVehicle'
    }

    [ordered]@{
        schemaVersion = '1.0.0'
        command = 'windows.portable-proof'
        status = 'pass'
        pythonVersion = $version.pythonVersion
        packageMode = $version.mode
        selfTests = $selfTest.summary.tests
        harnessTests = $harness.summary.tests
        resources = $checked.summary.resources
        files = $checked.summary.files
        contextArtifacts = $verified.summary.artifacts
        discoveryMatches = $search.summary.matches
        pathWithSpaces = $true
        oldPythonFirstOnPath = $true
        failingExitCode = $negativeExitCode
        junctionRejected = $true
    } | ConvertTo-Json -Compress
}
finally {
    $env:PATH = $previousPath
}
