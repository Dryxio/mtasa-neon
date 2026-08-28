param(
    [ValidateSet('neon-pair', 'neon-multiclient')]
    [string] $Profile = 'neon-pair',
    [string] $Repository = 'C:\Mac\Home\Documents\GitHub\mtasa-neon',
    [string] $SourceBin = 'C:\dev\mtasa-vm-custom\Bin',
    [string] $Python = 'C:\Users\salimtrouve\AppData\Local\Programs\Python\Python314\python.exe',
    [int] $Port = 22133
)

$ErrorActionPreference = 'Stop'
$slug = if ($Profile -eq 'neon-pair') { 'pair' } else { 'multiclient' }
$testRoot = "C:\dev\neon-cli-proof-$slug-$Port"
if (Test-Path -LiteralPath $testRoot) {
    throw "Refusing to overwrite existing proof directory: $testRoot"
}
if (!(Test-Path -LiteralPath $Python -PathType Leaf)) {
    throw "Python 3.10 or newer is unavailable at $Python"
}

$tool = Join-Path $Repository 'Tools\neon-api\neon.py'
$serverRoot = Join-Path $testRoot 'server'
$workspace = Join-Path $testRoot 'workspace'
New-Item -ItemType Directory -Path $testRoot, $workspace | Out-Null
$sourceServer = Join-Path $SourceBin 'server'
$excludedResources = Join-Path $sourceServer 'mods\deathmatch\resources'
$excludedCache = Join-Path $sourceServer 'mods\deathmatch\resource-cache'
$excludedDisabled = Join-Path $sourceServer 'mods\deathmatch\resources-disabled'
$excludedBackups = Join-Path $sourceServer 'mods\deathmatch\backups'
$excludedLogs = Join-Path $sourceServer 'mods\deathmatch\logs'
& robocopy $sourceServer $serverRoot /E /XD $excludedResources $excludedCache $excludedDisabled $excludedBackups $excludedLogs /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -gt 7) {
    throw "Failed to create the isolated server runtime (robocopy exit $LASTEXITCODE)"
}
New-Item -ItemType Directory -Path (Join-Path $serverRoot 'mods\deathmatch\resources') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $Repository 'Tools\neon-api\neon-api.json') -Destination (Join-Path $workspace 'api.json')

$project = Get-Content -LiteralPath (Join-Path $Repository 'neon.project.json') -Raw | ConvertFrom-Json
$project.catalogue = 'api.json'
$project.profile = $Profile
$projectJson = $project | ConvertTo-Json -Depth 20
[IO.File]::WriteAllText((Join-Path $workspace 'neon.project.json'), $projectJson, (New-Object Text.UTF8Encoding($false)))

$configPath = Join-Path $serverRoot 'mods\deathmatch\mtaserver.conf'
$config = [IO.File]::ReadAllText($configPath)
$config = [regex]::Replace($config, '<serverport>[^<]+</serverport>', "<serverport>$Port</serverport>")
$config = [regex]::Replace($config, '<httpport>[^<]+</httpport>', '<httpport>' + ($Port + 2) + '</httpport>')
$config = [regex]::Replace($config, '<check_duplicate_serials>[^<]+</check_duplicate_serials>', '<check_duplicate_serials>0</check_duplicate_serials>')
if ($config -notmatch '<resource\s+src=["'']neon-agent-probe["'']') {
    $config = [regex]::Replace(
        $config,
        '</config>\s*$',
        "    <resource src=`"neon-agent-probe`" startup=`"1`" protected=`"0`" />`r`n</config>`r`n"
    )
}
[IO.File]::WriteAllText($configPath, $config, (New-Object Text.UTF8Encoding($false)))

function Invoke-NeonJson {
    param([string[]] $Arguments)
    $output = & $Python $tool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Neon command failed ($LASTEXITCODE): $output"
    }
    return ($output | ConvertFrom-Json)
}

function Wait-ServerLogPattern {
    param(
        [string] $Path,
        [string] $Pattern,
        [int] $TimeoutSeconds = 90
    )
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            try {
                $reader = $null
                $stream = New-Object IO.FileStream(
                    $Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
                    [IO.FileShare]::ReadWrite
                )
                try {
                    $reader = New-Object IO.StreamReader($stream)
                    $content = $reader.ReadToEnd()
                }
                finally {
                    if ($reader) { $reader.Dispose() }
                    else { $stream.Dispose() }
                }
                if ($content -match $Pattern) {
                    return
                }
            }
            catch [IO.IOException] {
                # The server may be rotating or reopening the log. Retry until
                # the same bounded deadline rather than treating this as JOIN.
            }
        }
        Start-Sleep -Milliseconds 500
    }
    throw "Server log did not match '$Pattern' within $TimeoutSeconds seconds"
}

$sessionRelative = $null
try {
    $install = Invoke-NeonJson @('runtime', 'probe', 'install', '--server-root', $serverRoot, '--json')
    $started = Invoke-NeonJson @(
        'supervisor', 'start', '--workspace', $workspace, '--project', 'neon.project.json',
        '--snapshot', 'runtime.json', '--output', 'sessions', '--ttl', '240',
        '--enable', 'resource.lifecycle', '--enable', 'client.launch',
        '--server-root', $serverRoot, '--client-root', $SourceBin,
        '--connect-port', $Port.ToString(), '--json'
    )
    $sessionRelative = $started.session.sessionPath
    Start-Sleep -Seconds 10
    $resource = Invoke-NeonJson @(
        'resource', 'start', $sessionRelative, 'neon-agent-probe',
        '--workspace', $workspace, '--timeout-ms', '10000', '--json'
    )
    Start-Sleep -Seconds 3
    $primary = Invoke-NeonJson @(
        'client', 'launch', $sessionRelative, 'client-1',
        '--workspace', $workspace, '--timeout-ms', '10000', '--json'
    )
    if ($Profile -eq 'neon-multiclient') {
        # MTA's secondary profile is reliable once the primary has completed
        # its loader and joined. A fixed sleep made the acceptance test depend
        # on VM load and occasionally raced the installation/startup guards.
        Wait-ServerLogPattern -Path (Join-Path $serverRoot 'mods\deathmatch\logs\server.log') -Pattern '\] JOIN: '
        $secondary = Invoke-NeonJson @(
            'client', 'launch', $sessionRelative, 'client-2',
            '--workspace', $workspace, '--timeout-ms', '10000', '--json'
        )
    }
    $proof = Invoke-NeonJson @(
        'runtime', 'prove', $sessionRelative, '--workspace', $workspace,
        '--timeout-ms', '120000', '--poll-ms', '500', '--json'
    )
    [ordered]@{
        status = 'pass'
        profile = $Profile
        testRoot = $testRoot
        install = $install
        resource = $resource
        primary = $primary
        secondary = $secondary
        proof = $proof
    } | ConvertTo-Json -Depth 30 -Compress
}
finally {
    if ($sessionRelative) {
        try {
            & $Python $tool supervisor stop $sessionRelative --workspace $workspace --json | Out-Null
        }
        catch {
            Write-Warning "Supervisor cleanup failed: $_"
        }
    }
}
