# Modulation-selection training data

## Files

- `mode_tuning_trials.jsonl`: long-format tuning history. One JSON object is one SPS/EPS/TPS parameter trial; successful and failed trials are both retained.
- `mode_selection_labels.jsonl`: one finalized label per operating condition. A label is written only after SPS, EPS, and TPS have each been tuned over their planned safe range at the same condition.
- `raw_serial/session.log`: append-only raw evidence used to reproduce the aggregated fields.

## Fair-comparison key

All trials intended for direct SPS/EPS/TPS comparison share the same `operating_condition_id`. A fair comparison also requires matching:

- hardware and firmware revision;
- bench supply voltage and current limit;
- nominal and externally measured load resistance;
- voltage reference and power-limit command;
- controller gains, test duration, warm-up/stable-window rule, and measurement source.

If any required item differs or is not yet verified, `fair_comparison_eligible` remains false and the reason is saved in `comparison_exclusion_reason`.

## Model features and labels

The deployment-time feature set is explicitly stored under `selection_features`: input voltage, target output voltage, load resistance or requested power, source/current constraints, initial temperatures, and any other value available before choosing the mode.

The firmware `p` command, outer-phase cap, PI gains, and EPS/TPS inner shifts are optimized controller parameters. They are stored under `controller_parameters`/`outer_parameters`/`inner_parameters`, never as deployment-time condition features. Trials that change only these parameters remain in the same physical `operating_condition_id`.

Post-run measurements are stored under `evaluation_metrics` and must not be used as input features for a model that chooses the modulation mode before running. They are used only to calculate candidate scores and the final `selected_mode` label.

Each mode may have several tuning trials. `mode_tuning_complete=false` means that a failed parameter trial must not be interpreted as a failed modulation mode. A final label is allowed only when `all_modes_tuned=true` and `selection_ready=true`.

`best_parameters_by_condition.jsonl` stores one current best parameter record per physical operating condition and modulation mode. Every such record carries `code_integration_status`. A condition is not considered fully delivered until its selected settings have been added to the firmware lookup/interpolation table and verified after flashing.

The selection score will prioritize, in order: no fault or abnormal sound, meeting the requested voltage/range, absolute voltage error, regulation spread, current/thermal margins, then calibrated efficiency. Firmware `Pout/Pin` is retained as a telemetry metric but is not treated as physical efficiency until external current calibration is available.

## Required categorical fields

- `modulation_requested`: `SPS`, `EPS`, or `TPS`.
- `modulation_effective`: actual firmware mode.
- `control_mode`: `PI_CLOSED_LOOP` or `MANUAL_PHASE`.
- `control_architecture`: `VOLTAGE_PI_SINGLE_LOOP`, `CASCADED_VOLTAGE_AVG_CURRENT`, or `CASCADED_VOLTAGE_POWER`. Legacy `PI_CLOSED_LOOP` trials without this field map to `VOLTAGE_PI_SINGLE_LOOP`.
- `inner_control`: `AUTO` or `MANUAL`.
- `trial_status`: `SUCCESS`, `VOLTAGE_MISS`, `FAULT`, `ABNORMAL_SOUND`, `SOURCE_LIMIT`, or `INVALID_DATA`.
- `training_role`: `TUNING_TRIAL`, `MODE_CANDIDATE`, or `FINAL_LABEL`.

All numeric field names include units. Missing external measurements are `null`, never silently replaced with firmware telemetry.

Only closed-loop candidates may contribute to the final modulation-selection label. `MANUAL_PHASE` runs are diagnostic/tuning evidence and must set `eligible_for_final_mode_label=false`. Closed-loop tuning records must include Kp, Ki, phase/output saturation status, startup peak/overshoot, settling time, steady-state error/spread, and the stable-window rule.

When multiple control architectures are compared, `control_architecture` is mandatory so the modulation selector does not mix single-loop and cascade behavior. If deployment fixes the architecture before choosing SPS/EPS/TPS, it is a system-configuration feature. If a future model jointly chooses architecture and modulation, the pair is the label instead. Cascade trials additionally save outer/inner gains, feedback source and calibration version, feedforward version, slew limits, both saturation states, and bumpless-transfer status.

## Scheduled parameter bundle

The trained system does not output only PID gains. For each SPS/EPS/TPS candidate it retains a complete `scheduled_parameter_bundle` containing:

