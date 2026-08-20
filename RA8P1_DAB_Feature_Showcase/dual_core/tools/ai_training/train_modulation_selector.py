#!/usr/bin/env python3
"""Train the bounded SPS/EPS/TPS selector and parameter-bundle classifier.

The model emits one tensor: the first three values are SPS/EPS/TPS scores and
the remaining values select a validated parameter_bundle_id.  Firmware still
owns dwell time, legality checks, bumpless switching, fallback, and PWM.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Any

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import pandas as pd
import tensorflow as tf

from training_common import (
    SCHEMA_VERSION,
    classification_metrics,
    export_int8_tflite,
    fixed_range_normalize,
    parse_bool,
    require_columns,
    run_int8_tflite,
    save_json,
    set_deterministic,
    sha256_files,
    split_grouped,
)


MODEL_VERSION = "modulation-selector-v1"
MODE_LABELS = ["SPS", "EPS", "TPS"]

SCENARIO_CODES = {
    "CV_CHARGE": 0,
    "CP_ENERGY_ROUTE": 1,
    "MPPT_ONLY": 2,
    "SOLAR_CHARGER": 3,
    "CR_SUPPORT": 4,
    "CC_SUPPORT": 5,
}
LOAD_MODE_CODES = {"CV": 0, "CP": 1, "CR": 2, "CC": 3}
CONTROL_ARCH_CODES = {
    "VOLTAGE_PI_SINGLE": 0,
    "VOLTAGE_PI_SINGLE_LOOP": 0,
    "OUTPUT_CURRENT_PI": 1,
    "ENERGY_SUPERVISOR": 2,
    "CPU1_ENERGY_SUPERVISOR": 2,
    "CP_DAMPED_CASCADE": 3,
    "CP_POWER_PI_CURRENT_PI_DAMPED": 3,
}
BATTERY_CODES = {
    "NONE": 0,
    "LIION10": 1,
    "LI_ION_10S": 1,
    "LFP12": 2,
    "LFP_12S": 2,
    "LEAD36": 3,
    "LEAD_ACID_36V": 3,
}
CHARGE_STAGE_CODES = {
    "OFF": 0,
    "IDLE": 0,
    "PRECHARGE": 1,
    "CC": 2,
    "BULK": 2,
    "CV": 3,
    "ABSORPTION": 3,
    "FLOAT": 4,
    "DONE": 5,
}

# Fixed ranges are part of the MCU contract.  Do not replace these with fitted
# mean/std statistics without versioning the firmware preprocessor.
FEATURE_SPECS: list[tuple[str, float, float]] = [
    ("vin_v", 16.0, 26.0),
    ("vout_target_v", 34.0, 50.0),
    ("iout_target_a", 0.0, 18.0),
    ("pout_target_w", 0.0, 400.0),
    ("source_current_limit_a", 0.0, 25.0),
    ("power_limit_w", 20.0, 400.0),
    ("temp_primary_c", -20.0, 110.0),
    ("temp_secondary_c", -20.0, 110.0),
    ("scenario_code", 0.0, 5.0),
    ("load_mode_code", 0.0, 3.0),
    ("control_arch_code", 0.0, 3.0),
    ("battery_code", 0.0, 3.0),
    ("charge_stage_code", 0.0, 5.0),
    ("vinadapt_enabled", 0.0, 1.0),
    ("cp_damping_enabled", 0.0, 1.0),
]

REQUIRED_COLUMNS = {
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
}

BUNDLE_FIELDS = [
    "modulation",
    "current_direction",
    "primary_inner_ns",
    "secondary_inner_ns",
    "phase_cap_ns",
    "current_kp_ns_per_a",
    "current_ki_us_per_a_s",
    "current_slew_a_per_s",
]
BUNDLE_TEXT_FIELDS = ["modulation", "current_direction"]
BUNDLE_NUMERIC_FIELDS = [item for item in BUNDLE_FIELDS if item not in BUNDLE_TEXT_FIELDS]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", type=Path, nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=160)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--seed", type=int, default=20260817)
    parser.add_argument("--min-rows", type=int, default=180)
    parser.add_argument("--min-conditions", type=int, default=60)
    parser.add_argument(
        "--allow-provisional-labels",
        action="store_true",
        help="Use board-ADC/provisional labels; output remains non-deployable.",
    )
    parser.add_argument(
        "--allow-smoke",
        action="store_true",
        help="Bypass sample gates for pipeline tests; output remains non-deployable.",
    )
    return parser.parse_args()


def encode_category(series: pd.Series, mapping: dict[str, int], name: str) -> np.ndarray:
    normalized = series.fillna("").astype(str).str.strip().str.upper()
    unknown = sorted(set(normalized) - set(mapping))
    if unknown:
        raise ValueError(f"unknown {name} values: {unknown}")
    return normalized.map(mapping).to_numpy(dtype=np.float32)


def load_dataset(args: argparse.Namespace) -> tuple[pd.DataFrame, list[str]]:
    for path in args.data:
        if not path.is_file():
            raise FileNotFoundError(path)
    frames = [pd.read_csv(path) for path in args.data]
    frame = pd.concat(frames, ignore_index=True)
    require_columns(frame.columns, REQUIRED_COLUMNS)

    frame["fair_comparison_eligible"] = frame["fair_comparison_eligible"].map(
        parse_bool
    )
    frame["external_efficiency_validated"] = frame[
        "external_efficiency_validated"
    ].map(parse_bool)
    frame = frame[frame["fair_comparison_eligible"]].copy()
    if not args.allow_provisional_labels:
        frame = frame[frame["external_efficiency_validated"]].copy()
    if frame.empty:
        raise ValueError(
            "no eligible labels remain; use externally validated labels or "
            "--allow-provisional-labels for a non-deployable experiment"
        )

    frame["selected_modulation"] = (
        frame["selected_modulation"].astype(str).str.strip().str.upper()
    )
    invalid_modes = sorted(set(frame["selected_modulation"]) - set(MODE_LABELS))
    if invalid_modes:
        raise ValueError(f"invalid selected_modulation values: {invalid_modes}")
    frame["parameter_bundle_id"] = (
        frame["parameter_bundle_id"].astype(str).str.strip()
    )
    if (frame["parameter_bundle_id"] == "").any():
        raise ValueError("parameter_bundle_id must not be empty")

    readiness_issues: list[str] = []
    if len(frame) < args.min_rows:
        readiness_issues.append(f"rows {len(frame)} < {args.min_rows}")
    unique_conditions = frame["condition_id"].nunique()
    if unique_conditions < args.min_conditions:
        readiness_issues.append(
            f"conditions {unique_conditions} < {args.min_conditions}"
        )
    if set(frame["selected_modulation"]) != set(MODE_LABELS):
        readiness_issues.append("labels do not cover SPS, EPS, and TPS")
    mode_counts = frame["selected_modulation"].value_counts()
    if any(int(mode_counts.get(mode, 0)) < 10 for mode in MODE_LABELS):
        readiness_issues.append("at least one modulation class has fewer than 10 rows")
    if frame["condition_id"].nunique() < 5:
        readiness_issues.append("fewer than five independent condition groups")
    if not frame["external_efficiency_validated"].all():
        readiness_issues.append("provisional/non-independent efficiency labels are present")

    missing_bundle_fields = [item for item in BUNDLE_FIELDS if item not in frame]
    if missing_bundle_fields:
        readiness_issues.append(
            "bundle parameters missing: " + ", ".join(missing_bundle_fields)
        )
    else:
        incomplete = frame[BUNDLE_FIELDS].isna().any(axis=1)
        if incomplete.any():
            readiness_issues.append("one or more parameter bundles are incomplete")
        bundle_counts = frame["parameter_bundle_id"].value_counts()
        if int(bundle_counts.min()) < 3:
            readiness_issues.append("each parameter bundle needs at least three repeats")
        inconsistent: list[str] = []
        for bundle_id, rows in frame.groupby("parameter_bundle_id"):
            for field in BUNDLE_TEXT_FIELDS:
                if rows[field].astype(str).str.strip().str.lower().nunique() != 1:
                    inconsistent.append(f"{bundle_id}:{field}")
            for field in BUNDLE_NUMERIC_FIELDS:
                values = pd.to_numeric(rows[field], errors="coerce").dropna()
                if len(values) != len(rows) or float(values.max() - values.min()) > 1.0e-6:
                    inconsistent.append(f"{bundle_id}:{field}")
        if inconsistent:
            readiness_issues.append(
                "inconsistent bundle fields: " + ", ".join(inconsistent[:20])
            )

    if readiness_issues and not args.allow_smoke:
        raise ValueError(
            "dataset is not ready for formal training:\n- "
            + "\n- ".join(readiness_issues)
        )
    return frame, readiness_issues


def build_features(frame: pd.DataFrame) -> np.ndarray:
    features = pd.DataFrame(index=frame.index)
    for name in [
        "vin_v",
        "vout_target_v",
        "iout_target_a",
        "pout_target_w",
        "source_current_limit_a",
        "power_limit_w",
        "temp_primary_c",
        "temp_secondary_c",
    ]:
        features[name] = pd.to_numeric(frame[name], errors="raise")

    features["scenario_code"] = encode_category(
        frame["scenario_family"], SCENARIO_CODES, "scenario_family"
    )
    features["load_mode_code"] = encode_category(
        frame["load_mode"], LOAD_MODE_CODES, "load_mode"
    )
    features["control_arch_code"] = encode_category(
        frame["control_arch"], CONTROL_ARCH_CODES, "control_arch"
    )
    features["battery_code"] = encode_category(
        frame["battery_profile"], BATTERY_CODES, "battery_profile"
    )
    features["charge_stage_code"] = encode_category(
        frame["charge_stage"], CHARGE_STAGE_CODES, "charge_stage"
    )
    features["vinadapt_enabled"] = frame["vinadapt_enabled"].map(parse_bool).astype(float)
    features["cp_damping_enabled"] = (
        frame["cp_damping_enabled"].map(parse_bool).astype(float)
    )
    features = features[[item[0] for item in FEATURE_SPECS]]
    if not np.isfinite(features.to_numpy(dtype=np.float32)).all():
        raise ValueError("features contain NaN or infinity")
    return fixed_range_normalize(features.to_numpy(dtype=np.float32), FEATURE_SPECS)


def build_bundle_registry(frame: pd.DataFrame, bundle_labels: list[str]) -> dict[str, Any]:
    registry: dict[str, Any] = {}
    for bundle_id in bundle_labels:
        rows = frame[frame["parameter_bundle_id"] == bundle_id]
        entry: dict[str, Any] = {
            "parameter_bundle_id": bundle_id,
            "selected_modulation": str(rows.iloc[0]["selected_modulation"]),
            "repeat_count": int(len(rows)),
            "parameters_complete": all(item in rows for item in BUNDLE_FIELDS),
        }
        if entry["parameters_complete"]:
            for field in BUNDLE_TEXT_FIELDS:
                values = rows[field].dropna().astype(str).str.strip().unique().tolist()
                entry[field] = values[0] if len(values) == 1 else values
                if len(values) != 1:
                    entry["parameters_complete"] = False
            for field in BUNDLE_NUMERIC_FIELDS:
                values = pd.to_numeric(rows[field], errors="coerce").dropna()
                if len(values) != len(rows) or float(values.max() - values.min()) > 1.0e-6:
                    entry[field] = [float(item) for item in sorted(values.unique())]
                    entry["parameters_complete"] = False
                else:
                    entry[field] = float(values.iloc[0])
        registry[bundle_id] = entry
    return registry


def build_model(input_count: int, bundle_count: int) -> tf.keras.Model:
    inputs = tf.keras.Input(shape=(input_count,), name="condition_features")
    x = tf.keras.layers.Dense(32, activation="relu", name="dense_0")(inputs)
    x = tf.keras.layers.Dense(24, activation="relu", name="dense_1")(x)
    mode = tf.keras.layers.Dense(3, activation="softmax", name="mode_scores")(x)
    bundle = tf.keras.layers.Dense(
        bundle_count, activation="softmax", name="bundle_scores"
    )(x)
    combined = tf.keras.layers.Concatenate(name="mode_and_bundle_scores")(
        [mode, bundle]
    )
    return tf.keras.Model(inputs=inputs, outputs=combined, name="dab_modulation_selector")


def main() -> None:
    args = parse_args()
    set_deterministic(args.seed)
    tf.keras.utils.set_random_seed(args.seed)
    args.output.mkdir(parents=True, exist_ok=True)

    frame, readiness_issues = load_dataset(args)
    x = build_features(frame)
    mode_index = np.asarray(
        [MODE_LABELS.index(item) for item in frame["selected_modulation"]],
        dtype=np.int64,
    )
    bundle_labels = sorted(frame["parameter_bundle_id"].unique().tolist())
    bundle_lookup = {label: index for index, label in enumerate(bundle_labels)}
    bundle_index = np.asarray(
        [bundle_lookup[item] for item in frame["parameter_bundle_id"]],
        dtype=np.int64,
    )
    y = np.concatenate(
        [
            tf.one_hot(mode_index, 3).numpy(),
            tf.one_hot(bundle_index, len(bundle_labels)).numpy(),
        ],
        axis=1,
    ).astype(np.float32)

    train, validation, test = split_grouped(frame["condition_id"], args.seed)
    model = build_model(x.shape[1], len(bundle_labels))
    categorical_crossentropy = tf.keras.losses.CategoricalCrossentropy()

    def dual_head_loss(y_true: tf.Tensor, y_pred: tf.Tensor) -> tf.Tensor:
        mode_loss = categorical_crossentropy(y_true[:, :3], y_pred[:, :3])
        bundle_loss = categorical_crossentropy(y_true[:, 3:], y_pred[:, 3:])
        return mode_loss + bundle_loss

    model.compile(optimizer=tf.keras.optimizers.Adam(1.0e-3), loss=dual_head_loss)
    history = model.fit(
        x[train],
        y[train],
        validation_data=(x[validation], y[validation]),
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
    float_mode = classification_metrics(mode_index[test], float_scores[:, :3], MODE_LABELS)
    float_bundle = classification_metrics(
        bundle_index[test], float_scores[:, 3:], bundle_labels
    )

    (args.output / "model_architecture.json").write_text(
        model.to_json() + "\n", encoding="utf-8"
    )
    model.save_weights(args.output / "float_model.weights.h5")

    tflite_path = args.output / "modulation_selector_int8.tflite"
    tflite_contract = export_int8_tflite(model, x[train], tflite_path)
    int8_scores = run_int8_tflite(tflite_path, x[test])
    int8_mode = classification_metrics(mode_index[test], int8_scores[:, :3], MODE_LABELS)
    int8_bundle = classification_metrics(
        bundle_index[test], int8_scores[:, 3:], bundle_labels
    )

    metric_issues: list[str] = []
    if int8_mode["accuracy"] < 0.68:
        metric_issues.append("INT8 mode accuracy below 0.68")
    if int8_bundle["accuracy"] < 0.60:
        metric_issues.append("INT8 bundle accuracy below 0.60")
    if float_mode["accuracy"] - int8_mode["accuracy"] > 0.05:
        metric_issues.append("INT8 mode accuracy drop exceeds 0.05")

    deployment_ready = not (
        args.allow_smoke
        or args.allow_provisional_labels
        or readiness_issues
        or metric_issues
    )
    bundle_registry = build_bundle_registry(frame, bundle_labels)
    save_json(args.output / "parameter_bundles.json", bundle_registry)
    save_json(
        args.output / "training_history.json",
        {key: [float(value) for value in values] for key, values in history.history.items()},
    )

    metadata = {
        "schema_version": SCHEMA_VERSION,
        "model_version": MODEL_VERSION,
        "model_role": "SPS_EPS_TPS_SELECTOR_AND_PARAMETER_BUNDLE",
        "deployment_ready": deployment_ready,
        "readiness_issues": readiness_issues,
        "metric_issues": metric_issues,
        "safety_contract": {
            "output_is_advisory": True,
            "model_never_writes_pwm": True,
            "firmware_validates_bundle": True,
            "firmware_owns_dwell_bumpless_switch_and_rollback": True,
        },
        "input": {
            "feature_order": [item[0] for item in FEATURE_SPECS],
            "physical_ranges": {
                name: {"minimum": low, "maximum": high}
                for name, low, high in FEATURE_SPECS
            },
            "normalization": "clip_to_range_then_map_to_minus1_plus1",
            "category_maps": {
                "scenario": SCENARIO_CODES,
                "load_mode": LOAD_MODE_CODES,
                "control_arch": CONTROL_ARCH_CODES,
                "battery": BATTERY_CODES,
                "charge_stage": CHARGE_STAGE_CODES,
            },
        },
        "output": {
            "single_tensor_layout": {
                "mode_scores": {"offset": 0, "length": 3, "labels": MODE_LABELS},
                "bundle_scores": {
                    "offset": 3,
                    "length": len(bundle_labels),
                    "labels": bundle_labels,
                },
            },
            "firmware_returns": [
                "modulation_hint",
                "parameter_bundle_id",
                "confidence",
                "ttl_ms",
                "valid",
            ],
        },
        "dataset": {
            "files": [str(path.resolve()) for path in args.data],
            "sha256": sha256_files(args.data),
            "rows": int(len(frame)),
            "unique_conditions": int(frame["condition_id"].nunique()),
            "mode_counts": {
                key: int(value)
                for key, value in frame["selected_modulation"].value_counts().items()
            },
            "bundle_count": len(bundle_labels),
            "split_is_grouped_by": "condition_id",
            "split_rows": {
                "train": int(len(train)),
                "validation": int(len(validation)),
                "test": int(len(test)),
            },
        },
        "metrics": {
            "float_mode": float_mode,
            "float_bundle": float_bundle,
            "int8_mode": int8_mode,
            "int8_bundle": int8_bundle,
        },
        "tflite": tflite_contract,
    }
    save_json(args.output / "metadata.json", metadata)
    print(
        json.dumps(
            {
                "model": str(tflite_path),
                "deployment_ready": deployment_ready,
                "int8_mode_accuracy": int8_mode["accuracy"],
                "int8_bundle_accuracy": int8_bundle["accuracy"],
                "readiness_issues": readiness_issues,
                "metric_issues": metric_issues,
            },
            ensure_ascii=False,
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
