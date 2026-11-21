param(
    [string]$PortName = 'COM24',
    [int]$BaudRate = 115200,
    [string]$LogPath = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $LogPath = Join-Path $PSScriptRoot "..\docs\validation\serial_logs\rk8510_load_on_${stamp}.log"
}

$resolvedLogPath = [System.IO.Path]::GetFullPath($LogPath)
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($resolvedLogPath)) | Out-Null
$serial = [System.IO.Ports.SerialPort]::new($PortName, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.Encoding = [System.Text.Encoding]::ASCII
$serial.NewLine = "`n"
$serial.ReadTimeout = 500
$serial.WriteTimeout = 500
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$writer = [System.IO.StreamWriter]::new($resolvedLogPath, $false, $utf8NoBom)
$writer.AutoFlush = $true

function Invoke-Query([string]$Command) {
    $serial.DiscardInBuffer()
    $serial.Write("$Command`r`n")
    return $serial.ReadLine().Trim()
}

try {
    $serial.Open()
    $identity = Invoke-Query '*IDN?'
    $before = Invoke-Query 'FETC?'
    $writer.WriteLine(('# {0:O} instrument={1}' -f (Get-Date), $identity))
    $writer.WriteLine(('# {0:O} before={1}' -f (Get-Date), $before))
    $writer.WriteLine(('# {0:O} TX FUNCtion:ON' -f (Get-Date)))
    $serial.Write("FUNCtion:ON`r`n")
    Start-Sleep -Milliseconds 250
    $after = Invoke-Query 'FETC?'
    $writer.WriteLine(('# {0:O} after={1}' -f (Get-Date), $after))
    Write-Output "INSTRUMENT=$identity"
    Write-Output "BEFORE=$before"
    Write-Output 'TX=FUNCtion:ON'
    Write-Output "AFTER=$after"
}
finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
    $writer.Dispose()
}

Write-Output "LOG_PATH=$resolvedLogPath"
