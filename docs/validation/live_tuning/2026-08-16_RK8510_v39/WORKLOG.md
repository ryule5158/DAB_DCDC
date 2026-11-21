# 2026-08-16 RK8510 / v39 live test worklog

## Instrument and initial condition

- Input source: 24 V class bench supply; user keeps its current limit close to 20 A because repeatedly adjusting it is inconvenient. This is a source ceiling, not the requested operating current. All subsequent power/current steps must be commanded by the DAB control target, modulation parameters and RK8510 setting; do not ask the user to re-adjust the source limit for each point.
- Electronic load: RK8510, single channel, 400 W / 40 A. No remote cable is available today; all load settings are manual.
- At the diagnostic stop point the RK8510 load input was OFF, the DAB PWM and all power-path MOS controls were OFF, and the input source remained enabled.

## Startup-fault observations

- A current-loop startup attempt with the RK8510 in CV 36 V produced an immediate `fault=0x10` record. The single captured transient sample reported Vin 23.10 V, Iin 2.65 A, Ipri 1.77 A, Isec 0.78 A, Pin 61.2 W, Vout 0.13 V and phase 0 ns. This is not accepted as proof of sustained current or delivered power.
- With RK8510 LOAD OFF, an unloaded voltage-loop precharge attempt (`v36`, SPS, `p50`, `cap20`) still stopped immediately with `fault=0x18`. The transient sample reported Vin 23.15 V, Iin 1.80 A, Ipri 1.21 A, Isec 0.50 A, Pin 41.7 W, Vout 0.09 V and phase 0 ns.
- `0x18` maps to the independent active-low digital inputs `RAW_FAULT_N` and `IPRI_OC_N`. Therefore the electronic-load operating mode and the 50 W software power label are not the immediate trip cause.
- The bench source display remained at 0 A. Its display cannot resolve a very short pulse, but the firmware transient ADC sample alone also cannot distinguish real bridge current from switching-edge pickup, common-mode disturbance or sampling artifact.

## 24 V common-mode zero calibration

- The first combined `off/clear/zero/show` capture did not contain a complete `ZERO` response because the prior 50 ms save stream was still active; its near-zero idle samples were only provisional evidence.
- The stream was changed to `mode observe`, and `zero` was then sent as a separate command with PWM/RUN and all power paths OFF.
- Calibration passed: `[DAB][ZERO] PASS IIN=1.646V IOUT=1.643V IPRI=1.650V ISEC=1.652V`.
- A separate `show` immediately after calibration reported Vin 23.33 V, Vout 0.10 V, Iin 0.01 A, Iout -0.01 A, Ipri approximately 0 A, Isec approximately 0 A and Pin 0.2 W. State was READY, PWM=0, fault=0.
- Static hardware inputs were healthy after calibration: `RAW=1 IPRI_OC=1 ISEC_OC=1 PRI_OK=1 SEC_OK=1 AMB_OT_N=1`.

## Diagnostic conclusion and next gate

- The static current-zero calibration is valid at the present 24 V input common mode, and the actual steady input current with PWM OFF is effectively zero.
- The startup stop is not caused by the ADC zero offset. The best current evidence is a brief event coincident with PWM startup: either a real sub-display-time-scale current spike or an electrical/sampling transient. The simultaneous `RAW_FAULT_N` and `IPRI_OC_N` latch requires waveform-level confirmation or controlled startup blanking/debounce before another energized run is accepted.
- Do not label the startup ADC current or calculated Pin as efficiency/training data. Preserve it as a failed diagnostic record with `validation_status=transient_unverified`.

## Raw evidence

- `raw_serial/diag_load_off_precharge_cv36_20260816_184855.log`
- `raw_serial/rezero_24V_pwm_off_20260816_185045.log`
- `raw_serial/rezero_step1_observe_20260816.log`
- `raw_serial/rezero_step2_zero_20260816.log`
- `raw_serial/rezero_step3_show_20260816.log`

## v40 manual-stop recovery build

- User confirmed that the RK8510 LOAD input was enabled and reiterated that the proven commissioning behavior must not be blocked by MCU self-protection.
- Root cause of the regression was confirmed in the v39 compile-time configuration: `DAB_SW_PROTECTION_PROFILE_ALL_ENABLED` and `DAB_MANUAL_STOP_TEST_MODE=0` made a transient RAW/IPRI_OC sample an immediate application-level latched stop. The FSP POEG/GPT output clamp was already disabled, so this was an MCU software-policy stop rather than a POEG hardware shutdown.
- CPU0 was changed to `DAB_SW_PROTECTION_PROFILE_REFERENCE_TEST` and `DAB_MANUAL_STOP_TEST_MODE=1`. The firmware identity is `2026-08-16-dualcore-manual-stop-v40`. CPU1 and all v39 control, MPPT, battery, AI-hook, modulation and telemetry functions are unchanged.
- UART boot, `show` and `on` text now explicitly reports `MCU_AUTO=OFF MANUAL_STOP` so the protection policy is stored with every validation session. RAW/OC/OK, ADC/current/voltage/temperature remain observable but do not make the MCU stop automatically. External board, bench-supply and RK8510 protections are unaffected.
- CPU0 build passed with text/data/bss = 134236/460/170677 bytes.
- ELF: `RA8P1_DAB_Feature_Showcase/dual_core/RA8P1_DAB_DualCore_Solution_CPU0/Debug/RA8P1_DAB_DualCore_Solution_CPU0.elf`, SHA-256 `54F9E7EA5BB0A385B73BD6D9CAAE151BC03599BD3C41ED110B39A3152EE36381`.
- SREC: `RA8P1_DAB_Feature_Showcase/dual_core/RA8P1_DAB_DualCore_Solution_CPU0/Debug/RA8P1_DAB_DualCore_Solution_CPU0.srec`, SHA-256 `3BAF47DE7B5C3884A45ED70D9A0CC58D34DEA756320B47801246D8226958A588`.
- User manually flashed CPU0 v40. UART boot identity `2026-08-16-dualcore-manual-stop-v40`, zero-calibration PASS, READY state and sustained PWM power transfer were physically captured. This validates this board/session only; the firmware-derived power ratio is not an externally calibrated efficiency result.

## CV 36 V, 24 V-class input, 0.5 A comparison

Common control settings: RK8510 CV approximately 36 V with LOAD ON, `arch current`, `iref 0.50`, `ikp 2`, `iki 0.2`, `p 100`, `cap 80`, `alglog on`. Each run ended with an explicit `off`; every stable-window result below has `fault=0`.

| Modulation | Inner shift | Stable Vin / Vout | Stable Iin / Iout | Firmware Pin / Pout | Tpri / Tsec | Phase result | Firmware Pout/Pin | Interpretation |
|---|---|---:|---:|---:|---:|---|---:|---|
| SPS | N/A | 22.375 / 35.439 V | 10.343 / 5.730 A | 231.42 / 203.06 W | 29.22 / 27.27 C | effective=0 ns, lower saturated | 87.745% | Cannot reach 0.5 A; SPS minimum-power floor is much too high in this condition. |
| EPS | AUTO, primary approximately 716 ns, secondary 0 ns | 23.103 / 35.896 V | 2.272 / 1.201 A | 52.50 / 43.11 W | 32.36 / 28.19 C | raw approximately 359 ns, effective approximately 1.9 ns, lower saturated | 82.116% | Best of the three at this low-current point, but AUTO inner maximum still leaves an approximately 1.2 A floor. |
| TPS | AUTO, primary approximately 716 ns, secondary 180 ns | 22.967 / 35.695 V | 2.762 / 1.451 A | 63.44 / 51.80 W | 34.47 / 29.24 C | raw approximately 268 ns, effective=0 ns, lower saturated | 81.653% | Worse than EPS after AUTO inner targets reached their steady values. |

The Pout/Pin column is only the ratio of firmware ADC-derived quantities. Do not use it as final efficiency until input and output power are measured by calibrated external instruments.

Raw evidence:

- `raw_serial/v40_cv36_sps_i0p5_config_20260816.log`
- `raw_serial/v40_cv36_sps_i0p5_run_20260816.log`
- `raw_serial/v40_cv36_eps_i0p5_config_20260816.log`
- `raw_serial/v40_cv36_eps_auto_i0p5_run_20260816.log`
- `raw_serial/v40_cv36_tps_i0p5_config_20260816.log`
- `raw_serial/v40_cv36_tps_auto_i0p5_run_20260816.log`

