[CmdletBinding()]
param(
    [string] $ReferenceProject = 'C:\Users\LENOVO\e2_studio\workspace\RA8P1_Project1',
    [switch] $RequireBuildArtifacts,
    [switch] $AllowEthernetPhase1,
    [switch] $AllowEnergyControlV39
)

$ErrorActionPreference = 'Stop'

$dualRoot = Split-Path -Parent $PSScriptRoot
$solution = Join-Path $dualRoot 'RA8P1_DAB_DualCore_Solution'
$cpu0 = Join-Path $dualRoot 'RA8P1_DAB_DualCore_Solution_CPU0'
$cpu1 = Join-Path $dualRoot 'RA8P1_DAB_DualCore_Solution_CPU1'
$script:failures = 0

function Pass([string] $message) {
    Write-Host "[PASS] $message" -ForegroundColor Green
}

function Fail([string] $message) {
    Write-Host "[FAIL] $message" -ForegroundColor Red
    $script:failures++
}

function Assert-True([bool] $condition, [string] $message) {
    if ($condition) { Pass $message } else { Fail $message }
}

function Read-Text([string] $path) {
    return (Get-Content -Raw -Encoding UTF8 -LiteralPath $path).Replace("`r`n", "`n")
}

function Same-Hash([string] $left, [string] $right) {
    if (!(Test-Path -LiteralPath $left) -or !(Test-Path -LiteralPath $right)) {
        return $false
    }
    return ((Get-FileHash -Algorithm SHA256 -LiteralPath $left).Hash -eq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $right).Hash)
}

function Same-Sequence([object[]] $left, [object[]] $right) {
    [string[]] $leftText = @($left | ForEach-Object { [string] $_ })
    [string[]] $rightText = @($right | ForEach-Object { [string] $_ })
    return ([string]::Join("`n", $leftText) -ceq
            [string]::Join("`n", $rightText))
}

function Read-ZipEntryXml([string] $zipPath, [string] $entryName) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $entry = $archive.GetEntry($entryName)
        if ($null -eq $entry) {
            throw "Archive entry '$entryName' is missing from '$zipPath'"
        }
        $reader = [System.IO.StreamReader]::new($entry.Open())
        try {
            return [xml] $reader.ReadToEnd()
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $archive.Dispose()
    }
}

function Normalize-AllowedSource([string] $name, [string] $text) {
    switch ($name) {
        'User_Init_Task_entry.c' {
            $text = $text -replace '(?m)^#include "dab_dual_core_cpu0\.h"\n', ''
            $text = $text -replace '(?m)^    dab_dual_core_cpu0_start\(\);\n', ''
            if ($AllowEthernetPhase1) {
                $text = $text -replace '(?m)^#include "dab_ethernet\.h"\n', ''
                $text = $text -replace '(?m)^#include "dab_remote_control\.h"\n', ''
                $text = $text -replace '(?m)^static char\s+s_load_mode\[DAB_REMOTE_TEXT_LENGTH\] = "resistive_load";\n', ''
                $text = $text -replace '(?m)^static void dab_remote_command_service\(void\);\n', ''
                $text = $text -replace '(?m)^static void dab_remote_snapshot_publish_now\(void\);\n', ''
                $text = $text.Replace(
                    '    dab_log("[DRV] RMAC1/RMII1 + LAN8720A PHY@0 + FreeRTOS+TCP static-IP test enabled\r\n");',
                    '    dab_log("[DRV] LAN8720A MDIO library compiled; ESWM/RMII1 transport remains power-test disabled\r\n");')
                $text = $text -replace '(?ms)^static void dab_remote_text_set\(.*?^static void dab_single_gate_diag', 'static void dab_single_gate_diag'
                $text = $text -replace '(?ms)^    dab_remote_snapshot_publish_now\(\);\n    if \(dab_ethernet_start\(\)\)\n    \{.*?^    \}\n\n(?=    TickType_t last_telemetry)', ''
                $text = $text -replace '(?m)^    TickType_t last_remote_snapshot = last_telemetry;\n', ''
                $text = $text -replace '(?m)^        dab_remote_command_service\(\);\n', ''
                $text = $text -replace '(?ms)^        if \(\(now - last_remote_snapshot\) >= pdMS_TO_TICKS\(50U\)\)\n        \{\n            last_remote_snapshot = now;\n            dab_remote_snapshot_publish_now\(\);\n        \}\n', ''
            }
            $manualStopMiss = @'
            if (s_pwm_update_miss_count < UINT32_MAX)
            {
                s_pwm_update_miss_count++;
            }
            if (s_pwm_update_miss_count > 100U)
            {
                if (DAB_SOFTWARE_AUTO_TRIP_ENABLE)
                {
                    dab_fault_latch(DAB_FAULT_PWM_UPDATE);
                }
                else if (101U == s_pwm_update_miss_count)
                {
                    dab_log("[DAB][WARN] PWM update missed >100 times; retaining last applied phase; manual-stop mode remains ON\r\n");
                }
            }
'@
            $referenceMiss = @'
            s_pwm_update_miss_count++;
            if (s_pwm_update_miss_count > 100U)
            {
                dab_fault_latch(DAB_FAULT_PWM_UPDATE);
            }
'@
            $text = $text.Replace($manualStopMiss, $referenceMiss)
        }
        'ra8p1_dab_peripheral_test.c' {
            $text = $text -replace '(?m)^#include "dab_dual_core_cpu0\.h"\n', ''
            $text = $text -replace '(?m)^    dab_dual_core_cpu0_start\(\);\n', ''
        }
        'dab_measurements.c' {
            $text = $text -replace '(?m)^#include "dab_stdio_compat\.h"\n', ''
            $text = $text.Replace('dab_stdio_printf', 'printf')
            if ($text -notmatch '(?m)^#include <stdio\.h>$') {
                $text = $text.Replace('#include <string.h>', "#include <stdio.h>`n#include <string.h>")
            }
        }
        'dab_ra8p1_scope_test.c' {
            $text = $text -replace '(?m)^#include "dab_stdio_compat\.h"\n', ''
            $text = $text.Replace('#define printf dab_stdio_printf', '#include <stdio.h>')
        }
        'dab_first_power_config.h' {
            $text = $text -replace '(?m)^#define DAB_FW_VERSION\s+"[^"]+"$', '#define DAB_FW_VERSION "<ADAPTER_VERSION>"'
            $start = $text.IndexOf('/* Test build:')
            if ($start -lt 0) {
                $start = $text.IndexOf("/*`n * Software-protection profile.")
            }
            $end = $text.IndexOf('/* GPT0/1/5/8', $start)
            if (($start -lt 0) -or ($end -lt 0)) {
                throw 'Could not locate software-protection section'
            }
            $text = $text.Substring(0, $start) + "<SOFTWARE_PROTECTION_PROFILE>`n" + $text.Substring($end)
            $text = $text -replace '(?ms)\n#if \(DAB_SOFTWARE_AUTO_TRIP_ENABLE != 0U\)\n#error "The force-PWM commissioning image requires manual-stop-only control\."\n#endif\n', ''
        }
        'dab_pwm_hw.c' {
            $text = $text.Replace("(uint32_t) IOPORT_CFG_PERIPHERAL_PIN |`n                         (uint32_t) IOPORT_PERIPHERAL_GPT1", 'IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_GPT1')
            $manualStopOpen = @'
    dab_pwm_hw_pins_gpio_low();
#if DAB_POEG_PWM_SHUTDOWN_ENABLE
    fsp_err_t const poeg_err = R_POEG_Open(&g_poeg0_ctrl, &g_poeg0_cfg);
    if (FSP_SUCCESS != poeg_err)
    {
        return poeg_err;
    }
#endif
'@
            $referenceOpen = @'
    dab_pwm_hw_pins_gpio_low();
    fsp_err_t err = R_POEG_Open(&g_poeg0_ctrl, &g_poeg0_cfg);
    if (FSP_SUCCESS != err)
    {
        return err;
    }
'@
            $text = $text.Replace($manualStopOpen, $referenceOpen)
            $text = $text.Replace("#if !DAB_POEG_PWM_SHUTDOWN_ENABLE`n    return true;`n#else`n", '')
            $text = $text -replace '(?m)^#endif\n(?=}\n\nbool dab_pwm_hw_(?:poeg_ok|is_open))', ''
        }
    }
    return $text
}

