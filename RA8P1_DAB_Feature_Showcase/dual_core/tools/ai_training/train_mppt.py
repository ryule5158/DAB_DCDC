#!/usr/bin/env python3
"""Train a small INT8 MPPT voltage-bin classifier for RA8P1/Ethos-U55.

Input is a fixed nine-point power scan from 18 V to 24 V.  Output is one of
thirteen 0.5 V Vmpp bins.  The prediction is advisory; deterministic P&O or
incremental-conductance control remains the fallback and owns all bounds.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import pandas as pd
import tensorflow as tf

from training_common import (
    SCHEMA_VERSION,
    classification_metrics,
    export_int8_tflite,
    parse_bool,
    require_columns,
    run_int8_tflite,
    save_json,
    set_deterministic,
    sha256_files,
    split_grouped,
)


MODEL_VERSION = "mppt-vmpp-bin-v1"
SCAN_VOLTAGES_V = [18.0, 18.75, 19.5, 20.25, 21.0, 21.75, 22.5, 23.25, 24.0]
POWER_COLUMNS = [f"p{index}_w" for index in range(len(SCAN_VOLTAGES_V))]
VMPP_CLASS_VOLTAGES_V = [18.0 + 0.5 * index for index in range(13)]
CLASS_LABELS = [f"{voltage:.1f}V" for voltage in VMPP_CLASS_VOLTAGES_V]

REQUIRED_COLUMNS = {
    "episode_id",
    "session_id",
    "curve_id",
    "source_profile_id",
    "true_vmpp_v",
    "true_pmpp_w",
    "ground_truth_source",
    "ground_truth_validated",
    *POWER_COLUMNS,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", type=Path, nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=180)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--seed", type=int, default=20260817)
    parser.add_argument("--power-scale-w", type=float, default=555.6)
    parser.add_argument("--min-episodes", type=int, default=50)
    parser.add_argument("--min-curves", type=int, default=10)
    parser.add_argument(
        "--allow-unvalidated-ground-truth",
        action="store_true",
        help="Use heuristic Vmpp labels; output remains non-deployable.",
    )
    parser.add_argument(
        "--allow-smoke",
        action="store_true",
        help="Bypass sample gates for pipeline tests; output remains non-deployable.",
    )
    return parser.parse_args()


def load_dataset(args: argparse.Namespace) -> tuple[pd.DataFrame, list[str]]:
    if args.power_scale_w <= 0.0:
        raise ValueError("--power-scale-w must be positive")
    for path in args.data:
        if not path.is_file():
            raise FileNotFoundError(path)
    frame = pd.concat([pd.read_csv(path) for path in args.data], ignore_index=True)
    require_columns(frame.columns, REQUIRED_COLUMNS)
    frame["ground_truth_validated"] = frame["ground_truth_validated"].map(parse_bool)
    if not args.allow_unvalidated_ground_truth:
        frame = frame[frame["ground_truth_validated"]].copy()
    if frame.empty:
        raise ValueError(
            "no validated MPPT episodes remain; provide scan/reference labels or use "
            "--allow-unvalidated-ground-truth for a non-deployable experiment"
        )

    numeric_columns = POWER_COLUMNS + ["true_vmpp_v", "true_pmpp_w"]
    for column in numeric_columns:
        frame[column] = pd.to_numeric(frame[column], errors="raise")
    if not np.isfinite(frame[numeric_columns].to_numpy(dtype=np.float32)).all():
        raise ValueError("MPPT dataset contains NaN or infinity")
    if (frame[POWER_COLUMNS].to_numpy(dtype=np.float32) < 0.0).any():
        raise ValueError("power scan values must be non-negative")

    readiness_issues: list[str] = []
    if len(frame) < args.min_episodes:
        readiness_issues.append(f"episodes {len(frame)} < {args.min_episodes}")
    if frame["curve_id"].nunique() < args.min_curves:
        readiness_issues.append(
            f"curves {frame['curve_id'].nunique()} < {args.min_curves}"
        )
    if frame["curve_id"].nunique() < 5:
        readiness_issues.append("fewer than five independent curve groups")
    if not frame["ground_truth_validated"].all():
        readiness_issues.append("heuristic/unvalidated Vmpp labels are present")
    if frame[POWER_COLUMNS].to_numpy(dtype=np.float32).max() > args.power_scale_w:
        readiness_issues.append("power scan exceeds --power-scale-w and would clip")
    if (
        (frame["true_vmpp_v"] < VMPP_CLASS_VOLTAGES_V[0]).any()
        or (frame["true_vmpp_v"] > VMPP_CLASS_VOLTAGES_V[-1]).any()
    ):
        readiness_issues.append("true Vmpp lies outside the 18--24 V deployment grid")

    labels = nearest_vmpp_class(frame["true_vmpp_v"].to_numpy(dtype=np.float32))
    if len(np.unique(labels)) < 4:
        readiness_issues.append("fewer than four Vmpp classes are represented")

    if readiness_issues and not args.allow_smoke:
        raise ValueError(
            "dataset is not ready for formal MPPT training:\n- "
            + "\n- ".join(readiness_issues)
        )
    return frame, readiness_issues


def nearest_vmpp_class(vmpp_v: np.ndarray) -> np.ndarray:
    classes = np.asarray(VMPP_CLASS_VOLTAGES_V, dtype=np.float32)
    return np.argmin(np.abs(vmpp_v[:, np.newaxis] - classes[np.newaxis, :]), axis=1)


def build_model(input_count: int) -> tf.keras.Model:
    inputs = tf.keras.Input(shape=(input_count,), name="normalized_power_scan")
    x = tf.keras.layers.Dense(32, activation="relu", name="dense_0")(inputs)
    x = tf.keras.layers.Dense(24, activation="relu", name="dense_1")(x)
    outputs = tf.keras.layers.Dense(
        len(CLASS_LABELS), activation="softmax", name="vmpp_scores"
    )(x)
    return tf.keras.Model(inputs=inputs, outputs=outputs, name="dab_mppt_vmpp")


def voltage_metrics(truth: np.ndarray, probabilities: np.ndarray) -> dict[str, float]:
    predicted = np.argmax(probabilities, axis=1)
    true_voltage = np.asarray(VMPP_CLASS_VOLTAGES_V, dtype=np.float32)[truth]
    predicted_voltage = np.asarray(VMPP_CLASS_VOLTAGES_V, dtype=np.float32)[predicted]
    error = np.abs(predicted_voltage - true_voltage)
    return {
        "mae_v": float(np.mean(error)),
        "maximum_error_v": float(np.max(error)),
        "within_one_bin_fraction": float(np.mean(error <= 0.5)),
    }


def main() -> None:
    args = parse_args()
    set_deterministic(args.seed)
    tf.keras.utils.set_random_seed(args.seed)
    args.output.mkdir(parents=True, exist_ok=True)

    frame, readiness_issues = load_dataset(args)
    raw_power = frame[POWER_COLUMNS].to_numpy(dtype=np.float32)
    x = np.clip(raw_power / args.power_scale_w, 0.0, 1.0).astype(np.float32)
    labels = nearest_vmpp_class(frame["true_vmpp_v"].to_numpy(dtype=np.float32))
    train, validation, test = split_grouped(frame["curve_id"], args.seed)

    model = build_model(x.shape[1])
    model.compile(
        optimizer=tf.keras.optimizers.Adam(1.0e-3),
        loss=tf.keras.losses.SparseCategoricalCrossentropy(),
        metrics=[tf.keras.metrics.SparseCategoricalAccuracy(name="accuracy")],
    )
    history = model.fit(
        x[train],
        labels[train],
        validation_data=(x[validation], labels[validation]),
        epochs=args.epochs,
        batch_size=min(args.batch_size, len(train)),
        verbose=2,
        callbacks=[
            tf.keras.callbacks.EarlyStopping(
                monitor="val_loss", patience=18, restore_best_weights=True
            )
        ],
    )

    float_scores = model.predict(x[test], verbose=0)
    float_classification = classification_metrics(labels[test], float_scores, CLASS_LABELS)
    float_voltage = voltage_metrics(labels[test], float_scores)

    (args.output / "model_architecture.json").write_text(
        model.to_json() + "\n", encoding="utf-8"
    )
    model.save_weights(args.output / "float_model.weights.h5")
    tflite_path = args.output / "mppt_vmpp_int8.tflite"
    tflite_contract = export_int8_tflite(model, x[train], tflite_path)
    int8_scores = run_int8_tflite(tflite_path, x[test])
    int8_classification = classification_metrics(labels[test], int8_scores, CLASS_LABELS)
    int8_voltage = voltage_metrics(labels[test], int8_scores)

    metric_issues: list[str] = []
    if int8_voltage["mae_v"] > 0.50:
        metric_issues.append("INT8 Vmpp MAE exceeds 0.50 V")
    if int8_voltage["within_one_bin_fraction"] < 0.90:
        metric_issues.append("fewer than 90 percent of INT8 predictions are within one bin")
    if float_classification["accuracy"] - int8_classification["accuracy"] > 0.05:
        metric_issues.append("INT8 exact-bin accuracy drop exceeds 0.05")

    deployment_ready = not (
        args.allow_smoke
        or args.allow_unvalidated_ground_truth
        or readiness_issues
        or metric_issues
    )
    save_json(
        args.output / "training_history.json",
        {key: [float(value) for value in values] for key, values in history.history.items()},
    )
    metadata = {
        "schema_version": SCHEMA_VERSION,
        "model_version": MODEL_VERSION,
        "model_role": "MPPT_VMPP_HINT",
        "deployment_ready": deployment_ready,
        "readiness_issues": readiness_issues,
        "metric_issues": metric_issues,
        "safety_contract": {
            "output_is_advisory": True,
            "model_never_writes_pwm": True,
            "firmware_clamps_voltage_and_power": True,
            "fallback": ["PERTURB_AND_OBSERVE", "INCREMENTAL_CONDUCTANCE"],
        },
        "input": {
            "tensor_elements": len(POWER_COLUMNS),
            "column_order": POWER_COLUMNS,
            "scan_voltages_v": SCAN_VOLTAGES_V,
            "power_scale_w": args.power_scale_w,
            "normalization": "clip(power_w / power_scale_w, 0, 1)",
        },
        "output": {
            "class_voltages_v": VMPP_CLASS_VOLTAGES_V,
            "class_labels": CLASS_LABELS,
            "confidence": "maximum_softmax_score",
            "firmware_adapter_change_required": {
                "vmpp_min_mv": 18000,
                "vmpp_step_mv": 500,
                "output_classes": 13,
            },
        },
        "dataset": {
            "files": [str(path.resolve()) for path in args.data],
            "sha256": sha256_files(args.data),
            "episodes": int(len(frame)),
            "unique_curves": int(frame["curve_id"].nunique()),
            "ground_truth_sources": sorted(
                frame["ground_truth_source"].astype(str).unique().tolist()
            ),
            "split_is_grouped_by": "curve_id",
            "split_rows": {
                "train": int(len(train)),
                "validation": int(len(validation)),
                "test": int(len(test)),
            },
        },
        "metrics": {
            "float_classification": float_classification,
            "float_voltage": float_voltage,
            "int8_classification": int8_classification,
            "int8_voltage": int8_voltage,
        },
        "tflite": tflite_contract,
    }
    save_json(args.output / "metadata.json", metadata)
    print(
        json.dumps(
            {
                "model": str(tflite_path),
                "deployment_ready": deployment_ready,
                "int8_exact_bin_accuracy": int8_classification["accuracy"],
                "int8_vmpp_mae_v": int8_voltage["mae_v"],
                "readiness_issues": readiness_issues,
                "metric_issues": metric_issues,
            },
            ensure_ascii=False,
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