## EPS 2.0 A actuator and PI tuning

Common settings: RK8510 CV approximately 36 V, LOAD ON, source limit unchanged near 20 A, `arch current`, `iref 2.00`, `ikp 2`, `iki 0.2`, `islew 0.5`, `p 100`, `mod eps`, `alglog on`.

1. AUTO inner and `cap 80`: primary inner reached 716 ns, command/effective cap reached 80/82 ns, but stable Iout was only 1.459 A. This was an upper actuator saturation, so changing PI gains first would not solve the missing current.
2. AUTO inner and `cap 240`: raw phase reached the 480 ns hardware boundary, primary inner remained 716 ns, so effective phase was limited to 122 ns. Stable Iout was 1.5213 A and error was +0.4787 A. Increasing configured `cap` further cannot exceed the raw 480 ns boundary in this partition.
3. Manual primary/secondary inner 480/0 ns: effective phase fell to its 0 ns lower bound while stable Iout rose to 2.8821 A. This bracketed the 2 A operating point between 480 and 716 ns primary inner shift.
4. Manual primary/secondary inner 600/0 ns: the target entered the controllable region and the existing PI gains converged accurately. Stable last-100-RUN-row averages were Vin=23.0021 V, Vout=35.8597 V, Iin=3.6241 A, Iout=1.9985 A, Pin=83.3602 W, Pout=71.6651 W, Tpri=38.1613 C and Tsec=30.4430 C. Current error averaged +0.0016 A (range -0.011 to +0.018 A), raw/effective/command phase averaged 303.76/3.76/2.00 ns, primary/secondary inner was 600/0 ns, and `fault=0`.

Best saved EPS parameter bundle for this tested point:

```text
condition: input 24 V class, RK8510 CV approximately 36 V, output-current target 2.00 A
source_current_limit: approximately 20 A (fixed ceiling, not target)
control_arch: OUTPUT_CURRENT_PI
modulation: EPS
inner_mode: MANUAL
primary_inner_ns: 600
secondary_inner_ns: 0
phase_cap_ns: 240
current_kp_ns_per_a: 2.0
current_ki_us_per_a_s: 0.2
current_reference_slew_a_per_s: 0.5
measured_iout_a: 1.9985
measured_pout_w_firmware: 71.6651
validation_status: physically_sustained_uart_firmware_adc
external_efficiency_validated: false
```

Raw evidence:

- `raw_serial/v40_cv36_eps_i2_config_20260816.log`
- `raw_serial/v40_cv36_eps_auto_i2_run_20260816.log`
- `raw_serial/v40_cv36_eps_i2_cap240_config_20260816.log`
- `raw_serial/v40_cv36_eps_auto_i2_cap240_run_20260816.log`
- `raw_serial/v40_cv36_eps_i2_inner480_config_20260816.log`
- `raw_serial/v40_cv36_eps_i2_inner480_run_20260816.log`
- `raw_serial/v40_cv36_eps_i2_inner600_config_20260816.log`
- `raw_serial/v40_cv36_eps_i2_inner600_run_20260816.log`

## SPS / TPS 2.0 A tuning before v41

Common instrument condition remained unchanged: 24 V-class input, source current ceiling fixed near 20 A, RK8510 CV approximately 36 V with LOAD ON. Every run ended with explicit `off` and all stable windows had `fault=0`.

- SPS was fully driven to its 0 ns lower phase boundary with `iref 2.00`, `ikp 2`, `iki 0.2`, `islew 0.5`, `cap 240`. It still delivered Vin=22.3961 V, Vout=35.4460 V, Iin=10.2840 A, Iout=5.6816 A, Pin=230.3208 W and Pout=201.3926 W. This is a physically measured SPS minimum-power floor in this condition, not a conclusion based on untuned gains.
- TPS AUTO at the same 2 A target reached primary/secondary inner=716/180 ns and command/effective phase=240/212 ns, but only delivered Iout=1.4398 A and Pout=51.3881 W. It was upper saturated, so the target was outside that AUTO partition's actuator range.
- TPS manual 600/180 ns reached its lower command boundary: command/effective/raw=0/2/212 ns. Stable last-100 averages were Vin=22.9062 V, Vout=35.6693 V, Iin=4.0821 A, Iout=2.3031 A, Pin=93.5042 W, Pout=82.1508 W, Tpri=40.9716 C and Tsec=31.7309 C. The current remained 0.303 A above target because the forward PI could not command below 0 ns.
- TPS manual 640/180 ns also reached its lower command boundary, but happened to land almost exactly at the setpoint: stable Vin=23.0476 V, Vout=35.9056 V, Iin=3.6136 A, Iout=2.0012 A, Pin=83.2852 W, Pout=71.8553 W, Tpri=39.6145 C and Tsec=31.3464 C; raw/effective/command=232/2/0 ns. This is a useful boundary parameter but not accepted as a robust closed-loop optimum because it has no downward control margin.
- TPS manual 660/180 ns exposed the plant-sign issue. As the forward PI increased command from approximately 7 to 240 ns, Iout generally fell from approximately 1.85 A to a stable 1.5376 A; the final stable Pout was 55.1772 W. The present forward controller therefore acts in the wrong direction in this negative-gain TPS region. This is a software control-direction limitation, not evidence that TPS itself is unsuitable.

Raw evidence:

- `raw_serial/v40_cv36_sps_i2_config_20260816.log`
- `raw_serial/v40_cv36_sps_i2_run_20260816.log`
- `raw_serial/v40_cv36_tps_auto_i2_config_20260816.log`
- `raw_serial/v40_cv36_tps_auto_i2_run_20260816.log`
- `raw_serial/v40_cv36_tps_i2_inner600_180_config_20260816.log`
- `raw_serial/v40_cv36_tps_i2_inner600_180_run_20260816.log`
- `raw_serial/v40_cv36_tps_i2_inner640_180_config_20260816.log`
- `raw_serial/v40_cv36_tps_i2_inner640_180_run_20260816.log`
- `raw_serial/v40_cv36_tps_i2_inner660_180_config_20260816.log`
- `raw_serial/v40_cv36_tps_i2_inner660_180_run_20260816.log`

## v41 signed current-loop direction build

- Added the UART command `idir forward|reverse|show`. The power-on default is `forward`, preserving all v40 SPS/EPS and previous behavior. Changes are accepted only in READY with PWM/power OFF and reset the PI state.
- The physical error `Iref-Iout` remains unchanged in telemetry. Only the error presented to the PI is multiplied by `current_control_direction` (`+1` forward, `-1` reverse).
- `ALG:` gained one append-only final field, `current_control_direction`, so prior positional fields remain stable and training rows explicitly identify the loop direction. Telemetry schema text is now v40; legacy DAB rows are unchanged.
- Firmware identity: `2026-08-16-dualcore-manual-stop-v41`.
- CPU0 build passed with text/data/bss = 134988/460/170677 bytes.
- ELF SHA-256: `4D4288228ED48A338BDC75B2347F4C46AD4D15B88BE08079A712AFB795ACCF1B`.
- SREC SHA-256: `32EEB6210A4E8F2AE6630ADE74FA439D456953F706C3C68708650EE54AAB81D3`.
- Next physical gate after manual flashing: confirm v41 boot identity and `idir reverse`, then retest TPS manual 600/180 ns at 2.00 A with the same Kp/Ki/slew/cap settings before judging TPS.

## v41 TPS 2.0 A reverse-loop tuning

- The manually flashed v41 image exposed the new `idir` command and append-only ALG direction field. Initial READY state had PWM OFF, `fault=0`, healthy digital inputs and `idir=forward` by default.
- Common settings: 24 V-class source with its current ceiling unchanged near 20 A, RK8510 CV approximately 36 V with LOAD ON, `arch current`, `mod tps`, `iref 2.00`, `ikp 2`, `iki 0.2`, `islew 0.5`, `idir reverse`, manual secondary inner=180 ns and explicit `off` after every run.
- Manual primary inner=600 ns and `cap 240` proved the sign correction: stable Iout moved from the prior forward-loop 2.3031 A floor to 2.0093 A. However command/effective/raw phase=240/242/452 ns remained at the configured cap.
- Raising `cap` to 320 ns with primary inner still 600 ns held Iout close to 2 A but drove raw phase to its 480 ns hardware maximum. This point was accurate but lacked further phase authority.
- Manual primary inner=620 ns, secondary=180 ns and `cap 320` moved the operating point into the actuator interior. Stable last-100 averages were Vin=23.0370 V, Vout=35.8984 V, Iin=3.5719 A, Iout=1.9973 A, Pin=82.2848 W, Pout=71.7017 W, Tpri=43.1532 C and Tsec=31.1469 C. Current error averaged +0.00268 A with range -0.014 to +0.017 A. Raw/effective/command phase=296/76/77.97 ns, leaving margin to both phase boundaries; `fault=0`.

