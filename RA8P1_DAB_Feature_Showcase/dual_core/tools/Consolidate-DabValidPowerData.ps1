[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $TelemetryCsv,

    [Parameter(Mandatory = $true)]
    [string] $ConditionMapCsv,

    [Parameter(Mandatory = $true)]
    [string] $TargetDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Convert-ToNumber([string] $Text)
{
    return [double]::Parse($Text, [Globalization.CultureInfo]::InvariantCulture)
}

function Test-TransferRow($Row)
{
    return (([int] $Row.control_on -eq 1) -and
            ([int] $Row.pwm_on -eq 1) -and
            (($Row.fault_hex -eq '0') -or ($Row.fault_hex -eq '00000000')) -and
            ([int] $Row.adc_valid -eq 1) -and
            ([int] $Row.state_code -in 6, 7) -and
            ((Convert-ToNumber $Row.phase_ns) -gt 0.0) -and
            ((Convert-ToNumber $Row.pin_w) -gt 2.0) -and
            ((Convert-ToNumber $Row.pout_w) -gt 2.0))
}

function Test-SourceInRule([string] $SourceFile, $Rule)
{
    if ($Rule.source_file)
    {
        return $SourceFile -eq $Rule.source_file
    }

    return (($SourceFile -ge $Rule.source_from) -and
            ($SourceFile -le $Rule.source_to))
}

if (-not (Test-Path -LiteralPath $TelemetryCsv -PathType Leaf))
{
    throw "Telemetry CSV not found: $TelemetryCsv"
}
if (-not (Test-Path -LiteralPath $ConditionMapCsv -PathType Leaf))
{
    throw "Condition map not found: $ConditionMapCsv"
}

$rules = @(Import-Csv -LiteralPath $ConditionMapCsv)
if ($rules.Count -eq 0)
{
    throw 'Condition map is empty.'
}

$duplicateNames = @($rules | Group-Object condition_file | Where-Object Count -gt 1)
if ($duplicateNames.Count -gt 0)
{
    # Multiple source ranges may intentionally append to one condition file.
    # The rule rows still need unique rule IDs for traceability.
    $duplicateRuleIds = @($rules | Group-Object rule_id | Where-Object Count -gt 1)
    if ($duplicateRuleIds.Count -gt 0)
    {
        throw 'Condition map contains duplicate rule_id values.'
    }
}

New-Item -ItemType Directory -Path $TargetDirectory -Force | Out-Null
$targetConditionMap = Join-Path $TargetDirectory 'CONDITION_MAP.csv'
$sourceConditionMapFullPath = [System.IO.Path]::GetFullPath($ConditionMapCsv)
$targetConditionMapFullPath = [System.IO.Path]::GetFullPath($targetConditionMap)
if (-not $sourceConditionMapFullPath.Equals($targetConditionMapFullPath, [System.StringComparison]::OrdinalIgnoreCase))
{
    Copy-Item -LiteralPath $ConditionMapCsv -Destination $targetConditionMap -Force
}

$allRows = @(Import-Csv -LiteralPath $TelemetryCsv)
$transferRows = @($allRows | Where-Object { Test-TransferRow $_ })
$mappedKeys = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$summaries = [Collections.Generic.List[object]]::new()

foreach ($conditionName in @($rules.condition_file | Sort-Object -Unique))
{
    if ([IO.Path]::GetFileName($conditionName) -ne $conditionName -or
        [IO.Path]::GetExtension($conditionName) -ne '.csv')
    {
        throw "condition_file must be a plain .csv filename: $conditionName"
    }

    $conditionRules = @($rules | Where-Object condition_file -eq $conditionName)
    $selected = @($transferRows | Where-Object {
        $row = $_
        @($conditionRules | Where-Object {
            Test-SourceInRule $row.source_file $_
        }).Count -gt 0
    })

    $normalized = foreach ($row in $selected)
    {
        $rule = @($conditionRules | Where-Object {
            Test-SourceInRule $row.source_file $_
        })[0]
        $key = "$($row.source_file)|$($row.seq)"
        [void] $mappedKeys.Add($key)

        $externalVout = ''
        $externalPout = ''
        $externalLabel = 0
        if ($rule.external_label_phase_ns -and
            ([int] $row.phase_ns -eq [int] $rule.external_label_phase_ns))
        {
            $externalVout = $rule.external_vout_v
            $externalPout = $rule.external_pout_w
            $externalLabel = 1
        }

        [pscustomobject] [ordered] @{
            condition_file             = $conditionName
            rule_id                    = $rule.rule_id
            session_id                 = $row.session_id
            device_boot_id             = $row.device_boot_id
            source_file                = $row.source_file
            source_line                = $row.line_no
            session_open_time          = $row.session_open_time
            last_command_time          = $row.last_command_time
            last_command               = $row.last_command
            device_uptime_s            = $row.device_uptime_s
            sequence                   = $row.seq
            firmware_vin_v             = $row.vin_v
            firmware_vout_v            = $row.vout_v
            firmware_iin_a             = $row.iin_a
            firmware_iout_a            = $row.iout_a
            firmware_ipri_a            = $row.ipri_a
            firmware_isec_a            = $row.isec_a
            primary_temperature_c      = $row.tpri_c
            secondary_temperature_c    = $row.tsec_c
            firmware_pin_w             = $row.pin_w
            firmware_pout_w            = $row.pout_w
            afe_reference_v            = $row.vafe_v
            phase_ns                   = $row.phase_ns
            power_limit_label_w        = $row.plimit_label_w
            voltage_reference_v        = $row.vref_v
            state_code                 = $row.state_code
            control_on                 = $row.control_on
            pwm_on                     = $row.pwm_on
            fault_hex                  = $row.fault_hex
            adc_valid                  = $row.adc_valid
            modulation_requested       = $row.mod_requested
            modulation_effective       = $row.mod_effective
            bench_supply_v             = $rule.bench_supply_v
            load_ohm                   = $rule.load_ohm
            external_vout_v            = $externalVout
            external_pout_w            = $externalPout
            has_sparse_external_label  = $externalLabel
            actual_power_transfer      = 1
            accepted_for_training      = 1
            measurement_note           = $rule.measurement_note
        }
    }

    $targetPath = Join-Path $TargetDirectory $conditionName
    $combined = @($normalized)
    if (Test-Path -LiteralPath $targetPath -PathType Leaf)
    {
        $combined = @((Import-Csv -LiteralPath $targetPath) + $combined)
    }

    $deduplicated = @($combined |
        Group-Object { "$($_.source_file)|$($_.sequence)" } |
        ForEach-Object { $_.Group[0] } |
        Sort-Object session_open_time, @{Expression = {[uint64] $_.sequence}})

    if ($deduplicated.Count -gt 0)
    {
        $deduplicated | Export-Csv -LiteralPath $targetPath -NoTypeInformation -Encoding utf8
        $summaries.Add([pscustomobject] [ordered] @{
            condition_file = $conditionName
            sample_count = $deduplicated.Count
            source_file_count = @($deduplicated.source_file | Sort-Object -Unique).Count
            firmware_vin_avg_v = [math]::Round(($deduplicated | Measure-Object firmware_vin_v -Average).Average, 3)
            firmware_vout_avg_v = [math]::Round(($deduplicated | Measure-Object firmware_vout_v -Average).Average, 3)
            firmware_pin_avg_w = [math]::Round(($deduplicated | Measure-Object firmware_pin_w -Average).Average, 3)
            firmware_pout_avg_w = [math]::Round(($deduplicated | Measure-Object firmware_pout_w -Average).Average, 3)
            phase_min_ns = ($deduplicated | Measure-Object phase_ns -Minimum).Minimum
            phase_max_ns = ($deduplicated | Measure-Object phase_ns -Maximum).Maximum
        })
    }
}

$unmapped = @($transferRows | Where-Object {
    -not $mappedKeys.Contains("$($_.source_file)|$($_.seq)")
})

$summaries | Sort-Object condition_file |
    Export-Csv -LiteralPath (Join-Path $TargetDirectory 'INDEX.csv') -NoTypeInformation -Encoding utf8

if ($unmapped.Count -gt 0)
{
    $unmappedFiles = @($unmapped.source_file | Sort-Object -Unique)
    throw ("{0} valid transfer rows are not mapped. Add their source files to CONDITION_MAP.csv. Files: {1}" -f
           $unmapped.Count, ($unmappedFiles -join ', '))
}

Write-Output ("Consolidated {0} accepted samples into {1} condition files at {2}" -f
              $transferRows.Count, $summaries.Count, $TargetDirectory)
