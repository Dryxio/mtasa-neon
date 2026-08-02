[CmdletBinding()]
param(
    [string] $SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string] $OutputDirectory = (Get-Location).Path
)

$ErrorActionPreference = 'Stop'

$resolvedSourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$resolvedOutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$serverRoot = Join-Path $resolvedSourceRoot 'Bin\server'
$packageName = 'MTA-Neon-Server-Windows-x64'
$packageRoot = Join-Path $resolvedOutputDirectory $packageName
$archivePath = Join-Path $resolvedOutputDirectory "$packageName.zip"

# Fail on stale output instead of merging it into a public package. This keeps
# files removed by a newer build from surviving unnoticed in a release archive.
foreach ($outputPath in @($packageRoot, $archivePath))
{
    if (Test-Path -LiteralPath $outputPath)
    {
        throw "Server package output already exists: $outputPath"
    }
}

New-Item -ItemType Directory -Path $packageRoot | Out-Null

# Copy only the x64 server runtime and its resource tree. compose_files also
# carries client data and omits resources, so it is unsuitable for the public
# standalone server download.
Copy-Item -LiteralPath (Join-Path $serverRoot 'MTA Server64.exe') -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $serverRoot 'x64') -Destination $packageRoot -Recurse
Copy-Item -LiteralPath (Join-Path $serverRoot 'mods') -Destination $packageRoot -Recurse
Copy-Item -LiteralPath (Join-Path $resolvedSourceRoot 'Server\output\LICENSE') -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $resolvedSourceRoot 'Server\output\NOTICE') -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $resolvedSourceRoot 'Server\output\README') -Destination $packageRoot

@'
MTA:SA Neon Windows server

1. Edit mods\deathmatch\mtaserver.conf.
2. Start MTA Server64.exe from this directory.
3. Allow the configured UDP game port and TCP HTTP port through the firewall.

Keep this server on the same Neon build as clients that use Neon-specific
network features. Documentation: https://mtasa-neon-wiki.vercel.app/neon/download
'@ | Set-Content -LiteralPath (Join-Path $packageRoot 'README-FIRST.txt') -Encoding ascii

$requiredFiles = @(
    'MTA Server64.exe',
    'x64\core.dll',
    'x64\deathmatch.dll',
    'x64\lua5.1.dll',
    'x64\net.dll',
    'x64\xmll.dll',
    'mods\deathmatch\mtaserver.conf',
    'mods\deathmatch\acl.xml',
    'README-FIRST.txt'
)
foreach ($relativePath in $requiredFiles)
{
    $filePath = Join-Path $packageRoot $relativePath
    if (-not (Test-Path -LiteralPath $filePath -PathType Leaf))
    {
        throw "Windows server package is missing: $relativePath"
    }
}

$resourcesPath = Join-Path $packageRoot 'mods\deathmatch\resources'
if (-not (Test-Path -LiteralPath $resourcesPath -PathType Container))
{
    throw 'Windows server package is missing its resources directory'
}
if (-not (Get-ChildItem -LiteralPath $resourcesPath -File -Recurse | Select-Object -First 1))
{
    throw 'Windows server package contains an empty resources directory'
}

$archiveCreated = $false
try
{
    Compress-Archive -LiteralPath $packageRoot -DestinationPath $archivePath -CompressionLevel Optimal
    $archiveCreated = $true

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zipArchive = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
    try
    {
        if ($zipArchive.Entries.Count -eq 0)
        {
            throw 'Windows server package archive is empty'
        }
    }
    finally
    {
        $zipArchive.Dispose()
    }
}
catch
{
    if (-not $archiveCreated)
    {
        throw "Windows server package could not be created: $($_.Exception.Message)"
    }
    throw
}

$archive = Get-Item -LiteralPath $archivePath
$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath
Write-Host "Windows server package: $($archive.FullName)"
Write-Host "Size: $($archive.Length) bytes"
Write-Host "SHA-256: $($hash.Hash)"
Write-Output $archive.FullName