Best saved TPS parameter bundle for this tested point:

```text
condition: input 24 V class, RK8510 CV approximately 36 V, output-current target 2.00 A
source_current_limit: approximately 20 A (fixed ceiling, not target)
control_arch: OUTPUT_CURRENT_PI
modulation: TPS
current_control_direction: -1 (reverse)
inner_mode: MANUAL
primary_inner_ns: 620
secondary_inner_ns: 180
phase_cap_ns: 320
current_kp_ns_per_a: 2.0
current_ki_us_per_a_s: 0.2
current_reference_slew_a_per_s: 0.5
measured_iout_a: 1.9973
measured_pout_w_firmware: 71.7017
raw_phase_ns: 296
effective_phase_ns: 76
command_phase_ns: 77.97
validation_status: physically_sustained_uart_firmware_adc
external_efficiency_validated: false
```

Raw evidence:

- `raw_serial/v41_boot_show_20260816.log`
- `raw_serial/v41_cv36_tps_i2_reverse_inner600_180_config_20260816.log`
- `raw_serial/v41_cv36_tps_i2_reverse_inner600_180_run_20260816.log`
- `raw_serial/v41_cv36_tps_i2_reverse_inner600_180_cap320_config_20260816.log`
- `raw_serial/v41_cv36_tps_i2_reverse_inner600_180_cap320_run_20260816.log`
- `raw_serial/v41_cv36_tps_i2_reverse_inner620_180_cap320_config_20260816.log`
- `raw_serial/v41_cv36_tps_i2_reverse_inner620_180_cap320_run_20260816.log`

## CV 36 V, EPS 4.0 A / approximately 144 W tuning

- Common condition: 24 V-class source, source current ceiling unchanged near 20 A, RK8510 CV approximately 36 V with LOAD ON, `arch current`, `mod eps`, `iref 4.00`, `ikp 2`, `iki 0.2`, `islew 0.5`, `cap 320`, `p 180`, metadata `layer 150`.
- Inner 600/0 ns with forward direction reached raw phase 480 ns but only approximately 2.05 A. This combination cannot reach 4 A.
- Inner 300/0 ns with reverse direction drove command to the upper cap and produced approximately 4.46 A; the measured phase response in this region was positive, so reverse was the wrong direction.
- Inner 300/0 and 320/0 ns with forward direction both reached the 0 ns lower command boundary and remained near 4.18 A. These combinations have no downward current authority at the target.
- Inner 400/0 ns with forward direction reached the raw 480 ns boundary but only produced approximately 3.38 A. These sweeps bracketed the controllable 4 A window between 320 and 400 ns primary inner shift.
- Inner 360/0 ns with forward direction converged accurately without actuator saturation. Stable last-100 averages: Vin=22.8785 V, Vout=35.9614 V, Iin=7.1844 A, Iout=4.0007 A, Pin=164.3677 W, Pout=143.8692 W, Tpri=53.6943 C and Tsec=34.3922 C. Current error averaged -0.00066 A with range -0.020 to +0.019 A. Raw/effective/command phase averaged 465.04/285.04/286.00 ns; `fault=0`.

Best saved EPS parameter bundle:

```text
condition: input 24 V class, RK8510 CV approximately 36 V, output-current target 4.00 A
source_current_limit: approximately 20 A (fixed ceiling, not target)
control_arch: OUTPUT_CURRENT_PI
modulation: EPS
current_control_direction: +1 (forward)
inner_mode: MANUAL
primary_inner_ns: 360
secondary_inner_ns: 0
phase_cap_ns: 320
current_kp_ns_per_a: 2.0
current_ki_us_per_a_s: 0.2
current_reference_slew_a_per_s: 0.5
measured_iout_a: 4.0007
measured_pout_w_firmware: 143.8692
raw_phase_ns: 465.04
effective_phase_ns: 285.04
command_phase_ns: 286.00
validation_status: physically_sustained_uart_firmware_adc
external_efficiency_validated: false
```

Raw evidence includes all `raw_serial/v41_cv36_eps_i4_*_20260816.log` configuration and run files for inner 600, 300, 320, 400 and 360 ns in both tested directions.

## CV 36 V, TPS 4.0 A / approximately 144 W tuning

- Common condition matched the EPS run: 24 V-class source with current ceiling unchanged near 20 A, RK8510 CV approximately 36 V with LOAD ON, `arch current`, `mod tps`, `iref 4.00`, `ikp 2`, `iki 0.2`, `islew 0.5`, `cap 320`, `p 240`, metadata `layer 150`.
- Manual inner 400/180 ns with forward direction reached the upper command range but delivered only approximately 3.55 A, so this parameter pair lacked sufficient current authority.
- Manual inner 360/180 ns with forward direction converged accurately in the actuator interior. Stable last-100 averages: Vin=22.8832 V, Vout=35.9364 V, Iin=6.9976 A, Iout=4.0004 A, Pin=160.1295 W, Pout=143.7629 W, Tpri=54.2759 C and Tsec=34.5676 C. Current error averaged -0.00043 A with range -0.014 to +0.014 A. Raw/effective/command phase averaged 213.04/123.04/122.00 ns; `fault=0`.

Best saved TPS parameter bundle:

```text
condition: input 24 V class, RK8510 CV approximately 36 V, output-current target 4.00 A
source_current_limit: approximately 20 A (fixed ceiling, not target)
control_arch: OUTPUT_CURRENT_PI
modulation: TPS
current_control_direction: +1 (forward)
inner_mode: MANUAL
primary_inner_ns: 360
secondary_inner_ns: 180
phase_cap_ns: 320
current_kp_ns_per_a: 2.0
current_ki_us_per_a_s: 0.2
current_reference_slew_a_per_s: 0.5
measured_iout_a: 4.0004
measured_pout_w_firmware: 143.7629
raw_phase_ns: 213.04
effective_phase_ns: 123.04
command_phase_ns: 122.00
validation_status: physically_sustained_uart_firmware_adc
external_efficiency_validated: false
```

Raw evidence:

- `raw_serial/v41_cv36_tps_i4_forward_inner400_180_config_20260816.log`
- `raw_serial/v41_cv36_tps_i4_forward_inner400_180_run_20260816.log`
- `raw_serial/v41_cv36_tps_i4_forward_inner360_180_config_20260816.log`
- `raw_serial/v41_cv36_tps_i4_forward_inner360_180_run_20260816.log`

## CV 36 V, SPS requested 4.0 A / measured minimum-power floor

- Common condition: 24 V-class source with current ceiling unchanged near 20 A, RK8510 CV approximately 36 V with LOAD ON, `arch current`, `mod sps`, `iref 4.00`, `ikp 2`, `iki 0.2`, `islew 0.5`, `cap 320`, `p 240`, metadata `layer 150`.
- The current loop drove SPS to its physical 0 ns phase-command boundary. Stable last-100 averages were Vin=22.3380 V, Vout=35.4614 V, Iin=10.0750 A, Iout=5.5351 A, Pin=225.0557 W, Pout=196.2816 W, Tpri=52.3558 C and Tsec=34.5620 C. Iout ranged from 5.521 to 5.549 A and `fault=0`.
- Because the tuned loop exhausted its lower actuator boundary while remaining 1.535 A above the 4 A target, SPS cannot regulate this particular hardware/input/CV-load condition down to 4 A. This is an observed SPS minimum-power floor, not a conclusion from untuned PI gains.

Raw evidence:

- `raw_serial/v41_cv36_sps_i4_run_20260816.log`

## Same-condition 4 A modulation comparison

