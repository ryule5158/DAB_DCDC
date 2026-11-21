#!/usr/bin/env python3
"""Generate firmware parameter tables from powered PASS records.

Only records with the required numeric controller bundle and an explicit
powered-success label are promoted.  Failure and provisional capability data
remain in the training datasets but never enter the runtime tables.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import zlib
from pathlib import Path


MODULATION = {"SPS": 0, "EPS": 1, "TPS": 2}
BATTERY = {"LIION_10S_42V": 1, "LIFEPO4_12S_43P8V": 2,
           "LEAD_ACID_18S_36V": 3}
STAGE = {"PRECHARGE": 2, "CC": 3, "CV": 4, "FLOAT": 5}

# Retain the raw trial for incident analysis, but never promote it again.  This
# 48 V / 0.5 A SPS seed was later extrapolated outside its validated condition
# during G7 and is therefore retired even though the original row says PASS.
RETIRED_CURRENT_RECORDS = {"G5_SPS_CV48_I0P5"}


def number(value, default=None):
    try:
        result = float(value)
    except (TypeError, ValueError):
        return default
    return result if math.isfinite(result) else default


def stable_id(text: str) -> int:
    return 100000 + (zlib.crc32(text.encode("utf-8")) & 0x3FFFFFFF)


def cf(value: float) -> str:
    return f"{value:.6f}F"


def read_jsonl(path: Path):
    if not path.exists():
        return []
    records = []
    with path.open("r", encoding="utf-8") as stream:
        for line in stream:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


def current_records(path: Path):
    selected = {}
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            modulation = row.get("modulation", "").upper()
            values = {
                "vin": number(row.get("vin_board_avg_v")),
                "vref": number(row.get("eload_set_v")),
                "iref": number(row.get("iref_target_a")),
                "kp": number(row.get("ikp")),
                "ki": number(row.get("iki")),
                "slew": number(row.get("islew_a_per_s")),
                "cap": number(row.get("phase_cap_ns")),
                "pri": number(row.get("inner_primary_ns")),
                "sec": number(row.get("inner_secondary_ns")),
                "pin": number(row.get("pin_board_avg_w")),
                "pout": number(row.get("pout_board_avg_w")),
            }
            if (row.get("test_id") in RETIRED_CURRENT_RECORDS or
                    row.get("result") != "PASS" or
                    row.get("control_arch") != "current" or
                    modulation not in MODULATION or
                    any(values[key] is None for key in
                        ("vin", "vref", "iref", "kp", "ki", "slew",
                         "cap", "pri", "sec", "pin", "pout")) or
                    values["pin"] <= 0.0):
                continue
            key = (modulation, round(values["vin"]), values["vref"],
                   values["iref"])
            values.update({
                "id": stable_id(row["test_id"]),
                "source": row["test_id"],
                "mod": MODULATION[modulation],
                "direction": 1,
                "eta": 100.0 * values["pout"] / values["pin"],
            })
            # Later rows supersede earlier rows at the same rounded condition;
            # this intentionally promotes the later autotune validation.
            selected[key] = values
    return sorted(selected.values(), key=lambda x: (x["vref"], x["iref"],
                                                     x["mod"], x["vin"]))


def voltage_records(path: Path):
    selected = {}
    for row in read_jsonl(path):
        if row.get("record_type") != "mode_best_parameter":
            continue
        modulation = str(row.get("modulation", "")).upper()
        condition = row.get("condition_features", {})
        controller = row.get("controller_parameters", {})
        result = row.get("result_summary", {})
        vin = number(condition.get("bench_vin_set_v"))
        vref = number(condition.get("vref_v"))
        rload = number(condition.get("load_nominal_ohm"))
        vout = number(result.get("firmware_vout_mean_v"))
        fault = number(result.get("fault_max"), 1.0)
        cap = number(controller.get("phase_cap_ns"))
        kp = number(controller.get("kp_ns_per_v"))
        ki = number(controller.get("ki_us_per_vs"))
        kd = number(controller.get("kd_ns_s_per_v"), 0.0)
        alpha = number(controller.get("derivative_filter_alpha"), 0.05)
        power = number(controller.get("power_command_label_w"))
        if (modulation not in MODULATION or
                any(v is None for v in (vin, vref, rload, vout, cap, kp,
                                         ki, power)) or
                abs(vout - vref) > 0.25 or fault != 0.0):
            continue
        pri = number(controller.get("primary_inner_ns"),
                     number(controller.get("primary_inner_command_ns"), 0.0))
        sec = number(controller.get("secondary_inner_ns"), 0.0)
        layer = number(condition.get("requested_output_power_layer_w"), 200.0)
        source = str(row.get("source_trial_id", row.get("operating_condition_id")))
        record = {
            "id": stable_id(source), "source": source,
            "mod": MODULATION[modulation], "vin": vin, "vref": vref,
            "rload": rload, "layer": layer, "power": power,
            "kp": kp, "ki": ki, "kd": kd, "alpha": alpha,
            "cap": cap, "ff": 0.0, "pri": pri, "sec": sec,
        }
        selected[(modulation, vin, vref, rload, layer)] = record
    return sorted(selected.values(), key=lambda x: (x["vref"], x["mod"]))


def charger_records(path: Path):
    selected = {}
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            result = row.get("result", "")
            profile = row.get("battery_profile", "")
            stage = row.get("charge_stage", "")
            modulation = row.get("modulation", "").upper()
            if (not result.startswith("passed_") or "failed" in result or
                    profile not in BATTERY or stage not in STAGE or
                    stage not in ("CC", "CV") or modulation not in MODULATION):
                continue
            names = ("nominal_input_v", "load_set_v", "pri_inner_ns",
                     "sec_inner_ns", "phase_cap_ns", "current_kp",
                     "current_ki", "current_slew_a_s", "configured_imax_a",
                     "configured_iterm_a", "board_vin_v", "board_vout_v",
                     "board_iout_a", "board_pin_w", "board_pout_w",
                     "rk_voltage_v", "rk_current_a", "rk_power_w")
            values = {name: number(row.get(name)) for name in names}
            if any(value is None for value in values.values()):
                continue
            eta = number(row.get("external_to_board_input_eff_pct"), 0.0)
            source = row["record_id"]
            record = dict(values)
            record.update({"id": stable_id(source), "source": source,
                           "profile": BATTERY[profile], "stage": STAGE[stage],
                           "mod": MODULATION[modulation], "eta": eta})
            key = (profile, stage, values["load_set_v"],
                   values["configured_imax_a"])
            selected[key] = record
    return sorted(selected.values(), key=lambda x: (x["profile"], x["stage"],
                                                     x["load_set_v"]))


def charger_regression_records(path: Path):
    """Promote powered MOS-regression PASS rows as controller seeds.

    Commissioning rows remain parameter evidence.  The resulting firmware
    still requires a high-level-only autonomous replay before feature PASS.
    """
    records = []
    profile_ids = {"liion10": BATTERY["LIION_10S_42V"],
                   "lfp12": BATTERY["LIFEPO4_12S_43P8V"]}
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            result = row.get("result", "")
            profile = row.get("battery_profile", "").lower()
            modulation = row.get("modulation_effective", "").upper()
            stage_path = row.get("stage_path", "").upper()
            if (row.get("event_type") != "RESULT" or
                    not result.startswith("PASS_") or
                    row.get("reuse_allowed", "").lower() != "true" or
                    row.get("control_arch") != "energy" or
                    profile not in profile_ids or modulation not in MODULATION):
                continue
            stage = "CV" if ("CV" in stage_path and
                             ("DONE" in stage_path or "CV_DONE" in result)) else "CC"
            names = {
                "nominal_input_v": "vin_set_v",
                "load_set_v": "eload_cv_v",
                "pri_inner_ns": "primary_inner_ns",
                "sec_inner_ns": "secondary_inner_ns",
                "phase_cap_ns": "phase_cap_ns",
                "current_kp": "ikp_ns_per_a",
                "current_ki": "iki_us_per_as",
                "current_slew_a_s": "islew_a_per_s",
                "configured_imax_a": "imax_a",
                "configured_iterm_a": "iterm_a",
                "board_vin_v": "steady_board_vin_v",
                "board_vout_v": "steady_board_vout_v",
                "board_iout_a": "steady_board_iout_a",
                "board_pin_w": "steady_board_pin_w",
                "board_pout_w": "steady_board_pout_w",
                "rk_voltage_v": "eload_cv_v",
                "rk_current_a": "steady_external_i_avg_a",
                "rk_power_w": "steady_external_p_avg_w",
            }
            values = {name: number(row.get(column))
                      for name, column in names.items()}
            if any(value is None for value in values.values()):
                continue
            source = row["test_id"]
            eta = (100.0 * values["rk_power_w"] / values["board_pin_w"]
                   if values["board_pin_w"] > 0.0 else 0.0)
            record = dict(values)
            record.update({"id": stable_id(source), "source": source,
                           "profile": profile_ids[profile],
                           "stage": STAGE[stage],
                           "mod": MODULATION[modulation], "eta": eta})
            records.append(record)
    return sorted(records, key=lambda x: (x["profile"], x["stage"],
                                           x["load_set_v"], x["board_vin_v"]))


def write_tuned(path: Path, voltage, current):
    lines = [
        "#ifndef DAB_TUNED_PARAMETER_RECORDS_GENERATED_H",
        "#define DAB_TUNED_PARAMETER_RECORDS_GENERATED_H", "",
        "/* Generated by promote_validated_to_firmware.py; do not hand edit. */",
        '#define DAB_TUNED_PARAMETER_TABLE_VERSION "v47-exact-powered-seeds-retired-incident-records"',
        f"#define DAB_TUNED_PARAMETER_TABLE_ENTRY_COUNT ({len(voltage)}UL)",
        f"#define DAB_TUNED_CURRENT_ENTRY_COUNT ({len(current)}UL)", "",
        "static const dab_tuned_parameter_bundle_t g_dab_tuned_parameter_storage[] =", "{",
    ]
    if voltage:
        for r in voltage:
            lines.append(
                f"    /* {r['source']} */\n"
                f"    {{true, {r['id']}UL, {r['mod']}U, 0U, {cf(r['vin'])}, {cf(r['vref'])}, "
                f"{cf(r['rload'])}, {cf(r['layer'])}, {cf(r['power'])}, {cf(r['kp'])}, "
                f"{cf(r['ki'])}, {cf(r['kd'])}, {cf(r['alpha'])}, {cf(r['cap'])}, "
                f"{cf(r['ff'])}, {cf(r['pri'])}, {cf(r['sec'])}}},")
    else:
        lines.append("    {false, 0UL, 0U, 0U, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F},")
    lines.extend(["};", "", "static const dab_tuned_current_bundle_t g_dab_tuned_current_storage[] =", "{"])
    for r in current:
        lines.append(
            f"    /* {r['source']} */\n"
            f"    {{true, {r['id']}UL, {r['mod']}U, 1U, {cf(r['vin'])}, {cf(r['vref'])}, "
            f"{cf(r['iref'])}, {r['direction']}, {cf(r['kp'])}, {cf(r['ki'])}, "
            f"{cf(r['slew'])}, {cf(r['cap'])}, {cf(r['pri'])}, {cf(r['sec'])}, {cf(r['eta'])}}},")
    lines.extend(["};", "", "#endif /* DAB_TUNED_PARAMETER_RECORDS_GENERATED_H */", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def write_charger(path: Path, records):
    lines = [
        "#ifndef DAB_VALIDATED_CHARGER_RECORDS_GENERATED_H",
        "#define DAB_VALIDATED_CHARGER_RECORDS_GENERATED_H", "",
        "/* Generated by promote_validated_to_firmware.py; do not hand edit. */",
        '#define DAB_VALIDATED_CHARGER_TABLE_VERSION "v48-autonomous-charge-powered-seeds"',
        f"#define DAB_VALIDATED_CHARGER_TABLE_ENTRY_COUNT ({len(records)}UL)", "",
        "static const dab_validated_charger_entry_t g_dab_validated_charger_table[] =", "{",
    ]
    for r in records:
        lines.append(
            f"    /* {r['source']} */\n"
            f"    {{{r['id']}UL, {r['profile']}U, {r['stage']}U, {r['mod']}U, 2U, "
            f"{cf(r['nominal_input_v'])}, {cf(r['load_set_v'])}, {cf(r['pri_inner_ns'])}, "
            f"{cf(r['sec_inner_ns'])}, {cf(r['phase_cap_ns'])}, {cf(r['current_kp'])}, "
            f"{cf(r['current_ki'])}, {cf(r['current_slew_a_s'])}, {cf(r['configured_imax_a'])}, "
            f"{cf(r['configured_iterm_a'])}, {cf(r['board_vin_v'])}, {cf(r['board_vout_v'])}, "
            f"{cf(r['board_iout_a'])}, {cf(r['board_pin_w'])}, {cf(r['board_pout_w'])}, "
            f"{cf(r['rk_voltage_v'])}, {cf(r['rk_current_a'])}, {cf(r['rk_power_w'])}, {cf(r['eta'])}}},")
    lines.extend(["};", "", "#endif /* DAB_VALIDATED_CHARGER_RECORDS_GENERATED_H */", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="fail if generated files differ; do not overwrite")
    args = parser.parse_args()
    dual_core = Path(__file__).resolve().parents[2]
    workspace = Path(__file__).resolve().parents[5]
    advanced = workspace / "docs/validation/live_tuning/2026-08-17_v44_advanced_control/advanced_control_trials.csv"
    older = workspace / "docs/validation/live_tuning/2026-08-15_24V_resistive_multimode/best_parameters_by_condition.jsonl"
    charger = workspace / "docs/validation/live_tuning/2026-08-16_RK8510_v39/smart_charge_trajectory_points_24v_cv.csv"
    charger_regression = workspace / "docs/validation/live_tuning/2026-08-19_mos_regression_v47l/mos_regression_trials.csv"
    src = dual_core / "RA8P1_DAB_DualCore_Solution_CPU0/src"
    tuned_out = src / "dab_tuned_parameter_records.generated.h"
    charger_out = src / "dab_validated_charger_records.generated.h"

    voltage = voltage_records(older)
    current = current_records(advanced)
    charge = charger_records(charger) + charger_regression_records(charger_regression)
    if not current or not charge:
        raise SystemExit("refusing to emit empty powered current/charger tables")

    if args.check:
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            a = Path(tmp) / tuned_out.name
            b = Path(tmp) / charger_out.name
            write_tuned(a, voltage, current)
            write_charger(b, charge)
            if (not tuned_out.exists() or tuned_out.read_bytes() != a.read_bytes() or
                    not charger_out.exists() or charger_out.read_bytes() != b.read_bytes()):
                raise SystemExit("generated firmware tables are stale")
    else:
        write_tuned(tuned_out, voltage, current)
        write_charger(charger_out, charge)
    print(json.dumps({"voltage_entries": len(voltage),
                      "current_entries": len(current),
                      "charger_entries": len(charge),
                      "check": args.check}, sort_keys=True))


if __name__ == "__main__":
    main()
