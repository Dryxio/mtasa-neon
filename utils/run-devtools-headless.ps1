param(
    [string]$VmSourceRoot = 'C:\dev\mtasa-vm-custom',
    [int]$TimeoutSeconds = 240
)

$ErrorActionPreference = 'Stop'
$serverRoot = Join-Path $VmSourceRoot 'Bin\server'
$serverExe = Join-Path $serverRoot 'MTA Server64.exe'
$liveConfig = Join-Path $serverRoot 'mods\deathmatch\mtaserver.conf'
$resourceTarget = Join-Path $serverRoot 'mods\deathmatch\resources\neon-devtools-test'
$canonicalResource = 'C:\Mac\Home\Documents\GitHub\mtasa-neon\test-resources\neon-devtools-test'
$harnessRoot = Join-Path $VmSourceRoot 'Build\devtools-headless'
$configArgument = 'devtools-headless.conf'
$configPath = Join-Path $serverRoot ('mods\deathmatch\' + $configArgument)
$stdoutPath = Join-Path $harnessRoot 'stdout.log'
$stderrPath = Join-Path $harnessRoot 'stderr.log'

if (-not (Test-Path -LiteralPath $serverExe) -or -not (Test-Path -LiteralPath $liveConfig)) {
    throw 'Headless harness requires an installed x64 server and its normal configuration.'
}

New-Item -ItemType Directory -Path $harnessRoot -Force | Out-Null
New-Item -ItemType Directory -Path $resourceTarget -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $canonicalResource 'meta.xml') -Destination $resourceTarget -Force
Copy-Item -LiteralPath (Join-Path $canonicalResource 'server.lua') -Destination $resourceTarget -Force
Copy-Item -LiteralPath (Join-Path $canonicalResource 'client.lua') -Destination $resourceTarget -Force

[xml]$config = Get-Content -LiteralPath $liveConfig
$config.config.serverport = '22133'
$config.config.httpport = '22135'
$config.config.servername = 'Neon DevTools Headless Harness'
$config.config.resource | ForEach-Object { [void]$config.config.RemoveChild($_) }
$resource = $config.CreateElement('resource')
$resource.SetAttribute('src', 'neon-devtools-test')
$resource.SetAttribute('startup', '1')
$resource.SetAttribute('protected', '0')
[void]$config.config.AppendChild($resource)
$config.Save($configPath)

Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
$argumentLine = '-t -u --config "{0}"' -f $configArgument
$process = Start-Process -FilePath $serverExe -ArgumentList $argumentLine -WorkingDirectory $serverRoot -PassThru `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath

try {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $ready = $false
    while ([DateTime]::UtcNow -lt $deadline -and -not $process.HasExited) {
        Start-Sleep -Milliseconds 250
        $currentOutput = if (Test-Path -LiteralPath $stdoutPath) { Get-Content -LiteralPath $stdoutPath -Raw } else { '' }
        if ($null -ne $currentOutput -and $currentOutput.Contains('[NEON_DEVTOOLS_TEST] headless-ready')) {
            $ready = $true
            Start-Sleep -Seconds 2
            break
        }
    }
    $output = if (Test-Path -LiteralPath $stdoutPath) { Get-Content -LiteralPath $stdoutPath -Raw } else { '' }
    if (-not $ready) { throw "Server did not start the diagnostic resource. Output:`n$output" }
    if ($output -notmatch 'server\.lua' -or $output -notmatch '(?i)error' -or $output -notmatch '(?i)warning') {
        throw "The resource started, but real error/warning evidence is incomplete. Output:`n$output"
    }
    $udpReady = Get-NetUDPEndpoint -LocalPort 22133 -ErrorAction SilentlyContinue
    $httpReady = Get-NetTCPConnection -LocalPort 22135 -State Listen -ErrorAction SilentlyContinue
    if (-not $udpReady -or -not $httpReady) {
        throw 'The resource ran, but the isolated game/HTTP listeners were not both ready.'
    }
    Write-Host '[PASS] Actual server started the resource and emitted real Lua errors and native warnings.'
    Write-Host $stdoutPath
}
finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
}