| Modulation | Regulation result | Iout (A) | Pin ADC (W) | Pout ADC (W) | Pout/Pin ADC ratio | Tpri/Tsec (C) | Saved actuator point |
|---|---|---:|---:|---:|---:|---:|---|
| TPS | accurate, interior | 4.0004 | 160.1295 | 143.7629 | 89.779% | 54.28/34.57 | inner 360/180 ns, forward, raw/eff/cmd 213.04/123.04/122.00 ns |
| EPS | accurate, interior | 4.0007 | 164.3677 | 143.8692 | 87.529% | 53.69/34.39 | inner 360/0 ns, forward, raw/eff/cmd 465.04/285.04/286.00 ns |
| SPS | not reachable at 4 A | 5.5351 | 225.0557 | 196.2816 | 87.215% | 52.36/34.56 | lower phase boundary 0 ns |

The ratio above uses firmware ADC-derived Pout/Pin only. It ranks the measurements available from the controller and is not yet an externally calibrated efficiency measurement. At the valid 4 A setpoint, TPS is the current leading modulation by this internal ratio; SPS is excluded from the accurate-regulation ranking because it cannot reach the common 4 A target.

## CV 36 V, 6.0 A / approximately 215 W same-condition efficiency tuning

Common physical condition: 24 V-class source, source current ceiling left fixed near 20 A, RK8510 in CV at approximately 36 V with LOAD ON. The DAB used `arch current`, `iref 6`, `ikp 2`, `iki 0.2`, `islew 0.5`, forward current-loop direction, metadata `layer 220`, and an explicit `off` after each run. Every stable last-100 window below had `fault=0`. The source-current ceiling was not used as a current target and was not changed during the sweep.

### Fully tuned mode representatives

| Modulation | Regulation result | Iout (A) | Pin ADC (W) | Pout ADC (W) | Pout/Pin ADC ratio | Tpri/Tsec (C) | Saved actuator point |
|---|---|---:|---:|---:|---:|---:|---|
| TPS | accurate, global leader in this sweep | 5.99637 | 239.9497 | 214.2494 | 89.28930% | 56.63/34.61 | inner 128/40 ns, cap 300 ns, raw/eff/cmd 171.80/127.80/126.00 ns |
| EPS | accurate, mode-best point | 5.99611 | 242.3494 | 215.0736 | 88.74526% | 58.20/35.14 | inner 100/0 ns, requested cap 430 ns (telemetry quantized to 432 ns), raw/eff/cmd 139.92/89.92/86.00 ns |
| SPS | accurate, mode-best point | 5.99814 | 246.6687 | 215.4051 | 87.32567% | 48.71/33.71 | raw/eff/cmd 16.00/16.00/14.00 ns |

TPS is the current internal-efficiency leader among three fully tuned and current-accurate representatives: +0.544 percentage points over EPS and +1.964 percentage points over SPS. This ranking uses controller ADC-derived power only. It is not proof of externally calibrated converter efficiency and must not be reported as such.

### EPS actuator tuning record

| Primary/secondary inner (ns) | Requested cap (ns) | Iout (A) | ADC ratio | Result and interpretation |
|---:|---:|---:|---:|---|
| 300/0 | 320 | 4.40523 | 88.01710% | upper saturated; parameter authority insufficient |
| 200/0 | 320 | 4.94499 | 88.47844% | upper saturated; closer but still under target |
| 200/0 | 380 | 5.36449 | 86.99268% | upper saturated; raising cap increased current but reduced ratio |
| 100/0 | 430 (432 telemetry) | 5.99611 | 88.74526% | accurate and unsaturated; accepted EPS point |

The first three EPS results are parameter failures, not evidence that EPS modulation is intrinsically unsuitable. Tuning was continued until the fourth point reached the target accurately.

### TPS efficiency and authority sweep

| Primary/secondary inner (ns) | Cap (ns) | Iout (A) | Pin/Pout ADC (W) | ADC ratio | Result |
|---:|---:|---:|---:|---:|---|
| 360/180 | 320 | 3.86762 | 160.8668/138.9341 | 86.36593% | upper saturated; parameter pair cannot reach 6 A |
| 100/40 | 300 | 6.00087 | 242.2419/215.6246 | 89.01210% | accurate |
| 100/80 | 300 | 5.99991 | 241.5485/214.6274 | 88.85478% | accurate; secondary shift 80 ns is worse |
| 80/40 | 300 | 5.99863 | 243.4170/215.7118 | 88.61821% | accurate; primary shift 80 ns is worse |
| 120/40 | 300 | 5.99896 | 240.3597/214.4953 | 89.23929% | accurate |
| 120/40 recheck | 300 | 5.99984 | 241.1651/215.2441 | 89.25176% | accurate repeat; confirms the local high-efficiency region |
| 124/40 | 300 | 5.99302 | 241.4048/215.5271 | 89.28037% | accurate |
| 128/40 | 300 | 5.99637 | 239.9497/214.2494 | 89.28930% | accurate; best measured internal ratio |
| 132/40 | 300 | 5.98646 | 241.5045/214.9733 | 89.01420% | near target but ratio has fallen |
| 140/40 | 300 | 5.10673 | 208.9737/182.3996 | 87.28352% | non-monotonic region; command saturated at 300 ns after current had peaked and fallen |

The usable TPS efficiency ridge is primary inner 124--128 ns with secondary inner 40 ns. The 0.009 percentage-point difference between 124 and 128 ns is below a defensible external measurement resolution, so both are retained as a robust candidate band. Primary inner 128 ns is the lookup/model seed because it has the highest recorded controller-ADC ratio; it is not claimed to be externally proven superior to 124 ns.

Best saved TPS parameter bundle for this tested point:

```text
condition: input 24 V class, RK8510 CV approximately 36 V, output-current target 6.00 A
source_current_limit: approximately 20 A (fixed ceiling, not target)
control_arch: OUTPUT_CURRENT_PI
modulation: TPS
current_control_direction: +1 (forward)
inner_mode: MANUAL
primary_inner_ns: 128
secondary_inner_ns: 40
phase_cap_ns: 300
current_kp_ns_per_a: 2.0
current_ki_us_per_a_s: 0.2
current_reference_slew_a_per_s: 0.5
measured_vin_v: 22.53201
measured_vout_v: 35.73007
measured_iin_a: 10.64927
measured_iout_a: 5.99637
measured_pin_w_firmware_adc: 239.9497
measured_pout_w_firmware_adc: 214.2494
firmware_adc_pout_pin_ratio_pct: 89.28930
measured_tpri_c: 56.6261
measured_tsec_c: 34.6115
raw_phase_ns: 171.80
effective_phase_ns: 127.80
command_phase_ns: 126.00
current_error_a: +0.00363
stable_iout_range_a: 5.980 to 6.013
fault_max: 0
validation_status: physically_sustained_uart_firmware_adc
external_efficiency_validated: false
```

Training-friendly rows, including failed/saturated parameter attempts and explicit selection flags, are saved in `modulation_training_24v_cv36_i6.csv`. Raw evidence is every `raw_serial/v41_cv36_*i6*_20260816.log` file; the accepted TPS point is `raw_serial/v41_cv36_tps_i6_forward_inner128_40_cap300_run_20260816.log` and the repeatability check is `raw_serial/v41_cv36_tps_i6_forward_inner120_40_cap300_recheck_run_20260816.log`.

## First live CPU1 smart-charger test and v42 IPC-priority fix

The first physical smart-charge test used the unchanged 24 V-class source/current ceiling and RK8510 CV approximately 36 V. To keep PRECHARGE and CC inside one low-current EPS actuator region, the LFP12 profile was deliberately scaled to `ipre=1.0 A`, `imax=1.5 A`, `iterm=0.3 A`; this was an algorithm/IPC transition test, not a claim that 1.5 A is the final industry charging current. CPU0 selected `arch energy`; CPU1 selected CHARGER_ONLY and reported LFP12 PRECHARGE with a 32.4 V transition threshold.

The energized run was stable and fault-free but failed its algorithm criterion. In the last 100 PRECHARGE/RUN samples, Vout averaged 35.91985 V and Iout 1.18069 A, while CPU1 continued to request 1.000 A and report PRECHARGE even though Vout was already above 32.4 V. Pin/Pout ADC averages were 51.7294/42.4109 W, internal ratio 81.9861%, Tpri/Tsec 39.7063/31.2296 C and `fault=0`. CPU1 reference age rose from 30.310 s to 35.260 s in this stable window. A delayed CC indication appeared only after explicit `off`, so this run is classified `failed_ipc_starvation`, not a successful PRECHARGE-to-CC transition.