function Xml-Signatures([xml] $document, [string] $xpath) {
    return @($document.SelectNodes($xpath) | ForEach-Object {
        $attributes = @($_.Attributes | ForEach-Object { $_.Name + '=' + $_.Value } | Sort-Object)
        $_.Name + '|' + [string]::Join('|', $attributes)
    } | Sort-Object)
}

foreach ($path in @($ReferenceProject, $solution, $cpu0, $cpu1)) {
    Assert-True (Test-Path -LiteralPath $path) "project path exists: $path"
}

$formalXmlFiles = @(
    (Join-Path $solution 'solution.xml'),
    (Join-Path $solution '.secure_xml'),
    (Join-Path $solution '.project'),
    (Join-Path $cpu0 'configuration.xml'),
    (Join-Path $cpu0 '.secure_xml'),
    (Join-Path $cpu0 '.project'),
    (Join-Path $cpu0 '.cproject'),
    (Join-Path $cpu0 'RA8P1_DAB_DualCore_Solution_CPU0 Debug_Flat.launch'),
    (Join-Path $cpu1 'configuration.xml'),
    (Join-Path $cpu1 '.secure_xml'),
    (Join-Path $cpu1 '.project'),
    (Join-Path $cpu1 '.cproject'),
    (Join-Path $cpu1 'RA8P1_DAB_DualCore_Solution_CPU1 Debug_Multicore.launch')
)
$xmlParseFailures = @()
foreach ($xmlPath in $formalXmlFiles) {
    try {
        [xml] (Get-Content -Raw -Encoding UTF8 -LiteralPath $xmlPath) | Out-Null
    }
    catch {
        $xmlParseFailures += $xmlPath
    }
}
Assert-True ($xmlParseFailures.Count -eq 0) ('all formal e2 studio XML files parse as UTF-8 XML' +
    $(if ($xmlParseFailures.Count) { ': ' + [string]::Join(', ', $xmlParseFailures) } else { '' }))

$referenceSource = Join-Path $ReferenceProject 'src'
$cpu0Source = Join-Path $cpu0 'src'
$allowedDifferences = @(
    'User_Init_Task_entry.c',
    'ra8p1_dab_peripheral_test.c',
    'dab_measurements.c',
    'dab_ra8p1_scope_test.c',
    'dab_first_power_config.h',
    'dab_pwm_hw.c'
)

$missing = @()
$unexpected = @()
foreach ($file in Get-ChildItem -LiteralPath $referenceSource -File) {
    $candidate = Join-Path $cpu0Source $file.Name
    if (!(Test-Path -LiteralPath $candidate)) {
        $missing += $file.Name
    } elseif ($file.Name -notin $allowedDifferences) {
        if ((Read-Text $file.FullName) -cne (Read-Text $candidate)) { $unexpected += $file.Name }
    } else {
        $referenceText = Normalize-AllowedSource $file.Name (Read-Text $file.FullName)
        $candidateText = Normalize-AllowedSource $file.Name (Read-Text $candidate)
        if ($referenceText -cne $candidateText) { $unexpected += $file.Name }
    }
}
Assert-True ($missing.Count -eq 0) 'CPU0 contains every source file from the reference project'
if ($AllowEnergyControlV39) {
    $unexpected = @($unexpected | Where-Object {
        $_ -notin @('User_Init_Task_entry.c', 'dab_first_power_config.h')
    })
}
Assert-True ($unexpected.Count -eq 0) ('all common CPU0 sources are identical after the documented dual-core/test adapters' +
    $(if ($unexpected.Count) { ': ' + [string]::Join(', ', $unexpected) } else { '' }))

