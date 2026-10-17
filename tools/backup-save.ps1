param(
    [Parameter(Mandatory)][string]$SaveRoot,
    [string]$BackupRoot = (Join-Path $PSScriptRoot '..\re\backups')
)
$ErrorActionPreference = 'Stop'

if (Get-Process -Name MonsterHunterWorld -ErrorAction SilentlyContinue) {
    throw 'Close Monster Hunter: World before creating a verified save backup.'
}
$source = (Resolve-Path -LiteralPath $SaveRoot).Path.TrimEnd('\', '/')
$backupBase = [IO.Path]::GetFullPath($BackupRoot).TrimEnd('\', '/')
if ($backupBase.Equals($source, [StringComparison]::OrdinalIgnoreCase) -or
    $backupBase.StartsWith($source + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'BackupRoot must be outside SaveRoot.'
}
if (-not (Test-Path -LiteralPath (Join-Path $source 'remote\SAVEDATA1000') -PathType Leaf)) {
    throw 'SaveRoot must be the Steam 582010 directory containing remote\SAVEDATA1000.'
}
$files = @(Get-ChildItem -LiteralPath $source -File -Recurse | Sort-Object FullName)
if (Get-ChildItem -LiteralPath $source -Recurse -Attributes ReparsePoint) {
    throw 'SaveRoot contains a reparse point; inspect it before backing up.'
}
$before = @($files | ForEach-Object {
    [PSCustomObject]@{
        path = [IO.Path]::GetRelativePath($source, $_.FullName)
        length = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
})
New-Item -ItemType Directory -Force -Path $backupBase | Out-Null
$name = '582010-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$destination = Join-Path $backupBase $name
New-Item -ItemType Directory -Path $destination | Out-Null
foreach ($entry in $before) {
    $target = Join-Path $destination $entry.path
    New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($target)) | Out-Null
    Copy-Item -LiteralPath (Join-Path $source $entry.path) -Destination $target
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Backup verification failed: $($entry.path). Incomplete backup retained at $destination"
    }
}
$after = @(Get-ChildItem -LiteralPath $source -File -Recurse | Sort-Object FullName)
$afterPaths = @($after | ForEach-Object { [IO.Path]::GetRelativePath($source, $_.FullName) })
if (@(Compare-Object @($before.path) $afterPaths).Count -ne 0) {
    throw "Save file list changed during backup. Incomplete backup retained at $destination"
}
foreach ($entry in $before) {
    if ((Get-FileHash -LiteralPath (Join-Path $source $entry.path) -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Save changed during backup: $($entry.path). Incomplete backup retained at $destination"
    }
}
if (Get-Process -Name MonsterHunterWorld -ErrorAction SilentlyContinue) {
    throw "Game started during backup. Backup is not certified: $destination"
}
$manifest = [ordered]@{
    format = 1
    created_utc = [DateTime]::UtcNow.ToString('O')
    source = $source
    game_closed = $true
    files = $before
}
$manifestPath = Join-Path $destination 'backup-manifest.json'
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8NoBOM
[PSCustomObject]@{ backup = $destination; manifest = $manifestPath; files = $before.Count; verified = $true } | ConvertTo-Json
