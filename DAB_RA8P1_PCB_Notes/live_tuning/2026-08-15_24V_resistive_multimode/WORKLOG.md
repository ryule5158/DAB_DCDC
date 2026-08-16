# 24 V resistive-load SPS/EPS/TPS live tuning worklog

## Record policy

- Every energized run must retain the raw COM4 log, a structured annotation, the applied parameters, and the result/fault status.
- Modulation must be recorded as both requested and effective mode. Phase data must include raw, effective, command, primary-inner, and secondary-inner phase.
- External DMM voltage is authoritative for voltage-accuracy conclusions. Firmware telemetry without a synchronized DMM reading remains usable for controller/mode comparison but is marked as pending external calibration.
- Abnormal acoustic behavior requires an immediate stop; do not automatically restart or increase phase.
- Never judge SPS, EPS, or TPS from default parameters alone. At every voltage/input/load point, scan and tune the safe parameter range before drawing a mode conclusion.
- Voltage targets are every integer point from 36 V through 48 V wherever safe operation permits.
- Once a 36-48 V target is selected, hold that same output target while Vin is varied from 24 V toward 18 V. Do not change Vref to hide line-regulation error; log any unreachable input corner only after safe tuning is exhausted.
- The final power matrix must extend beyond 200 W toward the board's 400 W design target. Keep command-limit labels separate from measured power and validate in staged 200 W, 300 W, then 400 W operation.

## Bench configuration

- Supply setting: 24.0 V, 7.0 A current limit.
- Load: user-selected nominal 18 ohm resistive load. The v35 SPS steady telemetry gives approximately 15.29 ohm from Vout/Iout; do not replace the nominal value with this estimate until independently measured.
- Raw serial data: `raw_serial/session.log`.
- Structured training annotations: `annotations.jsonl`.
- Stable parameter records: `validated_parameters.jsonl`.

## Firmware and data format

- CPU0/CPU1 v35 were built successfully; flashing was performed manually by the user.
- v35 was verified live by the 26-field VOFA header.
- Fields 20/21 are requested/effective modulation IDs. The appended fields are effective phase, command phase, primary-inner phase, and secondary-inner phase.
- v35 compensates the raw outer phase when EPS/TPS inner phase is applied so the controller command continues to represent effective pulse-center phase.

## Preserved pre-v35 SPS observations at the nominal 18 ohm setting

- Open-loop SPS showed diminishing voltage gain: approximately 43.32 V at 40 ns, 44.18 V at 80 ns, 44.44 V at 100 ns, and 44.56 V at 120 ns.
- Closed-loop 44.0 V saturated near the 80 ns phase cap and held approximately 43.80 V while Vin drooped to approximately 22.63 V.
- Closed-loop 43.5 V regulated successfully near 44/48 ns, around 124 W output. This operating point was selected as the first equal-condition SPS/EPS/TPS comparison target.

## v35 test result: SPS, Vref 43.5 V

- Run time: 2026-08-15 21:06:58 +08:00; automatic `off` at the end.
- Commands: `off`, `unlock 200`, `p 200`, `v 43.5`, `mod sps`, `save`, `on`.
- Stable samples: 211.
- Vin: mean 22.895 V, range 22.866 to 22.925 V.
- Vout: mean 43.499 V, range 43.429 to 43.545 V.
- Iin/Iout means: 5.809 A / 2.845 A.
- Pin/Pout means: 132.997 W / 123.755 W.
- Raw/effective phase mean: 46.900 ns; command phase mean: 46.365 ns.
- Primary/secondary inner phase: 0/0 ns; requested/effective mode IDs: 0/0 (SPS/SPS).
- Maximum reported temperatures: primary 29.61 C, secondary 27.69 C.
- Fault maximum: 0. PWM and all four path MOS were confirmed OFF after the run.
- External DMM reading and run-specific abnormal-sound report: pending.

## v37 44 V modulation tuning continuation

- All trials below used the same physical condition metadata: 24 V source setting, 44 V target, nominal 10 ohm resistive load, 200 W requested layer, p400/160 ns outer phase cap, Kp=20, Ki=5, Kd=0, and last-100-valid-RUN stable windows. Each capture ended with automatic `off`; all stable windows had fault=0.
- SPS p400 reached 40.826 V and 192.601 W at Vin=22.794 V. The ramp peaked around 120--140 ns and lost slight output by 160 ns, so extra phase cap did not recover 44 V.
- EPS 40/0 reached 40.343 V; EPS 0/40 reached 41.011 V; EPS 0/80 reached 41.043 V and 194.409 W; EPS 0/120 fell to 40.633 V while Vin drooped. EPS 0/80 is the provisional EPS capability-best point, but it is not promotion-eligible because the fixed 44 V target was missed under saturation.
- TPS 20/80 reached 40.690 V and 192.427 W. TPS 20/120 reached 40.837 V and 193.142 W at Vin=22.729 V; its normalized Vout/Vin ratio of 1.796729 is effectively unchanged from TPS 20/80 (1.797814), so the secondary-shift direction is near a plateau.
- TPS tuning is not concluded. The next isolated change is primary inner shift 20 -> 40 ns while secondary inner shift remains 120 ns. PI gains remain fixed because every stable sample is at the actuator limit; changing Kp/Ki cannot remove that steady-state ceiling.
- Raw captures and complete machine-readable trial fields are retained in `raw_serial/session.log`, `annotations.jsonl`, and `mode_tuning_trials.jsonl`. External DMM values and run-specific sound reports remain pending annotations.
- TPS 40/120 produced 40.675 V and 191.240 W at Vin=22.773 V, normalized ratio 1.786086. It is worse than 20/120 in both absolute and normalized transfer, so increasing primary inner shift is rejected; one 10/80 ns refinement point remains before closing TPS for this condition.
- TPS 10/80 command quantized to a stable actual 12/80 ns and produced 40.856 V, 193.387 W at Vin=22.710 V, normalized ratio 1.799040. This is the TPS provisional capability-best point; TPS tuning is closed for this condition.
- Mode-best records are now saved for SPS, EPS, and TPS. EPS 0/80 is the absolute capability leader at 41.043 V, but no mode met fixed 44 V. The outcome is stored as `NO_MODE_MEETS_FIXED_TARGET`, not as a final selected-mode training label, because all candidates were saturated and no external DMM comparison was supplied.