$protection = Read-Text (Join-Path $cpu0Source 'dab_first_power_config.h')
if ($AllowEnergyControlV39) {
    Assert-True (($protection -match 'dualcore-energy-control-v39') -and
                 ($protection -match '#define DAB_SW_PROTECTION_PROFILE\s+DAB_SW_PROTECTION_PROFILE_ALL_ENABLED') -and
                 ($protection -match '#define DAB_MANUAL_STOP_TEST_MODE\s+\(0U\)')) 'v39 restores compile-time software trips and identifies the energy-control image'
    $energyApplication = Read-Text (Join-Path $cpu0Source 'User_Init_Task_entry.c')
    Assert-True (($energyApplication -match 'DAB_GUARD_PROFILE_BENCH') -and
                 ($energyApplication -match 'DAB_CONTROL_ARCH_OUTPUT_CURRENT_PI') -and
                 ($energyApplication -match 'DAB_CONTROL_ARCH_ENERGY_SUPERVISOR') -and
                 ($energyApplication -match 'ALG:%lu')) 'v39 contains runtime BENCH guards, CPU0 current PI, CPU1 supervisor selection and joined algorithm telemetry'
    $energyShared = Read-Text (Join-Path $dualRoot 'shared\dab_energy_control.c')
    Assert-True (($energyApplication -match 's_control_architecture\s*=\s*\r?\n\s*DAB_CONTROL_ARCH_VOLTAGE_PI_SINGLE') -and
                 ($energyApplication -match 'static bool\s+s_algorithm_logging_enabled;') -and
                 ($energyApplication -match 'if \(!s_algorithm_logging_enabled\)') -and
                 ($energyShared -match 'config\.mode\s*=\s*DAB_ENERGY_MODE_OFF')) 'v39 boots in the legacy voltage-PI path with energy control and ALG extension opt-in only'
}
else {
    Assert-True ($protection -match '#define DAB_SW_PROTECTION_PROFILE\s+DAB_SW_PROTECTION_PROFILE_REFERENCE_TEST') 'default protection profile matches the reference test firmware'
    Assert-True (($protection -match '#define DAB_MANUAL_STOP_TEST_MODE\s+\(1U\)') -and
                 ($protection -match '#define DAB_POEG_PWM_SHUTDOWN_ENABLE\s+\(0U\)') -and
                 ($protection -match 'Manual-stop test mode requires every MCU automatic shutdown path disabled')) 'test image compile-time lock disables all MCU automatic shutdown paths including POEG'
}
foreach ($switchName in @(
    'DAB_SOFTWARE_AUTO_TRIP_ENABLE',
    'DAB_GPIO_OC_TRIP_ENABLE',
    'DAB_ADC_CURRENT_GUARDS_ENABLE',
    'DAB_VIN_UV_TRIP_ENABLE',
    'DAB_NO_RISE_TRIP_ENABLE'
)) {
    Assert-True ($protection -match "#ifndef $switchName") "software protection is independently overridable: $switchName"
}
$cpu0Application = Read-Text (Join-Path $cpu0Source 'User_Init_Task_entry.c')
$cpu0Pwm = Read-Text (Join-Path $cpu0Source 'dab_pwm_hw.c')
$pwmMissPathsPresent = (($cpu0Application -match '(?s)DAB_SOFTWARE_AUTO_TRIP_ENABLE\).*?dab_fault_latch\(DAB_FAULT_PWM_UPDATE\)') -and
                        ($cpu0Application -match 'retaining last applied phase; manual-stop mode remains ON'))
$poegSwitchPresent = (($cpu0Pwm -match '#if DAB_POEG_PWM_SHUTDOWN_ENABLE\s+fsp_err_t const poeg_err = R_POEG_Open') -and
                      ([regex]::Matches($cpu0Pwm, '#if !DAB_POEG_PWM_SHUTDOWN_ENABLE\s+return true;')).Count -eq 2)
if ($AllowEnergyControlV39) {
    Assert-True $pwmMissPathsPresent 'v39 retains both protected PWM-update fault handling and the legacy manual-stop branch'
    Assert-True $poegSwitchPresent 'v39 retains the explicit POEG software-shutdown compile switch and legacy-disabled path'
}
else {
    Assert-True $pwmMissPathsPresent 'repeated PWM register-update misses cannot auto-stop the manual-stop test image'
    Assert-True $poegSwitchPresent 'POEG open/reset/status paths are compiled out in the manual-stop test image'
}

[xml] $referenceXml = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $ReferenceProject 'configuration.xml')
[xml] $cpu0Xml = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $cpu0 'configuration.xml')
[xml] $cpu1Xml = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $cpu1 'configuration.xml')
[xml] $solutionXml = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $solution 'solution.xml')
[xml] $solutionSecureXml = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $solution '.secure_xml')
[xml] $cpu0SecureXml = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $cpu0 '.secure_xml')
[xml] $cpu1SecureXml = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $cpu1 '.secure_xml')

Assert-True ($cpu0Xml.SelectSingleNode('//option[@key="#TargetARCHITECTURE#"]/@value').'#text' -eq 'cortex-m85') 'CPU0 target is Cortex-M85'
Assert-True ($cpu1Xml.SelectSingleNode('//option[@key="#TargetARCHITECTURE#"]/@value').'#text' -eq 'cortex-m33') 'CPU1 target is Cortex-M33'

$referenceModules = @($referenceXml.SelectNodes('//module/property') | ForEach-Object { $_.id + '=' + $_.value } | Sort-Object)
$cpu0Modules = @($cpu0Xml.SelectNodes('//module/property') | ForEach-Object { $_.id + '=' + $_.value } | Sort-Object)
if ($AllowEthernetPhase1) {
    $missingReferenceModules = @($referenceModules | Where-Object { $_ -notin $cpu0Modules })
    $ethernetPropertyPrefixes = @(
        'module.driver.ether_phy_lsi.',
        'module.driver.ether_phy.',
        'module.driver.ether.',
        'module.driver.freertos_plus_tcp_on_ether.',
        'module.driver.layer3_switch.',
        'module.driver.rmac_phy.',
        'module.driver.rmac.'
    )
    $unexpectedModuleProperties = @($cpu0Modules | Where-Object {
        if ($_ -in $referenceModules) { return $false }
        foreach ($prefix in $ethernetPropertyPrefixes) {
            if ($_.StartsWith($prefix, [System.StringComparison]::Ordinal)) { return $false }
        }
        return $true
    })
    Assert-True (($missingReferenceModules.Count -eq 0) -and
                 ($unexpectedModuleProperties.Count -eq 0)) "all reference FSP module properties remain unchanged; additions are Ethernet-only"
}
else {
    Assert-True (Same-Sequence $referenceModules $cpu0Modules) "FSP module properties match ($($referenceModules.Count) entries)"
}

