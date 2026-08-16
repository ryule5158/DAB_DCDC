param(
    [string]$PortName = 'COM4',
    [int]$BaudRate = 115200,
    [int]$Seconds = 10,
    [string[]]$Commands = @(),
    [string[]]$EndCommands = @()
)

$ErrorActionPreference = 'Stop'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$rawDirectory = Join-Path $PSScriptRoot 'raw_serial'
$captureTool = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..\..\tools\Capture-DabSerial.ps1')
)
$logPath = Join-Path $rawDirectory "dab_serial_${stamp}.log"

& $captureTool `
    -PortName $PortName `
    -BaudRate $BaudRate `
    -Seconds $Seconds `
    -Commands $Commands `
    -EndCommands $EndCommands `
    -LogPath $logPath

& (Join-Path $PSScriptRoot 'build_dataset.ps1') -SessionRoot $PSScriptRoot