## End-of-day 300 W-layer staging at 6.5 ohm

- Physical load was changed by the user to nominal 6.5 ohm; firmware metadata was set to VinSet=24 V, Vref=44 V, Rload=6.5 ohm, requested layer=300 W. SPS remained closed-loop PI with Kp=20, Ki=5, Kd=0.
- SPS p300/120 ns: firmware Vin/Vout 22.215/32.050 V, Iin/Iout 14.657/8.606 A, Pin/Pout 325.609/275.817 W, primary/secondary maximum 35.18/27.76 C, saturated throughout, fault=0.
- SPS p400/160 ns: firmware Vin/Vout 22.494/32.362 V, Iin/Iout 14.617/8.566 A, Pin/Pout 328.799/277.210 W, primary/secondary maximum 43.31/29.13 C, saturated throughout, fault=0. The extra 40 ns gained only 0.313 V and 1.393 W of firmware-estimated output, so the phase-cap scan was closed.
- User-reported external input at the p400 point was 24.0 V and 14.5 A, approximately 348 W. This confirms a 300 W-class input condition, not a validated 300 W output.
- Output power remains unresolved: firmware Pout is 277.210 W, while 32.362 V across an actual 6.5 ohm resistor would imply only 161.128 W. External Vout/Iout or a checked load resistance is required before using this point as a physical efficiency or 300 W output label.
- Both runs ended with automatic `off`; today no further energization is authorized. EPS/TPS at this 300 W-layer condition remain pending for a future session after calibration checks.

## Final failure diagnosis and efficiency boundary

- The fixed 44 V target failed because the outer voltage PI was saturated for the entire stable window. Kp/Ki changes cannot remove a steady-state actuator ceiling.
- `p 400` is not 400 W control: the firmware maps it to 40 GPT counts or a 160 ns phase cap. The compiled PWM model allows an absolute 480 ns, but that range was not used by the closed loop and must not be opened without restored protection and waveform validation.
- Raising the cap from 120 ns to 160 ns gained only 0.313 V and 1.393 W in firmware output telemetry. This indicates a high-current plant/operating-envelope plateau at the present configuration; possible contributors are transformer ratio, leakage/series impedance, 400 ns dead time, device and wiring conduction drops, and source-to-board voltage drop. None is yet isolated as a defective component.
- A definite software/measurement problem remains: firmware Vout/Iout imply 3.778 ohm, while the user set nominal load is 6.5 ohm. Therefore firmware Pout and all labels derived from it are not physically trustworthy until current scaling or actual load resistance is verified.
- Efficiency is 84.31% only in the self-consistent firmware telemetry ratio (277.210/328.799). Combining firmware Pout with the user-reported 348 W external input gives 79.66%, but this mixes uncalibrated output telemetry with an external input reading. If Vout=32.362 V and the load truly is 6.5 ohm, output would be 161.128 W and efficiency 46.30%. The spread proves that physical efficiency is unresolved, not that the converter necessarily operates at 46.30%.
- Final assessment: no evidence of an outright board hardware defect (fault=0, stable switching, temperatures below limits), but the current hardware-plus-operating-point could not supply the requested gain at the allowed phase. The proven blockers are software control-authority mapping and measurement calibration; hardware capability can be judged only after those are corrected and external waveforms/power are measured.
- Last complete p400 capture ended READY, PWM=0, RUN=0 after automatic off. A redundant final `off` attempt could not open COM4 because the port had disappeared; no further hardware command was issued.

## v38 protected full-phase build and project-direction update

- Removed the artificial coupling `p400 -> 160 ns`. `p` is now the output-power safety ceiling; `cap` independently configures the closed-loop phase limit from 0 to 480 ns after `unlock 400`.
- Restored automatic software shutdown, digital RAW/OC/OK checks, ADC stale/current/power guards, Vin/Vout and temperature trips. Cold start still restricts phase to 8/80/160 ns below 5/15/30 V and phase rises at 4 ns per 100 ms.
- Expanded bus-current hard limits to 26 A input and 18 A output. Input soft power is 1.25 times the requested output-power ceiling, so a 400 W output request is not blocked at 400 W input. The hard power trip remains 1.35 times the output ceiling.
- CPU0 and CPU1 builds passed. CPU0 size is text 122650, data 444, bss 169897 bytes. Binary checks found the v38 firmware identity, v38 schema, independent cap command, automatic-trip banner and v38 empty-table identity.
- No v38 flash or physical test has been performed. The user remains responsible for manual flashing.
- Product direction is updated from resistive-load voltage regulation toward photovoltaic MPPT, battery CC/CV charging/discharging, bidirectional power flow and efficiency/ZVS-oriented SPS/EPS/TPS selection. The control hierarchy and electronic-load plan are saved in `PHOTOVOLTAIC_STORAGE_DAB_ROADMAP.md`.

## v37 SPS phase-cap completion: p400 confirms a transfer peak below 44 V

