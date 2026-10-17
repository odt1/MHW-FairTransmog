[CmdletBinding()]
param(
    [ValidateSet('release', 'diagnostic')]
    [string]$Preset = 'diagnostic'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot

Push-Location $ProjectRoot
try {
    devshell
    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for preset '$Preset'." }

    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed for preset '$Preset'." }

    ctest --preset "test-$Preset" --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "CTest failed for preset '$Preset'." }
}
finally {
    Pop-Location
}