Root cause: `CPU0_IPC_TASK_PRIORITY` was `tskIDLE_PRIORITY` while the generated application task is priority 1, Ethernet RX is priority 2 and `configMAX_PRIORITIES=5`. High-rate SAVE telemetry/control kept the application runnable and starved the 10 ms CPU0 IPC task. CPU1 therefore retained stale output measurements and CPU0 retained an old 1 A energy reference. This is an RTOS scheduling/software defect, not an EPS limitation or a battery-profile defect.

v42 changes only the CPU0 IPC task priority from 0 to 3. The IPC pass remains bounded and calls `vTaskDelayUntil(..., 10 ms)` every cycle. PWM, PID, modulation, manual-stop policy, protection observability and all default-off energy/AI behavior are unchanged.

- Firmware identity: `2026-08-16-dualcore-manual-stop-v42`.
- CPU0 build passed: text/data/bss = 134988/460/170677 bytes.
- ELF SHA-256: `32A9AC310310E9627724DF03D3E6DEA1339E83E5C0CB02353812390D7AF08605`.
- SREC SHA-256: `A471377AC365C19FEEDD3C01D67E88485BECC23F3396382BD8A4717AB5034D99`.
- Physical validation pending manual CPU0 flash. First gate: repeat the identical LFP12 test and require fresh energy-reference age plus PRECHARGE-to-CC transition while RUN remains enabled.

Raw evidence:

- `raw_serial/v41_cv36_charge_lfp12_scaled_config_20260816.log`
- `raw_serial/v41_cv36_charge_lfp12_scaled_run_20260816.log`

The failed charger row is saved in `smart_charge_training_24v_cv.csv` so the stale-IPC run cannot be mixed with valid charging behavior.

## v42 CPU1 charger transition recheck

After the user flashed v42, the identical scaled LFP12 test proved the RTOS priority fix. During RUN the CPU1 energy-reference age stayed at 0 ms and the supervisor reported PRECHARGE, CC and then CV rather than remaining stale. The stable CV-stage last-100 window was Vin=23.03378 V, Vout=35.85947 V, Iin=3.45286 A, Iout=1.91772 A, Pin/Pout from board ADC=79.5147/68.7736 W, board-ADC ratio=86.49168%, Tpri/Tsec=33.2540/29.0678 C and fault=0. This validates IPC freshness and stage execution, but not low-current accuracy: CPU1 requested 1.5 A while EPS AUTO sat at an approximately 1.92 A physical actuator floor.

The run also exposed a separate algorithm defect. Before the RK8510 CV clamp began sinking current, Vout briefly reached approximately 49.8 V. The charger accepted that transient as a legitimate CV-entry condition. Therefore this run is classified `passed_ipc_transition_current_floor`, not a complete smart-charge acceptance. Its training row and raw evidence are saved in `smart_charge_training_24v_cv.csv` and `raw_serial/v42_cv36_charge_lfp12_scaled_run_20260816.log`.

## RK8510 SCPI reference channel

The Prolific RS232 adapter enumerated as COM24; COM4 remains the J-Link CDC UART. The user configured SCPI, 115200 baud and address 255. A read-only `*IDN?` returned `REK,RK8510 ,0,1.0.20260313`. Measurement command `FETC?` returns voltage,current,power in one response. Standard `MEAS:*?` headers are unsupported on this instrument and returned SCPI error -113.

The vendor RK85xx executable was inspected for its actual SCPI strings. Confirmed commands are `FUNCtion:MODE 2` for CV and `:CV:VOLTage <V>` for the setpoint. The executable's single-channel front-panel handler uses `FUNCtion:ON`/`FUNCtion:OFF`; `INPut:MODE` is a range/level selector and is not LOAD ON. This corrects the earlier provisional interpretation. On the no-suffix RK8510, repeating `FUNCtion:ON` can flip an already-open load and rewriting mode/CV can unload it; `FETCh:STAte?` is not a reliable commanded-load indication at zero input voltage. Therefore all energized tests now preconfigure CV while DAB is OFF, open the load exactly once with user confirmation, and use only `*IDN?`/`FETC?` reads during power transfer. `tools/Capture-Rk8510Scpi.ps1` is the read-only reference stream. The board ADC remains the fast control feedback and board/input/output measurements remain separately labelled.

## CV36 synchronized board-ADC plus RK8510 tests

Physical condition: source 24 V class with its current ceiling left fixed near 20 A; RK8510 CV 36 V with LOAD ON. Each DAB run ended with explicit `off`, all fault maxima were zero, and external input power was not independently measured. Consequently `Pout_RK8510 / Pin_board_ADC` is only a hybrid reference, not an externally calibrated efficiency claim.

At 5 A, the repeat TPS 240/110 ns point gave board averages Vin=22.83148 V, Vout=36.03082 V, Iin=8.70612 A, Iout=4.99919 A, Pin/Pout=198.7733/180.1258 W and board-ADC ratio=90.61871%. RK8510 stable output was approximately 35.99 V, 5.02 A and 180.7 W. The two output-side measurement channels agree closely enough to use COM24 for step validation.

At 7 A, tuning continued until every modulation had an accurate representative:

| Modulation | Inner ns | Cap ns | Board Vout/Iout | Board Pin/Pout | Board ratio | RK8510 V/I/P | Result |
|---|---:|---:|---:|---:|---:|---:|---|
| TPS | 240/110 | 360 | 35.79344 V / 4.67220 A | 188.7966/167.2364 W | 88.58020% | 35.94 V / 4.69 A / 168.50 W | failed parameter point; saturated/negative-gain region |
| TPS | 40/20 | 320 | 35.89030 V / 7.04725 A | 287.4556/252.9286 W | 87.98875% | 35.99193 V / 7.03833 A / 253.3225 W | accurate, selected overall |
| EPS | 240/0 | 380 | 35.78912 V / 4.14680 A | 168.3024/148.4108 W | 88.18104% | 35.99420 V / 4.41020 A / 158.7409 W | failed parameter point; negative-gain region |
| EPS | 40/0 | 320 | 36.06664 V / 7.09134 A | 292.1122/255.7612 W | 87.55581% | 36.00187 V / 7.04693 A / 253.7027 W | accurate |
| SPS | 0/0 | 320 | 36.13019 V / 6.99725 A | 288.2370/252.8131 W | 87.71015% | 35.99473 V / 7.03147 A / 253.0949 W | accurate and lowest stable current spread |

At this one condition the provisional board-ADC ranking is TPS > SPS > EPS. The 0.28--0.43 percentage-point spread is small and must not be generalized to other input voltages, power levels or battery stages. All successful and failed rows are saved in `modulation_training_24v_cv36_i7_v42.csv`, with separate board and RK8510 columns and explicit selection/failure labels.

## Ethernet physical-link observation

With the cable connected, both the PC NIC and board RJ45 Link LEDs remained off. Windows reported the Realtek PCIe GbE interface disconnected at 0 bps. This proves the block is below static IP/Ping/TCP; no IP configuration was attempted. Native netlist evidence and FSP configuration agree on LAN8720A address 0, external 50 MHz RMII1 reference clock, reset P513 and the documented RMII pins. Live proof of PHY 3.3 V, RESET_N high, 50 MHz clock and MDIO PHY ID is still missing. Ethernet diagnosis remains separate and does not block CV power tests.

## Plan correction: CV and smart charging remain the active gate

The project plan was corrected after the user rejected moving on to CP/CR before CV functionality was complete. The single 24 V/CV36 modulation point is only one matrix cell. Tonight's remaining CV gate is:

1. Validate Li-ion 10S, LFP 12S and lead-acid 18S charging stages using automatic RK8510 CV steps.
2. Validate P&O, incremental-conductance and fixed-power MPPT supervisor behavior.
3. Repeat representative charger/MPPT conditions at 24, 22, 20 and 18 V input while the source current ceiling remains fixed near 20 A.
4. At each relevant power layer run tuned SPS, EPS and TPS representatives; do not classify a modulation until its parameters have been tuned.
5. Save board ADC, RK8510, stage, MPPT, current-loop gains, phase/inner shifts, temperatures, saturation flags, raw logs and training rows for every success and failure.

## v43 charger-entry qualification

v43 changes only the CPU1 energy supervisor and its shared host-test source. PRECHARGE-to-CC now requires the precharge threshold continuously for 500 ms. CC-to-CV requires Vout to stay between `CV target - 0.10 V` and `CV target + 0.50 V` for 1000 ms. A high startup overshoot can no longer force CV entry. Stage changes clear their qualification timers and retain the existing 30 s termination debounce. Ordinary voltage/current architectures, PWM, PID, modulation and manual-stop behavior are unchanged.

