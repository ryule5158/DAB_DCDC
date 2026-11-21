#!/usr/bin/env python3
"""Audit a provisional two-model run and enforce its non-deployable boundary."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

import numpy as np
import pandas as pd

from train_mppt import POWER_COLUMNS, nearest_vmpp_class
from training_common import split_grouped


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parent
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=root / "data" / "provisional_2026-08-21",
    )
    parser.add_argument(
        "--artifact-dir",
        type=Path,
        default=root / "artifacts" / "provisional_2026-08-21",
    )
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def disjoint_group_check(groups: pd.Series, seed: int) -> dict[str, Any]:
    train, validation, test = split_grouped(groups, seed)
    values = groups.astype(str).to_numpy()
    partitions = {
        "train": set(values[train]),
        "validation": set(values[validation]),
        "test": set(values[test]),
    }
    overlap = (
        (partitions["train"] & partitions["validation"])
        | (partitions["train"] & partitions["test"])
        | (partitions["validation"] & partitions["test"])
    )
    return {
        "passed": not overlap,
        "overlap": sorted(overlap),
        "rows": {
            "train": int(len(train)),
            "validation": int(len(validation)),
            "test": int(len(test)),
        },
        "groups": {name: sorted(items) for name, items in partitions.items()},
    }


def audit_model(artifact_dir: Path, name: str) -> dict[str, Any]:
    directory = artifact_dir / name
    metadata_path = directory / "metadata.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    tflite_files = list(directory.glob("*.tflite"))
    if len(tflite_files) != 1:
        raise ValueError(f"expected exactly one TFLite file in {directory}")
    tflite_path = tflite_files[0]
    inputs = metadata["tflite"]["inputs"]
    outputs = metadata["tflite"]["outputs"]
    recorded_hash = metadata["tflite"]["sha256"]
    actual_hash = sha256(tflite_path)
    return {
        "metadata": str(metadata_path.resolve()),
        "tflite": str(tflite_path.resolve()),
        "deployment_ready": bool(metadata["deployment_ready"]),
        "readiness_issues": metadata["readiness_issues"],
        "metric_issues": metadata["metric_issues"],
        "fully_int8_io": bool(
            inputs
            and outputs
            and all(item["dtype"] == "int8" for item in inputs + outputs)
        ),
        "sha256_matches": recorded_hash == actual_hash,
        "sha256": actual_hash,
        "input_shapes": [item["shape"] for item in inputs],
        "output_shapes": [item["shape"] for item in outputs],
    }


def main() -> None:
    args = parse_args()
    data_dir = args.data_dir.resolve()
    artifact_dir = args.artifact_dir.resolve()
    output_path = (
        args.output.resolve()
        if args.output
        else artifact_dir / "DATA_QUALITY_AUDIT.json"
    )

    modulation = pd.read_csv(data_dir / "modulation_conditions_provisional.csv")
    mppt = pd.read_csv(data_dir / "mppt_episodes_provisional.csv")
    manifest = json.loads(
        (data_dir / "dataset_manifest.json").read_text(encoding="utf-8")
    )

    modulation_duplicates = int(modulation["record_id"].duplicated().sum())
    modulation_null_required = int(
        modulation[
            [
                "record_id",
                "condition_id",
                "selected_modulation",
                "parameter_bundle_id",
            ]
        ]
        .isna()
        .sum()
        .sum()
    )
    modulation_group_split = disjoint_group_check(
        modulation["condition_id"], 20260817
    )

    mppt_duplicates = int(mppt["episode_id"].duplicated().sum())
    curve_duplicates = int(mppt["curve_id"].duplicated().sum())
    mppt_power = mppt[POWER_COLUMNS].to_numpy(dtype=np.float64)
    label_class = nearest_vmpp_class(mppt["true_vmpp_v"].to_numpy(dtype=np.float32))
    input_argmax = np.argmax(mppt_power, axis=1)
    scan_voltage = np.asarray(
        [18.0, 18.75, 19.5, 20.25, 21.0, 21.75, 22.5, 23.25, 24.0]
    )
    derived_vmpp = scan_voltage[input_argmax]
    labels_derived_from_input = np.isclose(
        derived_vmpp, mppt["true_vmpp_v"].to_numpy(dtype=np.float64)
    )
    edge_hold_count = sum(
        bool(item["interpolation_includes_edge_hold"])
        for item in manifest["mppt"]["diagnostics"]
    )
    mppt_group_split = disjoint_group_check(mppt["curve_id"], 20260817)

    model_audit = {
        "modulation": audit_model(artifact_dir, "modulation_selector_v1_run1"),
        "mppt": audit_model(artifact_dir, "mppt_v1_run1"),
    }
    blocking_boundary_passed = bool(
        not model_audit["modulation"]["deployment_ready"]
        and not model_audit["mppt"]["deployment_ready"]
        and model_audit["modulation"]["fully_int8_io"]
        and model_audit["mppt"]["fully_int8_io"]
        and model_audit["modulation"]["sha256_matches"]
        and model_audit["mppt"]["sha256_matches"]
        and modulation_group_split["passed"]
        and mppt_group_split["passed"]
    )

    report = {
        "intended_use": "pipeline smoke and first-pass training only",
        "blocking_boundary_passed": blocking_boundary_passed,
        "overall_data_quality_for_deployment": "FAIL",
        "modulation_dataset": {
            "grain": "one selected winner per independent matched condition",
            "rows": int(len(modulation)),
            "columns": int(len(modulation.columns)),
            "unique_conditions": int(modulation["condition_id"].nunique()),
            "duplicate_record_ids": modulation_duplicates,
            "null_required_key_fields": modulation_null_required,
            "class_counts": {
                str(key): int(value)
                for key, value in modulation["selected_modulation"].value_counts().items()
            },
            "unique_parameter_bundles": int(
                modulation["parameter_bundle_id"].nunique()
            ),
            "minimum_bundle_repeat_count": int(
                modulation["parameter_bundle_id"].value_counts().min()
            ),
            "all_external_efficiency_validated": bool(
                modulation["external_efficiency_validated"].astype(bool).all()
            ),
            "group_split": modulation_group_split,
        },
        "mppt_dataset": {
            "grain": "one reconstructed nine-point scan per source log",
            "rows": int(len(mppt)),
            "columns": int(len(mppt.columns)),
            "unique_curves": int(mppt["curve_id"].nunique()),
            "duplicate_episode_ids": mppt_duplicates,
            "duplicate_curve_ids": curve_duplicates,
            "negative_power_cells": int((mppt_power < 0.0).sum()),
            "ground_truth_validated_rows": int(
                mppt["ground_truth_validated"].astype(bool).sum()
            ),
            "represented_nearest_half_volt_classes": int(len(np.unique(label_class))),
            "labels_derived_from_same_input_rows": int(labels_derived_from_input.sum()),
            "edge_hold_interpolation_rows": int(edge_hold_count),
            "group_split": mppt_group_split,
        },
        "models": model_audit,
        "findings": [
            {
                "severity": "critical",
                "confidence": "high",
                "finding": "both datasets fail formal sample and label gates",
                "impact": "no deployment, RUHMI promotion, firmware integration, or control claim",
            },
            {
                "severity": "critical",
                "confidence": "high",
                "finding": (
                    f"MPPT labels are derived from the same nine input powers in "
                    f"{int(labels_derived_from_input.sum())}/{len(mppt)} rows"
                ),
                "impact": "test metrics cannot measure independent Vmpp generalization",
            },
            {
                "severity": "high",
                "confidence": "high",
                "finding": (
                    f"MPPT interpolation holds an observed edge in {edge_hold_count}/{len(mppt)} rows"
                ),
                "impact": "unobserved scan voltages can create artificial plateaus and boundary labels",
            },
            {
                "severity": "high",
                "confidence": "high",
                "finding": (
                    "modulation labels cover only EPS and TPS; SPS is absent and each "
                    "parameter bundle appears once"
                ),
                "impact": "mode and parameter-bundle accuracy are not statistically meaningful",
            },
        ],
        "automated_gate": {
            "must_remain_deployment_ready_false": True,
            "must_have_disjoint_group_splits": True,
            "must_have_fully_int8_io": True,
            "must_have_matching_tflite_sha256": True,
        },
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if not blocking_boundary_passed:
        raise SystemExit("provisional model safety boundary audit failed")


if __name__ == "__main__":
    main()