if ($AllowEthernetPhase1) {
    $ethernetSource = Read-Text (Join-Path $cpu0Source 'dab_ethernet.c')
    $ethernetHeader = Read-Text (Join-Path $cpu0Source 'dab_ethernet.h')
    $remoteSource = Read-Text (Join-Path $cpu0Source 'dab_remote_control.c')
    $propertyValue = @{}
    foreach ($property in $cpu0Xml.SelectNodes('//property')) {
        $propertyValue[$property.id] = $property.value
    }
    Assert-True (($cpu0Xml.SelectNodes('//component[@subgroup="r_rmac"]').Count -eq 1) -and
                 ($cpu0Xml.SelectNodes('//component[@subgroup="r_rmac_phy"]').Count -eq 1) -and
                 ($cpu0Xml.SelectNodes('//component[@subgroup="rm_freertos_plus_tcp"]').Count -eq 1) -and
                 ($cpu0Xml.SelectNodes('//component[@subgroup="r_ether"]').Count -eq 0)) 'Ethernet uses r_rmac/r_rmac_phy and FreeRTOS+TCP with no legacy r_ether component'
    Assert-True (($propertyValue['module.driver.ether.channel'] -eq '1') -and
                 ($propertyValue['module.driver.ether_phy.channel'] -eq 'module.driver.ether_phy.channel.1') -and
                 ($propertyValue['module.driver.ether_phy.mii_type'] -eq 'module.driver.mii_type.rmii') -and
                 ($propertyValue['module.driver.ether_phy_lsi.phy_lsi_address'] -eq '0') -and
                 ($propertyValue['config.driver.rmac_phy.use_reference_clock'] -eq 'config.driver.rmac_phy.use_reference_clock.enabled')) 'RMAC1/RMII PHY address 0 uses the external 50 MHz reference clock'
    Assert-True (($propertyValue['config.aws.freertosplus.freertosplus_tcp.ipconfigUSE_DHCP'] -eq 'config.aws.freertosplus.freertosplus_tcp.ipconfigUSE_DHCP.0') -and
                 ($propertyValue['config.aws.freertosplus.freertosplus_tcp.ipconfigUSE_TCP'] -eq 'config.aws.freertosplus.freertosplus_tcp.ipconfigUSE_TCP.1') -and
                 ($propertyValue['config.aws.freertosplus.freertosplus_tcp.ipconfigREPLY_TO_INCOMING_PINGS'] -eq 'config.aws.freertosplus.freertosplus_tcp.ipconfigREPLY_TO_INCOMING_PINGS.1')) 'FreeRTOS+TCP is static-IP, TCP-enabled, and replies to ICMP echo'
    Assert-True (($ethernetHeader -match '#define DAB_ETHERNET_IP_ADDRESS\s+"192\.168\.10\.100"') -and
                 ($ethernetHeader -match '#define DAB_ETHERNET_TCP_PORT\s+\(5000U\)') -and
                 ($ethernetSource -match 'FreeRTOS_IPInit\(s_ip_address') -and
                 ($ethernetSource -match 'FreeRTOS_accept\(')) 'firmware exposes static 192.168.10.100 and TCP server port 5000'
    $missingProtocolCommands = @('ping', 'get_status', 'get_parameter', 'set_parameter', 'start', 'stop') |
        Where-Object { $ethernetSource -notmatch ('strcmp\(command, "' + [regex]::Escape($_) + '"\)') }
    Assert-True (($missingProtocolCommands.Count -eq 0) -and
                 ($ethernetSource -match '\\"type\\":\\"telemetry\\"') -and
                 ($ethernetSource -match 'DAB_ETHERNET_HEARTBEAT_TIMEOUT_MS\s+\(15000U\)')) 'JSON Lines protocol contains all six commands, telemetry, and heartbeat timeout'
    Assert-True (($ethernetSource -match 'dropping_oversize_line') -and
                 ($ethernetSource -match "'\\n' == character") -and
                 ($ethernetSource -match 'DAB_ETHERNET_JSON_LINE_SIZE\s+\(1024U\)') -and
                 ($ethernetSource -match 'FreeRTOS_closesocket\(client_socket\)')) 'JSON Lines framer handles split/sticky/oversize input and client disconnects'
    Assert-True (($ethernetSource -match 'vTaskDelay\(pdMS_TO_TICKS\(25U\)\)') -and
                 ($ethernetSource -match '(?s)dab_board_phy_reset_set\(false\).*?dab_board_phy_reset_set\(true\)') -and
                 ($cpu0Application -match '(?s)if \(dab_ethernet_start\(\)\).*?DAB UART/control continues unchanged')) 'PHY reset timing is staged and Ethernet task-creation failure is non-blocking to DAB control'
    Assert-True (($remoteSource -match 'taskENTER_CRITICAL\(\)') -and
                 ($cpu0Application -match 'load_mode_control_active = false') -and
                 ($cpu0Application -match '(?s)DAB_REMOTE_COMMAND_START.*?dab_control_start_request\(\)') -and
                 ($cpu0Application -match '(?s)DAB_REMOTE_COMMAND_STOP.*?dab_force_all_safe\(\)')) 'remote mailbox is synchronized; load mode is metadata-only; start/stop reuse the existing safe control path'
}

$referencePins = @($referenceXml.SelectNodes('//raPinConfiguration//configSetting') |
    Where-Object { $_.configurationId -notmatch '\.pairing$' } |
    ForEach-Object { $_.configurationId + '=' + $_.altId } | Sort-Object)
$cpu0Pins = @($cpu0Xml.SelectNodes('//raPinConfiguration//configSetting') |
    Where-Object { $_.configurationId -notmatch '\.pairing$' } |
    ForEach-Object { $_.configurationId + '=' + $_.altId } | Sort-Object)
$expectedSolutionPins = @(
    'jtag_fslash_swd.mode=jtag_fslash_swd.mode.swd.free',
    'jtag_fslash_swd.swclk=jtag_fslash_swd.swclk.p211',
    'jtag_fslash_swd.swdio=jtag_fslash_swd.swdio.p210',
    'p210=p210.jtag_fslash_swd.swdio',
    'p210.gpio_mode=p210.gpio_mode.gpio_mode_peripheral',
    'p211=p211.jtag_fslash_swd.swclk',
    'p211.gpio_mode=p211.gpio_mode.gpio_mode_peripheral'
) | Sort-Object
$solutionOnlyPins = @($expectedSolutionPins | Where-Object { $_ -notmatch '\.mode=' })
$referenceCpu0Pins = @($referencePins | Where-Object { $_ -notin $solutionOnlyPins })
Assert-True (Same-Sequence $referenceCpu0Pins $cpu0Pins) "CPU0 pins match the reference after moving six physical SWD assignments to the Solution ($($cpu0Pins.Count) CPU0 entries)"
$solutionPins = @($solutionXml.SelectNodes('//raPinConfiguration//configSetting') |
    ForEach-Object { $_.configurationId + '=' + $_.altId } | Sort-Object)
