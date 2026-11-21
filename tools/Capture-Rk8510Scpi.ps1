param(
    [string]$PortName = 'COM24',
    [int]$BaudRate = 115200,
    [int]$Address = 255,
    [int]$Seconds = 10,
    [int]$IntervalMs = 100,
    [string]$LogPath = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $logDirectory = Join-Path $PSScriptRoot '..\docs\validation\serial_logs'
    $LogPath = Join-Path $logDirectory "rk8510_scpi_${stamp}.csv"
}

$resolvedLogPath = [System.IO.Path]::GetFullPath($LogPath)
$resolvedLogDirectory = [System.IO.Path]::GetDirectoryName($resolvedLogPath)
[System.IO.Directory]::CreateDirectory($resolvedLogDirectory) | Out-Null
$appendLog = [System.IO.File]::Exists($resolvedLogPath) -and ((Get-Item -LiteralPath $resolvedLogPath).Length -gt 0)

$serial = [System.IO.Ports.SerialPort]::new(
    $PortName,
    $BaudRate,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial.Encoding = [System.Text.Encoding]::ASCII
$serial.NewLine = "`n"
$serial.ReadTimeout = 500
$serial.WriteTimeout = 500
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.DtrEnable = $false
$serial.RtsEnable = $false

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$writer = [System.IO.StreamWriter]::new($resolvedLogPath, $true, $utf8NoBom)
$writer.AutoFlush = $true
$culture = [System.Globalization.CultureInfo]::InvariantCulture

function Invoke-ScpiQuery {
    param([string]$Command)

    $serial.DiscardInBuffer()
    $serial.Write("$Command`r`n")
    try {
        return $serial.ReadLine().Trim()
    }
    catch [System.TimeoutException] {
        return 'TIMEOUT'
    }
}

try {
    $serial.Open()
    $identity = Invoke-ScpiQuery -Command '*IDN?'
    $writer.WriteLine(('# capture={0:O}; instrument={1}; port={2}; baud={3}; address={4}; access=read-only-short-window' -f (Get-Date), $identity, $PortName, $BaudRate, $Address))
    if (-not $appendLog) {
        $writer.WriteLine('host_time,elapsed_ms,port,address,voltage_v,current_a,power_w,parse_ok,raw')
    }

    $started = Get-Date
    $deadline = $started.AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        $sampleTime = Get-Date
        $raw = Invoke-ScpiQuery -Command 'FETC?'
        $parts = $raw.Split(',')
        $voltage = 0.0
        $current = 0.0
        $power = 0.0
        $parseOk = ($parts.Count -ge 3) -and
            [double]::TryParse($parts[0], [System.Globalization.NumberStyles]::Float, $culture, [ref]$voltage) -and
            [double]::TryParse($parts[1], [System.Globalization.NumberStyles]::Float, $culture, [ref]$current) -and
            [double]::TryParse($parts[2], [System.Globalization.NumberStyles]::Float, $culture, [ref]$power)

        $elapsedMs = [long](($sampleTime - $started).TotalMilliseconds)
        $writer.WriteLine(('{0:O},{1},{2},{3},{4},{5},{6},{7},"{8}"' -f
            $sampleTime,
            $elapsedMs,
            $PortName,
            $Address,
            $voltage.ToString('F6', $culture),
            $current.ToString('F6', $culture),
            $power.ToString('F6', $culture),
            $parseOk.ToString().ToLowerInvariant(),
            $raw.Replace('"', '""')))

        Write-Output ('ELOAD:{0:O},{1:F6},{2:F6},{3:F6}' -f $sampleTime, $voltage, $current, $power)
        Start-Sleep -Milliseconds $IntervalMs
    }
}
finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
    $writer.Dispose()
}

Write-Output "LOG_PATH=$resolvedLogPath"
