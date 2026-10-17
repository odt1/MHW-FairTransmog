[CmdletBinding()]
param(
    [string]$GameDir = 'F:\SteamLibrary\steamapps\common\Monster Hunter World',
    [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\diagnostic')
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$DllName = 'MHW-FairTransmog.dll'
$PdbName = 'MHW-FairTransmog.pdb'

if (Get-Process -Name 'MonsterHunterWorld' -ErrorAction SilentlyContinue) {
    throw 'Refusing deployment while MonsterHunterWorld is running.'
}

$SourceDll = Join-Path $BuildDir $DllName
if (-not (Test-Path -LiteralPath $SourceDll -PathType Leaf)) {
    throw "Built DLL not found: $SourceDll"
}
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) {
    throw "Game directory not found: $GameDir"
}

$SourceHashes = @{}
foreach ($Name in @($DllName, $PdbName)) {
    $Source = Join-Path $BuildDir $Name
    if (Test-Path -LiteralPath $Source -PathType Leaf) {
        $SourceHashes[$Name] = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    }
}

$DestinationDir = Join-Path $GameDir 'nativePC\plugins'
New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
$BackupRoot = Join-Path $ProjectRoot 're\deployment-backups'
$BackupDir = Join-Path $BackupRoot ("{0}-{1}" -f (Get-Date -Format 'yyyyMMddTHHmmssfff'), [guid]::NewGuid().ToString('N'))
$BackupEntries = @()

foreach ($Name in @($DllName, $PdbName)) {
    $Destination = Join-Path $DestinationDir $Name
    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null
        $Hash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
        $Backup = Join-Path $BackupDir $Name
        Copy-Item -LiteralPath $Destination -Destination $Backup -ErrorAction Stop
        if ((Get-FileHash -LiteralPath $Backup -Algorithm SHA256).Hash -ne $Hash) {
            throw "Plugin backup verification failed: $Name. Existing plugin files were not replaced."
        }
        $BackupEntries += [pscustomobject]@{ File = $Name; Sha256 = $Hash }
    }
}

if ($BackupEntries.Count -gt 0) {
    $BackupEntries | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $BackupDir 'manifest.json') -Encoding utf8
}

if (Get-Process -Name 'MonsterHunterWorld' -ErrorAction SilentlyContinue) {
    throw 'Refusing deployment: MonsterHunterWorld started before plugin replacement.'
}

foreach ($Name in @($DllName, $PdbName)) {
    if (-not $SourceHashes.ContainsKey($Name)) { continue }
    $Destination = Join-Path $DestinationDir $Name
    Copy-Item -LiteralPath (Join-Path $BuildDir $Name) -Destination $Destination -Force
    if ((Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -ne $SourceHashes[$Name]) {
        throw "Plugin deployment verification failed: $Name. Keep the game closed and inspect deployment backups."
    }
}
