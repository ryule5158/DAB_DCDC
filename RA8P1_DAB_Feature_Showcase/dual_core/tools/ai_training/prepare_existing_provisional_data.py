#!/usr/bin/env python3
"""Build non-deployable first-pass datasets from existing measured records.

The modulation labels come only from completed three-mode comparisons or rows
already marked ``selected_overall``.  The MPPT rows are reconstructed from
recorded P&O trajectories and therefore remain explicitly unvalidated; they
must never be promoted to formal MPP ground truth.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

import numpy as np
import pandas as pd

from prepare_modulation_dataset import OUTPUT_COLUMNS, convert_row
from training_common import parse_bool


SCAN_VOLTAGES_V = np.asarray(
    [18.0, 18.75, 19.5, 20.25, 21.0, 21.75, 22.5, 23.25, 24.0],
    dtype=np.float64,
)

LEGACY_MODULATION_FILES = [
    Path("DAB_RA8P1_PCB_Notes/live_tuning/2026-08-16_RK8510_v39/")
    / "modulation_training_24v_cv36_i6.csv",
    Path("DAB_RA8P1_PCB_Notes/live_tuning/2026-08-16_RK8510_v39/")
    / "modulation_training_24v_cv36_i7_v42.csv",
]

ADVANCED_MODULATION_FILE = Path(
    "DAB_RA8P1_PCB_Notes/live_tuning/2026-08-17_v44_advanced_control/"
    "advanced_control_trials.csv"
)

MPPT_LOG_FILES = [
    "2026-08-20_FMP_VIN22_ILIM5PPLUS_CV33_v49ab_r99_po_trajectory_keep_continuous.log",
    "2026-08-20_FMP_VIN24_ILIM5PPLUS_CV33_v49ab_r101_po_trajectory_keep_continuous.log",
    "2026-08-20_FMP_VIN20_ILIM5PPLUS_CV33_v49ad_r105_po_hold_floor_debounce_continuous.log",
    "2026-08-20_FMP_VIN20_ILIM8_CV36_v49ai_r111_po_outerreplay_continuous.log",
    "2026-08-20_FMP_VIN22_ILIM8_CV36_v49ai_r112_po_outerreplay_continuous.log",
    "2026-08-20_FMP_VIN24_ILIM8_CV36_v49ai_r113_po_outerreplay_continuous.log",
    "2026-08-20_FMP_VIN24_ILIM10_CV36_v49ai_r114_po_outerreplay_continuous.log",
    "2026-08-20_FMP_VIN24_ILIM12_CV36_v49ai_r115_po_outerreplay_continuous.log",
    "2026-08-20_FMP_VIN22_ILIM10_CV36_v49ai_r116_po_outerreplay_continuous.log",
    "2026-08-20_FMP_VIN20_ILIM10_CV36_v49ai_r117_po_outerreplay_continuous.log",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--workspace-root",
        type=Path,
        default=Path(__file__).resolve().parents[4],
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path(__file__).resolve().parent / "data" / "provisional_2026-08-21",
    )
    return parser.parse_args()


def numeric(row: pd.Series, name: str, default: float = 0.0) -> float:
    value = row.get(name, default)
    if pd.isna(value) or str(value).strip() == "":
        return float(default)
    return float(value)


def advanced_winner_to_record(
    winner: pd.Series,
    source: Path,
    condition_id: str,
) -> dict[str, Any]:
    modulation = str(winner["modulation"]).strip().upper()
    target_vout = numeric(winner, "eload_set_v")
    target_iout = numeric(winner, "iref_target_a")
    firmware = str(winner.get("firmware", "v44")).strip()
    test_id = str(winner["test_id"]).strip()
    return {
        "record_id": f"advanced-best-{test_id}",
        "condition_id": condition_id,
        "session_id": f"advanced-20260817-gate{winner.get('gate', '')}",
        "firmware_version": firmware,
        "scenario_family": "CV_CHARGE",
        "load_mode": str(winner.get("eload_mode", "CV")).strip().upper(),
        "control_arch": "OUTPUT_CURRENT_PI",
        "battery_profile": "NONE",
        "charge_stage": "OFF",
        "vin_v": numeric(winner, "vin_board_avg_v", numeric(winner, "input_supply_set_v")),
        "vout_target_v": target_vout,
        "iout_target_a": target_iout,
        "pout_target_w": target_vout * target_iout,
        "source_current_limit_a": 20.0,
        "power_limit_w": numeric(winner, "power_limit_w", 400.0),
        "temp_primary_c": numeric(winner, "tpri_avg_c", 25.0),
        "temp_secondary_c": numeric(winner, "tsec_avg_c", 25.0),
        "vinadapt_enabled": str(winner.get("vinadapt", "off")).strip().lower()
        not in {"", "off", "0", "false"},
        "cp_damping_enabled": str(winner.get("cpdamp", "off")).strip().lower()
        not in {"", "off", "0", "false"},
        "selected_modulation": modulation,
        "parameter_bundle_id": f"advanced-{test_id}",
        "fair_comparison_eligible": True,
        "external_efficiency_validated": False,
        "label_quality": "PROVISIONAL_MATCHED_3_MODE_BOARD_ADC_RATIO",
        "modulation": modulation,
        "current_direction": "forward",
        "primary_inner_ns": numeric(winner, "inner_primary_ns"),
        "secondary_inner_ns": numeric(winner, "inner_secondary_ns"),
        "phase_cap_ns": numeric(winner, "phase_cap_ns"),
        "current_kp_ns_per_a": numeric(winner, "ikp"),
        "current_ki_us_per_a_s": numeric(winner, "iki"),
        "current_slew_a_per_s": numeric(winner, "islew_a_per_s"),
        "source_csv": str(source.resolve()),
        "source_log": "",
    }


def build_modulation_dataset(workspace_root: Path) -> tuple[pd.DataFrame, dict[str, Any]]:
    records: list[dict[str, Any]] = []
    source_rows = 0

    for relative in LEGACY_MODULATION_FILES:
        source = workspace_root / relative
        frame = pd.read_csv(source)
        source_rows += len(frame)
        winners = frame[frame["selected_overall"].map(parse_bool)]
        records.extend(
            convert_row(row, source, "provisional-existing")
            for _, row in winners.iterrows()
        )

    advanced_source = workspace_root / ADVANCED_MODULATION_FILE
    advanced = pd.read_csv(advanced_source)
    source_rows += len(advanced)
    comparable = advanced[
        (advanced["result"].astype(str).str.upper() == "PASS")
        & (
            advanced["scenario"].astype(str).str.lower()
            == "same_condition_modulation_compare"
        )
    ].copy()
    group_columns = ["gate", "eload_mode", "eload_set_v", "iref_target_a", "input_supply_set_v"]
    for group_key, rows in comparable.groupby(group_columns, dropna=False):
        if set(rows["modulation"].astype(str).str.upper()) != {"SPS", "EPS", "TPS"}:
            continue
        pin = pd.to_numeric(rows["pin_board_avg_w"], errors="coerce")
        pout = pd.to_numeric(rows["pout_board_avg_w"], errors="coerce")
        ratio = pout / pin.replace(0.0, np.nan)
        if ratio.notna().sum() != len(rows):
            continue
        winner = rows.loc[ratio.idxmax()]
        condition_id = "advanced-" + "-".join(
            re.sub(r"[^A-Za-z0-9_.-]+", "_", str(item)) for item in group_key
        )
        records.append(advanced_winner_to_record(winner, advanced_source, condition_id))

    output = pd.DataFrame(records, columns=OUTPUT_COLUMNS)
    manifest = {
        "purpose": "non-deployable first-pass training on existing measured records",
        "source_rows_inspected": source_rows,
        "promoted_rows": int(len(output)),
        "unique_conditions": int(output["condition_id"].nunique()),
        "mode_counts": {
            str(key): int(value)
            for key, value in output["selected_modulation"].value_counts().items()
        },
        "external_efficiency_validated": False,
        "selection_rule": (
            "legacy selected_overall, or highest board-ADC Pout/Pin among completed "
            "matched SPS/EPS/TPS PASS trials"
        ),
    }
    return output, manifest


def parse_alg_points(path: Path) -> tuple[np.ndarray, np.ndarray]:
    voltages: list[float] = []
    powers: list[float] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if not line.startswith("ALG:"):
                continue
            fields = line[4:].strip().split(",")
            if len(fields) <= 15:
                continue
            try:
                power_w = float(fields[13])
                voltage_v = float(fields[14])
            except ValueError:
                continue
            if power_w > 0.0 and 18.0 <= voltage_v <= 24.0:
                voltages.append(voltage_v)
                powers.append(power_w)
    if len(voltages) < 20:
        raise ValueError(f"too few positive MPPT trajectory points in {path}")
    return np.asarray(voltages), np.asarray(powers)


def aggregate_scan(voltages: np.ndarray, powers: np.ndarray) -> np.ndarray:
    rounded_voltage = np.round(voltages * 10.0) / 10.0
    unique_voltage = np.unique(rounded_voltage)
    median_power = np.asarray(
        [np.median(powers[rounded_voltage == voltage]) for voltage in unique_voltage],
        dtype=np.float64,
    )
    return np.interp(SCAN_VOLTAGES_V, unique_voltage, median_power)


def build_mppt_dataset(workspace_root: Path) -> tuple[pd.DataFrame, dict[str, Any]]:
    log_root = workspace_root / "DAB_RA8P1_PCB_Notes" / "serial_logs"
    rows: list[dict[str, Any]] = []
    diagnostics: list[dict[str, Any]] = []
    for file_name in MPPT_LOG_FILES:
        path = log_root / file_name
        voltages, powers = parse_alg_points(path)
        scan_power = aggregate_scan(voltages, powers)
        best_index = int(np.argmax(scan_power))
        session_match = re.search(r"_(r\d+)_", file_name)
        session_id = session_match.group(1) if session_match else path.stem
        row: dict[str, Any] = {
            "episode_id": f"provisional-{session_id}",
            "session_id": session_id,
            "curve_id": path.stem,
            "source_profile_id": re.sub(r"_v49.*", "", path.stem),
            "true_vmpp_v": float(SCAN_VOLTAGES_V[best_index]),
            "true_pmpp_w": float(scan_power[best_index]),
            "ground_truth_source": "PROVISIONAL_P_AND_O_TRAJECTORY_INTERPOLATION",
            "ground_truth_validated": False,
        }
        row.update({f"p{index}_w": float(value) for index, value in enumerate(scan_power)})
        rows.append(row)
        diagnostics.append(
            {
                "source_log": str(path.resolve()),
                "positive_points": int(len(voltages)),
                "observed_vmin": float(np.min(voltages)),
                "observed_vmax": float(np.max(voltages)),
                "interpolation_includes_edge_hold": bool(
                    np.min(voltages) > SCAN_VOLTAGES_V[0]
                    or np.max(voltages) < SCAN_VOLTAGES_V[-1]
                ),
                "provisional_vmpp_v": float(SCAN_VOLTAGES_V[best_index]),
            }
        )

    output = pd.DataFrame(rows)
    manifest = {
        "purpose": "non-deployable MPPT pipeline training from existing P&O trajectories",
        "episodes": int(len(output)),
        "unique_curves": int(output["curve_id"].nunique()),
        "ground_truth_validated": False,
        "warning": (
            "P&O trajectory medians and interpolation are not a complete independent P-V "
            "scan and must not be promoted as deployable Vmpp ground truth"
        ),
        "diagnostics": diagnostics,
    }
    return output, manifest


def main() -> None:
    args = parse_args()
    workspace_root = args.workspace_root.resolve()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    modulation, modulation_manifest = build_modulation_dataset(workspace_root)
    mppt, mppt_manifest = build_mppt_dataset(workspace_root)

    modulation_path = output_dir / "modulation_conditions_provisional.csv"
    mppt_path = output_dir / "mppt_episodes_provisional.csv"
    manifest_path = output_dir / "dataset_manifest.json"
    modulation.to_csv(modulation_path, index=False, encoding="utf-8")
    mppt.to_csv(mppt_path, index=False, encoding="utf-8")
    manifest_path.write_text(
        json.dumps(
            {"modulation": modulation_manifest, "mppt": mppt_manifest},
            ensure_ascii=False,
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print(
        json.dumps(
            {
                "modulation_csv": str(modulation_path),
                "modulation_rows": int(len(modulation)),
                "mppt_csv": str(mppt_path),
                "mppt_episodes": int(len(mppt)),
                "manifest": str(manifest_path),
                "deployment_ready": False,
            },
            ensure_ascii=False,
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
