param(
    [ValidateSet('solo-natural', 'solo-skip', 'coop-natural', 'coop-skip',
                 'solo-native-natural', 'solo-native-skip', 'coop-native-natural', 'coop-native-skip')]
    [string] $Profile = 'coop-skip',
    [string] $ResourcePath = 'C:\dev\mtasa-vm-custom\Bin\server\mods\deathmatch\resources\tagging-up-turf',
    [string] $ServerLog = 'C:\dev\mtasa-vm-custom\Bin\server\mods\deathmatch\logs\server.log',
    [string] $PrimaryClientLog = 'C:\dev\mtasa-vm-custom\Bin\MTA\logs\clientscript.log',
    [string] $SecondaryClientLog = 'C:\dev\mtasa-vm-custom\Bin\MTA\logs\clientscript-cl2.log',
    [string] $PrimaryPlayer = 'dryxio',
    [string] $SecondaryPlayer = 'EvasivePanpipe6_CL2',
    [int] $TimeoutSeconds = 1200
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class NeonTaggingUpTurfPhysicalInput
{
    [DllImport("user32.dll", SetLastError = true)]
    private static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);

    private const uint KeyUp = 0x0002;
    private const uint ScanCode = 0x0008;
    private const uint LeftDown = 0x0002;
    private const uint LeftUp = 0x0004;
    private const uint RightDown = 0x0008;
    private const uint RightUp = 0x0010;
    private const uint MouseMove = 0x0001;

    public static void PressW(int holdMilliseconds)
    {
        keybd_event(0, 0x11, ScanCode, UIntPtr.Zero);
        System.Threading.Thread.Sleep(holdMilliseconds);
        keybd_event(0, 0x11, ScanCode | KeyUp, UIntPtr.Zero);
    }

    public static void PressFire(int holdMilliseconds)
    {
        mouse_event(LeftDown, 0, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(holdMilliseconds);
        mouse_event(LeftUp, 0, 0, 0, UIntPtr.Zero);
    }

    public static void BeginAim()
    {
        mouse_event(RightDown, 0, 0, 0, UIntPtr.Zero);
    }

    public static void BeginFire()
    {
        mouse_event(LeftDown, 0, 0, 0, UIntPtr.Zero);
    }

    public static void MoveAim(int verticalDelta)
    {
        mouse_event(MouseMove, 0, unchecked((uint)verticalDelta), 0, UIntPtr.Zero);
    }

    public static void EndAimFire()
    {
        mouse_event(LeftUp, 0, 0, 0, UIntPtr.Zero);
        mouse_event(RightUp, 0, 0, 0, UIntPtr.Zero);
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
    if ($Player -eq $SecondaryPlayer -and $Clients.Count -ge 2)
    {
        return $Clients[1]
    }
    if ($Clients.Count -eq 1)
    {
        return $Clients[0]
    }
    throw "No deterministic GTA process mapping for player '$Player'."
}

function Invoke-PhysicalInput
{
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process] $Process,
        [Parameter(Mandatory = $true)]
        [string] $Control
    )

    $shell = New-Object -ComObject WScript.Shell
    if (-not $shell.AppActivate($Process.Id))
    {
        throw "Could not focus GTA client PID $($Process.Id)."
    }
    Start-Sleep -Milliseconds 250
    if ($Control -eq 'fire')
    {
        [NeonTaggingUpTurfPhysicalInput]::PressFire(1200)
    }
    elseif ($Control -eq 'accelerate' -or $Control -eq 'forwards')
    {
        [NeonTaggingUpTurfPhysicalInput]::PressW(1200)
    }
    else
    {
        throw "Unsupported physical control '$Control'."
    }
}

function Focus-ClientProcess
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

$script:activeNativeSpray = $null

function Stop-NativeSprayInput
{
    if ($null -eq $script:activeNativeSpray)
    {
        [NeonTaggingUpTurfPhysicalInput]::EndAimFire()
        return
    }
    if ($script:activeNativeSpray.SweepPosition -ne 0)
    {
        [NeonTaggingUpTurfPhysicalInput]::MoveAim(-[int] $script:activeNativeSpray.SweepPosition)
    }
    [NeonTaggingUpTurfPhysicalInput]::EndAimFire()
    $script:activeNativeSpray = $null
}

