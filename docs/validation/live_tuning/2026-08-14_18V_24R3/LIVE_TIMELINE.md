# Live tuning timeline

## Configuration

- Input supply: 18 V, CV.
- Output load: 24.3 ohm.
- Firmware: v32 live phase override, 299.76 kHz, 400 ns deadtime.
- Powered mode used for this sweep: SPS only.
- Raw UART mode: `mode save`, nominal 50 ms telemetry interval.

## Observed operating points

| Outer phase | Firmware Vout | Firmware Iin | Firmware Pin | Result |
|---:|---:|---:|---:|---|
| 80 ns | about 35.07 V | about 3.16 A | about 53.2 W | Stable |
| 84 ns | about 35.09 V | about 3.16 A | about 53.3 W | Stable |
| 92 ns | about 35.12 V | about 3.16 A | about 53.3 W | Stable |
| 100 ns | about 35.16 V | about 3.17 A | about 53.5 W | External meter: 35.63 V |
| 120 ns | about 35.15 V | about 3.18 A | about 53.7 W | Plateau |
| 160 ns | about 35.09 V | about 3.17 A | about 53.5 W | Output decreased |

The bench supply was confirmed to remain in CV, so the non-monotonic response
cannot be attributed to its current-limit mode. The board was stopped after the
sweep and verified at `READY`, `PWM=0`, `RUN=0`.

## 21 V / 30 ohm staged power point

- Bench supply display: 21.04 V, CV.
- Output load: 30.0 ohm.
- Control: SPS, manual outer phase 52 ns.
- External multimeter output: 42.00 V.
- Derived resistive-load output power: 58.8 W (`42.00^2 / 30.0`).
- Firmware steady telemetry: about 19.85 V input, 41.51 V output, 3.08 A
  input, 61.1 W input, and 34.3/32.7 degC after the hold.
- No firmware fault was reported. The board was explicitly stopped and verified
  at `READY`, `PWM=0`, `RUN=0` before changing the load.

## Control conclusions

- Current 18 V to approximately 36 V, 24.3 ohm operation should remain SPS.
- Do not run the present EPS, TPS, or AUTO implementation under power.
- Static audit found that inner shift changes each bridge pulse center without
  compensating the raw outer shift. The effective power phase is:
  `outer + sec_inner/2 - pri_inner/2`.
- Future firmware should control effective phase and derive raw outer phase from
  the selected inner shifts before AUTO testing.

## Load reminders for later voltage sweeps

To keep output near 50 to 55 W, use approximately 36 ohm at 42 V and 47 ohm at
48 V, with at least a 100 W resistor/load rating. At 300 W, the nominal loads
are 4.32 ohm at 36 V, 5.88 ohm at 42 V, and 7.68 ohm at 48 V; those runs also
require a substantially higher-current input source and a new staged test plan.
