#!/usr/bin/env python3
"""Import three RUHMI outputs with model-specific file and symbol prefixes.

RUHMI emits the same model.c/sub_0000_* names for every model.  This importer
keeps the generated code auditable while making all three models linkable in
one RA8P1 image.  Test vectors, generated hal_entry.c, and generated I/O data
are deliberately excluded from the firmware.
"""

from __future__ import annotations

import argparse
import re
import shutil
from pathlib import Path


ROOT = Path(r"C:\Renesas\AI\validation\deploy_output_v2")
MODELS = {
    "mppt": ROOT
    / "mppt_gmpp_qat_int8_NPU"
    / "deploy"
    / "build"
    / "MCU"
    / "compilation"
    / "src",
    "pv_fault": ROOT
    / "pv_fault_int8_NPU"
    / "deploy"
    / "build"
    / "MCU"
    / "compilation"
    / "src",
    "thermal": ROOT
    / "thermal_qat_int8_NPU"
    / "deploy"
    / "build"
    / "MCU"
    / "compilation"
    / "src",
}

COMMON_FILES = (
    "ethosu_common.h",
    "model.c",
    "model.h",
    "sub_0000_command_stream.c",
    "sub_0000_command_stream.h",
    "sub_0000_invoke.c",
    "sub_0000_invoke.h",
    "sub_0000_model_data.c",
    "sub_0000_model_data.h",
    "sub_0000_tensors.c",
    "sub_0000_tensors.h",
)
PV_CPU_FILES = (
    "compute_sub_0001.c",
    "compute_sub_0001.h",
    "kernel_library_int.c",
    "kernel_library_int.h",
    "kernel_library_utils.c",
    "kernel_library_utils.h",
)


def prefixed_name(prefix: str, name: str) -> str:
    return f"{prefix}_{name}"


def replace_identifiers(text: str, prefix: str) -> str:
    """Prefix every generated identifier that collides across model outputs."""

    pattern = re.compile(
        r"\b("
        r"TensorInfo|"
        r"RunModel|"
        r"GetModel[A-Za-z0-9_]*|"
        r"sub_0000[A-Za-z0-9_]*|"
        r"kArenaSize_sub_0000|"
        r"buf_[A-Za-z0-9_]+|"
        r"compute_sub_0001|"
        r"compute_arena_sub_0001|"
        r"kBufferSize_sub_0001|"
        r"BufferSize_sub_0001"
        r")\b"
    )
    return pattern.sub(lambda match: f"{prefix}_{match.group(1)}", text)


def replace_includes(text: str, prefix: str, files: tuple[str, ...]) -> str:
    for name in files:
        text = text.replace(f'"{name}"', f'"{prefixed_name(prefix, name)}"')
    return text


def replace_header_guard(text: str, prefix: str, output_name: str) -> str:
    lines = text.splitlines(keepends=True)
    first_ifndef = next(
        (i for i, line in enumerate(lines) if line.startswith("#ifndef ")), None
    )
    if first_ifndef is None:
        return text
    old_guard = lines[first_ifndef].split(maxsplit=1)[1].strip()
    stem = re.sub(r"[^A-Za-z0-9]", "_", output_name).upper()
    new_guard = f"DAB_RUHMI_{prefix.upper()}_{stem}_INCLUDED"
    for i, line in enumerate(lines):
        lines[i] = re.sub(rf"\b{re.escape(old_guard)}\b", new_guard, line)
    return "".join(lines)


def import_model(prefix: str, source: Path, destination: Path) -> None:
    files = COMMON_FILES + (PV_CPU_FILES if prefix == "pv_fault" else ())
    model_destination = destination / prefix
    model_destination.mkdir(parents=True, exist_ok=True)

    for name in files:
        source_path = source / name
        if not source_path.is_file():
            raise FileNotFoundError(source_path)
        output_name = prefixed_name(prefix, name)
        text = source_path.read_text(encoding="utf-8", errors="strict")
        text = replace_includes(text, prefix, files)
        text = replace_identifiers(text, prefix)
        if name.endswith(".h"):
            text = replace_header_guard(text, prefix, output_name)
        (model_destination / output_name).write_text(
            text, encoding="utf-8", newline="\n"
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()

    destination = args.destination.resolve()
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)

    for prefix, source in MODELS.items():
        import_model(prefix, source, destination)

    print(f"Imported {sum(1 for _ in destination.rglob('*') if _.is_file())} files")


if __name__ == "__main__":
    main()
