# Photovoltaic-storage DAB direction and electronic-load plan

## Project focus

The primary product objective is no longer a resistive-load voltage regulator. The DAB should become an isolated bidirectional power-processing stage for photovoltaic MPPT, battery charging/discharging and DC-bus energy management. Voltage accuracy remains a protection and battery-CV requirement, but modulation selection should optimize efficiency, soft switching, RMS current, thermal stress and ripple while meeting the requested power or charge current.

## Control ownership

- With the electronic load in CV mode, the load owns the simulated battery-terminal voltage. The DAB must control charge current or power. Do not run two tight output-voltage loops against each other.
- With the load in CC mode, use the DAB voltage/DC-bus loop for voltage-loop characterization. Do not simultaneously command the same current with two controllers.
- CR mode remains useful for reproducible loss maps and comparison with the existing resistor results, but it is not the main product control problem.
- CP mode, if available, is useful for DC-bus and converter-interaction tests; use bounded power steps because a constant-power load has negative incremental impedance.

## Recommended product control hierarchy

1. A fast inner transformer/port-current loop controls signed power flow and enforces peak/RMS current constraints.
2. A battery supervisor produces charge/discharge current limits from battery chemistry, pack voltage, temperature, SOC, SOH and BMS limits.
3. A CC/CV charge state machine selects precharge, constant-current, constant-voltage, taper/termination and maintenance states. Exact thresholds come from the cell/pack manufacturer and BMS, not from a generic chemistry label alone.
4. A photovoltaic MPPT loop controls PV-side voltage or power and generates the available positive power reference. Incremental-conductance or perturb-and-observe is a deterministic baseline; a learned MPPT policy can be compared later.
5. An energy-management layer arbitrates PV availability, battery limits, DC-bus demand and requested direction.
6. A slow modulation supervisor selects SPS/EPS/TPS and a bounded parameter bundle. It never bypasses the deterministic current loop or protection layer.

## Battery-chemistry support

- Lithium-ion families: low-voltage precharge when required, CC charge, chemistry-specific CV ceiling, taper-current termination, temperature and BMS interlocks. LFP, NMC/NCA and LTO require separate validated tables.
- Lead-acid: bulk current, absorption voltage/time, float voltage and temperature compensation; AGM, gel and flooded profiles remain separate.
- Every profile stores pack series count, voltage/current/temperature limits, SOC/SOH constraints, taper and timeout rules, and calibration revision.

## SPS/EPS/TPS training objective

Each physical operating point must test all three modes after per-mode tuning. Suggested features are Vin, battery/DC-bus voltage, signed current/power request, voltage ratio, direction, temperature, SOC/SOH, battery type, phase and inner shifts, measured RMS/peak current, ripple and ZVS indicators. The label is the mode and parameter bundle minimizing measured loss subject to tracking, current, thermal, ripple and transition constraints.

A suitable score is dominated by external measured efficiency, then penalizes target error, RMS/peak current, loss of ZVS, temperature rise, ripple and mode-transition cost. Saturated or calibration-invalid runs remain negative feasibility records and are never positive efficiency labels.

## Electronic-load plan for the next session

### Measurement gate

1. Verify external Vin, Iin, Vout and Iout simultaneously, or use calibrated input/output power meters.
2. Confirm the physical load resistance/current against firmware sensing; the current 6.5-ohm record is inconsistent with firmware Iout.
3. Flash v39 CPU0 and CPU1 manually and confirm `2026-08-15-dualcore-energy-control-v39`, schema v39, CPU1 energy-reference validity, READY state and BENCH absolute-protection banner before power transfer.

### CV battery-emulator matrix

- CV anchors: 36 V, 40 V, 44 V and 48 V.
- Input anchors: 18 V, 21 V and 24 V.
- Signed current/power layers: begin with 100 W, then 200 W and 300 W; add 400 W only after external power and thermal results pass.
- For each condition, command DAB current or power, then tune SPS, EPS and TPS under the identical CV/load condition.
- Record external efficiency, RMS/peak transformer current, temperatures, ripple, phase/inner shifts, controller gains, saturation and fault state.

The v39 image implements an output-current PI on CPU0 and deterministic MPPT/charger supervision on CPU1 while preserving the v38 voltage PI. These paths have passed builds and static verification only. Tomorrow's first gate is output-current sensor polarity/gain validation at low current; only then may `arch current` or `arch energy` be treated as physically closed-loop evidence.

## MPPT validation requirement

A fixed bench supply cannot validate MPPT. Use a solar-array simulator, a programmable source with an I-V curve, or a controlled source droop model. MPPT should move the PV operating voltage/current reference while the DAB current loop transfers the requested power within battery and converter limits.

## v39 UART preparation

- Direct current loop: `arch current`, `iref`, `ikp`, `iki`, `islew`.
- CPU1 supervisor: `arch energy`, `energy off|manual|mppt|charge|solar`.
- MPPT: `mppt po|inc|off`, plus `step`, `interval`, `vmin`, `vmax`, `pmax`, and `fixed`.
- Battery: `bat profile liion10|lfp12|lead36|custom`; every capacity, current, voltage, dwell, timeout, external temperature and CV-loop gain remains UART-adjustable.
- AI extension: `ai assist`, `ai hint CURRENT_A CONFIDENCE [TTL_MS]`, and record-only `ai mod sps|eps|tps CONFIDENCE [TTL_MS]`; no model is linked, current advice cannot raise the deterministic ceiling, and stale/low-confidence advice falls back to the local algorithm.
- Test guard: `protect bench` removes soft test interference; `protect normal` restores layered limits. Neither command bypasses board hardware faults or absolute software limits.
- `save` emits the unchanged `DAB:` record and a second `ALG:` record with the same ADC sequence join key.
