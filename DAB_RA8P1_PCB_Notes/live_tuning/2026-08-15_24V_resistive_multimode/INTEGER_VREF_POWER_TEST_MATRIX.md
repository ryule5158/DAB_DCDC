# Integer Vref and high-power test matrix

## Immediate correction

- The 43.5 V condition is frozen as a historical regression anchor. Do not continue the main sweep at 43.5 V.
- The next target is 44 V.
- All new voltage-capability runs prioritize integer Vrefs 36 through 48 V.
- Every directly compared condition still requires separately tuned SPS, EPS, and TPS candidates before a final modulation label.

## Immediate 44 V settings

For the first approximately 200 W output layer:

- target Vout: 44 V;
- resistive load: 9.68 ohm ideal; use 10.0 ohm nominal if that is the available safe value, giving approximately 193.6 W at 44 V;
- load-bank rating: at least 300 W with forced cooling and temperature monitoring;
- input supply: 24 V and at least 10 A available at the terminals; 11 A current-limit capability is preferred for margin, but use staged current limiting during startup;
- if the present source is physically limited to 7 A, it cannot support a 200 W output test. Its theoretical input ceiling is 168 W before conversion loss, so replace/parallel it with an approved higher-current source before the 10 ohm run.

Subsequent 44 V layers:

| Output layer | Ideal load | Practical requirement at 24 V input |
| ---: | ---: | --- |
| 200 W | 9.68 ohm | source >=10 A, preferably >=11 A; load bank >=300 W |
| 300 W | 6.45 ohm | source >=15 A, preferably >=16 A; load bank >=450 W |
| 400 W | 4.84 ohm | source >=20 A, preferably >=22 A; load bank >=600 W |

The current estimates use 85--90% conversion efficiency only for source sizing; firmware Pout/Pin is not accepted as calibrated efficiency. High-power commissioning remains staged and attended because the present application auto-trip behavior is not a substitute for independent hardware protection and external instruments.

## Ideal resistance table

`R = Vout^2 / Pout`.

| Vref (V) | R for 200 W (ohm) | R for 300 W (ohm) | R for 400 W (ohm) |
| ---: | ---: | ---: | ---: |
| 36 | 6.48 | 4.32 | 3.24 |
| 37 | 6.84 | 4.56 | 3.42 |
| 38 | 7.22 | 4.81 | 3.61 |
| 39 | 7.61 | 5.07 | 3.80 |
| 40 | 8.00 | 5.33 | 4.00 |
| 41 | 8.40 | 5.60 | 4.20 |
| 42 | 8.82 | 5.88 | 4.41 |
| 43 | 9.24 | 6.16 | 4.62 |
| 44 | 9.68 | 6.45 | 4.84 |
| 45 | 10.12 | 6.75 | 5.06 |
| 46 | 10.58 | 7.05 | 5.29 |
| 47 | 11.04 | 7.36 | 5.52 |
| 48 | 11.52 | 7.68 | 5.76 |

Actual load resistance must be externally measured and saved. Do not silently use firmware Vout/Iout as the physical resistance value.

## Efficient execution order

1. Move from the completed 43.5 V anchor to 44 V at the existing safe load only as a short integer-target baseline.
2. After the user installs the 10 ohm / >=300 W bank and a source capable of at least 24 V / 10--11 A, tune SPS/EPS/TPS at 44 V and approximately 200 W.
3. At 44 V, move to 300 W and 400 W only after the preceding layer is stable and the source/load/instrument ratings are adequate.
4. Use the 10 ohm bank for the 36--48 V integer sweep; it naturally spans approximately 129.6--230.4 W. All three modulation modes remain required at each point.
5. Add the dedicated 300 W and 400 W resistance rows across the integer targets as hardware capacity permits, prioritizing 36, 40, 44, and 48 V anchors before filling intermediate integers.
6. Repeat fixed integer Vref tests while reducing Vin 24 -> 22 -> 20 -> 18 V; a Vref is not marked robust until the output stays within tolerance or the physical saturation boundary is documented.