- Run time: 2026-08-15 22:14:05 +08:00; same physical condition and PI gains as the p300 trial, with only the phase cap raised to 160 ns (`p 400`). Automatic `off` completed at 22:14:21.
- Stable-window rule: last 100 valid RUN samples, sequence 45500468 to 46242375.
- Vin mean/range: 22.794 V / 22.778 to 22.808 V. Vout mean/range: 40.826 V / 40.799 to 40.850 V; target error 3.174 V.
- Iin/Iout means: 9.244 A / 4.718 A; Pin/Pout means: 210.711 W / 192.601 W; telemetry Pout/Pin ratio 91.405%.
- Raw/effective/command phase all reached 160 ns. Outer saturation remained asserted for every stable sample; fault maximum 0; maximum temperatures 30.44 C primary and 27.13 C secondary.
- Across the ramp, maximum Vout was 40.890 V. Mean Vout at raw phase 120--140 ns was 40.859 V, while the mean at 160 ns was 40.819 V. The added phase no longer increased output and slightly reduced it.
- Result: SPS cannot reach the fixed 44 V target at this measured Vin/load condition within the validated 0--160 ns scan. This is a physical SPS transfer-ceiling finding, not a Kp/Ki conclusion; changing gains cannot remove a saturated steady-state actuator ceiling.
- Do not promote this SPS bundle as a precise 44 V parameter set. SPS remains an in-range capability result near 40.8 V and a negative training candidate for the exact 44 V objective.
- Next tuning action: test EPS in closed loop at the same condition, first with the previously useful 40/0 ns inner partition and p400 headroom, then tune inner shift and PI only after determining whether 44 V is reachable without saturation.

## v37 EPS tuning point 1: inner 40/0 ns

- Run time: 2026-08-15 22:16:41 +08:00; Vref 44 V, nominal 10 ohm, requested 200 W layer, p400/160 ns cap, Kp=20, Ki=5, Kd=0. EPS manual inner partition 40/0 ns. Automatic `off` completed.
- Stable-window rule: last 100 valid RUN samples, sequence 68776832 to 69518738.
- Vin mean/range: 22.740 V / 22.727 to 22.755 V. Vout mean/range: 40.343 V / 40.317 to 40.367 V; target error 3.657 V.
- Iin/Iout means: 9.064 A / 4.673 A; Pin/Pout means: 206.122 W / 188.522 W; telemetry Pout/Pin ratio 91.462%.
- At saturation the effective/command phase was 160 ns, primary/secondary inner was 40/0 ns, and raw phase was 180 ns. Fault maximum 0; maximum temperatures 31.81 C primary and 27.75 C secondary.
- Result: this inner partition transfers less output than the SPS scan, but it is only the first EPS parameter point. EPS is not rejected and no PID parameters are promoted.
- Next tuning action: reverse the inner partition to 0/40 ns. This retains the same effective closed-loop cap but moves raw bridge displacement toward 140 ns, where the SPS scan showed its maximum.

## v37 EPS tuning point 2: inner 0/40 ns

- Run time: 2026-08-15 22:18:43 +08:00; same 44 V / nominal 10 ohm / 200 W-layer condition and Kp=20, Ki=5, Kd=0, p400 settings. EPS manual inner partition reversed to 0/40 ns. Automatic `off` completed.
- Stable-window rule: last 100 valid RUN samples, sequence 87114648 to 87856553.
- Vin mean/range: 22.793 V / 22.780 to 22.807 V. Vout mean/range: 41.011 V / 40.975 to 41.040 V; target error 2.989 V.
- Iin/Iout means: 9.298 A / 4.731 A; Pin/Pout means: 211.923 W / 194.007 W; telemetry Pout/Pin ratio 91.546%.
- Saturated effective/command phase was 160 ns, inner partition 0/40 ns, raw phase 140 ns. Fault maximum 0; maximum temperatures 32.89 C primary and 27.55 C secondary.
- Result: reversing the EPS inner-shift direction improved Vout by about 0.668 V relative to 40/0 ns and exceeded the SPS p400 result, but still did not reach 44 V. EPS remains under active tuning.
- Next tuning action: continue the beneficial direction at 0/80 ns with PI gains fixed, then stop or narrow the shift sweep when Vout ceases increasing.

## v37 EPS tuning point 3: inner 0/80 ns

- Run time: 2026-08-15 22:20:39 +08:00; same physical condition, PI gains, and p400 headroom. EPS inner partition 0/80 ns. Automatic `off` completed.
- Stable-window rule: last 100 valid RUN samples, sequence 104508224 to 105250130; a host/log line interleaving at shutdown was excluded and did not affect the stable window.
- Vin mean/range: 22.804 V / 22.792 to 22.821 V. Vout mean/range: 41.043 V / 41.016 to 41.067 V; target error 2.957 V.
- Iin/Iout means: 9.278 A / 4.737 A; Pin/Pout means: 211.582 W / 194.409 W; telemetry Pout/Pin ratio 91.884%.
- Saturated effective/command phase 160 ns, inner 0/80 ns, raw phase 120 ns. Fault maximum 0; maximum temperatures 33.60 C primary and 27.46 C secondary.
- Result: only 0.032 V improvement over 0/40 ns, so the EPS partition is near a plateau. The condition remains actuator-saturated and PID gains are still not the limiting variable.
- Next tuning action: one boundary point at 0/120 ns. If output does not materially improve, retain the 40--80 ns neighborhood as the EPS optimum region and move to TPS tuning.

## v37 EPS tuning point 4: inner 0/120 ns boundary

- Run time: 2026-08-15 22:22:35 +08:00; same nominal condition, PI gains, and p400 cap. EPS inner partition 0/120 ns. Automatic `off` completed.
- Stable-window rule: last 100 valid RUN samples, sequence 121901796 to 122643703.
- Vin mean/range: 22.525 V / 22.508 to 22.540 V. Vout mean/range: 40.633 V / 40.608 to 40.668 V; target error 3.367 V.
- Iin/Iout means: 9.241 A / 4.739 A; Pin/Pout means: 208.152 W / 192.564 W; Vout/Vin ratio 1.803951.
- Saturated effective/command phase 160 ns, inner 0/120 ns, raw phase 100 ns. Fault maximum 0; maximum temperatures 34.04 C primary and 28.04 C secondary.
- This run's Vin drooped about 0.28 V below the 0/80 trial, so the absolute Vout difference is not a clean inner-shift-only effect. Normalized transfer still shows no material advantage that justifies widening the EPS shift further.
- EPS sweep status: complete for this first 44 V condition. Provisional absolute-output best is 0/80 ns, Kp=20, Ki=5, Kd=0, p400, but it is not parameter-promotion eligible because it remains saturated about 2.96 V below Vref.
- Next tuning action: begin TPS at 20/80 ns, preserving the same closed-loop PI and phase cap.

