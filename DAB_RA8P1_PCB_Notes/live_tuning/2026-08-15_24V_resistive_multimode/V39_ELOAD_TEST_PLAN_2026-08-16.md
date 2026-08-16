# v39 electronic-load tuning and dataset plan

Canonical Chinese现场 execution document: `C:\Users\LENOVO\Desktop\Electric Simulation\DAB四种工作人格与SPS_EPS_TPS现场展示测试总计划_2026-08-16.txt`.

Current instrument-control boundary: no RS232-to-USB cable is available on 2026-08-16. The RK8510 load is operated only from its front panel; no SCPI/MODBUS connection or load-control software is used until the cable arrives and identity/read-only checks pass.

Confirmed load model: RK8510 (without suffix A), single channel, rated 400 W / 150 V / 40 A with RS232/RS485. The power progression is therefore 50/100/150/200/250/300/360/400 W on one channel, still bounded by the input-source capability.

## Objectives and instrument-demo acceptance

1. Prove that v39 still boots in yesterday's `VOLTAGE_PI_SINGLE_LOOP` path with energy control and `ALG:` logging OFF.
2. Calibrate and tune the new CPU0 output-current PI before using CPU1 MPPT or charging references.
3. At every labelled operating condition, tune SPS, EPS and TPS separately before selecting a mode. A failed parameter trial is not a failed modulation mode.
4. Build a training-friendly dataset containing raw UART evidence, external readings, all controller/modulation parameters, unsuccessful trials and one promoted best bundle per mode and condition.
5. Exercise lithium-ion, LiFePO4 and lead-acid charging state machines with electronic-load CV steps as the accepted project demonstration.
6. Test fixed-power, P&O and incremental-conductance MPPT behaviour with the available bench source as the accepted project demonstration.

The project and现场展示 use instruments as the acceptance environment. The electronic load is connected at the DAB output; CV mode supplies the repeatable voltage steps used for current-loop and charging tests. The bench source supplies the input-voltage/current-limit steps used for MPPT demonstrations. No real PV panel or battery is required for this test plan.

## Information and equipment gate

Before the first powered run, record:

- electronic-load model, maximum voltage/current/power, CV accuracy, remote-sense use and protection settings;
- source model, voltage/current/power limits and whether it is currently still limited to 24 V / 14.5 A;
- external input/output voltage and current instruments and their calibration/source;
- wiring, connector, cooling and temperature-sensor status;
- scope/current-probe availability for bridge-current/ripple comparison.

Use the external source/load ratings as hard test ceilings. Source sizing uses `Iin >= Pout / (Vin * 0.85)` only as a conservative planning estimate:

| Pout | 24 V | 22 V | 20 V | 18 V |
| ---: | ---: | ---: | ---: | ---: |
| 100 W | 4.9 A | 5.4 A | 5.9 A | 6.6 A |
| 200 W | 9.8 A | 10.7 A | 11.8 A | 13.1 A |
| 300 W | 14.7 A | 16.1 A | 17.7 A | 19.7 A |
| 400 W | 19.7 A | 21.4 A | 23.6 A | 26.2 A |

Add operating margin above these values. At 24 V, 300 W should use at least 15--16 A and 400 W at least 20--22 A. A 24 V / 14.5 A source is insufficiently margined for a 300 W output result and cannot support 400 W. At 18 V, 400 W is at or beyond the present 26 A firmware input absolute limit at the 85% sizing assumption; document this as a boundary rather than forcing the point.

The electronic load should have at least 25% current/power margin at the requested point. Use remote sense where available. Firmware telemetry efficiency remains diagnostic; final efficiency is calculated from external `Vin`, `Iin`, `Vout` and `Iout` readings.

## Stage 0: boot, compatibility and zero-current gate

Keep the power stage OFF and run:

```text
show
protect show
energy show
mppt show
bat show
arch show
alglog show
zero
```

Required observations:

- firmware identity `2026-08-15-dualcore-energy-control-v39`;
- state `READY`, PWM/RUN/path outputs OFF;
- `VOLTAGE_PI_SINGLE_LOOP`, energy OFF, AI OFF and `alglog` OFF;
- CPU1 peer seen and not stale;
- zero-current polarity and offsets consistent with external meters.

Then use `alglog on` and `save` to capture joined `DAB:` and `ALG:` records. Do not energize if firmware/external current polarity disagrees or CPU1 IPC is stale.

