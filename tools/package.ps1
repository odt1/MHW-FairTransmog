[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\release'),
    [string]$OutDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist')
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot

$Version = [regex]::Match((Get-Content -LiteralPath (Join-Path $ProjectRoot 'CMakeLists.txt') -Raw),
    'project\(MHWFairTransmog VERSION ([0-9.]+)').Groups[1].Value
if (-not $Version) { throw 'Could not read the project version from CMakeLists.txt.' }
$Name = "MHW-FairTransmog-$Version-beta"

$Dll = Join-Path $BuildDir 'MHW-FairTransmog.dll'
if (-not (Test-Path -LiteralPath $Dll -PathType Leaf)) { throw "Built DLL not found: $Dll" }

$Stage = Join-Path $OutDir $Name
if (Test-Path -LiteralPath $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
$Plugins = Join-Path $Stage 'nativePC\plugins'
New-Item -ItemType Directory -Force -Path $Plugins | Out-Null
Copy-Item -LiteralPath $Dll -Destination $Plugins
foreach ($Doc in 'README.md', 'ARMOR_IDS.md', 'THIRD_PARTY_NOTICES.md') {
    Copy-Item -LiteralPath (Join-Path $ProjectRoot $Doc) -Destination $Stage
}

$Zip = Join-Path $OutDir "$Name.zip"
if (Test-Path -LiteralPath $Zip) { Remove-Item -LiteralPath $Zip -Force }
Compress-Archive -Path (Join-Path $Stage '*') -DestinationPath $Zip
[pscustomobject]@{
    Zip = $Zip
    Sha256 = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash
    DllSha256 = (Get-FileHash -LiteralPath $Dll -Algorithm SHA256).Hash
}