Target builds passed:

- CPU0 text/data/bss = 134988/460/170685 bytes; ELF SHA-256 `F269FD4F3CEA3AA0F2D7CFDE30200E7C1B05C860D1D36634C47899D95CE8DEB6`; SREC SHA-256 `C0C65F3E82DC8DB36F17C4DA84C801A9A37F6CA3A7412DE26D8466DF45B2DBCC`.
- CPU1 text/data/bss = 16982/132/9104 bytes; ELF SHA-256 `9F952D46C9E2CCBEE45AFDE781FFC998C84084ECAA27CF8A7E560AB084AA6327`; SREC SHA-256 `07C5FE3CBF1D0919ACDD3E91C5EB2EDA203210E6FBA288B672AB778085D62C36`.

The available Renesas clang lacks Windows/wasm targets, so the host behavioral executable could not be produced on this PC. Both real ARM targets compile and link successfully; physical v43 charger-stage validation is still required after the user flashes both cores.

## v43 Li-ion 10S CV-stage physical validation and RK8510 sequencing correction

The user flashed both v43 cores. Several attempted automatic runs were deliberately retained but excluded from training conclusions because the RK8510 was unloaded by the host sequence. The first host script used an invalid combined PowerShell array and sent no valid sequence. A later script used the manual's `FUNCtion:ON1`, which did not open the no-suffix RK8510. After the vendor executable established the single-channel `FUNCtion:ON` command, two further runs were invalidated because the host repeated ON or rewrote CV/mode immediately before board startup, unloading the instrument. Board power was explicitly stopped after each invalid attempt. Raw evidence is retained in `raw_serial/v43_24v_liion10_*20260816.*`, including `v43_abort_after_rk_toggle_off_20260816.log` and `v43_abort_after_rk_config_unload_20260816.log`; these runs must not be used to score EPS.

The accepted run preconfigured RK8510 CV=42.2 V with DAB OFF, opened LOAD exactly once with user confirmation, then performed only read-only `FETC?` queries while energized. Firmware used Li-ion 10S, `ipre=1.0 A`, `imax=1.5 A`, `iterm=0.3 A`, ENERGY_SUPERVISOR, EPS forward/AUTO, cap 320 ns, current Kp/Ki=2/0.2 and slew=2 A/s. v43 progressed through PRECHARGE and CC into CV; CV persisted for 745 ALG samples with a fresh 13 ms CPU1 reference and no fault.

Accepted last-100 active averages: board Vin=23.00600 V, Vout=41.96387 V, Iin=3.15860 A, Iout=1.48137 A, Pin/Pout ADC=72.6665/62.1608 W, internal board ratio=85.54258%, Tpri/Tsec=39.3120/33.1276 C and effective phase=290.56 ns. The synchronized RK8510 last-100 average was 42.1832 V, 1.50219 A and 63.36675 W. This validates v43 CC-to-CV qualification and stable CV-stage current delivery. It does not validate charge termination: the board ADC stayed about 0.22 V below RK8510 and slightly below the 42.0 V internal target, so the CV supervisor did not taper below `iterm=0.3 A`. The next isolated test must preconfigure approximately 42.3 V while DAB is OFF, reopen LOAD once, and keep the RK8510 channel strictly read-only during transfer.

Training row: `charge_liion10_scaled_v43_001` in `smart_charge_training_24v_cv.csv`. Accepted raw logs are `raw_serial/v43_24v_liion10_cv42p2_eps_i1p5_readonly_board_20260816.log` and `raw_serial/v43_24v_liion10_cv42p2_eps_i1p5_readonly_rk8510_20260816.csv`.

## RK8510 420 W OPP unload and explicit user abort

The first 42.3 V termination attempt did not sink load current. RK8510 briefly observed more than 420 W and enforced its own OPP unload/alarm; the user identified the front-panel 420 W threshold. After the event, read-only SCPI returned `FETCh:STAte?=2048`, `SYSTem:OVP?=152`, `SYSTem:OCP?=42`, `SYSTem:OPP?=420`, timed unload 0 s, soft start 0 ms, unload voltage 0 V and startup voltage 2 V. The 100 ms/50 ms `FETC?` stream missed the short >420 W peak and showed only the post-trip 0 A state. This run is classified `invalid_rk8510_opp_unload`, not a charger or modulation result. Raw evidence: `raw_serial/v43_24v_liion10_cv42p3_taper_done_eps_i1p5_board_20260816.log`, its paired RK8510 CSV, and `raw_serial/v43_abort_cv42p3_no_load_current_20260816.log`.

To reduce startup energy without changing the RK8510's 420 W protection, the board was preconfigured while OFF for EPS manual inner 600/0 ns, phase cap 80 ns, current Kp/Ki 2/0.2 and slew 1 A/s. A single `FUNCtion:ON` did not clear/reopen the post-OPP unloaded state. The user then explicitly instructed `不要启动`. A board start had already begun and was immediately interrupted; the stop log proves READY, PWM=0 and RUN=0. Before stop, the brief invalid window reached about 43.16 V with about 7 W board-input ADC power and near-zero output current. It is classified `user_aborted_load_off`, excluded from training and modulation scoring. Raw evidence: `raw_serial/v43_24v_liion10_cv42p3_eps600_0_cap80_preconfig_20260816.log`, `raw_serial/v43_24v_liion10_cv42p3_eps600_0_cap80_run_board_20260816.log`, its paired RK8510 CSV and `raw_serial/v43_user_abort_no_start_20260816.log`.

Current handoff state: DAB READY, PWM=0, RUN=0; RK8510 LOAD is OFF per the user. Do not reopen the load, start DAB, send COM4 commands, or resume COM24 testing until the user explicitly authorizes it.

## Persistent-load operating rule for subsequent tests

The user changed the operating procedure for all subsequent tests: once the RK8510 LOAD has been opened, it remains continuously ON across parameter points. The host must not send `FUNCtion:OFF`, repeat `FUNCtion:ON`, or rewrite RK8510 mode/setpoint while the DAB is energized. COM24 is read-only during power transfer (`*IDN?`/`FETC?` as needed); daily closed-loop control continues to use the board ADC.

The DAB controller must manage transferred power so normal continuous output remains at or below the RK8510 400 W rating and does not reach its 420 W OPP trip. Power is to be raised by controlled reference/phase/current slew with live board-ADC and RK8510 observation, not by cycling LOAD. An actual fault, user stop command, or RK8510 self-protection trip remains an abnormal termination and must be recorded rather than treated as a valid modulation result.

This instruction changes the future run procedure only. At the time it was recorded, the confirmed physical state remained DAB READY/PWM=0/RUN=0 and RK8510 LOAD OFF; no device command was issued and no test was started.

## Manual-panel control and periodic read-only polling

The user revoked continuous COM24 ownership after finding that a long host capture prevented front-panel adjustment. From this point onward the user alone changes RK8510 CV setpoints and LOAD state. Host access to COM24 is limited to short read-only `*IDN?`/`FETC?` windows, nominally about one second, and the port must be closed immediately after every window. The host must not leave a background COM24 reader running and must not send mode, CV or LOAD commands unless the user explicitly changes this rule again.

The preceding EPS 80/0 ns, cap 20 ns, 1.0 A-command run at the manually held 36 V point was stable but actuator-limited: RK8510 remained near 36.01 V, 3.07 A and 110.5 W while the board reported about 35.76 V, 3.04 A and 108.8 W from roughly 120.7 W input ADC power. This is a valid observation of an excessive zero-command power floor for that parameter point, not proof that EPS itself is unsuitable.

The subsequent attempted automatic trajectory is invalid for training and modulation ranking. A host CV-write sequence occupied COM24 and initially left the RK8510 unloaded; later the user could not reliably adjust the panel while the port was held. After the interrupted host capture, DAB output was found still active near 40.4 V and 0.86 A, so a dedicated safe-stop capture was issued. `raw_serial/v43_restart4_abort_safe_off_20260816.log` proves final `READY`, `PWM=0`, `RUN=0`; the last short COM24 read then returned 0 V, 0 A and 0 W and released the port. Retain the paired `v43_24v_liion10_auto_continuous_restart4_eps600_0_cap20_*` files as invalid sequencing evidence only.

## Successful v43 Li-ion 10S continuous manual-panel trajectory