## Stage 1: output-current-loop commissioning

Initial equipment settings:

- source: 24.0 V, initial current limit 2.0 A;
- electronic load: CV 36.0 V, load protection limit 1.0 A / 50 W or the closest stricter supported limits;
- load input enabled before converter start, with remote sense if available;
- SPS only for this calibration gate.

Initial UART bundle:

```text
off
protect bench
arch current
iref 0.50
ikp 2.0
iki 0.20
islew 0.20
p 100
cap 80
mod sps
alglog on
save
on
```

Run 0.5 A first, then 1.0 A and 2.0 A only after the preceding point passes. At each point compare firmware and external output current and record current zero, polarity, gain error, voltage error, source-limit state, phase saturation, temperature and sound.

Tuning order at each accepted current region:

1. Hold `Ki=0.20` and compare `Kp=1, 2, 4, 8 ns/A` using small reference steps.
2. Hold the best Kp and compare `Ki=0.05, 0.10, 0.20, 0.40 us/(A*s)`.
3. Hold the best Kp/Ki and compare `Islew=0.20, 0.50, 1.0, 2.0 A/s`.
4. If all candidates saturate, establish a feasible phase-cap region before changing gains. If oscillation occurs, return to the last stable bundle and narrow the sweep.

Initial acceptance targets:

- current error no greater than `max(0.03 A, 2% of Iref)` in the stable window;
- less than 10% current overshoot and no sustained oscillation;
- no continuous phase-cap saturation, source CC limiting, fault or abnormal sound;
- firmware/external current gain error within 5% before high-power work;
- no rapid temperature rise; pause for review at 60 C even though the firmware absolute shutdown is 70 C.

These are test-acceptance gates, not changes to the firmware protection implementation.

## Stage 2: first fair SPS/EPS/TPS comparison

Use the first comparison anchor:

- Vin 24.0 V;
- electronic-load CV 44.0 V;
- Pout target 100 W, hence Iout reference 2.273 A;
- current-loop architecture, identical instruments, limits, cooling and stable-window rules.

For every mode, first find a stable unsaturated actuator region, then tune Kp/Ki/slew. Starting inner-shift search:

- SPS: `inner 0 0` equivalent, no inner-shift sweep;
- EPS: `inner 0 0`, `20 0`, `40 0`, `60 0 ns`, then +/-10 ns around the best point; previous 40/0 ns is only a starting candidate;
- TPS: coarse grid `0/0`, `20/0`, `0/20`, `20/20`, `40/20`, `20/40`, `40/40 ns`, then a local fine sweep around the best candidate.

Each saved candidate uses 15 s settling plus a 10 s stable window (200 samples at the current 50 ms save rate). At 300 W and above, use at least 60 s thermal settling plus a 30 s stable window after the initial short stability gate.

Selection priority is:

1. no fault, abnormal sound or invalid data;
2. reference tracking without source/phase saturation;
3. calibrated external efficiency;
4. current/ripple/circulating-current stress where instrumented;
5. temperature rise and regulation spread;
6. transition behaviour and available margin.

No `selected_mode` label is written until all three modes have `mode_tuning_complete=true` at the same condition.

## Stage 3: operating-condition matrix

### 3A. 24 V voltage/power anchors

Run all three tuned modes at 36, 40, 44 and 48 V. Begin with 50, 100 and 200 W; add 300 and 400 W only when the source/load/instrument ratings pass the equipment gate.

| CV setpoint | 50 W Iout | 100 W Iout | 200 W Iout | 300 W Iout | 400 W Iout |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 36 V | 1.389 A | 2.778 A | 5.556 A | 8.333 A | 11.111 A |
| 40 V | 1.250 A | 2.500 A | 5.000 A | 7.500 A | 10.000 A |
| 44 V | 1.136 A | 2.273 A | 4.545 A | 6.818 A | 9.091 A |
| 48 V | 1.042 A | 2.083 A | 4.167 A | 6.250 A | 8.333 A |

High-power progression is 200 -> 300 -> 400 W at 44 V first. After that anchor is understood, repeat 300/400 W at 36, 40 and 48 V as the hardware ratings permit.

### 3B. Input-disturbance anchors

At fixed 44 V CV output, repeat 24 -> 22 -> 20 -> 18 V for 100 and 200 W, then 300 W where source current margin permits. Use the same tuned mode bundle initially, record disturbance response, and then retune per Vin if needed. A point that hits a physical/source/current/phase limit is saved as a saturation-boundary result, not deleted.

