#!/usr/bin/env python3
"""Convert legacy trial-per-mode CSV files into selector condition labels.

Only rows explicitly marked selected_overall are promoted.  Failed trials stay
in the source files and are not silently turned into positive selector labels.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

import pandas as pd

from training_common import parse_bool


OUTPUT_COLUMNS = [
    "record_id",
    "condition_id",
    "session_id",
    "firmware_version",
    "scenario_family",
    "load_mode",
    "control_arch",
    "battery_profile",
    "charge_stage",
    "vin_v",
    "vout_target_v",
    "iout_target_a",
    "pout_target_w",
    "source_current_limit_a",
    "power_limit_w",
    "temp_primary_c",
    "temp_secondary_c",
    "vinadapt_enabled",
    "cp_damping_enabled",
    "selected_modulation",
    "parameter_bundle_id",
    "fair_comparison_eligible",
    "external_efficiency_validated",
    "label_quality",
    "modulation",
    "current_direction",
    "primary_inner_ns",
    "secondary_inner_ns",
    "phase_cap_ns",
    "current_kp_ns_per_a",
    "current_ki_us_per_a_s",
    "current_slew_a_per_s",
    "source_csv",
    "source_log",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--session-prefix",
        default="legacy",
        help="Prefix used when the legacy CSV has no session_id column.",
    )
    return parser.parse_args()


def first(row: pd.Series, names: list[str], default: Any = "") -> Any:
    for name in names:
        if name in row and pd.notna(row[name]) and str(row[name]).strip() != "":
            return row[name]
    return default


def numeric(row: pd.Series, names: list[str], default: float = 0.0) -> float:
    value = first(row, names, default)
    return float(value)


def convert_row(row: pd.Series, source: Path, session_prefix: str) -> dict[str, Any]:
    load_mode = str(first(row, ["load_mode"], "CV")).upper()
    external_valid = parse_bool(
        first(
            row,
            ["external_efficiency_validated", "external_input_power_validated"],
            False,
        )
    )
    regulation = str(first(row, ["regulation_status"], "")).lower()
    fault = int(float(first(row, ["fault_max"], 0)))
    selected = parse_bool(first(row, ["selected_overall"], False))
    modulation = str(first(row, ["modulation"], "")).upper()
    record_id = str(first(row, ["record_id"], f"{source.stem}-{row.name}"))
    target_vout = numeric(row, ["load_set_v", "vout_target_v"], 0.0)
    target_iout = numeric(row, ["target_iout_a", "iout_target_a"], 0.0)
    target_power = numeric(
        row,
        ["power_layer_label_w", "target_pout_w"],
        target_vout * target_iout,
    )
    firmware = str(first(row, ["firmware_version"], "legacy-unversioned"))
    control_arch = str(first(row, ["control_arch"], "OUTPUT_CURRENT_PI")).upper()
    if control_arch == "VOLTAGE_PI_SINGLE_LOOP":
        control_arch = "VOLTAGE_PI_SINGLE"

    return {
        "record_id": record_id,
        "condition_id": str(first(row, ["condition_id"], source.stem)),
        "session_id": str(
            first(row, ["session_id"], f"{session_prefix}-{source.stem}")
        ),
        "firmware_version": firmware,
        "scenario_family": "CP_ENERGY_ROUTE" if load_mode == "CP" else "CV_CHARGE",
        "load_mode": load_mode,
        "control_arch": control_arch,
        "battery_profile": str(first(row, ["battery_profile"], "NONE")).upper(),
        "charge_stage": str(first(row, ["charge_stage"], "OFF")).upper(),
        "vin_v": numeric(row, ["vin_avg_v", "vin_v", "input_source_class_v"], 24.0),
        "vout_target_v": target_vout,
        "iout_target_a": target_iout,
        "pout_target_w": target_power,
        "source_current_limit_a": numeric(row, ["source_current_limit_a"], 20.0),
        "power_limit_w": numeric(row, ["power_limit_w"], 400.0),
        "temp_primary_c": numeric(row, ["tpri_avg_c", "temp_primary_c"], 25.0),
        "temp_secondary_c": numeric(row, ["tsec_avg_c", "temp_secondary_c"], 25.0),
        "vinadapt_enabled": parse_bool(first(row, ["vinadapt_enabled"], False)),
        "cp_damping_enabled": parse_bool(first(row, ["cp_damping_enabled"], False)),
        "selected_modulation": modulation,
        "parameter_bundle_id": str(
            first(row, ["parameter_bundle_id"], f"bundle-{record_id}")
        ),
        "fair_comparison_eligible": selected and fault == 0 and regulation == "accurate",
        "external_efficiency_validated": external_valid,
        "label_quality": "VERIFIED_EXTERNAL" if external_valid else "PROVISIONAL_BOARD",
        "modulation": modulation,
        "current_direction": str(first(row, ["current_direction"], "forward")).lower(),
        "primary_inner_ns": numeric(row, ["primary_inner_ns"], 0.0),
        "secondary_inner_ns": numeric(row, ["secondary_inner_ns"], 0.0),
        "phase_cap_ns": numeric(
            row,
            ["requested_phase_cap_ns", "phase_cap_ns", "telemetry_phase_cap_ns"],
            0.0,
        ),
        "current_kp_ns_per_a": numeric(row, ["current_kp_ns_per_a"], 0.0),
        "current_ki_us_per_a_s": numeric(
            row, ["current_ki_us_per_a_s", "current_ki_us_per_as"], 0.0
        ),
        "current_slew_a_per_s": numeric(row, ["current_slew_a_per_s"], 0.0),
        "source_csv": str(source.resolve()),
        "source_log": str(first(row, ["board_source_log", "source_log"], "")),
    }


def main() -> None:
    args = parse_args()
    converted: list[dict[str, Any]] = []
    source_rows = 0
    for source in args.input:
        if not source.is_file():
            raise FileNotFoundError(source)
        frame = pd.read_csv(source)
        source_rows += len(frame)
        if "selected_overall" not in frame:
            continue
        winners = frame[frame["selected_overall"].map(parse_bool)]
        converted.extend(
            convert_row(row, source, args.session_prefix)
            for _, row in winners.iterrows()
        )

    output = pd.DataFrame(converted, columns=OUTPUT_COLUMNS)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    output.to_csv(args.output, index=False, encoding="utf-8")
    print(
        f"source_rows={source_rows} promoted_overall_labels={len(output)} "
        f"output={args.output.resolve()}"
    )
    if output.empty:
        print("No selected_overall labels were promoted; this is a data gap, not an error.")


if __name__ == "__main__":
    main()