The accepted run used 24 V nominal input with the user's reduced source-current ceiling, RK8510 CV with LOAD continuously ON and user-operated voltage setpoints, Li-ion 10S, `imax=1.0 A`, `ipre=0.5 A`, `iterm=0.5 A`, ENERGY_SUPERVISOR current PI `Kp=0.5`, `Ki=0.05`, slew `0.05 A/s`, EPS manual inner 600/0 ns and phase cap 20 ns. COM24 was opened only for approximately one-second read-only windows and released after every read. Setpoints 36.0, 38.0, 40.0, 41.0, 41.5, 41.8, 42.0, 42.1, 42.2 and 42.3 V were physically traversed without a fault.

The run demonstrated CC at the lower points, CC-to-CV at the 42.1 V external point and CV-to-DONE after the 30 s termination debounce. Representative RK8510 points fell from 36.019 V/1.956 A/70.461 W to 42.205 V/0.362 A/15.278 W and ended at 42.314 V/0.388 A/16.396 W. At DONE the firmware reference was 0 A and stage ID was 6, but manual EPS inner 600 ns retained approximately 0.40 A and 17 W board-output power. The board was explicitly stopped; `raw_serial/v43_restart5_done_actuator_floor_safe_off_20260816.log` proves final READY, PWM=0 and RUN=0. This is classified as a successful charge-stage trajectory with a failed zero-power DONE actuator action, not as a failed charging-algorithm test.

The run-level training row is `charge_liion10_manual_trajectory_v43_001` in `smart_charge_training_24v_cv.csv`. Pointwise, training-friendly records are in `smart_charge_trajectory_points_24v_cv.csv`, with pairing quality explicitly marked as representative live windows. Complete board streams are `raw_serial/v43_24v_liion10_manualpanel_periodic_restart5_eps600_0_cap20_board_20260816.log` and `raw_serial/v43_24v_liion10_manualpanel_periodic_restart5b_eps600_0_cap20_board_20260816.log`. Earlier short RK reads reused a pathname and therefore retained only the last window per file; the pointwise external values were preserved from the live readouts, and `tools/Capture-Rk8510Scpi.ps1` is now append-only so future periodic reads remain in one auditable CSV.

## v43 LFP12 trajectory and high-voltage CC retuning

The next accepted-start run selected `LIFEPO4_12S_43P8V` with the same ENERGY_SUPERVISOR gains and EPS manual 600/0 ns. External points 36.008 V/1.960 A/70.575 W, 38.011 V/1.570 A/59.677 W and 40.004 V/1.127 A/45.084 W were stable. At 42.015 V the RK8510 current had already fallen to about 0.430 A even though the firmware remained in CC with `Iref=1.000 A`. At 43.003 V it fell further to 0.113 A while the board reported about 42.60 V/0.10 A. These are stable measurements but failed CC-current tracking points; they must not be labelled as successful 1 A control.

With inner 600 ns the 20 ns phase cap was saturated. A live request for cap 40 ns was correctly rejected because phase-cap changes require READY. After an explicit OFF/restart, cap 40 ns was applied, but inner 600 ns still produced only about 0.06--0.09 A board current at the 43 V external point. This parameter pair is therefore recorded as `failed_high_voltage_cc_undertrack`, without classifying EPS itself. Raw evidence is `raw_serial/v43_24v_lfp12_manualpanel_trajectory1_eps600_0_cap20_board_20260816.log`, `raw_serial/v43_24v_lfp12_manualpanel_trajectory1_rk8510_20260816.csv`, `raw_serial/v43_24v_lfp12_cv43_tune_cap40_restart_board_20260816.log` and `raw_serial/v43_lfp12_cap40_inner600_undertrack_safe_off_20260816.log`.

The next tuning point is cap 40 ns with inner 240/0 ns. It must be retested from external CV 42.0 V, because the 42 V/0.4 A point was already under-delivering the same 1.0 A CC reference. Current handoff state is READY, PWM=0, RUN=0; RK8510 LOAD was not toggled.

## LFP12 CV42 closed-loop retuning and selected EPS point

The RK8510 remained manually set near 42.0 V with LOAD continuously ON. COM24 was opened only for one-second read-only windows and released immediately. All runs used LFP12 with configured `imax=1.0 A`, `iterm=0.5 A`, EPS forward, secondary inner 0 ns and current-reference slew 0.05 A/s. Every energized window had `fault=0` and remained below 50 W board-input ADC power.

The first cap-40/inner-240 run exposed two separate issues. Its last-100 board window was Vin=22.97610 V, Vout=41.62659 V, Iin=1.72774 A, Iout=0.81265 A, Pin/Pout=39.6969/33.8298 W and Tpri/Tsec=36.1921/32.6255 C. RK8510 was 42.013--42.015 V and 0.845--0.865 A. The cap saturated and current remained below the 1.0 A request. More importantly, the charger changed CC to CV after the startup trajectory stayed from 43.88 V down through 43.70 V for about one second while output current was approximately zero. The v43 voltage-only qualification therefore still accepts an unloaded startup crossing; future firmware must also require active DAB power transfer and a minimum positive charging current or output power.

The next cap-40/inner-220 run inherited DONE after the state machine continued termination timing while the DAB was OFF. `energy_Iref=0 A`, yet the last-100 board window was 41.84010 V/0.90194 A and 37.7392 W output. This is a failed zero-command actuator-floor point, not current tracking. It also proves charger stage timers must be gated by the converter RUN/energy-enabled state.

After a complete `bat stop -> energy off -> profile/config -> energy charge -> bat start` reset, cap 60/inner 240 with Kp/Ki=0.5/0.05 stayed in CC. Last-100 board averages were Vin=23.05191 V, Vout=41.76085 V, Iin=1.92082 A, Iout=0.91020 A, Pin/Pout=44.2769/38.0115 W and Tpri/Tsec=35.5502/32.7867 C. RK8510 was 42.012 V, 0.950--0.951 A and approximately 39.94 W. This was near target but the 60 ns cap saturated.

Raising the cap to 80 ns while retaining Kp/Ki=0.5/0.05 removed cap saturation but did not improve the 30-second stable result: board Iout averaged 0.89135 A and command phase averaged only 54.92 ns. The point is classified `failed_slow_pi_undertrack`; it showed that PI convergence, not the 80 ns cap, was limiting regulation.

The selected 42 V point increased only the current PI to Kp=2.0 ns/A and Ki=0.200 us/(A*s), retaining inner 240/0 ns, cap 80 ns and slew 0.05 A/s. A 40-second repeat produced the following last-100 board averages:

```text
condition: nominal 24 V source, RK8510 CV 42.0 V, LFP12 CC request 1.000 A
modulation: EPS
primary_inner_ns: 240
secondary_inner_ns: 0
phase_cap_ns: 80
current_kp_ns_per_a: 2.0
current_ki_us_per_a_s: 0.2
current_reference_slew_a_per_s: 0.05
measured_vin_v: 23.07536
measured_vout_v: 41.82335
measured_iin_a: 2.09671
measured_iout_a: 0.99888
measured_pin_w_firmware_adc: 48.3833
measured_pout_w_firmware_adc: 41.7767
firmware_adc_pout_pin_ratio_pct: 86.3453
measured_tpri_c: 36.7009
measured_tsec_c: 33.7108
effective_phase_ns_avg: 68.92
command_phase_ns_avg: 70.00
current_error_a_avg: +0.00112
fault_max: 0
charge_stage: CC
```

The synchronized RK8510 window was 42.011 V, 1.026--1.031 A and 43.103--43.313 W. Board ADC remains the control reference as required; the approximately +3% RK8510 current difference is retained as a separately labelled external measurement rather than used to alter the board current loop. This parameter set is `passed_selected_42v` and is the current EPS lookup candidate for nominal 24 V input, LFP12 CC, external CV approximately 42 V and 1 A output.

All five selected/failed point records are appended to `smart_charge_trajectory_points_24v_cv.csv`. Raw board evidence is `raw_serial/v43_24v_lfp12_cv42_*20260816.log`; append-only RK evidence is `raw_serial/v43_24v_lfp12_tuning_rk8510_20260816.csv`. Current handoff is DAB READY, PWM=0, RUN=0; RK8510 LOAD was not toggled.

## LFP12 CV43 retuning and selected EPS point

