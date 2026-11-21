"""Shared, deployment-oriented helpers for the two RA8P1 AI models."""

from __future__ import annotations

import hashlib
import json
import os
import random
from pathlib import Path
from typing import Any, Iterable

import numpy as np


SCHEMA_VERSION = "ra8p1-dab-ai-v1"


def set_deterministic(seed: int) -> None:
    """Set deterministic seeds before model construction."""

    os.environ.setdefault("PYTHONHASHSEED", str(seed))
    random.seed(seed)
    np.random.seed(seed)


def parse_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if value is None:
        return False
    if isinstance(value, (int, float)):
        return bool(value)
    return str(value).strip().lower() in {"1", "true", "yes", "y", "on"}


def require_columns(columns: Iterable[str], required: Iterable[str]) -> None:
    present = set(columns)
    missing = sorted(set(required) - present)
    if missing:
        raise ValueError(f"missing required columns: {', '.join(missing)}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_files(paths: Iterable[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted((p.resolve() for p in paths), key=str):
        digest.update(str(path).encode("utf-8"))
        digest.update(bytes.fromhex(sha256_file(path)))
    return digest.hexdigest()


def save_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def fixed_range_normalize(
    values: np.ndarray,
    feature_specs: list[tuple[str, float, float]],
) -> np.ndarray:
    """Clip physical features to fixed deployment ranges and map to [-1, 1]."""

    values = np.asarray(values, dtype=np.float32)
    if values.ndim != 2 or values.shape[1] != len(feature_specs):
        raise ValueError(
            f"expected [N,{len(feature_specs)}] features, got {values.shape}"
        )
    low = np.asarray([item[1] for item in feature_specs], dtype=np.float32)
    high = np.asarray([item[2] for item in feature_specs], dtype=np.float32)
    clipped = np.clip(values, low, high)
    return (2.0 * (clipped - low) / (high - low) - 1.0).astype(np.float32)


def split_grouped(
    groups: Iterable[Any],
    seed: int,
    validation_fraction: float = 0.15,
    test_fraction: float = 0.15,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Split by complete condition/curve groups to prevent trajectory leakage."""

    group_array = np.asarray([str(item) for item in groups])
    unique_groups = np.unique(group_array)
    if len(unique_groups) < 3:
        raise ValueError("at least three unique groups are required for train/val/test")

    rng = np.random.default_rng(seed)
    shuffled = unique_groups.copy()
    rng.shuffle(shuffled)
    test_count = max(1, int(round(len(shuffled) * test_fraction)))
    validation_count = max(1, int(round(len(shuffled) * validation_fraction)))
    if test_count + validation_count >= len(shuffled):
        test_count = 1
        validation_count = 1

    test_groups = set(shuffled[:test_count])
    validation_groups = set(shuffled[test_count : test_count + validation_count])
    train_groups = set(shuffled[test_count + validation_count :])

    train = np.flatnonzero(np.isin(group_array, list(train_groups)))
    validation = np.flatnonzero(np.isin(group_array, list(validation_groups)))
    test = np.flatnonzero(np.isin(group_array, list(test_groups)))
    if min(len(train), len(validation), len(test)) == 0:
        raise ValueError("group split produced an empty partition")
    return train, validation, test


def export_int8_tflite(
    model: Any,
    representative_x: np.ndarray,
    output_path: Path,
) -> dict[str, Any]:
    """Export a fully integer TFLite model and return its concrete I/O contract."""

    import tensorflow as tf

    representative_x = np.asarray(representative_x, dtype=np.float32)

    def representative_dataset():
        limit = min(len(representative_x), 512)
        for row in representative_x[:limit]:
            yield [row[np.newaxis, :]]

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    model_bytes = converter.convert()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(model_bytes)

    interpreter = tf.lite.Interpreter(model_path=str(output_path))
    interpreter.allocate_tensors()

    def tensor_contract(detail: dict[str, Any]) -> dict[str, Any]:
        scale, zero_point = detail["quantization"]
        return {
            "name": detail["name"],
            "shape": [int(item) for item in detail["shape"]],
            "dtype": np.dtype(detail["dtype"]).name,
            "scale": float(scale),
            "zero_point": int(zero_point),
        }

    return {
        "sha256": sha256_file(output_path),
        "size_bytes": output_path.stat().st_size,
        "inputs": [tensor_contract(item) for item in interpreter.get_input_details()],
        "outputs": [tensor_contract(item) for item in interpreter.get_output_details()],
    }


def run_int8_tflite(model_path: Path, x: np.ndarray) -> np.ndarray:
    """Run a fully integer TFLite model and dequantize its single output."""

    import tensorflow as tf

    interpreter = tf.lite.Interpreter(model_path=str(model_path))
    interpreter.allocate_tensors()
    input_detail = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()
    if len(output_details) != 1:
        raise ValueError("training pipeline requires one deployment output tensor")

    input_scale, input_zero = input_detail["quantization"]
    if input_scale <= 0.0:
        raise ValueError("INT8 input quantization scale is invalid")
    x = np.asarray(x, dtype=np.float32)
    results: list[np.ndarray] = []
    for row in x:
        quantized = np.rint(row / input_scale + input_zero)
        quantized = np.clip(quantized, -128, 127).astype(np.int8)
        interpreter.set_tensor(input_detail["index"], quantized[np.newaxis, :])
        interpreter.invoke()
        detail = output_details[0]
        scale, zero_point = detail["quantization"]
        raw = interpreter.get_tensor(detail["index"])[0].astype(np.float32)
        results.append((raw - float(zero_point)) * float(scale))
    return np.asarray(results, dtype=np.float32)


def classification_metrics(
    truth: np.ndarray,
    probabilities: np.ndarray,
    labels: list[str],
) -> dict[str, Any]:
    from sklearn.metrics import accuracy_score, confusion_matrix, f1_score

    truth = np.asarray(truth, dtype=np.int64)
    predicted = np.argmax(probabilities, axis=1)
    return {
        "accuracy": float(accuracy_score(truth, predicted)),
        "macro_f1": float(f1_score(truth, predicted, average="macro", zero_division=0)),
        "confusion_matrix": confusion_matrix(
            truth, predicted, labels=np.arange(len(labels))
        ).astype(int).tolist(),
        "labels": labels,
    }
