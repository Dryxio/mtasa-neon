param(
    [string] $ServerLog = 'C:\dev\mtasa-vm-custom\Bin\server\mods\deathmatch\logs\server.log',
    [string] $PrimaryClientLog = 'C:\dev\mtasa-vm-custom\Bin\MTA\logs\console.log',
    [string] $SecondaryClientLog = 'C:\dev\mtasa-vm-custom\Bin\MTA\logs\console-cl2.log',
    [string] $PrimaryPlayer = 'dryxio',
    [string] $SecondaryPlayer = '',
    [int] $TimeoutSeconds = 1200
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class NeonSweetAndKendlPhysicalInput
{
    [DllImport("user32.dll", SetLastError = true)]
    private static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

    private const uint KeyUp = 0x0002;
    private const uint ScanCode = 0x0008;

    public static void KeyDown(byte scanCode)
    {
        keybd_event(0, scanCode, ScanCode, UIntPtr.Zero);
    }

    public static void KeyUpEvent(byte scanCode)
    {
        keybd_event(0, scanCode, ScanCode | KeyUp, UIntPtr.Zero);
    }
}
'@

function Get-ClientProcesses
{
    $clients = @(Get-Process gta_sa -ErrorAction Stop | Sort-Object StartTime)
    if ($clients.Count -lt 1)
    {
        throw 'No running gta_sa.exe client was found.'
    }
    return $clients
}

function Invoke-PhysicalKey
{
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process] $Process,
        [Parameter(Mandatory = $true)]
        [byte] $ScanCode,
        [int] $HoldMilliseconds = 1200
    )

    $shell = New-Object -ComObject WScript.Shell
    if (-not $shell.AppActivate($Process.Id))
    {
        throw "Could not focus GTA client PID $($Process.Id)."
    }
    Start-Sleep -Milliseconds 250
    [NeonSweetAndKendlPhysicalInput]::KeyDown($ScanCode)
    try
    {
        Start-Sleep -Milliseconds $HoldMilliseconds
    }
    finally
    {
        [NeonSweetAndKendlPhysicalInput]::KeyUpEvent($ScanCode)
    }
}

function Set-ClientFocus
{
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process] $Process
    )

    $shell = New-Object -ComObject WScript.Shell
    if (-not $shell.AppActivate($Process.Id))
    {
        throw "Could not focus GTA client PID $($Process.Id)."
    }
    Start-Sleep -Milliseconds 250
}

function Resolve-ClientProcess
{
    param(
        [Parameter(Mandatory = $true)]
        [string] $Player,
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process[]] $Clients
    )

    if ($Player -eq $PrimaryPlayer)
    {
        return $Clients[0]
    }
    if ($SecondaryPlayer -and $Player -eq $SecondaryPlayer -and $Clients.Count -ge 2)
    {
        return $Clients[1]
    }
    if ($Clients.Count -eq 1)
    {
        return $Clients[0]
    }
    throw "No deterministic GTA process mapping for player '$Player'."
}

if (-not (Test-Path -LiteralPath $ServerLog -PathType Leaf))
{
    throw "Server log not found: $ServerLog"
}

$clients = Get-ClientProcesses
$logs = @($ServerLog, $PrimaryClientLog)
if ($SecondaryPlayer)
{
    $logs += $SecondaryClientLog
}
$lineCounts = @{}
foreach ($log in $logs)
{
    if (-not (Test-Path -LiteralPath $log -PathType Leaf))
    {
        throw "Runtime log not found: $log"
    }
    $lineCounts[$log] = @(Get-Content -LiteralPath $log).Count
}
$handledProbes = @{}
$startedAt = Get-Date

Write-Host '[sweet-and-kendl-transition-runner] Watching server and client logs'
Write-Host "[sweet-and-kendl-transition-runner] GTA PIDs: $(@($clients.Id) -join ', ')"

while (((Get-Date) - $startedAt).TotalSeconds -lt $TimeoutSeconds)
{
    foreach ($log in $logs)
    {
        $lines = @(Get-Content -LiteralPath $log)
        $previousCount = [int] $lineCounts[$log]
        if ($lines.Count -le $previousCount)
        {
            continue
        }
        foreach ($line in @($lines[$previousCount..($lines.Count - 1)]))
        {
            if ($line -match '\[sweet-and-kendl-transition-jsonl\]\s+(\[.*\])')
            {
                $records = @()
                try
                {
                    $records = @(ConvertFrom-Json -InputObject $Matches[1])
                }
                catch
                {
                    Write-Warning "Ignoring malformed transition JSONL: $line"
                }
                foreach ($record in $records)
                {
                    if ($record.event -ne 'INPUT_READY')
                    {
                        continue
                    }
                    $probeKey = "$($record.probeId):$($record.player):$($record.control)"
                    if ($handledProbes.ContainsKey($probeKey))
                    {
                        continue
                    }
                    $client = Resolve-ClientProcess -Player ([string] $record.player) -Clients $clients
                    Set-ClientFocus -Process $client
                    Write-Host "[sweet-and-kendl-transition-runner] probe=$probeKey PID=$($client.Id) raw-W then awaiting-ui-W"
                    Invoke-PhysicalKey -Process $client -ScanCode 0x11 -HoldMilliseconds 1200
                    $handledProbes[$probeKey] = $true
                }
            }
            if ($log -eq $ServerLog)
            {
                if ($line -match '\[sweet-and-kendl-transition\]\s+PASS\s+profile=([^\s]+)')
                {
                    Write-Host "[sweet-and-kendl-transition-runner] PASS profile=$($Matches[1])"
                    exit 0
                }
                if ($line -match '\[sweet-and-kendl-transition\]\s+FAIL\s+profile=([^\s]+)(.*)$')
                {
                    Write-Error "[sweet-and-kendl-transition-runner] FAIL profile=$($Matches[1])$($Matches[2])"
                    exit 1
                }
            }
        }
        $lineCounts[$log] = $lines.Count
    }
    Start-Sleep -Milliseconds 100
}

Write-Error "[sweet-and-kendl-transition-runner] FAIL: no terminal verdict within $TimeoutSeconds seconds"
exit 2