The user manually changed RK8510 CV to 43.0 V and kept LOAD continuously ON. A short read-only `:CV:VOLTage?` query confirmed the setpoint as 43 V and immediately released COM24; the host did not write the RK8510 configuration or LOAD state. Both runs retained the selected CV42 current-loop gains and EPS inner shift: primary/secondary inner 240/0 ns, Kp=2.0 ns/A, Ki=0.200 us/(A*s), slew 0.05 A/s, LFP12 CC request 1.000 A and `iterm=0.5 A`.

The first run retained the 80 ns phase cap. Its last-100 board averages were Vin=23.07271 V, Vout=42.73897 V, Iin=1.55369 A, Iout=0.70061 A, Pin/Pout=35.8462/29.9425 W, board ADC ratio=83.5305%, Tpri/Tsec=34.0637/31.4985 C, effective/command phase=80/80 ns and average current error=+0.29939 A. The actuator was saturated at the cap. The synchronized RK8510 window was 43.003 V, 0.730 A and 31.392 W. This is `failed_cap_saturated_undertrack`, a parameter failure rather than an EPS verdict.

The second run changed only the phase cap from 80 to 120 ns. Its last-100 board averages were Vin=22.94452 V, Vout=42.60344 V, Iin=2.13081 A, Iout=0.99805 A, Pin/Pout=48.8895/42.5205 W, board ADC ratio=86.9727%, Tpri/Tsec=35.7405/32.8377 C, effective phase=97.24 ns, command phase=98.00 ns and average current error=+0.00195 A. The synchronized RK8510 window averaged 43.002 V, 1.034 A and 44.464 W, giving 90.948% external-output-to-board-input ratio. Fault remained zero and the charger remained in CC. This is `passed_selected_43v`; the 24 V nominal/LFP12/43 V/1 A EPS lookup candidate is inner 240/0 ns, cap 120 ns, Kp/Ki 2.0/0.2 and slew 0.05 A/s.

Both parameter points are appended to `smart_charge_trajectory_points_24v_cv.csv`. Board evidence is `raw_serial/v43_24v_lfp12_cv43_eps_inner240_cap80_ikp2_iki0p2_board_20260816.log` and `raw_serial/v43_24v_lfp12_cv43_eps_inner240_cap120_ikp2_iki0p2_board_20260816.log`; synchronized external evidence is in append-only `raw_serial/v43_24v_lfp12_tuning_rk8510_20260816.csv`. Each run ended with an explicit DAB OFF and final READY/PWM=0/RUN=0; RK8510 LOAD was not toggled.

At the next user-set external point of 43.5 V, the selected CV43 parameters required no further retuning. Last-100 board averages were Vin=23.08464 V, Vout=43.30539 V, Iin=2.14674 A, Iout=0.99983 A, Pin/Pout=49.5561/43.2969 W, board ADC ratio=87.3695%, Tpri/Tsec=33.9048/31.4252 C, effective/command phase=106.32/106.00 ns and current error=+0.00017 A. RK8510 averaged 43.507 V, 1.0332 A and 44.9508 W, corresponding to 90.7069% external-output-to-board-input ratio. The cap remained unsaturated, fault stayed zero and stage remained CC. This point is saved as `passed_selected_43p5v`; the run ended READY/PWM=0/RUN=0 without toggling RK8510 LOAD.

At external CV 43.7 V, the same parameters again tracked 1 A without retuning. Last-100 board averages were Vin=23.06668 V, Vout=43.45808 V, Iin=2.15479 A, Iout=0.99969 A, Pin/Pout=49.7034/43.4452 W, board ADC ratio=87.4089%, Tpri/Tsec=34.1218/31.4792 C, effective/command phase=110.48/110.00 ns and current error=+0.00031 A. RK8510 averaged 43.713 V, 1.0310 A and 45.0674 W, corresponding to 90.6727% external-output-to-board-input ratio. The charger correctly remained in CC because board Vout stayed about 0.34 V below the internal 43.8 V target and below the 43.70 V CV qualification threshold. This point is `passed_selected_43p7v`; fault remained zero and the run ended READY/PWM=0/RUN=0 without toggling RK8510 LOAD.

At the user-set external CV 44.0 V point, the same EPS parameters exercised the complete high-voltage transition. The board moved from CC to CV after its one-second voltage qualification, regulated board Vout near the 43.8 V LFP12 target, reduced the charge reference to 0.420 A, then entered DONE after the 30-second termination debounce. The last-100 CV-stage board averages were Vin=23.14036 V, Vout=43.79035 V, Iin=1.03282 A, Iout=0.42055 A, Pin/Pout=23.8992/18.4181 W, Tpri/Tsec=34.5257/32.1300 C, effective/command phase=88.08/90.00 ns and current error=-0.00055 A. The synchronized RK8510 CV window was 44.008 V, 0.4556 A and 20.0494 W. This validates controlled CV taper and the CC-to-CV transition.

In DONE, the last-100 board window was Vin=23.23609 V, Vout=43.80644 V, Iin=0.18897 A, Iout=0.00369 A, Pin/Pout=4.3908/0.1613 W, Tpri/Tsec=35.1236/32.4862 C, effective/command phase=42.60/42.73 ns and current reference 0 A. The synchronized RK8510 read after DONE returned five repeated 0 V/0 A/0 W samples even though the board-side Vout ADC remained near 43.8 V. No RK command or LOAD toggle was sent. This is retained as an unresolved output-path/external-measurement-state discrepancy, but the charge-stage algorithm and board current reduction are accepted. The DAB was explicitly stopped and the final log proves READY/PWM=0/RUN=0 with fault zero.

Two stage-labelled training rows, `lfp12_v43_cv44_cv_taper_eps` and `lfp12_v43_cv44_done_eps`, preserve the CV and DONE windows separately. Raw evidence is `raw_serial/v43_24v_lfp12_cv44_eps_inner240_cap120_ikp2_iki0p2_transition_board_20260816.log` plus the append-only RK8510 log. The transition is not averaged together with the earlier 1 A CC points.

## Qualified v43 code and parameter preservation

The qualified 24 V/LFP12/EPS records were promoted into the dual-core source tree as the record-only table `RA8P1_DAB_Feature_Showcase/dual_core/RA8P1_DAB_DualCore_Solution_CPU0/src/dab_validated_charger_table.h`. It contains the physically accepted 42.0, 43.0, 43.5 and 43.7 V CC points and the 44.0 V CV taper point, including current PI gains, slew, modulation, inner shifts, phase cap, board measurements, external measurements and efficiency. It is intentionally separate from the older voltage-PI schedule and is never applied automatically. The read-only UART command `chgtable show` exposes the compiled records; all existing boot and control defaults remain unchanged.

Both target builds passed after adding the table and updating the energy algorithm version to v43. CPU0 size was text/data/bss = 136164/460/170685 bytes; CPU1 was 16982/132/9104 bytes. Artifact SHA-256 values:

- CPU0 ELF `E03F18B5BE1492BE1F66680C5EAA2A12A477577F016D1FB7EDC635AC25DB1695`
- CPU0 SREC `5B89E87661A768D249B5BC6B1E3F90FAF6D599B3B079129314C4FE8713688739`
- CPU1 ELF `B694A8609BCD12C19B65295F158C378271A96D4997DC45BB15C8C7B287AB8616`
- CPU1 SREC `07C5FE3CBF1D0919ACDD3E91C5EB2EDA203210E6FBA288B672AB778085D62C36`

This build proves compilation/linking only. The physical power results in this worklog were obtained with the already flashed v43 image before the record-only table was added; the new `chgtable show` command itself has not yet been flashed or physically exercised.

`llvm-strings` inspection of the rebuilt CPU0 ELF confirmed that the table version, all three `[DAB][CHGVAL]` format records and the `chgtable show` help string are present in the linked image. A post-run short COM24 read-only check could open the port but all of `FETCh:STAte?`, `:CV:VOLTage?` and `FETC?` timed out; COM24 was immediately released. Therefore the earlier post-DONE 0/0/0 RK samples remain excluded from efficiency claims and labelled as an unresolved instrument communication/output-path state.

Git preservation is pending environment approval. An exact-scope `git add` was attempted for the dual-core v43 source, validated table, plan, worklog, raw logs, training CSVs and RK8510 scripts, but the sandbox could not write `.git/index.lock`; the follow-up elevation was rejected because the automatic approval service reported its usage limit. No Git workaround was attempted. All files remain saved on disk, while vendor packages, manuals and the user's unrelated newly created `可能加的功能.txt` remain untracked and outside the intended commit.