### 3C. Integer-voltage fill

After the anchor map is stable, fill every integer CV setpoint from 36 through 48 V at 100 and 200 W, with SPS/EPS/TPS all tuned and saved. Use interpolation only to seed gains; the measured point must be independently validated. Then fill intermediate 300/400 W points where the anchor results show useful mode boundaries or interpolation error.

This staged order avoids performing the full multi-hundred-run matrix before sensor, control-loop and equipment limits are known.

## Stage 4: battery-charge instrument demonstration

Use 24 V input, CV electronic-load mode, `Imax=1.0 A`, `Ipre=0.5 A`, `Iterm=0.30 A` initially, and `arch energy`. Label every record `BATTERY_DEMO_CV` and retain the exact instrument settings for现场复现.

- 10S Li-ion: begin at 36 V for CC, step toward 41 V, then 42.0 V for CV/taper/DONE testing.
- 12S LiFePO4: begin at 36 V for CC, step toward 42 V, then 43.8 V for CV/taper/DONE testing.
- 18-cell lead-acid: begin at 36 V for bulk, step to 43.2 V absorption, then verify transition to 40.95 V float. Use a shortened, explicitly recorded `bat absorb` value for a bench state-machine test rather than waiting the production default.
- Profile precharge thresholds below the project's 36 V application range are not a main power-test requirement; any optional low-voltage instrument run is separately labelled.
- Exercise temperature logic by UART at 25 C and selected boundary/out-of-range values while power is low, and record each demonstrated state transition.

For every profile, save requested current, active current, voltage target, charge stage, limited flags, temperatures, transition time and the exact UART overrides.

## Stage 5: MPPT instrument demonstration

With the present stiff source, test only fixed-power reference plumbing at 50, 100 and 200 W:

```text
off
mppt fixed 50
energy mppt
arch energy
save
on
```

Repeat fixed-power mode with rated equipment at higher power. Then run P&O and incremental-conductance modes with controlled source-voltage/current-limit steps, for example 24 -> 23 -> 22 -> 23 -> 24 V, while recording `mppt_V`, `mppt_P`, active output-current reference, source power, response time, boundedness and external conversion efficiency. Tune `mppt step`, `interval`, `vmin`, `vmax` and `pmax` for the most stable and repeatable现场演示, and retain separate best bundles for P&O and incremental conductance.

## Data capture and promotion rules

Create one append-only raw session per bench session. Every trial record must include:

- firmware/hardware/calibration revision and UTC/local timestamps;
- operating-condition ID, load mode (`CV`, `CC`, `CR` or `CP`) and all load setpoints/limits;
- source setpoint and current limit;
- architecture, SPS/EPS/TPS requested/effective mode, inner shifts, phase cap, Kp/Ki/slew and energy/battery/MPPT configuration;
- joined `DAB:` and `ALG:` rows using `seq` only;
- external Vin/Iin/Vout/Iout/Pin/Pout/efficiency and instrument source;
- stable-window definition, overshoot, settling, saturation, temperature, fault and abnormal-sound fields;
- `trial_status`, failure cause, `mode_tuning_complete`, `fair_comparison_eligible` and exclusion reason.

Update after every accepted condition:

- `mode_tuning_trials.jsonl`: all attempts, including failures;
- `best_parameters_by_condition.jsonl`: current best bundle for each mode;
- `mode_selection_labels.jsonl`: only after all three modes are tuned;
- `WORKLOG.md`: human-readable settings, observations and next action.

Validated parameters remain `PENDING_FIRMWARE_TABLE` until they are added to the lookup/interpolation table, rebuilt, flashed and regression-tested. Instantaneous phase is telemetry/output, not a fixed trained input parameter.

## Showcase features enabled by the present platform

### Priority A: available with the present firmware and instruments

1. **Four electronic-load personalities**
   - CR: use the legacy voltage PI to demonstrate regulated DC-bus output under resistance/load steps.
   - CV: use output-current/energy control to demonstrate Li-ion, LiFePO4 and lead-acid charging profiles.
   - CC: demonstrate current limiting, reference tracking and controlled voltage response.
   - CP: demonstrate constant-power load response, source-current growth and the 400 W operating envelope.
   - Every personality has a separate `load_mode`, setpoint and controller-architecture label in the dataset.

