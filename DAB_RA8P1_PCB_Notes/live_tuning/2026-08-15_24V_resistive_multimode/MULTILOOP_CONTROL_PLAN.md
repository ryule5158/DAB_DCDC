# Multi-loop control plan

## Decision

Multi-loop control is the preferred architecture for holding a fixed 36--48 V target while the input changes from 24 V toward 18 V and the resistive load/power changes. It improves disturbance rejection and gives current stress an explicit control objective. It cannot create transfer capability beyond the source, transformer, leakage inductance, switching, thermal, or modulation limits; saturation must still be identified and recorded.

## Proposed structure

1. Outer voltage PI: `Vref - Vout -> Iref/Pref`. This loop owns zero steady-state output-voltage error.
2. Inner average-current or power PI: `Iref/Pref - I/P feedback -> effective phase command`. It reacts faster to input and load changes and enforces a bounded current/power reference.
3. Vin/Vout feedforward: initialize the required effective-phase region from the measured conversion ratio and the saved tuned-parameter map. Feedback removes residual error.
4. Slow modulation supervisor: select SPS/EPS/TPS and its tuned inner shifts using the condition table/model. Apply hysteresis, minimum dwell time, bumpless state transfer, and phase slew limits.
5. Independent protection: hardware overcurrent/fault paths and validated software voltage/current/temperature limits remain outside the regulation loops.

The present firmware already updates the control at approximately 49.96 kHz and applies a one-pole measurement filter with approximately 0.64 ms time constant. These update figures do not determine the final loop bandwidth. Inner and outer bandwidths will be chosen from measured small-step responses, with the inner loop clearly faster than the outer loop and adequate phase/gain margin.

## Sensor-validation gate

Do not close a fast current loop using `IPRI` or `ISEC` yet. Their zero, gain, polarity, ADC trigger phase, aliasing, and relationship to actual bridge/inductor current have not been verified with an external current probe or synchronized oscilloscope capture. Current telemetry in the present runs is not sufficient proof of fast-loop suitability.

Staged sensor choice:

- First candidate for an average inner loop: calibrated `IIN`, `IOUT`, `Pin`, or `Pout`, because the requirement is average resistive-load regulation and power limiting.
- `IPRI`/`ISEC`: retain for protection/telemetry until waveform-synchronous sampling and external calibration are proven; only then consider a faster bridge-current loop.
- Firmware `Pout/Pin` is not physical efficiency until current calibration is complete.

## Implementation and validation stages

1. Keep v36 single voltage PI as the reference architecture and finish its Kp/Ki baseline tests at the current safe condition.
2. Capture external current and voltage evidence; verify current-channel zero, gain, polarity, linearity, and ADC sampling instant at several phase/power points.
3. Add an optional, default-off cascade controller in the next firmware revision. Preserve the v36-compatible single-loop path for A/B comparison.
4. Add feedforward, reference/phase slew limits, anti-windup, bumpless single/cascade and SPS/EPS/TPS transitions, hysteresis, dwell time, and explicit saturation telemetry.
5. User flashes manually. Compare `VOLTAGE_PI_SINGLE_LOOP` and the selected cascade architecture under identical Vin, Vref, load, modulation, thermal start, and safety limits.
6. Test input steps `24 -> 22 -> 20 -> 18 V` while holding one Vref fixed, then cover integer Vrefs 36--48 V and the planned load/power layers. Tune SPS/EPS/TPS before any modulation conclusion.

## Required saved fields

Every new trial must save:

- `control_architecture`: `VOLTAGE_PI_SINGLE_LOOP`, `CASCADED_VOLTAGE_AVG_CURRENT`, or `CASCADED_VOLTAGE_POWER`;
- outer-loop gains, inner-loop gains, feedforward enabled/version, reference and phase slew limits;
- current/power feedback source and calibration version;
- inner/outer saturation flags and time in saturation;
- Vin/Vref/load/power layer, SPS/EPS/TPS, all modulation parameters, dynamic voltage metrics, currents, temperatures, faults, and abnormal-sound report;
- raw-log path and code-integration status for the best parameter set.

Legacy records with `control_mode=PI_CLOSED_LOOP` and no `control_architecture` map to `VOLTAGE_PI_SINGLE_LOOP`. Manual-phase records remain diagnostics and are ineligible for final mode labels.