$expectedPlayers = if ($Profile.StartsWith('coop-')) { 2 } else { 1 }
$nativeTags = $Profile.Contains('-native-')
$mode = if ($Profile.EndsWith('-skip')) { 'skip' } else { 'natural' }
$requestMode = if ($nativeTags) { "native-$mode" } else { $mode }
$requestName = "tagup-transition-$requestMode-$expectedPlayers.request"
$requestPath = Join-Path $ResourcePath $requestName

if (-not (Test-Path -LiteralPath $ResourcePath -PathType Container))
{
    throw "Deployed resource not found: $ResourcePath"
}
if (-not (Test-Path -LiteralPath $ServerLog -PathType Leaf))
{
    throw "Server log not found: $ServerLog"
}
$staleRequests = @(Get-ChildItem -LiteralPath $ResourcePath -Filter 'tagup-transition-*.request' -File -ErrorAction Stop)
if ($staleRequests.Count -gt 0)
{
    throw "Refusing to start while another transition request exists: $(@($staleRequests.Name) -join ', ')"
}

$clients = Get-ClientProcesses
if ($clients.Count -ne $expectedPlayers)
{
    throw "Profile $Profile requires exactly $expectedPlayers gta_sa.exe process(es); found $($clients.Count)."
}

$logs = @($ServerLog, $PrimaryClientLog)
if ($expectedPlayers -eq 2)
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
[IO.File]::WriteAllText($requestPath, "leader=$PrimaryPlayer`r`nprofile=$Profile`r`n")
Write-Host "[tagging-up-turf-transition-runner] Requested profile=$Profile file=$requestName"
Write-Host "[tagging-up-turf-transition-runner] GTA PIDs: $(@($clients.Id) -join ', ')"

