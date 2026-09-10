# Pin the official x86 redistributable used by the two FMOD DSP plugins. Keep it
# inside the installer so upgrading a clean player PC never needs another download.
[CmdletBinding()]
param([string]$OutputDirectory = (Join-Path $PSScriptRoot '..\..\Build\runtime-prerequisites'))
$ErrorActionPreference = 'Stop'
$expectedHash = '99dce3c841cc6028560830f7866c9ce2928c98cf3256892ef8e6cf755147b0d8'
$url = 'https://download.microsoft.com/download/1/6/5/165255E7-1014-4D0A-B094-B6A430A6BFFC/vcredist_x86.exe'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$destination = Join-Path $OutputDirectory 'vcredist_x86.exe'
if ((Test-Path -LiteralPath $destination) -and (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq $expectedHash) {
    Write-Host 'Pinned Visual C++ 2010 SP1 x86 installer already verified.'
    return
}
$temporary = Join-Path $OutputDirectory ([IO.Path]::GetRandomFileName())
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $temporary
    if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $expectedHash) {
        throw 'Visual C++ 2010 SP1 x86 download failed SHA-256 verification.'
    }
    Move-Item -LiteralPath $temporary -Destination $destination -Force
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
}
Write-Host 'Pinned Visual C++ 2010 SP1 x86 installer ready.'