## v37 TPS tuning point 1: inner 20/80 ns

- Run time: 2026-08-15 22:24:47 +08:00; same nominal 44 V condition, Kp=20, Ki=5, Kd=0, p400. TPS manual inner partition 20/80 ns. Automatic `off` completed.
- Stable-window rule: last 100 valid RUN samples, sequence 141708440 to 142450346.
- Vin mean/range: 22.633 V / 22.618 to 22.645 V. Vout mean/range: 40.690 V / 40.668 to 40.718 V; target error 3.310 V; Vout/Vin ratio 1.797814.
- Iin/Iout means: 9.246 A / 4.729 A; Pin/Pout means: 209.277 W / 192.427 W; telemetry Pout/Pin ratio 91.947%.
- Command phase 160 ns, applied effective phase 162 ns due count/partition quantization, raw phase 132 ns, inner 20/80 ns. Fault maximum 0; maximum temperatures 34.00 C primary and 28.69 C secondary.
- Result: first TPS point remains saturated and is not yet a mode verdict. Its normalized transfer is slightly below the tuned EPS neighborhood.
- Next tuning action: retain primary inner 20 ns and raise secondary inner to 120 ns, then compare normalized transfer before narrowing TPS.

## v35 test result: TPS, Vref 43.5 V

- Run time: 2026-08-15 21:10:36 +08:00; automatic `off` at the end.
- Commands: `off`, `unlock 200`, `p 200`, `v 43.5`, `mod tps`, `save`, `on`.
- Saturated steady samples: 200.
- Vin: mean 23.008 V, range 22.984 to 23.033 V.
- Vout: mean 32.879 V, range 32.847 to 32.912 V; below both the 43.5 V target and the required 36 V lower bound.
- Iin/Iout means: 3.579 A / 2.124 A.
- Pin/Pout means: 82.357 W / 69.841 W.
- Command/effective phase: 80/82 ns at the configured cap and quantization.
- Raw phase: 352 ns; primary/secondary inner phase: 720/180 ns.
- Requested/effective mode IDs: 2/2 (TPS/TPS).
- Maximum reported temperatures: primary 31.64 C, secondary 27.52 C; fault maximum 0.
- Interim conclusion: the default TPS trajectory failed at this point. It transferred slightly more power than the failed EPS parameters but remained far below SPS. Preserve it as a negative parameter sample; it does not prove TPS itself is unsuitable. Retest after inner/outer tuning.
- External DMM reading and run-specific abnormal-sound report: pending.

## Same-condition mode comparison at the nominal 18 ohm setting

| Mode | Vout mean | Pout mean | Command/effective phase | Inner phase | Result |
|---|---:|---:|---:|---:|---|
| SPS | 43.499 V | 123.755 W | 46.365/46.900 ns mean | 0/0 ns | Regulated; current best |
| EPS | 31.771 V | 65.108 W | 80/80 ns saturated | 720/0 ns | Default parameters failed; retune pending |
| TPS | 32.879 V | 69.841 W | 80/82 ns saturated | 720/180 ns | Default parameters failed; retune pending |

The comparison is based on firmware telemetry. Final voltage-accuracy and efficiency ranking still requires synchronized external DMM/current measurements.

## Next execution order

1. Build and manually flash v36 with slow inner-phase slew, live `inner` tuning, EPS/TPS effective `ph` tuning, and a 400 W command ceiling.
2. Retune EPS and TPS at the same 43.5 V point, starting from small inner shifts and sweeping safe inner/outer parameters before any conclusion.
3. Scan the 36-48 V integer targets for SPS/EPS/TPS at the current input/load point and retain every stable parameter set.
4. Hold selected Vref points constant while Vin is stepped 24, 22, 20, and 18 V; retune without changing the target.
5. Expand load/power layers through measured 200 W, 300 W, and 400 W only after source-current capacity and switching waveforms are ready.

## v35 test result: EPS, Vref 43.5 V

- Run time: 2026-08-15 21:09:18 +08:00; automatic `off` at the end.
- Commands: `off`, `unlock 200`, `p 200`, `v 43.5`, `mod eps`, `save`, `on`.
- Saturated steady samples: 200.
- Vin: mean 23.016 V, range 22.995 to 23.036 V.
- Vout: mean 31.771 V, range 31.733 to 31.804 V; below both the 43.5 V target and the required 36 V lower bound.
- Iin/Iout means: 3.334 A / 2.049 A.
- Pin/Pout means: 76.741 W / 65.108 W.
- Command/effective phase: 80/80 ns at the configured cap.
- Raw phase: 440 ns; primary/secondary inner phase: 720/0 ns.
- Requested/effective mode IDs: 1/1 (EPS/EPS).
- Maximum reported temperatures: primary 30.42 C, secondary 27.37 C; fault maximum 0.
- Interim conclusion: the default EPS trajectory failed at this point. The large 720 ns primary-inner phase reduced transferable power even with the effective outer phase saturated. Preserve it as a negative parameter sample; it does not prove EPS itself is unsuitable. Retest after inner/outer tuning.
- External DMM reading and run-specific abnormal-sound report: pending.

## v36 tuning changes in progress

- Reduce inner-phase slew from 20 ns/ms to 0.04 ns/ms so the voltage controller can follow instead of allowing the inner shift to run away.
- Add `inner PRI_NS SEC_NS`, `inner auto`, and `inner show` UART commands. Manual targets are logged and slewed, not stepped directly into the PWM.
- Allow `ph` to command effective outer phase in SPS, EPS, and TPS; raw phase compensation remains active.
- Append `inner_manual` to the VOFA data without changing the existing field order.
- Add `unlock 400` and allow the power-limit command up to 400 W. This changes the test ceiling only; it is not physical proof of 400 W capability.
- Extend the secondary-bridge software current ceiling from 14 A to 30 A. Its existing TMCS1133 conversion is not clamped at 14 A and has ample ADC headroom; the change is to the software threshold, not the sensor gain.
- Extend the output-bus software current ceiling from 7 A to 14 A, covering 400 W at 36 V (11.11 A) with margin while remaining below the nominal 20 A ACS725 range.
- Hardware ISEC comparator/gate-driver protection is unchanged and must not be bypassed. The present manual-stop image keeps ADC-current guards as telemetry only, so external current and switching-waveform checks remain mandatory before high-power operation.