try
{
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
                if ($line -match '\[tagging-up-turf-harness-jsonl\]\s+(\[.*\])')
                {
                    $records = @()
                    try
                    {
                        $records = @(ConvertFrom-Json -InputObject $Matches[1])
                    }
                    catch
                    {
                        Write-Warning "Ignoring malformed harness JSONL: $line"
                    }
                    foreach ($record in $records)
                    {
                        if ($record.event -eq 'SPRAY_AIM_READY' -and $record.player)
                        {
                            $sprayKey = "$($record.run):$($record.sprayId):$($record.tagId):$($record.player)"
                            if ($null -eq $script:activeNativeSpray -or $script:activeNativeSpray.Key -ne $sprayKey)
                            {
                                Stop-NativeSprayInput
                                $client = Resolve-ClientProcess -Player ([string] $record.player) -Clients $clients
                                Focus-ClientProcess -Process $client
                                [NeonTaggingUpTurfPhysicalInput]::BeginAim()
                                $script:activeNativeSpray = [PSCustomObject] @{
                                    Key = $sprayKey
                                    Run = [int] $record.run
                                    SprayId = [int] $record.sprayId
                                    TagId = [int] $record.tagId
                                    Player = [string] $record.player
                                    Process = $client
                                    FireDown = $false
                                    SweepPosition = 0
                                    SweepDirection = 1
                                    NextSweepAt = (Get-Date)
                                }
                                Write-Host "[tagging-up-turf-transition-runner] native-aim=$sprayKey PID=$($client.Id)"
                            }
                            continue
                        }
                        if ($record.event -eq 'SPRAY_FIRE_READY' -and $record.player)
                        {
                            $spray = $script:activeNativeSpray
                            if ($null -eq $spray -or $spray.Run -ne [int] $record.run -or $spray.SprayId -ne [int] $record.sprayId -or
                                $spray.TagId -ne [int] $record.tagId -or $spray.Player -ne [string] $record.player)
                            {
                                throw "Received SPRAY_FIRE_READY without the matching physical aim session."
                            }
                            if (-not $spray.FireDown)
                            {
                                [NeonTaggingUpTurfPhysicalInput]::BeginFire()
                                $spray.FireDown = $true
                                $spray.NextSweepAt = Get-Date
                                Write-Host "[tagging-up-turf-transition-runner] native-fire=$($spray.Key)"
                            }
                            continue
                        }
                        if ($record.event -eq 'NATIVE_TAG_COMPLETE')
                        {
                            $spray = $script:activeNativeSpray
                            if ($null -ne $spray -and $spray.Run -eq [int] $record.run -and $spray.SprayId -eq [int] $record.sprayId -and
                                $spray.TagId -eq [int] $record.tagId)
                            {
                                Write-Host "[tagging-up-turf-transition-runner] native-complete tag=$($spray.TagId)"
                                Stop-NativeSprayInput
                            }
                            continue
                        }
                        if ($record.event -eq 'SPRAY_INPUT_FAILURE')
                        {
                            Stop-NativeSprayInput
                        }
                        if ($record.event -ne 'INPUT_READY' -or -not $record.player)
                        {
                            continue
                        }
                        $probeKey = "$($record.run):$($record.probeId):$($record.player):$($record.control)"
                        if ($handledProbes.ContainsKey($probeKey))
                        {
                            continue
                        }
                        $client = Resolve-ClientProcess -Player ([string] $record.player) -Clients $clients
                        Write-Host "[tagging-up-turf-transition-runner] probe=$probeKey PID=$($client.Id)"
                        Invoke-PhysicalInput -Process $client -Control ([string] $record.control)
                        $handledProbes[$probeKey] = $true
                    }
                }
                if ($log -eq $ServerLog)
                {
                    if ($line -match '\[tagging-up-turf-harness\]\s+PASS\s+profile=([^\s]+)')
                    {
                        if ($Matches[1] -ne $Profile)
                        {
                            [Console]::Error.WriteLine("[tagging-up-turf-transition-runner] FAIL: terminal PASS belongs to profile=$($Matches[1]), expected=$Profile")
                            exit 1
                        }
                        Write-Host "[tagging-up-turf-transition-runner] PASS profile=$($Matches[1])"
                        exit 0
                    }
                    if ($line -match '\[tagging-up-turf-harness\]\s+FAIL\s+profile=([^\s]+)(.*)$')
                    {
                        if ($Matches[1] -ne $Profile)
                        {
                            [Console]::Error.WriteLine("[tagging-up-turf-transition-runner] FAIL: terminal verdict belongs to profile=$($Matches[1]), expected=$Profile")
                            exit 1
                        }
                        [Console]::Error.WriteLine("[tagging-up-turf-transition-runner] FAIL profile=$($Matches[1])$($Matches[2])")
                        exit 1
                    }
                }
            }
            $lineCounts[$log] = $lines.Count
        }
        $spray = $script:activeNativeSpray
        if ($null -ne $spray -and -not $spray.FireDown -and (Get-Date) -ge $spray.NextSweepAt)
        {
            # Keep the adjustment genuinely physical, but use the client's
            # native aim ray as the gate before LMB is ever pressed.
            $delta = 4 * [int] $spray.SweepDirection
            [NeonTaggingUpTurfPhysicalInput]::MoveAim($delta)
            $spray.SweepPosition = [int] $spray.SweepPosition + $delta
            if ($spray.SweepPosition -le -240)
            {
                $spray.SweepDirection = 1
            }
            elseif ($spray.SweepPosition -ge 240)
            {
                $spray.SweepDirection = -1
            }
            $spray.NextSweepAt = (Get-Date).AddMilliseconds(50)
        }
        Start-Sleep -Milliseconds 100
    }
}
finally
{
    Stop-NativeSprayInput
    if (Test-Path -LiteralPath $requestPath -PathType Leaf)
    {
        Remove-Item -LiteralPath $requestPath -Force
    }
}

[Console]::Error.WriteLine("[tagging-up-turf-transition-runner] FAIL: no terminal verdict within $TimeoutSeconds seconds")
exit 2
