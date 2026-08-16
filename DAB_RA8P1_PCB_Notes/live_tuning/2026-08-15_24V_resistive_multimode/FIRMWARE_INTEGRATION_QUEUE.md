# Tuned-parameter firmware integration queue

No tested operating condition is allowed to disappear into prose-only notes. The code-ready source of truth is `best_parameters_by_condition.jsonl`.

## Integration gates

1. Finish SPS, EPS, and TPS tuning for the physical operating condition.
2. Choose and record the best parameters for each mode.
3. Write the cross-mode `selected_mode` label only after the comparison gates pass.
4. Generate/update the `RA8P1_DAB_DualCore_Solution_CPU0/src/dab_tuned_parameter_table.h` contract (and generated `.c` storage when entries are promoted) with Vin, Vref, load/power layer, control architecture, mode, phase cap, Kp/Ki/Kd, feedforward, and inner shifts.
5. Build CPU0/CPU1, user flashes manually, then run a lookup-point and interpolation/boundary verification.
6. Change `code_integration_status` from `PENDING_FIRMWARE_TABLE` to `BUILT`, then `FLASH_VERIFIED` only with corresponding evidence.

## Pending entries

- `OC_VIN24_VREF43P5_NOM18_V36` / SPS revision 1: pending firmware table; mode candidate validated by firmware telemetry, external voltage pending.
- `OC_VIN24_VREF43P5_NOM18_V36` / EPS revision 2: pending firmware table; inner40/0 and p400 candidate, provisional until remaining EPS comparison is complete.
- TPS entry: not yet available; tuning in progress.

The table will be integrated after enough neighboring Vin/Vref/load points exist to define safe lookup and interpolation behavior. Until then the JSONL record is authoritative and must not be silently hand-copied.

## v37 framework status

- The header-only validated-table contract, exact-match lookup gate, runtime schedule command, bundle telemetry, and control-architecture labels are built into CPU0 v37.
- Promoted entry count intentionally remains zero. The provisional 43.5 V records were not copied into the 44 V / 200 W condition.
- Existing best-parameter records remain `PENDING_FIRMWARE_TABLE`; framework compilation alone does not change an entry to `BUILT`.
- CPU0 and CPU1 build checks passed. Manual flash and boot-banner verification are pending; see `firmware_v37_manifest.json` and `V37_FLASH_CHECKLIST.md`.
