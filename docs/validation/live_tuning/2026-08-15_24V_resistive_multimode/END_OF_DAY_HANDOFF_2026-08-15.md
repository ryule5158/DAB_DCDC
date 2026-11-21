# End-of-day handoff: 2026-08-15

## Safe state

- Last energized run ended with automatic `off`, READY, PWM=0, RUN=0, fault=0.
- Bench input remained user-reported at 24.0 V; the user reported 14.5 A during the final p400 run.
- Physical load is user-set to nominal 6.5 ohm. Do not energize from a future session until this setting and cooling are reconfirmed.

## Completed today

- At 24 V / Vref 44 V / nominal 10 ohm / 200 W layer, SPS, EPS, and TPS were each tuned before comparison.
- Provisional mode bests: SPS p400; EPS p400 with inner 0/80 ns; TPS p400 with command 10/80 ns and stable actual 12/80 ns.
- EPS produced the highest firmware Vout, 41.043 V, but no mode achieved the fixed 44 V target. No final selector label or firmware-table promotion was made.
- At nominal 6.5 ohm / 300 W layer, SPS p300 and p400 were staged. Final p400 firmware means: Vin 22.494 V, Vout 32.362 V, Pin 328.799 W, Pout 277.210 W; external input observation 24.0 V and 14.5 A.

## Required next-session gate

1. Reconfirm READY, PWM=0, RUN=0 and the physical 6.5 ohm load/cooling/source limit.
2. Measure external Vout and Iout, or power off and measure the actual load resistance. Firmware Iout/Pout currently conflicts with Vout squared divided by 6.5 ohm.
3. Do not train on the final point as a validated 300 W output label. It is valid only as a 300 W-class input/staging record until calibration is resolved.
4. After the measurement gate, tune EPS and TPS at the same 300 W-layer condition before selecting a mode.

All raw UART evidence is append-only in `raw_serial/session.log`. Structured trials and parameter candidates are in `annotations.jsonl`, `mode_tuning_trials.jsonl`, and `best_parameters_by_condition.jsonl`.

## Final diagnosis

- Do not tune PID first: the voltage loop was saturated throughout both 300 W-layer stable windows.
- `p 400` currently caps the loop at 160 ns even though the compiled absolute phase model reaches 480 ns. Any expansion requires protection restoration and oscilloscope validation; it is not authorized by this handoff.
- Physical efficiency is not determined. Firmware-only efficiency is 84.31%; firmware Pout divided by the user-reported 348 W input is 79.66%; nominal 6.5 ohm power calculated from firmware Vout would imply 46.30%. These cannot all describe the same calibrated plant.
- Treat the primary failure as software control-authority/calibration incompleteness plus an uncharacterized high-current plant limit. No hardware defect was proven today.
- A redundant final serial `off` confirmation failed because COM4 was absent. The immediately preceding complete run had already confirmed automatic `off`, READY, PWM=0, RUN=0.