## v36 build result

- CPU0 `make -j8 all`: PASS; text 112770, data 428, bss 169709 bytes.
- CPU1 `make -j8 all`: PASS; text 11766, data 92, bss 6496 bytes.
- Build proof is not flashing proof or physical power validation. The user performs flashing manually.

## v36 live verification

- 2026-08-15 21:21:58 +08:00: live firmware identified by `unlock400`, the 27-field VOFA header, and working `inner show` output.
- At verification, Vin telemetry was 23.32 V even though power-stage testing had been paused. Control, PWM, and all four path MOS were OFF; fault word was zero.
- Manual-inner normal limit reported 240 ns before high-power unlock.

## v36 tuned test result: EPS inner 40/0 ns, Vref 43.5 V

- Run time: 2026-08-15 after v36 live verification; automatic `off` at the end.
- Commands: `off`, `unlock 400`, `p 200`, `v 43.5`, `mod eps`, `inner 40 0`, `save`, `on`.
- Stable samples: 196; sequence window 6338347 to 7799677.
- Vin: mean 22.739 V, range 22.707 to 22.763 V.
- Vout: mean 43.502 V, range 43.457 to 43.537 V.
- Iin/Iout means: 5.850 A / 2.869 A.
- Pin/Pout means: 133.033 W / 124.803 W. The telemetry ratio Pout/Pin was 93.814%; this is not a physical efficiency result because external current calibration is pending.
- Raw/effective/command phase means: 92.857/72.857/72.740 ns; raw range 88 to 96 ns, effective range 68 to 76 ns.
- Primary/secondary inner phase: 40/0 ns; `inner_manual=1`; requested/effective mode IDs: 1/1 (EPS/EPS).
- Maximum reported temperatures: primary 29.00 C, secondary 26.92 C; fault maximum 0.
- PWM and all four path MOS were confirmed OFF after the run.
- Result: stable firmware-voltage regulation benchmark and a suitable tuned EPS parameter point. This proves the earlier v35 EPS result was a failed default trajectory, not rejection of EPS modulation.
- Final mode selection remains open until the EPS/TPS parameter scans and synchronized external measurements are complete.
- External DMM reading and run-specific abnormal-sound report: pending.

## Training-friendly mode-selection records

- Added `TRAINING_DATA_SCHEMA.md`, `mode_tuning_trials.jsonl`, and `mode_selection_labels.jsonl`.
- Every SPS/EPS/TPS attempt is now a long-format trial under a shared `operating_condition_id`; parameter failures and successful candidates are both retained.
- Deployment-time `selection_features` are kept separate from post-run `evaluation_metrics` to prevent target leakage.
- A final `selected_mode` label is forbidden until SPS, EPS, and TPS have all been tuned at the same firmware, source, target, load, controller, duration, and stable-window rule.
- The historical v35 SPS point is retained but excluded from the final v36 fair comparison until SPS is repeated on v36.

## v36 tuning trial: EPS inner 80/0 ns, Vref 43.5 V, p200

- Run time: 2026-08-15 21:26:08 +08:00; automatic `off` at the end.
- Commands: `off`, `unlock 400`, `p 200`, `v 43.5`, `mod eps`, `inner 80 0`, `save`, `on`.
- Stable saturated samples: 194; sequence window 39503343 to 40949687.
- Vin: mean 22.876 V, range 22.850 to 22.901 V.
- Vout: mean 42.777 V, range 42.730 to 42.812 V; absolute target error 0.723 V.
- Iin/Iout means: 5.629 A / 2.811 A; Pin/Pout means: 128.778 W / 120.267 W.
- Raw/effective/command phase: 120/80/80 ns; primary/secondary inner phase 80/0 ns.
- Maximum reported temperatures: primary 29.42 C, secondary 27.21 C; fault maximum 0.
- PWM and all four path MOS were confirmed OFF after the run.
- Interpretation: this parameter set reached the p200 effective-phase cap. It is a negative tuning sample, not an EPS-mode failure. Retest the same inner setting with a higher outer-phase cap.
- External DMM reading and run-specific abnormal-sound report: pending.

## v36 tuning trial: EPS inner 80/0 ns, Vref 43.5 V, p250

- Run time: 2026-08-15 21:27:58 +08:00; automatic `off` at the end.
- Same EPS inner setting and Vref as the preceding trial; only the phase-cap label increased from p200 to p250.
- Stable saturated samples: 184; sequence window 56017722 to 57389125.
- Vin mean/range: 22.902 V / 22.876 to 22.922 V.
- Vout mean/range: 43.094 V / 43.051 to 43.132 V; absolute target error 0.406 V.
- Iin/Iout means: 5.704 A / 2.829 A; Pin/Pout means: 130.639 W / 121.918 W.
- Raw/effective/command phase: 140/100/100 ns; primary/secondary inner phase 80/0 ns.
- Maximum reported temperatures: primary 29.96 C, secondary 27.57 C; fault maximum 0.
- PWM and all four path MOS were confirmed OFF after the run.
- Interpretation: raising the effective-phase cap improved target error from 0.723 V to 0.406 V, but the controller again saturated. Continue the same parameter point at p300 before judging it.
- External DMM reading and run-specific abnormal-sound report: pending.

## v36 tuning trial: EPS inner 80/0 ns, Vref 43.5 V, p300

