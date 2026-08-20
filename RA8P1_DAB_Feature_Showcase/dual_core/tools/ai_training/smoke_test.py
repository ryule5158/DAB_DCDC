#!/usr/bin/env python3
"""End-to-end synthetic smoke test for both training and INT8 export paths."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import pandas as pd


ROOT = Path(__file__).resolve().parent


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--epochs", type=int, default=3)
    parser.add_argument("--keep-output", type=Path)
    return parser.parse_args()


def make_modulation_data(path: Path) -> None:
    rows = []
    for condition in range(30):
        vin = 18.0 + 2.0 * (condition % 4)
        target_power = 50.0 + 50.0 * (condition % 7)
        if target_power <= 100.0:
            mode = "SPS"
        elif vin <= 20.0 or target_power >= 300.0:
            mode = "TPS"
        else:
            mode = "EPS"
        bundle_id = f"{mode.lower()}-bundle"
        parameters = {
            "SPS": (0.0, 0.0, 180.0, 2.0, 0.20, 1.0),
            "EPS": (40.0, 0.0, 260.0, 2.2, 0.22, 1.5),
            "TPS": (40.0, 20.0, 320.0, 2.5, 0.25, 2.0),
        }[mode]
        for repeat in range(3):
            rows.append(
                {
                    "record_id": f"smoke-{condition}-{repeat}",
                    "condition_id": f"condition-{condition}",
                    "session_id": f"session-{condition // 3}",
                    "firmware_version": "synthetic-smoke",
                    "scenario_family": "CP_ENERGY_ROUTE"
                    if condition % 2
                    else "CV_CHARGE",
                    "load_mode": "CP" if condition % 2 else "CV",
                    "control_arch": "CP_DAMPED_CASCADE"
                    if condition % 2
                    else "OUTPUT_CURRENT_PI",
                    "battery_profile": "NONE",
                    "charge_stage": "OFF",
                    "vin_v": vin + 0.02 * repeat,
                    "vout_target_v": 36.0 + 2.0 * (condition % 5),
                    "iout_target_a": target_power / 42.0,
                    "pout_target_w": target_power,
                    "source_current_limit_a": 20.0,
                    "power_limit_w": 400.0,
                    "temp_primary_c": 30.0 + condition % 15,
                    "temp_secondary_c": 28.0 + condition % 10,
                    "vinadapt_enabled": vin < 24.0,
                    "cp_damping_enabled": bool(condition % 2),
                    "selected_modulation": mode,
                    "parameter_bundle_id": bundle_id,
                    "fair_comparison_eligible": True,
                    "external_efficiency_validated": True,
                    "label_quality": "SYNTHETIC_SMOKE",
                    "modulation": mode,
                    "current_direction": "forward",
                    "primary_inner_ns": parameters[0],
                    "secondary_inner_ns": parameters[1],
                    "phase_cap_ns": parameters[2],
                    "current_kp_ns_per_a": parameters[3],
                    "current_ki_us_per_a_s": parameters[4],
                    "current_slew_a_per_s": parameters[5],
                    "source_csv": "synthetic",
                    "source_log": "synthetic",
                }
            )
    pd.DataFrame(rows).to_csv(path, index=False)


def make_mppt_data(path: Path) -> None:
    scan_v = np.asarray(
        [18.0, 18.75, 19.5, 20.25, 21.0, 21.75, 22.5, 23.25, 24.0]
    )
    rows = []
    for curve in range(20):
        vmpp = 18.0 + 0.5 * (curve % 13)
        pmpp = 80.0 + 12.0 * curve
        curvature = 7.0 + (curve % 4)
        for episode in range(4):
            power = np.maximum(
                0.0,
                pmpp - curvature * np.square(scan_v - vmpp) + 0.05 * episode,
            )
            row = {
                "episode_id": f"episode-{curve}-{episode}",
                "session_id": f"session-{curve // 2}",
                "curve_id": f"curve-{curve}",
                "source_profile_id": f"synthetic-profile-{curve}",
                "true_vmpp_v": vmpp,
                "true_pmpp_w": pmpp,
                "ground_truth_source": "SYNTHETIC_ANALYTIC_CURVE",
                "ground_truth_validated": True,
            }
            row.update({f"p{i}_w": float(value) for i, value in enumerate(power)})
            rows.append(row)
    pd.DataFrame(rows).to_csv(path, index=False)


def run(command: list[str]) -> None:
    print("RUN", " ".join(command))
    subprocess.run(command, cwd=ROOT, check=True)


def verify_output(path: Path) -> None:
    metadata = json.loads((path / "metadata.json").read_text(encoding="utf-8"))
    if metadata["deployment_ready"]:
        raise AssertionError("smoke output must never be marked deployment_ready")
    if metadata["tflite"]["inputs"][0]["dtype"] != "int8":
        raise AssertionError("model input is not INT8")
    if metadata["tflite"]["outputs"][0]["dtype"] != "int8":
        raise AssertionError("model output is not INT8")


def execute(output_root: Path, epochs: int) -> None:
    data = output_root / "data"
    modulation_output = output_root / "modulation"
    mppt_output = output_root / "mppt"
    data.mkdir(parents=True, exist_ok=True)
    modulation_csv = data / "modulation.csv"
    mppt_csv = data / "mppt.csv"
    make_modulation_data(modulation_csv)
    make_mppt_data(mppt_csv)

    run(
        [
            sys.executable,
            str(ROOT / "train_modulation_selector.py"),
            "--data",
            str(modulation_csv),
            "--output",
            str(modulation_output),
            "--epochs",
            str(epochs),
            "--allow-smoke",
        ]
    )
    run(
        [
            sys.executable,
            str(ROOT / "train_mppt.py"),
            "--data",
            str(mppt_csv),
            "--output",
            str(mppt_output),
            "--epochs",
            str(epochs),
            "--allow-smoke",
        ]
    )
    verify_output(modulation_output)
    verify_output(mppt_output)
    print(f"Both synthetic INT8 pipelines passed: {output_root}")


def main() -> None:
    args = parse_args()
    if args.keep_output:
        args.keep_output.mkdir(parents=True, exist_ok=True)
        execute(args.keep_output.resolve(), args.epochs)
    else:
        with tempfile.TemporaryDirectory(prefix="ra8p1-ai-smoke-") as directory:
            execute(Path(directory), args.epochs)


if __name__ == "__main__":
    main()
