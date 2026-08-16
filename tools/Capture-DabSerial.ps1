param(
    [string]$PortName = 'COM4',
    [int]$BaudRate = 115200,
    [int]$Seconds = 10,
    [string[]]$Commands = @(),
    [string[]]$EndCommands = @(),
    [string]$LogPath = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $logDirectory = Join-Path $PSScriptRoot '..\DAB_RA8P1_PCB_Notes\serial_logs'
    $LogPath = Join-Path $logDirectory "dab_serial_${stamp}.log"
}

$resolvedLogPath = [System.IO.Path]::GetFullPath($LogPath)
$resolvedLogDirectory = [System.IO.Path]::GetDirectoryName($resolvedLogPath)
[System.IO.Directory]::CreateDirectory($resolvedLogDirectory) | Out-Null

$serial = [System.IO.Ports.SerialPort]::new(
    $PortName,
    $BaudRate,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial.Encoding = [System.Text.Encoding]::ASCII
$serial.NewLine = "`r`n"
$serial.ReadTimeout = 100
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$writer = [System.IO.StreamWriter]::new($resolvedLogPath, $true, $utf8NoBom)
$writer.AutoFlush = $true

try {
    $serial.Open()
    $started = Get-Date
    $deadline = $started.AddSeconds($Seconds)
    $openRecord = '#HOST {0:O} OPEN {1} {2}-8N1 duration={3}s' -f `
        $started, $PortName, $BaudRate, $Seconds
    $writer.WriteLine($openRecord)
    Write-Output $openRecord

    Start-Sleep -Milliseconds 250
    foreach ($command in $Commands) {
        if ([string]::IsNullOrWhiteSpace($command)) {
            continue
        }
        $commandRecord = '#HOST {0:O} TX {1}' -f (Get-Date), $command
        $writer.WriteLine($commandRecord)
        Write-Output $commandRecord
        $serial.Write("$command`r`n")
        Start-Sleep -Milliseconds 250
    }

    while ((Get-Date) -lt $deadline) {
        $available = $serial.BytesToRead
        if ($available -gt 0) {
            $chunk = $serial.ReadExisting()
            if ($chunk.Length -gt 0) {
                $writer.Write($chunk)
                [Console]::Write($chunk)
            }
        }
        else {
            Start-Sleep -Milliseconds 20
        }
    }

    foreach ($command in $EndCommands) {
        if ([string]::IsNullOrWhiteSpace($command)) {
            continue
        }
        $commandRecord = '#HOST {0:O} TX-END {1}' -f (Get-Date), $command
        $writer.WriteLine($commandRecord)
        Write-Output $commandRecord
        $serial.Write("$command`r`n")
        Start-Sleep -Milliseconds 250
    }

    Start-Sleep -Milliseconds 100
    if ($serial.BytesToRead -gt 0) {
        $chunk = $serial.ReadExisting()
        $writer.Write($chunk)
        [Console]::Write($chunk)
    }

    $closeRecord = '#HOST {0:O} CLOSE samples-log={1}' -f (Get-Date), $resolvedLogPath
    $writer.WriteLine()
    $writer.WriteLine($closeRecord)
    Write-Output $closeRecord
}
finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
    $writer.Dispose()
}

Write-Output "LOG_PATH=$resolvedLogPath"
