param(
    [string]$PortName = 'COM24',
    [int]$BaudRate = 115200,
    [int]$Address = 255,
    [int]$Seconds = 30,
    [int]$IntervalMs = 100,
    [string[]]$CvSteps = @('0:36.0'),
    [switch]$LeaveLoadOn,
    [switch]$PreserveLoadState,
    [string]$LogPath = ''
)

$ErrorActionPreference = 'Stop'
$culture = [System.Globalization.CultureInfo]::InvariantCulture

if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $logDirectory = Join-Path $PSScriptRoot '..\docs\validation\serial_logs'
    $LogPath = Join-Path $logDirectory "rk8510_cv_sequence_${stamp}.csv"
}

$steps = foreach ($stepText in $CvSteps) {
    $parts = $stepText.Split(':')
    if ($parts.Count -ne 2) {
        throw "Invalid CvSteps item '$stepText'; expected elapsed_seconds:voltage_v"
    }
    [pscustomobject]@{
        TimeMs = [int]([double]::Parse($parts[0], $culture) * 1000.0)
        Voltage = [double]::Parse($parts[1], $culture)
    }
}
$steps = @($steps | Sort-Object TimeMs)
if (($steps.Count -eq 0) -or ($steps[0].TimeMs -ne 0)) {
    throw 'CvSteps must contain an initial 0:voltage_v item'
}
foreach ($step in $steps) {
    if (($step.Voltage -lt 0.0) -or ($step.Voltage -gt 150.0)) {
        throw "CV step $($step.Voltage) V is outside the RK8510 command range"
    }
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

function Invoke-ScpiQuery {
    param([string]$Command)
    $serial.DiscardInBuffer()
    $serial.Write("$Command`r`n")
    return $serial.ReadLine().Trim()
}

function Send-ScpiCommand {
    param([string]$Command)
    $serial.Write("$Command`r`n")
    Start-Sleep -Milliseconds 60
}

try {
    $serial.Open()
    $identity = Invoke-ScpiQuery -Command '*IDN?'
    $originalMode = Invoke-ScpiQuery -Command 'FUNCtion:MODE?'
    $originalCv = Invoke-ScpiQuery -Command ':CV:VOLTage?'
    $originalRemote = Invoke-ScpiQuery -Command 'FUNCtion:LOAD:REMOte?'
    $stateBeforeRaw = Invoke-ScpiQuery -Command 'FETCh:STAte?'
    $stateBeforeValue = 0
    if (-not [int]::TryParse($stateBeforeRaw, [ref]$stateBeforeValue)) {
        throw "RK8510 state parse failed before setup: '$stateBeforeRaw'"
    }
    $writer.WriteLine('record_type,event,host_time,elapsed_ms,port,address,function_mode,cv_set_v,voltage_v,current_a,power_w,state_raw,load_active,parse_ok,raw')

    Send-ScpiCommand -Command 'FUNCtion:LOAD:REMOte 1'
    Send-ScpiCommand -Command 'FUNCtion:MODE 2'
    $currentCv = $steps[0].Voltage
    Send-ScpiCommand -Command (':CV:VOLTage {0:F3}' -f $currentCv)
    $loadWasOpened = $false
    if ((-not $PreserveLoadState.IsPresent) -and (($stateBeforeValue -band 1) -eq 0)) {
        Send-ScpiCommand -Command 'FUNCtion:ON'
        $loadWasOpened = $true
    }
    $verifiedRemote = Invoke-ScpiQuery -Command 'FUNCtion:LOAD:REMOte?'
    $verifiedMode = Invoke-ScpiQuery -Command 'FUNCtion:MODE?'
    $verifiedCv = [double]::Parse((Invoke-ScpiQuery -Command ':CV:VOLTage?'), $culture)
    $stateAfterRaw = Invoke-ScpiQuery -Command 'FETCh:STAte?'
    $stateAfterValue = 0
    $stateAfterOk = [int]::TryParse($stateAfterRaw, [ref]$stateAfterValue)
    if (($verifiedRemote -ne '1') -or ($verifiedMode -ne '2') -or
        ([Math]::Abs($verifiedCv - $currentCv) -gt 0.01) -or
        (-not $stateAfterOk) -or
        ((-not $PreserveLoadState.IsPresent) -and (($stateAfterValue -band 1) -eq 0))) {
        throw "RK8510 CV setup verification failed: remote=$verifiedRemote mode=$verifiedMode cv=$verifiedCv state=$stateAfterRaw"
    }
    $writer.WriteLine(('# instrument={0}; port={1}; baud={2}; address={3}; original_mode={4}; original_cv_v={5}; original_remote={6}; leave_load_on={7}; preserve_load_state={8}; state_before={9}; state_after={10}; load_open_command_sent={11}' -f
        $identity, $PortName, $BaudRate, $Address, $originalMode, $originalCv, $originalRemote,
        $LeaveLoadOn.IsPresent.ToString().ToLowerInvariant(),
        $PreserveLoadState.IsPresent.ToString().ToLowerInvariant(), $stateBeforeRaw, $stateAfterRaw,
        $loadWasOpened.ToString().ToLowerInvariant()))

    $started = Get-Date
    $deadline = $started.AddSeconds($Seconds)
    $nextStep = 1
    $pendingEvent = ('cv_set_{0:F3}_load_on' -f $currentCv)
    while ((Get-Date) -lt $deadline) {
        $sampleTime = Get-Date
        $elapsedMs = [long](($sampleTime - $started).TotalMilliseconds)
        while (($nextStep -lt $steps.Count) -and
               ($elapsedMs -ge $steps[$nextStep].TimeMs)) {
            $currentCv = $steps[$nextStep].Voltage
            Send-ScpiCommand -Command (':CV:VOLTage {0:F3}' -f $currentCv)
            $verifiedCv = [double]::Parse((Invoke-ScpiQuery -Command ':CV:VOLTage?'), $culture)
            if ([Math]::Abs($verifiedCv - $currentCv) -gt 0.01) {
                throw "RK8510 CV step verification failed: requested=$currentCv readback=$verifiedCv"
            }
            $pendingEvent = ('cv_set_{0:F3}' -f $currentCv)
            $nextStep++
        }

        $raw = Invoke-ScpiQuery -Command 'FETC?'
        $stateRaw = Invoke-ScpiQuery -Command 'FETCh:STAte?'
        $stateValue = 0
        $stateOk = [int]::TryParse($stateRaw, [ref]$stateValue)
        $loadActive = $stateOk -and (($stateValue -band 1) -ne 0)
        $parts = $raw.Split(',')
        $voltage = 0.0
        $current = 0.0
        $power = 0.0
        $parseOk = ($parts.Count -ge 3) -and
            [double]::TryParse($parts[0], [System.Globalization.NumberStyles]::Float, $culture, [ref]$voltage) -and
            [double]::TryParse($parts[1], [System.Globalization.NumberStyles]::Float, $culture, [ref]$current) -and
            [double]::TryParse($parts[2], [System.Globalization.NumberStyles]::Float, $culture, [ref]$power)

        $writer.WriteLine(('{0},{1},{2:O},{3},{4},{5},2,{6},{7},{8},{9},{10},{11},{12},"{13}"' -f
            'sample',
            $pendingEvent,
            $sampleTime,
            $elapsedMs,
            $PortName,
            $Address,
            $currentCv.ToString('F3', $culture),
            $voltage.ToString('F6', $culture),
            $current.ToString('F6', $culture),
            $power.ToString('F6', $culture),
            $stateRaw,
            $loadActive.ToString().ToLowerInvariant(),
            $parseOk.ToString().ToLowerInvariant(),
            $raw.Replace('"', '""')))
        $pendingEvent = ''
        Start-Sleep -Milliseconds $IntervalMs
    }
}
finally {
    if ($serial.IsOpen -and -not $LeaveLoadOn.IsPresent -and -not $PreserveLoadState.IsPresent) {
        try {
            $stateBeforeOffRaw = Invoke-ScpiQuery -Command 'FETCh:STAte?'
            $stateBeforeOffValue = 0
            if ([int]::TryParse($stateBeforeOffRaw, [ref]$stateBeforeOffValue) -and
                (($stateBeforeOffValue -band 1) -ne 0)) {
                Send-ScpiCommand -Command 'FUNCtion:OFF'
            }
        }
        catch {
            # Preserve the primary error while still attempting a clean disconnect.
        }
        $serial.Close()
    }
    $serial.Dispose()
    $writer.Dispose()
}

Write-Output "LOG_PATH=$resolvedLogPath"
