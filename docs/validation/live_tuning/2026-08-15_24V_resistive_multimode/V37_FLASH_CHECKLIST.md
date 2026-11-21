# v37 manual flash checklist

## Before flashing

- Keep the DAB power path OFF. Do not energize the converter during flashing.
- Flash both cores with the existing dual-core e2 studio workflow. CPU0 contains the v37 application changes; CPU1 is the verified companion image.

## Artifacts

- CPU0 ELF: `RA8P1_DAB_Feature_Showcase/dual_core/RA8P1_DAB_DualCore_Solution_CPU0/Debug/RA8P1_DAB_DualCore_Solution_CPU0.elf`
- CPU1 ELF: `RA8P1_DAB_Feature_Showcase/dual_core/RA8P1_DAB_DualCore_Solution_CPU1/Debug/RA8P1_DAB_DualCore_Solution_CPU1.elf`
- Hashes and S-record paths are recorded in `firmware_v37_manifest.json`.

## Expected post-flash evidence

1. Boot banner contains `2026-08-15-dualcore-schedule-jsonl-v37`.
2. Reset state remains power OFF; no automatic start occurs.
3. `bundle show` reports `VOLTAGE_PI_SINGLE_LOOP`, Kp=20, Ki=5, Kd=0, VinSet=24 V, Vref=44 V, Rload=10 ohm, and requested layer=200 W.
4. `sched show` reports zero promoted entries and schedule disabled.
5. `arch current` or `arch power` is rejected pending current-sensor validation.
6. `mode save` prints the v37 two-part field declaration before numeric telemetry.

After these checks, the automated capture/tuning sequence will explicitly set the condition and controller bundle again before the first 44 V run. A successful build or flash is not physical power validation.
