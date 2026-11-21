[CmdletBinding()]
param(
    [switch] $CheckOnly
)

$ErrorActionPreference = 'Stop'
$dualCoreRoot = Split-Path -Parent $PSScriptRoot
$generator = Join-Path $PSScriptRoot 'ai_training\promote_validated_to_firmware.py'
$cpu0Debug = Join-Path $dualCoreRoot 'RA8P1_DAB_DualCore_Solution_CPU0\Debug'

$arguments = @('-3', $generator)
if ($CheckOnly)
{
    $arguments += '--check'
}

& py @arguments
if ($LASTEXITCODE -ne 0)
{
    throw "Validated-parameter generation/check failed with exit code $LASTEXITCODE"
}

Push-Location $cpu0Debug
try
{
    & make -j8 all
    if ($LASTEXITCODE -ne 0)
    {
        throw "CPU0 build failed with exit code $LASTEXITCODE"
    }
}
finally
{
    Pop-Location
}

Write-Output 'PROMOTION_OK: powered PASS records and CPU0 firmware are synchronized'