$cpu1PinCfg = $cpu1Xml.SelectSingleNode('//raPinConfiguration/pincfg')
$cpu1Pins = @($cpu1Xml.SelectNodes('//raPinConfiguration/pincfg[@active="true"]//configSetting'))
Assert-True (Same-Sequence $expectedSolutionPins $solutionPins) 'Solution owns only the shared SWD pins P210/P211'
Assert-True (($cpu1PinCfg.active -eq 'false') -and ($cpu1Pins.Count -eq 0)) 'CPU1 pin-data generation is disabled and it owns no active external pins'
Assert-True (($cpu1Xml.SelectNodes('//component[@subgroup="r_ioport"]').Count -eq 0) -and
             ($cpu1Xml.SelectNodes('//module[starts-with(@id,"module.driver.ioport")]').Count -eq 0)) 'CPU1 has no IOPORT driver instance that could rewrite CPU0 pins'
$cpu1SecurePins = @($cpu1SecureXml.SelectNodes('//raPinConfiguration//configSetting') |
    ForEach-Object { $_.configurationId + '=' + $_.altId + '|' + $_.peripheral } | Sort-Object)
Assert-True ($cpu1SecurePins.Count -eq 0) 'CPU1 hidden pin cache contains no local or effective pin settings'

$referenceClocks = Xml-Signatures $referenceXml '//raClockConfiguration/node'
$solutionClocks = Xml-Signatures $solutionXml '//raClockConfiguration/node'
$cpu0Clocks = Xml-Signatures $cpu0Xml '//raClockConfiguration/node'
$cpu1Clocks = Xml-Signatures $cpu1Xml '//raClockConfiguration/node'
$solutionSecureClocks = Xml-Signatures $solutionSecureXml '//raClockConfiguration/node'
Assert-True (Same-Sequence $referenceClocks $solutionClocks) "Solution clocks match ($($referenceClocks.Count) nodes)"
Assert-True (Same-Sequence $referenceClocks $solutionSecureClocks) "Solution hidden clock cache matches ($($referenceClocks.Count) nodes)"
Assert-True ($cpu0Clocks.Count -eq 0) 'CPU0 defers all global clocks to the Flat Solution'
Assert-True ($cpu1Clocks.Count -eq 0) 'CPU1 defers all global clocks to the Flat Solution'

$solutionProject = Read-Text (Join-Path $solution '.project')
$cpu0Cproject = Read-Text (Join-Path $cpu0 '.cproject')
$cpu1Cproject = Read-Text (Join-Path $cpu1 '.cproject')
Assert-True (($solutionProject -match 'ddscSolutionBuilder') -and
             ($solutionProject -match 'ddscSolutionProjectsBuilder')) 'Solution owns both the bundle and dependent-project builders'
Assert-True ($cpu0Cproject -match '\$\{workspace_loc:/RA8P1_DAB_DualCore_Solution\}/build/RA8P1_DAB_DualCore_Solution\.sbd') 'CPU0 consumes the Solution Smart Bundle'
Assert-True ($cpu1Cproject -match '\$\{workspace_loc:/RA8P1_DAB_DualCore_Solution_CPU0\}/\$\{ConfigName\}/RA8P1_DAB_DualCore_Solution_CPU0\.sbd') 'CPU1 consumes the CPU0 Smart Bundle'
Assert-True ($cpu1Cproject -notmatch '(?i)r_ioport[\\/]+r_ioport\.o') 'CPU1 build linkage contains no stale r_ioport object'
Assert-True (($cpu0Cproject -match 'parallelizationNumber="4"') -and
             ($cpu1Cproject -match 'parallelizationNumber="4"')) 'both CPU builds use the validated four-job limit'

$cpu0ConfigText = Read-Text (Join-Path $cpu0 'configuration.xml')
$cpu1ConfigText = Read-Text (Join-Path $cpu1 'configuration.xml')
$cpu0SourceText = @(Get-ChildItem -LiteralPath $cpu0Source -File |
    Where-Object Extension -in '.c', '.h' |
    ForEach-Object { Read-Text $_.FullName }) -join "`n"
Assert-True (($cpu0ConfigText -notmatch '(?i)CMSIS-DSP|cmsis_dsp') -and
             ($cpu1ConfigText -notmatch '(?i)CMSIS-DSP|cmsis_dsp') -and
             ($cpu0Cproject -notmatch '(?i)CMSIS-DSP') -and
             !(Test-Path -LiteralPath (Join-Path $cpu0 'ra\arm\CMSIS-DSP')) -and
             !(Test-Path -LiteralPath (Join-Path $cpu1 'ra\arm\CMSIS-DSP')) -and
             ($cpu0SourceText -notmatch '#include\s*[<"]arm_math\.h[>"]')) 'unused CMSIS-DSP stack/library is absent from both CPU projects'

$referenceOfs = Join-Path $ReferenceProject 'ra_cfg\fsp_cfg\bsp\bsp_mcu_ofs_cfg.h'
$cpu0Ofs = Join-Path $cpu0 'ra_cfg\fsp_cfg\bsp\bsp_mcu_ofs_cfg.h'
$cpu1Ofs = Join-Path $cpu1 'ra_cfg\fsp_cfg\bsp\bsp_mcu_ofs_cfg.h'
Assert-True ((Same-Hash $referenceOfs $cpu0Ofs) -and (Same-Hash $referenceOfs $cpu1Ofs)) 'both cores retain the reference project option-function settings; no new lock/watchdog policy is introduced'

[xml] $multicoreLaunch = Get-Content -Raw -Encoding UTF8 -LiteralPath (
    Join-Path $cpu1 'RA8P1_DAB_DualCore_Solution_CPU1 Debug_Multicore.launch')
$launchEntries = @($multicoreLaunch.SelectNodes('//listEntry') | ForEach-Object { $_.value })
$launchProgram = $multicoreLaunch.SelectSingleNode(
    '//stringAttribute[@key="org.eclipse.cdt.launch.PROGRAM_NAME"]/@value').'#text'
