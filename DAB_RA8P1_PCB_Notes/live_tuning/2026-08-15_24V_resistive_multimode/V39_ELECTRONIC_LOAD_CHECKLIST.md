# v39 electronic-load commissioning checklist

## Firmware identity and zero-power checks

1. Manually flash both S-records listed in `firmware_v39_manifest.json`; do not energize the bench during download.
2. Confirm the boot identity is `2026-08-15-dualcore-energy-control-v39` and the application reaches `READY` with PWM/RUN/path outputs off.
3. Run `show`, `protect show`, `energy show`, `mppt show`, and `bat show`. CPU1 IPC must report peer seen, not stale. `protect show` must report `BENCH_ABSOLUTE_ONLY`.
4. Before selecting a new controller, confirm `arch show` reports `VOLTAGE_PI_SINGLE_LOOP`, `energy show` reports energy OFF, and `alglog show` reports OFF. These are the legacy-function compatibility gates.
5. With all power paths off and both bus currents physically zero, run `zero`. Do not close a current loop if IOUT polarity, zero or gain is inconsistent with an external meter.
6. Send `alglog on`, then `save`; preserve both `DAB:` and `ALG:` lines. They join by `seq` and must never be independently resampled. Send `alglog off` when returning to yesterday's DAB-only capture workflow.

For modulation-model dataset preparation, `ai mod sps|eps|tps CONFIDENCE [TTL_MS]` stores a proposed mode in `ALG:ai_mod_hint`. It deliberately does not switch the bridge in v39; the requested/effective modulation fields already present in `DAB:` remain the physical ground truth.

## First output-current-loop gate

Use the electronic load in CV at 36 V, the source at 24 V, and conservative source/load current limits. Start with:

```text
off
protect bench
arch current
iref 0.50
ikp 2.0
iki 0.20
islew 0.20
p 100
cap 80
mod sps
save
on
```

Compare external output current with firmware IOUT and verify the phase rises smoothly. Stop immediately on wrong polarity, persistent current error growth, oscillation, abnormal sound, hardware fault input, or unexpected temperature rise. Only after a stable 0.5 A point may the reference advance to 1 A and 2 A. Tune `ikp`, `iki`, and `islew` at each accepted point and save the final values in the condition record.

## Battery-state-machine emulation

The electronic load is an emulator, not a pack or BMS. Example 10S Li-ion CC-stage setup:

```text
off
bat profile liion10
bat capacity 10
bat imax 1.0
bat ipre 0.5
bat iterm 0.30
bat temp 25
energy charge
bat start
arch energy
save
on
```

At a CV load below 42 V the charger should request CC. Moving the emulator to 42 V exercises CV/taper logic; an electronic load does not reproduce SOC-dependent battery taper automatically, so stage transitions must be induced deliberately and labelled as emulator tests. Use `lfp12` for 43.8 V and `lead36` for 43.2 V absorption/40.95 V float. Manufacturer and BMS limits always override the generic profiles.

## MPPT boundary

`mppt fixed W` can check power-reference plumbing with a stiff supply, but P&O or incremental-conductance tracking is not validated by a fixed-voltage supply. Dynamic MPPT evidence requires a PV-array simulator, programmable I-V curve, or documented source-droop model.

## Evidence boundary

The v39 builds and static verifier passed on 2026-08-15. No v39 flash, boot, UART, current-loop, electronic-load, MPPT or battery-emulator physical result has yet been obtained.