- `control_architecture` and `modulation`;
- outer-loop Kp/Ki/Kd and derivative-filter value, with Kd explicitly saved as zero for the present PI implementation;
- inner-loop Kp/Ki/Kd, feedback source, and calibration version when cascade control exists;
- phase cap, feedforward/initial effective phase, phase slew rate, anti-windup and integral limits;
- EPS/TPS primary/secondary inner shifts and raw-to-effective phase compensation version;
- current/power reference limits, transition hysteresis/dwell time, bumpless-transfer settings, and validated safety envelope;
- firmware, table, sensor-calibration, and validation revisions.

Instantaneous closed-loop phase is not a fixed lookup output. Trials retain commanded, raw, and effective phase time-series/statistics under `evaluation_metrics`; the scheduler supplies phase initialization/constraints while feedback continues to regulate Vout.

The recommended learning system uses mode ranking plus per-mode bounded parameter regression. Model outputs are clamped to validated ranges and fall back to a conservative validated table entry on low confidence, out-of-distribution conditions, invalid sensing, or abnormal behavior. Post-run metrics remain excluded from deployment inputs unless they are explicitly available from the immediately preceding valid operating state.

## Firmware v37 telemetry contract

Firmware `2026-08-15-dualcore-schedule-jsonl-v37` appends these numeric fields after the v36 VOFA fields: `kp_ns_per_v`, `ki_us_per_vs`, `kd_ns_s_per_v`, `d_filter_alpha`, `phase_cap_ns`, `bench_vin_set_v`, `load_nominal_ohm`, `requested_power_layer_w`, `control_arch`, `schedule_enabled`, `table_entry_id`, `outer_saturated`, `outer_saturation_updates`, `feedforward_enabled`, and `feedforward_effective_phase_ns`.

The first 27 v36 positions are retained unchanged for backward-compatible raw parsing. The firmware emits separate `fields1` and `fields2` declarations. New trials must record the firmware/schema version and map `control_arch=0` to `VOLTAGE_PI_SINGLE_LOOP`. A zero `table_entry_id` with `schedule_enabled=0` means a manually tuned parameter bundle, not missing data.

## Firmware v39 energy-control telemetry contract

Firmware `2026-08-15-dualcore-energy-control-v39` preserves every numeric position in the v38 `DAB:` line and emits one separate `ALG:` line per saved sample. Join the two lines only by the first `seq` field. `ALG:` fields are: `seq`, `arch`, `guard`, `Iref_cmd`, `Iref_active`, `Ierr`, `Ikp`, `Iki`, `Islew`, `energy_mode`, `energy_valid`, `energy_age_ms`, `energy_Iref`, `mppt_P`, `mppt_V`, `mppt_limited`, `bat_profile`, `charge_stage`, `charge_V`, `charge_limited`, `ai_hint`, `ai_conf`, `ai_applied`, `ai_mod_hint`, `ai_mod_conf`, `ai_mod_fresh`, `ipc_peer`, `ipc_stale`, `config_queued`, `config_dropped`.

Compatibility rule: v39 boots with the original voltage PI, energy mode OFF, AI OFF and `alglog` OFF. Thus `save` remains a legacy `DAB:`-only stream until a new current/energy configuration command succeeds or `alglog on` is sent. Use `alglog off` to return the serial stream to the legacy form without changing the selected controller.

Architecture mapping is `0=VOLTAGE_PI_SINGLE_LOOP`, `1=OUTPUT_CURRENT_PI`, `2=CPU1_ENERGY_SUPERVISOR_CURRENT_PI`. Guard mapping is `0=NORMAL_LAYERED`, `1=BENCH_ABSOLUTE_ONLY`; `ai_mod_hint` is `0=SPS`, `1=EPS`, `2=TPS`. `ai_mod_conf` and `ai_mod_fresh` are independent of the current-hint confidence/TTL, so recording a mode proposal cannot change current limiting. The modulation hint is a record-only model interface in v39 and does not prove that the live modulation was switched; use the existing `DAB:` requested/effective modulation fields as ground truth. Energy, MPPT, battery profile and charge-stage identifiers come from `dual_core/shared/dab_energy_control.h` and must be stored with firmware version rather than inferred from text. A sample with `energy_valid=0`, stale IPC, a dropped configuration or invalid current calibration is retained as diagnostic data but cannot be a positive controller/model label.