Assert-True (($launchEntries -contains 'CPU0|enabled') -and
             ($launchEntries -contains 'CPU1|enabled')) 'multicore debug configuration enables both cores'
Assert-True (($launchEntries -match 'RA8P1_DAB_DualCore_Solution_CPU0\.elf').Count -eq 1) 'multicore debug configuration explicitly includes the CPU0 ELF'
Assert-True ($launchProgram -eq 'Debug/RA8P1_DAB_DualCore_Solution_CPU1.elf') 'multicore debug configuration uses the CPU1 project ELF'

foreach ($name in @('pin_data.c', 'common_data.c', 'hal_data.c')) {
    $referenceGenerated = (Read-Text (Join-Path $ReferenceProject "ra_gen\$name")) -replace '\s+', ''
    $cpu0Generated = (Read-Text (Join-Path $cpu0 "ra_gen\$name")) -replace '\s+', ''
    if ($AllowEthernetPhase1 -and ($name -eq 'common_data.c')) {
        $legacyMarker = 'ioport_instance_ctrl_tg_ioport_ctrl;'
        $referenceLegacyIndex = $referenceGenerated.IndexOf($legacyMarker, [System.StringComparison]::Ordinal)
        $cpu0LegacyIndex = $cpu0Generated.IndexOf($legacyMarker, [System.StringComparison]::Ordinal)
        $legacyCommonDataMatches = ($referenceLegacyIndex -ge 0) -and ($cpu0LegacyIndex -ge 0) -and
            ($referenceGenerated.Substring($referenceLegacyIndex) -ceq
             $cpu0Generated.Substring($cpu0LegacyIndex))
        Assert-True $legacyCommonDataMatches 'generated legacy common_data IOPORT/init section remains token-equivalent after the Ethernet instances'
    }
    else {
        Assert-True ($referenceGenerated -ceq $cpu0Generated) "generated $name is token-equivalent"
    }
}
Assert-True ((Read-Text (Join-Path $ReferenceProject 'ra_gen\bsp_clock_cfg.h')) -ceq
             (Read-Text (Join-Path $cpu0 'ra_gen\bsp_clock_cfg.h'))) 'generated CPU0 clock header is text-identical'

$clockHeader = Read-Text (Join-Path $cpu0 'ra_gen\bsp_clock_cfg.h')
$halData = Read-Text (Join-Path $cpu0 'ra_gen\hal_data.c')
Assert-True ($clockHeader -match 'BSP_CFG_PLL2_SOURCE \(BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC\)') 'PLL2 source is XTAL'
Assert-True ($clockHeader -match 'BSP_CFG_SCICLK_SOURCE \(BSP_CLOCKS_SOURCE_CLOCK_PLL2R\)') 'SCICLK source is PLL2R'
Assert-True (($halData -match '0\.160% error') -and ($halData -match '\.brr = 64')) 'UART9 baud calculation matches the reference 115200 configuration'
Assert-True (([regex]::Matches($halData, '\.output_disable\s*=\s*\(gpt_output_disable_t\)\(GPT_OUTPUT_DISABLE_NONE\)')).Count -eq 4 -and
             ([regex]::Matches($halData, '\.gtioca_disable_setting\s*=\s*GPT_GTIOC_DISABLE_PROHIBITED')).Count -eq 4 -and
             ([regex]::Matches($halData, '\.gtiocb_disable_setting\s*=\s*GPT_GTIOC_DISABLE_PROHIBITED')).Count -eq 4) 'all four DAB GPT channels prohibit POEG/output-disable clamping'

$cpu0Dual = Read-Text (Join-Path $cpu0Source 'dab_dual_core_cpu0.c')
$cpu0Measurements = Read-Text (Join-Path $cpu0Source 'dab_measurements.c')
$cpu0Ipc = Read-Text (Join-Path $cpu0Source 'drv_ra8p1_ipc.c')
$cpu1Entry = Read-Text (Join-Path $cpu1 'src\new_thread0_entry.c')
$cpu1Ipc = Read-Text (Join-Path $cpu1 'src\drv_ra8p1_ipc.c')
$cpu1Crc = Read-Text (Join-Path $cpu1 'src\drv_ra8p1_crc.c')
$cpu1Sdram = Read-Text (Join-Path $cpu1 'src\drv_ra8p1_sdram.c')
Assert-True (($cpu0Dual -match 'R_BSP_SecondaryCoreStart') -and ($cpu0Dual -match 'xTaskCreateStatic')) 'CPU0 starts CPU1 and runs an RTOS IPC task'
Assert-True ($cpu0Dual -match '#define CPU0_IPC_TASK_PRIORITY\s+\(tskIDLE_PRIORITY\)') 'CPU0 IPC service runs below the reference DAB task and cannot preempt its control timing'
Assert-True (($cpu0Ipc -match 'R_IPC') -and ($cpu1Ipc -match 'R_IPC')) 'both cores compile the RA8P1 IPC FIFO driver'
if ($AllowEnergyControlV39) {
    Assert-True ((([regex]::Matches($cpu1Entry, 'xTaskCreateStatic')).Count -eq 3) -and
                 ($cpu1Entry -match 'cpu1_energy_task') -and
                 ($cpu1Entry -match 'dab_energy_update')) 'CPU1 creates integrity, memory and deterministic energy-supervisor RTOS tasks'
}
else {
    Assert-True (([regex]::Matches($cpu1Entry, 'xTaskCreateStatic')).Count -eq 2) 'CPU1 creates separate integrity and memory RTOS tasks'
}
Assert-True (($cpu1Crc -match 'R_BSP_MODULE_START\(FSP_IP_CRC') -and ($cpu1Crc -match 'R_CRC->CRCDIR')) 'CPU1 uses the RA8P1 hardware CRC peripheral'
Assert-True (($cpu1Sdram -match 'R_BUS->SDRAM') -and ($cpu1Sdram -match 'SDRAM_TEST_PATTERN_A')) 'CPU1 includes a gated SDRAM driver and quick test'
Assert-True (($cpu0Measurements -match 'vld1q_u16') -and
             ($cpu0Measurements -match 'vmovlbq_u16') -and
             ($cpu0Measurements -match 'vmulq_n_f32')) 'CPU0 measurement path uses Helium/MVE-F intrinsics'