- Run time: 2026-08-15 21:29:45 +08:00; automatic `off` at the end.
- Stable saturated samples: 173; sequence window 72192323 to 73481291.
- Vin mean/range: 22.760 V / 22.738 to 22.782 V.
- Vout mean/range: 43.037 V / 42.999 to 43.072 V; absolute target error 0.463 V.
- Iin/Iout means: 5.758 A / 2.838 A; Pin/Pout means: 131.056 W / 122.119 W.
- Raw/effective/command phase means: 160/120/119.988 ns; primary/secondary inner phase 80/0 ns.
- Maximum reported temperatures: primary 30.56 C, secondary 27.30 C; fault maximum 0.
- PWM and all four path MOS were confirmed OFF after the run.
- Interpretation: p300 did not improve on p250 and remained saturated. Complete the permitted outer-cap sweep at p400 before closing this inner-phase trial.
- External DMM reading and run-specific abnormal-sound report: pending.

## v36 tuning trial: EPS inner 80/0 ns, Vref 43.5 V, p400

- Run time: 2026-08-15 21:31:33 +08:00; automatic `off` at the end.
- Stable saturated samples: 153; sequence window 88466444 to 89605533.
- Vin mean/range: 22.874 V / 22.856 to 22.897 V.
- Vout mean/range: 42.905 V / 42.875 to 42.931 V; absolute target error 0.595 V.
- Iin/Iout means: 5.722 A / 2.813 A; Pin/Pout means: 130.877 W / 120.674 W.
- Raw/effective/command phase means: 200/160/159.993 ns; primary/secondary inner phase 80/0 ns.
- Maximum reported temperatures: primary 30.87 C, secondary 28.27 C; fault maximum 0.
- PWM and all four path MOS were confirmed OFF after the run.
- Inner80 outer-cap scan is complete across p200, p250, p300, and p400. Best voltage was p250 at 43.094 V; this inner setting is not a precise-regulation candidate at this condition. This conclusion applies to the parameter setting, not EPS modulation.
- EPS inner40/0 remains the successful mode candidate.
- External DMM reading and run-specific abnormal-sound report: pending.

## v36 fair-comparison candidate: SPS, Vref 43.5 V, p400

- Run time: 2026-08-15 21:33:30 +08:00; automatic `off` at the end.
- Stable-window rule: last 160 valid RUN samples; sequence window 105908579 to 107100126. The same rule is required for the EPS and TPS candidates in this comparison group.
- Vin mean/range: 22.623 V / 22.602 to 22.648 V.
- Vout mean/range: 43.500 V / 43.454 to 43.540 V.
- Iin/Iout means: 5.882 A / 2.877 A; Pin/Pout means: 133.072 W / 125.146 W.
- Raw/effective/command phase means: 62.950/62.950/62.581 ns; raw range 60 to 68 ns; inner phase 0/0 ns.
- Maximum reported temperatures: primary 31.02 C, secondary 28.03 C; fault maximum 0.
- PWM and all four path MOS were confirmed OFF after the run.
- Result: v36 SPS same-cap candidate for the p400 comparison group. Final mode label remains unset pending tuned EPS and TPS candidates and external measurement.
- External DMM reading and run-specific abnormal-sound report: pending.

## Hard persistence and firmware-integration rule

- Added `best_parameters_by_condition.jsonl` as the code-ready best-parameter source of truth and `FIRMWARE_INTEGRATION_QUEUE.md` as the completion gate.
- Every physical operating condition must retain all tuning trials, one current best parameter record per SPS/EPS/TPS mode, and—only after all modes are tuned—one final selected-mode label.
- Each best record includes `code_integration_status`; a condition is not complete while its selected parameters remain `PENDING_FIRMWARE_TABLE`.
- Corrected the training grouping: the `p` command/phase cap and inner shifts are optimized controller parameters, not operating-condition features. All p200/p250/p300/p400 scans at the same Vin/Vref/load belong to one canonical physical condition.
- Current canonical condition: `OC_VIN24_VREF43P5_NOM18_V36`. SPS revision 1 and EPS revision 1 are queued for later firmware table generation; TPS is still pending.

## v36 fair-comparison candidate: EPS inner 40/0 ns, Vref 43.5 V, p400

- Run time: 2026-08-15 21:37:15 +08:00; automatic `off` at the end.
- Stable-window rule: last 160 valid RUN samples; sequence window 139674298 to 140865845.
- Vin mean/range: 22.743 V / 22.720 to 22.769 V.
- Vout mean/range: 43.498 V / 43.461 to 43.529 V; absolute target error 0.002 V.
- Iin/Iout means: 5.845 A / 2.868 A; Pin/Pout means: 132.922 W / 124.767 W.
- Raw/effective/command phase means: 92.600/72.600/72.306 ns; raw range 88 to 96 ns; effective range 68 to 76 ns; inner phase 40/0 ns.
- Maximum reported temperatures: primary 30.25 C, secondary 27.43 C; fault maximum 0.
- PWM and all four path MOS were confirmed OFF after the run.
- Result: EPS inner40 remains a precise candidate with the common p400 ceiling. This supersedes EPS best-parameter revision 1 with revision 2; firmware integration remains pending.
- Final mode label remains unset pending tuned TPS and external measurement.
- External DMM reading and run-specific abnormal-sound report: pending.

## Closed-loop control and PI-tuning rule

- The live controller is closed loop: `error_v = Vref - Vout`; proportional and integral terms produce the outer phase command. The integral is clamped to the available phase limit, and current/power limiting retreats the command and clamps the integral.
- UART supports live `kp 0..40` ns/V and `ki 0..10` us/(V*s). Current baseline is Kp=20, Ki=5.
- DAB phase is the actuator, not the control objective. The `p`/phase-cap setting is retained only as sufficient actuator headroom and a safety constraint. Manual `ph` is diagnostic only.
- Final SPS/EPS/TPS candidates must use `PI_CLOSED_LOOP`. Manual-phase trials are never eligible for a final mode-selection label.
- At each physical condition and modulation mode: first remove phase saturation with a safe cap and inner-shift region; then tune Kp/Ki using startup/step response, overshoot, settling time, steady error/spread, current, temperature, and fault margins.
- Initial SPS PI sweep at the current physical condition: hold Ki=5 and compare Kp 10, 20, 30, 40; then hold the best Kp and compare Ki 2.5, 5, 7.5, 10. Stop or narrow the sweep for oscillation, abnormal sound, source limiting, fault, or excessive overshoot.
- Current firmware is PI, not PID. A filtered D term will be considered only if the tuned PI cannot meet dynamic requirements and voltage-feedback noise permits it.

