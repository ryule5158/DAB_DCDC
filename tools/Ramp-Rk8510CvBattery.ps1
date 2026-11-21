param(
    [string]$PortName = 'COM24',
    [int]$BaudRate = 115200,
    [int]$Address = 255,
    [double[]]$SetpointsV = @(36.0, 38.0, 40.0, 41.0, 41.5, 41.8, 42.0, 42.1, 42.2, 42.3),
    [int[]]$DwellSeconds = @(20, 8, 8, 8, 8, 8, 8, 8, 8, 45),
    [int]$IntervalMs = 100,
    [string]$LogPath = ''
)

$ErrorActionPreference = 'Stop'

if ($SetpointsV.Count -ne $DwellSeconds.Count) {
    throw 'SetpointsV and DwellSeconds must contain the same number of entries.'
}
if ($SetpointsV.Count -eq 0) {
    throw 'At least one CV setpoint is required.'
}
if ($SetpointsV | Where-Object { ($_ -lt 0.0) -or ($_ -gt 48.5) }) {
    throw 'Every CV setpoint must be in the bounded 0.0 to 48.5 V test range.'
}
if ($DwellSeconds | Where-Object { $_ -lt 1 }) {
    throw 'Every dwell time must be at least one second.'
}

if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $logDirectory = Join-Path $PSScriptRoot '..\docs\validation\serial_logs'
    $LogPath = Join-Path $logDirectory "rk8510_cv_battery_ramp_${stamp}.csv"
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
$serial.NewLine = "`n"
$serial.ReadTimeout = 500
$serial.WriteTimeout = 500
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.DtrEnable = $false
$serial.RtsEnable = $false

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$writer = [System.IO.StreamWriter]::new($resolvedLogPath, $false, $utf8NoBom)
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
        # A missed read must not abort a preloaded battery-voltage trajectory.
        # The CSV records parse_ok=false so the sample remains auditable.
        return 'TIMEOUT'
    }
}

function Set-CvVoltage {
    param([double]$VoltageV)

    $value = $VoltageV.ToString('F3', $culture)
    # Deliberately do not send FUNCtion:ON/OFF or FUNCtion:MODE here.
    $serial.Write(":CV:VOLTage $value`r`n")
    Write-Output ('ELOAD_SET:{0:O},{1}' -f (Get-Date), $value)
}

try {
    $serial.Open()
    $identity = Invoke-ScpiQuery -Command '*IDN?'
    $writer.WriteLine(('# instrument={0}; port={1}; baud={2}; address={3}; policy=CV-setpoint-and-FETC-only-no-load-toggle' -f $identity, $PortName, $BaudRate, $Address))
    $writer.WriteLine('host_time,elapsed_ms,step_index,setpoint_v,voltage_v,current_a,power_w,parse_ok,raw')

    $started = Get-Date
    for ($stepIndex = 0; $stepIndex -lt $SetpointsV.Count; $stepIndex++) {
        $setpoint = $SetpointsV[$stepIndex]
        Set-CvVoltage -VoltageV $setpoint
        $deadline = (Get-Date).AddSeconds($DwellSeconds[$stepIndex])

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
                $stepIndex,
                $setpoint.ToString('F3', $culture),
                $voltage.ToString('F6', $culture),
                $current.ToString('F6', $culture),
                $power.ToString('F6', $culture),
                $parseOk.ToString().ToLowerInvariant(),
                $raw.Replace('"', '""')))

            Write-Output ('ELOAD:{0:O},step={1},set={2:F3},V={3:F6},I={4:F6},P={5:F6}' -f
                $sampleTime, $stepIndex, $setpoint, $voltage, $current, $power)
            Start-Sleep -Milliseconds $IntervalMs
        }
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
