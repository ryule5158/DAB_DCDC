# Gain schedule and modulation-parameter model plan

## Control principle

The operating-condition sweep will form a validated gain/parameter schedule. A runtime query uses Vin, Vref, load or requested power, source limits, temperatures, control architecture, and transition state to obtain a complete bounded controller parameter bundle.

The scheduled bundle must not replace feedback with a fixed phase command. The instantaneous outer/effective phase remains the output of the closed loop. The table/model provides gains, feedforward initialization, limits, modulation partition parameters, and a safe operating envelope.

## Parameter bundle returned for each mode

Each SPS/EPS/TPS candidate stores and can return:

- control architecture and modulation mode;
- outer voltage-loop `Kp`, `Ki`, `Kd` and derivative-filter setting (`Kd=0` for the current PI firmware);
- cascade inner current/power-loop `Kp`, `Ki`, `Kd`, feedback source, and calibration version when implemented;
- `power_command_label_w`, effective-phase cap, initial/feedforward effective phase, phase slew/rate limit, integral/anti-windup limits;
- EPS/TPS primary and secondary inner shifts, their slew limits, and raw-to-effective phase compensation version;
- current/power reference limits, voltage/current/temperature safety envelope, hysteresis, dwell time, and bumpless-transfer state;
- firmware/table revision and validation status.

The saved trial also retains observed raw phase, effective phase, commanded phase, saturation time, voltage dynamics, current, power, temperature, fault, and abnormal-sound metrics. Observed results are training targets/evaluation metrics, not deployment-time input features unless they are genuinely available from the immediately preceding operating state.

## Recommended runtime hierarchy

1. Deterministic validated table is the safety baseline.
2. Interpolate only between neighboring validated condition anchors and clamp every output to the validated envelope.
3. A modulation-ranking model scores SPS/EPS/TPS for the current condition.
4. A per-mode parameter model predicts a bounded parameter bundle or a residual correction around the interpolated table value.
5. Low confidence, out-of-distribution input, invalid sensing, or abnormal behavior falls back to the nearest conservative validated table entry or stops according to the independent protection state.

This is preferable to one unconstrained network directly emitting all parameters because it preserves traceability, supports firmware fallback, and prevents the model from commanding raw PWM or untested parameter combinations.

## Training targets

For every canonical physical operating condition, retain all tuning trials and create:

- one best parameter bundle for SPS;
- one best parameter bundle for EPS;
- one best parameter bundle for TPS;
- one final modulation label only after all three modes are tuned;
- optional per-mode score/constraint margins so the model learns ranking rather than only a hard class;
- a confidence and interpolation-validity region.

Recommended model split:

- model A, classification/ranking: condition features -> SPS/EPS/TPS score and selected mode;
- model B-SPS/B-EPS/B-TPS, bounded regression: condition features -> that mode's controller and modulation parameter bundle;
- later optional multi-task model after the dataset is large and balanced enough.

## Sweep and interpolation rules

- Preserve the required anchors: Vin 24/22/20/18 V, integer Vref 36--48 V, planned load/power layers, and all three modulation modes.
- Tune each mode before judging it. First establish a feasible unsaturated modulation region, then tune inner loop if present, then outer loop, and finally transition behavior.
- Add refinement points around mode boundaries, saturation boundaries, high-current/high-temperature regions, and regions with poor interpolation error.
- Validate withheld points between anchors. A table/model is not accepted merely because it reproduces its training anchors.
- Never interpolate across different firmware, hardware, calibration, control architecture, or protection configurations without an explicit compatibility rule.
- Freeze 43.5 V as a historical regression anchor. New voltage-capability data prioritize integer Vrefs 36--48 V, beginning at 44 V, and explicitly include approximately 200 W, 300 W, and 400 W layers as source/load capacity permits.

## Present status

The current firmware is single voltage-loop PI, so `Kd=0` and there are no inner-loop gains yet. SPS and EPS have provisional/current best bundles at the present 24 V, 43.5 V, nominal 18 ohm condition; TPS and PI dynamic sweeps are incomplete. No trained runtime selector or cascade controller is claimed yet.