## Multi-loop control decision

- Multi-loop control is selected as the target architecture for rejecting Vin/load disturbances while holding a fixed output target, but it does not remove physical source/modulation/thermal saturation.
- Proposed path: outer voltage PI -> average current/power reference; faster inner average-current or power PI -> effective phase; Vin/Vout feedforward; slow SPS/EPS/TPS supervisor with hysteresis, dwell time, slew limiting, anti-windup, and bumpless transfer.
- Current v36 remains the single voltage-PI reference. Its control update is approximately 49.96 kHz and the common measurement filter has approximately 0.64 ms time constant; final loop bandwidths require measured plant/step-response evidence.
- Hard gate: do not close a fast loop on IPRI/ISEC until zero/gain/polarity and PWM-synchronous sample timing are externally verified. Begin with a calibrated average IIN/IOUT or Pin/Pout loop if validation supports it.
- Training records now require `control_architecture`; legacy `PI_CLOSED_LOOP` records map to `VOLTAGE_PI_SINGLE_LOOP`. Single-loop and cascade candidates must never be mixed without this label.
- Full design and validation stages are saved in `MULTILOOP_CONTROL_PLAN.md`. No cascade firmware or physical validation is claimed yet.

## Gain-schedule and model-output decision

- The condition sweep will produce a deterministic controller gain/parameter table. The scheduled output is a complete SPS/EPS/TPS parameter bundle, not PID alone.
- The bundle includes outer and future inner loop gains, with Kd explicitly zero while firmware remains PI; phase cap and feedforward phase; EPS/TPS inner shifts; slew, anti-windup, current/power limits; transition hysteresis/dwell; and calibration/firmware revisions.
- Instantaneous phase remains a closed-loop output. Commanded/raw/effective phase time series and statistics are retained for learning and scoring, while only initial/feedforward phase and validated phase constraints are scheduled.
- Preferred deployment hierarchy: validated table -> bounded interpolation -> SPS/EPS/TPS ranking model -> per-mode bounded parameter correction. Low-confidence or out-of-range inputs fall back to the nearest conservative validated entry.
- Required Vin/Vref/load/power anchors and all three modulation modes are retained. Additional points will be concentrated around mode/saturation/thermal boundaries and interpolation failures.
- Detailed schema and execution rules are saved in `GAIN_SCHEDULE_AND_MODEL_PLAN.md`. No trained selector is claimed yet.

## Integer-target and high-power correction

- Freeze 43.5 V as an existing regression anchor; do not spend further main-sweep time there.
- Next target is 44 V. All subsequent voltage-capability points prioritize integer Vrefs 36--48 V.
- First raised-power setting at 44 V is 9.68 ohm ideal or 10 ohm nominal, approximately 193.6 W. Use a >=300 W cooled load bank and a 24 V source capable of at least 10 A, preferably 11 A.
- The present 24 V / 7 A setting has only 168 W theoretical input power and cannot demonstrate 200 W output. Do not describe a source-limited run as a board power limit.
- At 44 V, ideal 300 W and 400 W loads are 6.45 ohm and 4.84 ohm. Source capability should be at least approximately 15 A and 20 A respectively, with preferred margins of 16 A and 22 A; load banks should be rated at least 450 W and 600 W.
- A full 36--48 V resistance table and optimized execution order are saved in `INTEGER_VREF_POWER_TEST_MATRIX.md`.
- The 400 W board design target remains a test objective, not a physically validated result. Each power increase remains staged, attended, externally measured, and separately tuned for SPS/EPS/TPS.

## v37 gain-schedule/data-contract build

- Implemented firmware `2026-08-15-dualcore-schedule-jsonl-v37` before starting the 44 V matrix.
- Added Kd with derivative-on-filtered-Vout and a configurable-recorded filter constant; safe default Kd=0 preserves the existing PI behavior.
- Added outer-loop saturation flag/update count and retained commanded/raw/effective phase plus EPS/TPS inner shifts.
- Added explicit Vin-set, nominal-load, requested-power-layer, control-architecture, schedule/table-entry, and feedforward metadata to UART VOFA and Ethernet JSONL telemetry.
- Added `vinset`, `rload`, `layer`, `kd`, `arch`, `sched`, and `bundle` commands. Condition metadata changes require READY with PWM/power OFF.
- Added the code-ready `dab_tuned_parameter_table.h` contract and exact-match application gate. Entry count is zero until a parameter record is deliberately promoted; automatic schedule therefore remains disabled.
- Cascade architecture commands are present but reject activation until current sensing and loop validation pass. Default architecture is `VOLTAGE_PI_SINGLE_LOOP`.
- Changed the non-energizing defaults for the next condition to Vref=44 V, VinSet=24 V, Rload=10 ohm, requested power layer=200 W. Actual power command still defaults conservatively to 20 W and reset never auto-starts.
- CPU0 and CPU1 builds passed. CPU0 size: text 119484, data 440, bss 169893 bytes; CPU1 size: text 11766, data 92, bss 6496 bytes.
- Binary strings verify the v37 firmware, single-loop architecture, empty validated-table version, and VOFA v37 schema. Hashes and artifact paths are saved in `firmware_v37_manifest.json`.
- User manual flash and post-flash boot/READY verification remain pending. No v37 physical power result is claimed.

## v37 first 44 V / nominal 10 ohm trial: SPS p300, saturated below target

