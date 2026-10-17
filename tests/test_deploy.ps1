$ErrorActionPreference = 'Stop'
$SourceScript = Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\deploy.ps1'
$Root = Join-Path ([IO.Path]::GetTempPath()) ('fairtransmog-deploy-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Root | Out-Null
$ProcessState = [pscustomobject]@{ CheckCount = 0; StartOnCheck = 0 }
$CopyState = [pscustomobject]@{ Corruption = '' }

function Copy-Item {
    [CmdletBinding()]
    param([string]$LiteralPath, [string]$Destination, [switch]$Force)
    Microsoft.PowerShell.Management\Copy-Item @PSBoundParameters
    if ([IO.Path]::GetFileName($Destination) -ne 'MHW-FairTransmog.dll') { return }
    $Normalized = $Destination.Replace('/', '\')
    if (($CopyState.Corruption -eq 'backup' -and $Normalized.Contains('\re\deployment-backups\')) -or
        ($CopyState.Corruption -eq 'deployment' -and $Normalized.Contains('\nativePC\plugins\'))) {
        [IO.File]::AppendAllText($Destination, 'fixture corruption')
    }
}

function Get-Process {
    [CmdletBinding()]
    param([string]$Name)
    if ($Name -ne 'MonsterHunterWorld') { throw "Unexpected process query: $Name" }
    $ProcessState.CheckCount++
    if ($ProcessState.StartOnCheck -gt 0 -and $ProcessState.CheckCount -ge $ProcessState.StartOnCheck) {
        [pscustomobject]@{ ProcessName = $Name }
    }
}

function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function New-Fixture([string]$Name) {
    $Project = Join-Path $Root "$Name\isolated project"
    $Tools = Join-Path $Project 'tools'
    $Game = Join-Path $Root "$Name\fake game with spaces"
    $Build = Join-Path $Root "$Name\build output with spaces"
    foreach ($Directory in @($Tools, $Game, $Build)) {
        New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    }
    $Script = Join-Path $Tools 'deploy.ps1'
    Copy-Item -LiteralPath $SourceScript -Destination $Script
    [pscustomobject]@{
        Project = $Project
        Game = $Game
        Build = $Build
        Script = $Script
        Plugins = Join-Path $Game 'nativePC\plugins'
        Backups = Join-Path $Project 're\deployment-backups'
    }
}

function Write-File([string]$Path, [string]$Content) {
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($Path)) -Force | Out-Null
    [IO.File]::WriteAllText($Path, $Content)
}

function Hash([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Deploy($Fixture, [int]$StartOnCheck = 0, [string]$Corruption = '') {
    $ProcessState.CheckCount = 0
    $ProcessState.StartOnCheck = $StartOnCheck
    $CopyState.Corruption = $Corruption
    & $Fixture.Script -GameDir $Fixture.Game -BuildDir $Fixture.Build
}

function Expect-Failure([scriptblock]$Action, [string]$Expected) {
    $Failed = $false
    try { & $Action }
    catch {
        Assert ($_.Exception.Message -like "*$Expected*") "Unexpected failure: $($_.Exception.Message)"
        $Failed = $true
    }
    Assert $Failed "Expected failure containing: $Expected"
}

try {
    $Missing = New-Fixture 'missing source'
    Expect-Failure { Deploy $Missing } 'Built DLL not found'
    Assert (-not (Test-Path -LiteralPath $Missing.Plugins)) 'Missing source created a plugin directory.'
    Assert (-not (Test-Path -LiteralPath $Missing.Backups)) 'Missing source created backups.'
    Write-Output 'PASS: missing source fails before writes'

    $First = New-Fixture 'first deployment'
    Write-File (Join-Path $First.Build 'MHW-FairTransmog.dll') 'first fake DLL'
    Write-File (Join-Path $First.Build 'MHW-FairTransmog.pdb') 'first fake PDB'
    Write-File (Join-Path $First.Build 'helper.dll') 'must never be deployed'
    Write-File (Join-Path $First.Plugins 'unrelated.dll') 'existing unrelated plugin'
    Write-File (Join-Path $First.Plugins 'settings.ini') 'existing settings'
    $UnrelatedHash = Hash (Join-Path $First.Plugins 'unrelated.dll')
    $SettingsHash = Hash (Join-Path $First.Plugins 'settings.ini')
    Deploy $First
    foreach ($Name in @('MHW-FairTransmog.dll', 'MHW-FairTransmog.pdb')) {
        Assert ((Hash (Join-Path $First.Build $Name)) -eq (Hash (Join-Path $First.Plugins $Name))) "First deployment mismatch: $Name"
    }
    Assert (-not (Test-Path -LiteralPath (Join-Path $First.Plugins 'helper.dll'))) 'Helper DLL was deployed.'
    Assert ((Hash (Join-Path $First.Plugins 'unrelated.dll')) -eq $UnrelatedHash) 'Unrelated plugin changed.'
    Assert ((Hash (Join-Path $First.Plugins 'settings.ini')) -eq $SettingsHash) 'Settings changed.'
    Assert (-not (Test-Path -LiteralPath $First.Backups)) 'First deployment created unnecessary backups.'
    Write-Output 'PASS: first deployment preserves unrelated files and ignores helper DLLs'

    $OldHashes = @{}
    foreach ($Name in @('MHW-FairTransmog.dll', 'MHW-FairTransmog.pdb')) {
        $OldHashes[$Name] = Hash (Join-Path $First.Plugins $Name)
        Write-File (Join-Path $First.Build $Name) "replacement fake $Name"
    }
    Deploy $First
    $BackupDirectories = @(Get-ChildItem -LiteralPath $First.Backups -Directory)
    Assert ($BackupDirectories.Count -eq 1) 'Expected exactly one replacement backup.'
    $BackupDirectory = $BackupDirectories[0].FullName
    $Manifest = @(Get-Content -LiteralPath (Join-Path $BackupDirectory 'manifest.json') -Raw | ConvertFrom-Json)
    Assert ($Manifest.Count -eq 2) 'Replacement manifest must contain DLL and PDB.'
    foreach ($Entry in $Manifest) {
        Assert ($OldHashes.ContainsKey($Entry.File)) 'Backup manifest contains an unrelated file.'
        Assert ($Entry.Sha256 -eq $OldHashes[$Entry.File]) 'Manifest hash differs from original.'
        Assert ((Hash (Join-Path $BackupDirectory $Entry.File)) -eq $OldHashes[$Entry.File]) 'Backup bytes differ from original.'
        Assert ((Hash (Join-Path $First.Plugins $Entry.File)) -eq (Hash (Join-Path $First.Build $Entry.File))) 'Replacement bytes differ from build.'
    }
    Assert ((Hash (Join-Path $First.Plugins 'unrelated.dll')) -eq $UnrelatedHash) 'Replacement changed unrelated plugin.'
    Assert ((Hash (Join-Path $First.Plugins 'settings.ini')) -eq $SettingsHash) 'Replacement changed settings.'
    Write-Output 'PASS: replacement backs up verified original bytes and verifies new bytes'

    $Optional = New-Fixture 'optional PDB'
    Write-File (Join-Path $Optional.Build 'MHW-FairTransmog.dll') 'fake DLL without PDB'
    Deploy $Optional
    Assert ((@(Get-ChildItem -LiteralPath $Optional.Plugins -File)).Count -eq 1) 'DLL-only deployment copied extra files.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $Optional.Plugins 'MHW-FairTransmog.pdb'))) 'Missing optional PDB was fabricated.'
    Write-Output 'PASS: missing optional PDB is supported'

    $Running = New-Fixture 'already running'
    Write-File (Join-Path $Running.Build 'MHW-FairTransmog.dll') 'fake DLL'
    Expect-Failure { Deploy $Running -StartOnCheck 1 } 'Refusing deployment while MonsterHunterWorld is running'
    Assert (-not (Test-Path -LiteralPath $Running.Plugins)) 'Running-game guard wrote a plugin directory.'
    Write-Output 'PASS: initial running-game guard rejects before writes'

    $Late = New-Fixture 'starts during backup'
    Write-File (Join-Path $Late.Build 'MHW-FairTransmog.dll') 'new fake DLL'
    Write-File (Join-Path $Late.Plugins 'MHW-FairTransmog.dll') 'old fake DLL'
    $OldHash = Hash (Join-Path $Late.Plugins 'MHW-FairTransmog.dll')
    Expect-Failure { Deploy $Late -StartOnCheck 2 } 'started before plugin replacement'
    Assert ((Hash (Join-Path $Late.Plugins 'MHW-FairTransmog.dll')) -eq $OldHash) 'Late running-game guard replaced plugin.'
    $LateBackups = @(Get-ChildItem -LiteralPath $Late.Backups -Directory)
    Assert ($LateBackups.Count -eq 1) 'Late guard lost original backup.'
    Assert ((Hash (Join-Path $LateBackups[0].FullName 'MHW-FairTransmog.dll')) -eq $OldHash) 'Late guard backup differs from original.'
    Write-Output 'PASS: second running-game guard preserves plugin and verified backup'

    $BadBackup = New-Fixture 'corrupt backup'
    foreach ($Name in @('MHW-FairTransmog.dll', 'MHW-FairTransmog.pdb')) {
        Write-File (Join-Path $BadBackup.Build $Name) "new fake $Name"
        Write-File (Join-Path $BadBackup.Plugins $Name) "old fake $Name"
    }
    $OriginalDllHash = Hash (Join-Path $BadBackup.Plugins 'MHW-FairTransmog.dll')
    $OriginalPdbHash = Hash (Join-Path $BadBackup.Plugins 'MHW-FairTransmog.pdb')
    Expect-Failure { Deploy $BadBackup -Corruption 'backup' } 'Plugin backup verification failed'
    Assert ((Hash (Join-Path $BadBackup.Plugins 'MHW-FairTransmog.dll')) -eq $OriginalDllHash) 'Backup rejection replaced original DLL.'
    Assert ((Hash (Join-Path $BadBackup.Plugins 'MHW-FairTransmog.pdb')) -eq $OriginalPdbHash) 'Backup rejection replaced original PDB.'
    Write-Output 'PASS: corrupt backup is rejected before either original is replaced'

    $BadDeployment = New-Fixture 'corrupt deployment'
    Write-File (Join-Path $BadDeployment.Build 'MHW-FairTransmog.dll') 'new fake DLL'
    Write-File (Join-Path $BadDeployment.Plugins 'MHW-FairTransmog.dll') 'old fake DLL'
    $OriginalDllHash = Hash (Join-Path $BadDeployment.Plugins 'MHW-FairTransmog.dll')
    Expect-Failure { Deploy $BadDeployment -Corruption 'deployment' } 'Plugin deployment verification failed'
    $BadDeploymentBackups = @(Get-ChildItem -LiteralPath $BadDeployment.Backups -Directory)
    Assert ($BadDeploymentBackups.Count -eq 1) 'Deployment rejection lost original backup.'
    $BackupDirectory = $BadDeploymentBackups[0].FullName
    Assert ((Hash (Join-Path $BackupDirectory 'MHW-FairTransmog.dll')) -eq $OriginalDllHash) 'Deployment rejection lost verified original bytes.'
    $Manifest = @(Get-Content -LiteralPath (Join-Path $BackupDirectory 'manifest.json') -Raw | ConvertFrom-Json)
    Assert ($Manifest.Count -eq 1 -and $Manifest[0].Sha256 -eq $OriginalDllHash) 'Deployment rejection lost verified backup manifest.'
    Assert ((Hash (Join-Path $BadDeployment.Plugins 'MHW-FairTransmog.dll')) -ne (Hash (Join-Path $BadDeployment.Build 'MHW-FairTransmog.dll'))) 'Corruption fixture did not change deployed bytes.'
    Write-Output 'PASS: corrupt deployment is reported while verified backup remains available'
}
finally {
    Remove-Item -LiteralPath $Root -Recurse -Force
}
