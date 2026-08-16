param(
    [string]$SessionRoot = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
$culture = [System.Globalization.CultureInfo]::InvariantCulture
$rawDirectory = Join-Path $SessionRoot 'raw_serial'
$datasetDirectory = Join-Path $SessionRoot 'dataset'
[System.IO.Directory]::CreateDirectory($datasetDirectory) | Out-Null

$telemetryCsvPath = Join-Path $datasetDirectory 'telemetry.csv'
$telemetryJsonlPath = Join-Path $datasetDirectory 'telemetry.jsonl'
$eventsJsonlPath = Join-Path $datasetDirectory 'events.jsonl'
$commandsCsvPath = Join-Path $datasetDirectory 'commands.csv'
$statsPath = Join-Path $datasetDirectory 'build_stats.json'

$fieldNames = @(
    'seq', 'vin_v', 'vout_v', 'iin_a', 'iout_a', 'ipri_a', 'isec_a',
    'tpri_c', 'tsec_c', 'pin_w', 'pout_w', 'vafe_v', 'phase_ns',
    'plimit_label_w', 'vref_v', 'state_code', 'control_on', 'pwm_on',
    'fault_hex', 'adc_valid', 'mod_requested', 'mod_effective'
)

function Convert-Number([string]$Text, [string]$Name) {
    if ($Name -eq 'fault_hex') {
        return $Text
    }
    if ($Name -in @('seq', 'phase_ns', 'state_code', 'control_on', 'pwm_on',
                    'adc_valid', 'mod_requested', 'mod_effective')) {
        return [int64]::Parse($Text, $culture)
    }
    return [double]::Parse($Text, $culture)
}

$telemetryRows = [System.Collections.Generic.List[object]]::new()
$commandRows = [System.Collections.Generic.List[object]]::new()
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$telemetryJsonl = [System.IO.StreamWriter]::new($telemetryJsonlPath, $false, $utf8NoBom)
$eventsJsonl = [System.IO.StreamWriter]::new($eventsJsonlPath, $false, $utf8NoBom)
$sampleCount = 0
$eventCount = 0
$parseErrorCount = 0
$globalSampleIndex = 0
$lastCommand = ''
$lastCommandTime = $null
$lastSequence = $null
$deviceBootIndex = 1

try {
    $files = Get-ChildItem -LiteralPath $rawDirectory -Filter '*.log' | Sort-Object Name
    foreach ($file in $files) {
        $lineNumber = 0
        $sampleIndex = 0
        $sessionOpenTime = $null

        foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
            $lineNumber++
            if ($line.StartsWith('DAB:')) {
                $parts = $line.Substring(4).Split(',')
                if ($parts.Count -ne $fieldNames.Count) {
                    $event = [ordered]@{
                        session_id = '2026-08-14_18V_24R3'
                        source_file = $file.Name
                        line_no = $lineNumber
                        event_type = 'parse_error'
                        message = "Expected $($fieldNames.Count) telemetry fields, got $($parts.Count)"
                        raw = $line
                    }
                    $eventsJsonl.WriteLine(($event | ConvertTo-Json -Compress -Depth 4))
                    $eventCount++
                    $parseErrorCount++
                    continue
                }

                try {
                    $sampleIndex++
                    $globalSampleIndex++
                    $sequence = Convert-Number $parts[0] 'seq'
                    if (($null -ne $lastSequence) -and ($sequence -lt $lastSequence)) {
                        $deviceBootIndex++
                    }
                    $lastSequence = $sequence
                    $row = [ordered]@{
                        session_id = '2026-08-14_18V_24R3'
                        device_boot_id = ('boot_{0:d2}' -f $deviceBootIndex)
                        source_file = $file.Name
                        line_no = $lineNumber
                        sample_index_in_file = $sampleIndex
                        sample_index_global = $globalSampleIndex
                        session_open_time = $sessionOpenTime
                        last_command_time = $lastCommandTime
                        last_command = $lastCommand
                        device_uptime_s = $sequence / 149880.0
                    }
                    for ($i = 0; $i -lt $fieldNames.Count; $i++) {
                        $row[$fieldNames[$i]] = Convert-Number $parts[$i] $fieldNames[$i]
                    }
                    $obj = [pscustomobject]$row
                    $telemetryRows.Add($obj)
                    $telemetryJsonl.WriteLine(($obj | ConvertTo-Json -Compress -Depth 4))
                    $sampleCount++
                }
                catch {
                    $event = [ordered]@{
                        session_id = '2026-08-14_18V_24R3'
                        source_file = $file.Name
                        line_no = $lineNumber
                        event_type = 'parse_error'
                        message = $_.Exception.Message
                        raw = $line
                    }
                    $eventsJsonl.WriteLine(($event | ConvertTo-Json -Compress -Depth 4))
                    $eventCount++
                    $parseErrorCount++
                }
                continue
            }

            if ([string]::IsNullOrWhiteSpace($line)) {
                continue
            }

            $eventType = 'device_event'
            $eventTime = $null
            $command = ''
            if ($line -match '^#HOST\s+(\S+)\s+(OPEN|TX-END|TX|CLOSE)\s*(.*)$') {
                $eventTime = $Matches[1]
                $hostAction = $Matches[2]
                $payload = $Matches[3]
                switch ($hostAction) {
                    'OPEN' {
                        $eventType = 'host_open'
                        $sessionOpenTime = $eventTime
                    }
                    'TX' {
                        $eventType = 'command_tx'
                        $command = $payload
                        $lastCommand = $command
                        $lastCommandTime = $eventTime
                    }
                    'TX-END' {
                        $eventType = 'command_tx_end'
                        $command = $payload
                        $lastCommand = $command
                        $lastCommandTime = $eventTime
                    }
                    'CLOSE' { $eventType = 'host_close' }
                }
                if ($command.Length -gt 0) {
                    $commandRows.Add([pscustomobject][ordered]@{
                        session_id = '2026-08-14_18V_24R3'
                        source_file = $file.Name
                        line_no = $lineNumber
                        timestamp = $eventTime
                        direction = 'host_to_device'
                        command = $command
                    })
                }
            }

            $event = [ordered]@{
                session_id = '2026-08-14_18V_24R3'
                source_file = $file.Name
                line_no = $lineNumber
                timestamp = $eventTime
                event_type = $eventType
                command = $command
                raw = $line
            }
            $eventsJsonl.WriteLine(($event | ConvertTo-Json -Compress -Depth 4))
            $eventCount++
        }
    }
}
finally {
    $telemetryJsonl.Dispose()
    $eventsJsonl.Dispose()
}

$telemetryRows | Export-Csv -LiteralPath $telemetryCsvPath -NoTypeInformation -Encoding UTF8
$commandRows | Export-Csv -LiteralPath $commandsCsvPath -NoTypeInformation -Encoding UTF8

$stats = [ordered]@{
    session_id = '2026-08-14_18V_24R3'
    generated_at = [DateTimeOffset]::Now.ToString('o')
    raw_log_count = (Get-ChildItem -LiteralPath $rawDirectory -Filter '*.log').Count
    telemetry_sample_count = $sampleCount
    event_count = $eventCount
    command_count = $commandRows.Count
    parse_error_count = $parseErrorCount
    telemetry_schema_version = '1.0.0'
}
[System.IO.File]::WriteAllText(
    $statsPath,
    ($stats | ConvertTo-Json -Depth 4),
    $utf8NoBom
)

Write-Output ($stats | ConvertTo-Json -Compress)