- Run time: 2026-08-15 22:10:29 +08:00; firmware identity and v37 schema were confirmed before energizing, and automatic `off` completed at the end.
- Condition metadata: Vin set 24 V, Vref 44 V, nominal load 10 ohm, requested layer 200 W; single voltage PI, SPS, Kp=20 ns/V, Ki=5 us/(V*s), Kd=0, phase cap 120 ns (`p 300`).
- Stable-window rule: last 100 valid RUN samples, sequence 12414461 to 13156367.
- Vin mean/range: 22.511 V / 22.486 to 22.534 V. Vout mean/range: 40.461 V / 40.429 to 40.492 V; target error 3.539 V.
- Iin/Iout means: 9.180 A / 4.726 A; Pin/Pout means: 206.651 W / 191.225 W; telemetry Pout/Pin ratio 92.532%.
- Raw/effective/command phase were all fixed at 120 ns for the full stable window. Outer saturation was asserted for every stable sample; saturation counter ended at 546901. Fault maximum was 0.
- Maximum stable-window temperatures: primary 28.79 C, secondary 26.08 C. PWM and RUN were confirmed off after capture.
- The telemetry V/I ratio implies approximately 8.561 ohm, which differs from the 10 ohm nominal setting. Keep both values separate until an external load measurement or current-calibration check resolves the difference.
- Result: output is inside the required 36--48 V capability band but does not meet the fixed 44 V target. This is not yet a PID-gain verdict because the actuator was saturated. Do not promote parameters or label SPS failed.
- Next tuning action: hold Kp/Ki/Kd and the physical condition fixed, raise only the SPS phase cap to 160 ns (`p 400`), and determine whether the transfer curve gains or loses voltage before tuning gains.
- External DMM reading and run-specific abnormal-sound report: pending.

## v39 dual-core MPPT, current-control and multi-chemistry charging preparation

- Implemented firmware `2026-08-15-dualcore-energy-control-v39` without removing the v38 voltage PI, SPS/EPS/TPS/manual-inner/manual-phase paths, UART commands, Ethernet remote path, original 42-field `DAB:` numeric positions, or reset-off behavior.
- Compatibility gate: boot remains `VOLTAGE_PI_SINGLE_LOOP`, CPU1 energy mode and AI remain OFF, and the new `ALG:` extension defaults OFF. No new controller can reach PWM until explicitly selected by UART; `save` remains DAB-only until a new configuration enables recording or `alglog on` is sent.
- CPU0 now has a frame-synchronous output-current PI with independent `iref`, `ikp`, `iki`, and `islew` settings. `arch current` uses the UART reference; `arch energy` uses the bounded CPU1 reference. CPU1 reference age is monitored and becomes invalid after 500 ms.
- CPU1 now runs a third static FreeRTOS task: a 100 ms deterministic energy supervisor. CPU0 sends quantized Vin/Iin/Vout/Iout/temperature snapshots over typed IPC; a 32-entry static queue and matching ACK/retry protocol deliver UART configuration without blocking the control task.
- Added local P&O, incremental-conductance and fixed-power MPPT. A fixed bench supply can test plumbing only; it cannot validate MPPT tracking.
- Added generic, fully UART-tunable charging profiles: 10S Li-ion at 42.0 V, 12S LiFePO4 at 43.8 V, and 18-cell 36 V lead-acid at 43.2 V absorption/40.95 V float, plus custom. Lithium uses precharge/CC/CV/taper/done; lead-acid rejects a pack below 1.75 V/cell, uses bulk/absorption/float and applies temperature compensation when an external battery temperature is marked valid. Pack manufacturer/BMS values remain authoritative.
- Added an AI extension point only, not an AI model. A fresh current hint with confidence at least 0.70 can only reduce the deterministic local current ceiling and expires by TTL; otherwise local control remains active. `ai mod sps|eps|tps CONFIDENCE [TTL_MS]` records a future selector recommendation without switching live modulation; its confidence, TTL and freshness are transported separately so it cannot refresh or alter the current hint.
- Added default runtime `BENCH_ABSOLUTE_ONLY`: soft power/current retreat, cold-output phase clamps and no-rise trips do not distort sweeps. RAW/OC/OK, ADC stale, absolute 55 V, 26 A input, 18 A output, bridge-current and temperature shutdown paths remain. `protect normal` restores layered soft guards.
- When the opt-in ALG extension is active, `save` emits a second `ALG:` row with the same measurement sequence as `DAB:`. It records current-loop parameters/state, guard, CPU1 reference/age, MPPT, battery stage/profile, AI current/modulation hints and IPC/config health for training joins.
- CPU0 build passed without warnings: text 136922, data 460, bss 170681 bytes. CPU1 build passed without warnings: text 16858, data 132, bss 9096 bytes.
- Added deterministic energy-algorithm test cases covering fixed-point configuration transport, lithium and lead-acid stages, fixed-power conversion and separation of AI current/modulation TTLs. Both test and implementation translation units cross-compile with `-Wall -Wextra -Werror`; execution is not claimed because no compatible host/ARM simulator was available.
- Binary strings verified the v39 identity, output-current architecture, BENCH guard, battery/MPPT/AI commands, joined ALG schema and CPU1 Energy task. S-record hashes and paths are stored in `firmware_v39_manifest.json`.
- `Verify-ReferenceParity.ps1 -RequireBuildArtifacts -AllowEthernetPhase1 -AllowEnergyControlV39` passed. It proves static/configuration/build parity only.
- No v39 flash or physical test was performed. Manual flashing and tomorrow's low-current electronic-load sequence are recorded in `V39_ELECTRONIC_LOAD_CHECKLIST.md`.

## Git repository consolidation

- Root remote is `https://github.com/ryule5158/DAB_DCDC.git`; the complete current workspace, firmware sources, recorded datasets and v39 release artifacts are tracked as ordinary files rather than an unresolved nested-repository pointer.
- Implementation/data commit: `f00d52639bfaef5b65f12fb9db847a5de00f0885`.
- Remote `main` had an unrelated H1.7 release history. It was merged without force or deletion in merge commit `8dfde618`; the original `release/` deliverables remain in place alongside the current development tree.
- The former embedded `RA8P1_DAB_Feature_Showcase` Git history is recoverable from `RA8P1_DAB_Feature_Showcase_Backups/RA8P1_DAB_Feature_Showcase_embedded_history_2026-08-16.bundle`; bundle verification and the backup Git-object integrity check passed before the root import.