$aiArtifacts = @(Get-ChildItem -LiteralPath (Join-Path $cpu0 'src'), (Join-Path $cpu1 'src') -File |
    Where-Object { $_.Name -match '(^|[_-])(ai|model|ethos|npu)([_\.-]|$)' })
Assert-True ($aiArtifacts.Count -eq 0) 'test firmware contains no AI model or inference source'

$cpu0Elf = Join-Path $cpu0 'Debug\RA8P1_DAB_DualCore_Solution_CPU0.elf'
$cpu1Elf = Join-Path $cpu1 'Debug\RA8P1_DAB_DualCore_Solution_CPU1.elf'
$solutionSbd = Join-Path $solution 'build\RA8P1_DAB_DualCore_Solution.sbd'
$cpu0Sbd = Join-Path $cpu0 'Debug\RA8P1_DAB_DualCore_Solution_CPU0.sbd'
$cpu1Sbd = Join-Path $cpu1 'Debug\RA8P1_DAB_DualCore_Solution_CPU1.sbd'
$cpu0LinkerInfo = Join-Path $cpu0 'Debug\bsp_linker_info.h'
$cpu1LinkerInfo = Join-Path $cpu1 'Debug\bsp_linker_info.h'
$cpu0Map = Join-Path $cpu0 'Debug\RA8P1_DAB_DualCore_Solution_CPU0.map'
$cpu0MeasurementObject = Join-Path $cpu0 'Debug\src\dab_measurements.o'
$cpu1DebugMakefile = Join-Path $cpu1 'Debug\makefile'
$workspaceMetadata = Join-Path $dualRoot '.metadata'
if ($RequireBuildArtifacts) {
    Assert-True (Test-Path -LiteralPath $solutionSbd) 'Solution Smart Bundle exists'
    Assert-True (Test-Path -LiteralPath $cpu0Sbd) 'CPU0 Smart Bundle exists'
    Assert-True (Test-Path -LiteralPath $cpu1Sbd) 'CPU1 Smart Bundle exists'
    Assert-True (Test-Path -LiteralPath $cpu0LinkerInfo) 'CPU0 linker information exists'
    Assert-True (Test-Path -LiteralPath $cpu1LinkerInfo) 'CPU1 linker information exists'
    Assert-True (Test-Path -LiteralPath $cpu0Elf) 'CPU0 ELF exists'
    Assert-True (Test-Path -LiteralPath $cpu1Elf) 'CPU1 ELF exists'
    Assert-True (Test-Path -LiteralPath $cpu0Map) 'CPU0 linker map exists'
    Assert-True (Test-Path -LiteralPath $cpu0MeasurementObject) 'CPU0 measurement object exists'
    if ((Test-Path -LiteralPath $cpu0LinkerInfo) -and
        (Test-Path -LiteralPath $cpu1LinkerInfo)) {
        $cpu0LinkerText = Read-Text $cpu0LinkerInfo
        $cpu1LinkerText = Read-Text $cpu1LinkerInfo
        $partitionContract = @(
            '#define BSP_PARTITION_FLASH_CPU0_S_START \(0x02000000\)',
            '#define BSP_PARTITION_FLASH_CPU0_S_SIZE \(0x80000\)',
            '#define BSP_PARTITION_FLASH_CPU1_S_START \(0x02080000\)',
            '#define BSP_PARTITION_FLASH_CPU1_S_SIZE \(0x80000\)',
            '#define BSP_PARTITION_RAM_CPU0_S_START \(0x22000000\)',
            '#define BSP_PARTITION_RAM_CPU0_S_SIZE \(0xEA000\)',
            '#define BSP_PARTITION_RAM_CPU1_S_START \(0x220EA000\)',
            '#define BSP_PARTITION_RAM_CPU1_S_SIZE \(0xEA000\)',
            '#define BSP_PARTITION_SDRAM_CPU0_S_START \(0x68000000\)',
            '#define BSP_PARTITION_SDRAM_CPU0_S_SIZE \(0x4000000\)',
            '#define BSP_PARTITION_SDRAM_CPU1_S_START \(0x6C000000\)',
            '#define BSP_PARTITION_SDRAM_CPU1_S_SIZE \(0x4000000\)'
        )
        $missingPartitions = @($partitionContract | Where-Object {
            ($cpu0LinkerText -notmatch $_) -or ($cpu1LinkerText -notmatch $_)
        })
        Assert-True ($missingPartitions.Count -eq 0) 'CPU0/CPU1 flash, SRAM, and SDRAM partitions are adjacent and non-overlapping'
    }
    if (Test-Path -LiteralPath $cpu1DebugMakefile) {
        Assert-True ((Read-Text $cpu1DebugMakefile) -notmatch '(?i)r_ioport/subdir\.mk') 'CPU1 generated build metadata contains no stale r_ioport source'
    }
    else {
        Fail 'CPU1 Debug makefile is missing'
    }

    if (Test-Path -LiteralPath $workspaceMetadata) {
        $workspaceProjects = Join-Path $workspaceMetadata '.plugins\org.eclipse.core.resources\.projects'
        $missingWorkspaceProjects = @(
            'RA8P1_DAB_DualCore_Solution',
            'RA8P1_DAB_DualCore_Solution_CPU0',
            'RA8P1_DAB_DualCore_Solution_CPU1'
        ) | Where-Object { !(Test-Path -LiteralPath (Join-Path $workspaceProjects $_)) }
        Assert-True ($missingWorkspaceProjects.Count -eq 0) 'active e2 studio workspace registers all three dual-core projects'

        $workspaceLog = Join-Path $workspaceMetadata '.log'
        if (Test-Path -LiteralPath $workspaceLog) {
            $workspaceLogText = Read-Text $workspaceLog
            Assert-True ($workspaceLogText -notmatch 'RA8P1_DAB_DualCore_Solution_CPU1/Debug/ra/fsp/src/r_ioport') 'active e2 studio workspace contains no stale CPU1 r_ioport resource failure'
        }
    }

    $cpu1PinData = Join-Path $cpu1 'ra_gen\pin_data.c'
    if (Test-Path -LiteralPath $cpu1PinData) {
        $cpu1PinText = Read-Text $cpu1PinData
        Assert-True ($cpu1PinText -notmatch 'BSP_IO_PORT_') 'generated CPU1 pin table is empty'
        Assert-True (($cpu1PinText -notmatch 'BSP_IO_PORT_02_PIN_08') -and
                     ($cpu1PinText -notmatch 'BSP_IO_PORT_02_PIN_09')) 'CPU1 generated code cannot reconfigure UART9 pins P208/P209'
    }
    else {
        Fail 'CPU1 generated pin_data.c is missing'
    }
    $cpu1CommonData = Read-Text (Join-Path $cpu1 'ra_gen\common_data.c')
    $cpu1WarmStart = Read-Text (Join-Path $cpu1 'src\hal_warmstart.c')
    Assert-True (($cpu1CommonData -notmatch 'g_ioport|g_bsp_pin_cfg') -and
                 ($cpu1WarmStart -notmatch 'R_IOPORT_Open|R_BSP_SdramInit')) 'CPU1 generated/runtime startup contains no global IOPORT or SDRAM initialization'

    if (Test-Path -LiteralPath $cpu0Map) {
        $mapText = Read-Text $cpu0Map
        Assert-True (($mapText -match '(?s)\.itcm_code_from_flash.*dab_measurements_update') -and
                     ($mapText -match '(?s)\.dtcm_from_flash.*s_zero') -and
                     ($mapText -match '(?s)\.dtcm.*s_filtered') -and
                     ($mapText -match '(?s)\.ram_nocache.*s_dma_ring')) 'linker map places hot code/state in TCM and DMA ring in non-cache SRAM'
    }

    $llvmObjdumpCandidates = @(@(
        'C:\Renesas\RA\e2studio_v2025-12_fsp_v6.4.0\toolchains\llvm_arm\ATfE-21.1.1-Windows-x86_64\bin\llvm-objdump.exe',
        (Get-Command llvm-objdump.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) })
    if (($llvmObjdumpCandidates.Count -gt 0) -and
        (Test-Path -LiteralPath $cpu0MeasurementObject)) {
        $disassembly = & $llvmObjdumpCandidates[0] -d $cpu0MeasurementObject 2>&1 | Out-String
        Assert-True (($LASTEXITCODE -eq 0) -and
                     ($disassembly -match 'vldrh\.u16') -and
                     ($disassembly -match 'vmovlb\.u16') -and
                     ($disassembly -match 'vcvt\.f32\.u32\s+q') -and
                     ($disassembly -match 'vfma\.f32\s+q')) 'measurement object contains real Helium/MVE-F vector instructions'
    }
    else {
        Fail 'LLVM objdump is unavailable, so binary Helium verification cannot run'
    }

    if (Test-Path -LiteralPath $solutionSbd) {
        [xml] $solutionSbdXml = Read-ZipEntryXml $solutionSbd 'cfg/secure.xml'
        $solutionSbdClocks = Xml-Signatures $solutionSbdXml '//raClockConfiguration/node'
        Assert-True (Same-Sequence $referenceClocks $solutionSbdClocks) "Solution Smart Bundle carries the expected $($referenceClocks.Count) clocks"
    }

    if ((Test-Path -LiteralPath $solutionSbd) -and
        (Test-Path -LiteralPath $cpu0Sbd) -and
        (Test-Path -LiteralPath $cpu0Elf) -and
        (Test-Path -LiteralPath $cpu1Sbd) -and
        (Test-Path -LiteralPath $cpu1Elf)) {
        $solutionSbdTime = (Get-Item -LiteralPath $solutionSbd).LastWriteTimeUtc
        $cpu0SbdTime = (Get-Item -LiteralPath $cpu0Sbd).LastWriteTimeUtc
        $cpu0ElfTime = (Get-Item -LiteralPath $cpu0Elf).LastWriteTimeUtc
        $cpu1SbdTime = (Get-Item -LiteralPath $cpu1Sbd).LastWriteTimeUtc
        $cpu1ElfTime = (Get-Item -LiteralPath $cpu1Elf).LastWriteTimeUtc
        Assert-True (($cpu0SbdTime -ge $solutionSbdTime) -and
                     ($cpu0ElfTime -ge $solutionSbdTime)) 'CPU0 bundle and ELF are not older than the Solution bundle'
        $cpu0InputTimes = @(
            (Get-Item -LiteralPath (Join-Path $cpu0 'src\User_Init_Task_entry.c')).LastWriteTimeUtc,
            (Get-Item -LiteralPath (Join-Path $cpu0 'src\dab_first_power_config.h')).LastWriteTimeUtc,
            (Get-Item -LiteralPath (Join-Path $cpu0 'src\dab_pwm_hw.c')).LastWriteTimeUtc,
            (Get-Item -LiteralPath (Join-Path $cpu0 'src\dab_dual_core_cpu0.c')).LastWriteTimeUtc
        )
        Assert-True (($cpu0InputTimes | Where-Object { $cpu0ElfTime -lt $_ }).Count -eq 0) 'CPU0 ELF contains the latest reference-equivalent startup and protection configuration'
        Assert-True (($cpu1SbdTime -ge $cpu0SbdTime) -and
                     ($cpu1ElfTime -ge $cpu0SbdTime)) 'CPU1 bundle and ELF are not older than the CPU0 bundle'
        $cpu1InputTimes = @(
            (Get-Item -LiteralPath (Join-Path $cpu1 'configuration.xml')).LastWriteTimeUtc,
            (Get-Item -LiteralPath (Join-Path $cpu1 '.secure_xml')).LastWriteTimeUtc,
            (Get-Item -LiteralPath $cpu1PinData).LastWriteTimeUtc,
            (Get-Item -LiteralPath (Join-Path $cpu1 'ra_gen\common_data.c')).LastWriteTimeUtc,
            (Get-Item -LiteralPath (Join-Path $cpu1 'src\hal_warmstart.c')).LastWriteTimeUtc
        )
        Assert-True (($cpu1InputTimes | Where-Object { $cpu1ElfTime -lt $_ }).Count -eq 0) 'CPU1 ELF is not older than its final config, hidden cache, generated data, or warm-start policy'
    }
} elseif ((Test-Path -LiteralPath $cpu0Elf) -and (Test-Path -LiteralPath $cpu1Elf)) {
    Pass 'both build artifacts are present'
} else {
    Write-Host '[INFO] Debug artifacts are absent; build from the Solution project to regenerate them.'
}

if ($script:failures -ne 0) {
    Write-Host "Parity verification failed: $script:failures check(s)." -ForegroundColor Red
    exit 1
}

Write-Host 'Parity verification passed. This proves static/configuration/build parity only, not board behavior.' -ForegroundColor Green
