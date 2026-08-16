# DAB live-tuning dataset: 2026-08-14, 18 V, 24.3 ohm

This directory is the canonical record for the live UART tuning session. Raw
logs are retained unchanged, while machine-readable derivatives separate
telemetry, commands, device events, and independent physical measurements.

## Layout

- `raw_serial/`: immutable copies of the UART capture logs.
- `dataset/telemetry.csv`: wide numeric table for analysis and classical ML.
- `dataset/telemetry.jsonl`: the same samples as one JSON object per line.
- `dataset/events.jsonl`: UART commands, device state messages, and parse errors.
- `dataset/commands.csv`: host-to-device interventions for causal/sequence use.
- `dataset/annotations.jsonl`: human instrument readings and experimental facts.
- `dataset/metadata.json`: hardware, firmware, units, provenance, and trust notes.
- `dataset/build_stats.json`: conversion counts and parse-error count.
- `LIVE_TIMELINE.md`: concise engineering interpretation of the run.
- `build_dataset.ps1`: reproducible raw-log converter.
- `capture_session.ps1`: writes new COM4 captures directly to `raw_serial/`
  and rebuilds the normalized dataset after the port is closed.

Do not train directly on row order across different source files without using
`device_boot_id`, `source_file`, `line_no`, `sample_index_in_file`,
`sample_index_global`, and command/event context. `device_uptime_s` is derived
from the 149880-frame/s ADC sequence counter; it is not a host wall-clock time.
Firmware ADC readings and user-reported meter readings are deliberately stored
as different sources; they are not interchangeable ground truth.