2. **SPS/EPS/TPS efficiency tournament**
   - At one fixed Vin/Vout/power point, automatically execute `off -> select mode/parameters -> on -> settle -> capture -> off` for all three modes.
   - Display external efficiency, current tracking, phase/inner shifts, temperature rise, saturation and fault margin as a ranked result.
   - Mode changes remain READY/PWM-OFF operations in v39; the demonstration does not require unsafe live switching.

3. **One-command battery-chemistry change**
   - Select `liion10`, `lfp12` or `lead36` by UART and show CC/CV/DONE or bulk/absorption/float states while the electronic-load CV setpoint moves.
   - Display the complete current/voltage/time/temperature parameter bundle rather than only a battery-type name.

4. **P&O versus incremental-conductance A/B display**
   - Apply the same source-voltage/current-limit sequence to both algorithms.
   - Plot MPPT voltage target, power target, measured input power, output-current reference and response time on the same axes.

5. **Dual-core live responsibility display**
   - Show CPU0 current-loop state and phase command beside CPU1 MPPT/charging state, IPC age, queue and stale flags.
   - Show Helium-accelerated ADC/calibration processing as a platform capability while preserving CPU0 real-time control priority.

### Priority B: small opt-in additions after the first dataset

1. **Automatic characterization command/script**: sweep reference, Kp/Ki/slew and EPS/TPS inner shifts, stop on the existing limits, calculate stable-window metrics and append every trial automatically.
2. **Automatic efficiency optimizer**: coarse-to-fine search of each mode's inner shifts and current gains, producing one best bundle plus confidence/margin values for the firmware table.
3. **Online plant identification and PI seed**: inject a small bounded current-reference step, estimate gain/time constant and generate a conservative starting Kp/Ki. It remains UART opt-in and never changes yesterday's boot path.
4. **Ethernet dashboard**: after PHY/link/IP/TCP validation, display live waveforms, charge stage, MPPT state, mode ranking and test-matrix progress from TCP JSONL instead of relying only on UART text.
5. **Soft-switching quality map**: after bridge-current sampling and oscilloscope timing are calibrated, map commutation-current/ZVS margin versus SPS/EPS/TPS parameters and add it to the mode score.
6. **Thermal capability estimator**: use temperatures and recent losses to display sustainable power, thermal headroom and recommended derating as information, initially without changing the control reference.

### Priority C: dataset-driven flagship functions

1. Train the SPS/EPS/TPS selector on fair-comparison records and deploy an INT8 ranking model on Ethos-U55. The deterministic table and current limits remain the fallback.
2. Add bounded bumpless mode transfer only after each mode has validated gains, inner shifts, dwell/hysteresis and transition tests. The current record-only `ai mod` interface becomes the candidate input to this supervisor.
3. Add anomaly classification using current/voltage/temperature/phase features and the saved negative trials, with a circular pre-trigger/post-trigger event recorder for现场故障回放.

Recommended现场主线: four load personalities -> battery-profile switch -> MPPT A/B -> SPS/EPS/TPS efficiency tournament -> dual-core/Helium/AI roadmap dashboard. This tells one coherent photovoltaic-storage energy-management story without changing the legacy boot behaviour.

## Run-stop and classification rules

Immediately send `off` for wrong current polarity, hardware fault, output excursion, source/load protection activation, persistent oscillation, abnormal sound, smoke/odor, cooling loss or rapid temperature rise. A 60 C test pause is used before the firmware's 70 C absolute trip.

Every stopped run is retained and classified as `FAULT`, `ABNORMAL_SOUND`, `SOURCE_LIMIT`, `VOLTAGE_MISS`, `SATURATION_BOUNDARY` or `INVALID_DATA`. A modulation mode is considered unsuitable only after its safe phase/inner-shift/gain region has been swept and documented at that condition.

## Immediate first session

The first live session ends after these gates, unless all evidence is clean:

1. boot/compatibility and CPU1 IPC check;
2. zero/current polarity calibration;
3. SPS current loop at 24 V input, CV 36 V output, 0.5 A;
4. SPS current loop at 1.0 A and 2.0 A;
5. Kp/Ki/slew selection for this calibration condition;
6. automatic `off`, raw-log preservation, external-reading annotations and promotion of the best SPS current-loop bundle.

Only then move to the 44 V / 100 W SPS/EPS/TPS comparison anchor.
