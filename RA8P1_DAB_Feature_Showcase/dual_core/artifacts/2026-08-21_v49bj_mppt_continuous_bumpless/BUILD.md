# v49bj continuous bumpless MPPT build

- Firmware: `2026-08-21-dualcore-mppt-continuous-bumpless-v49bj`.
- Failed live evidence: `RA8P1_DAB_UpperComputer/logs/session_20260821_031325.jsonl`.
- r145 was fault-free but failed MPPT stability: max Pin/Iin/temperature were 189.3 W / 9.14 A / 61.9 C. It first reached about 8.9 A, then repeated local topology audits entered the 832/180 ns source-unload waveform and produced 8.9 A -> 0.8 A / 185 W -> 17 W collapses lasting about 10 s. Above about 37 V, repeated audits and an incorrect knee latch also let Iin fall below the present 8.5 A acceptance target.
- v49bj keeps autonomous repeated SPS/EPS/TPS re-ranking. It does not lock the first winner. Normal local re-search inherits the live waveform and uses a 2.0 V measured-source margin before the commissioned unload waveform is allowed; the unload path remains available for a real low-Vin collapse.
- The complete three-mode audit window is shortened from roughly 20 s to roughly 9 s (`settle=1.5 s`, `observe=1.5 s`, `perturb=0.5 s`, persistent-condition confirm `0.5 s`). Physical current slew remains at or below 0.5 A/s and inner-phase slew remains at or below 0.2 ns/ms.
- A waveform-floor logical rebase is no longer allowed to teach its own same-cycle current jump as a source MPP knee. During peak hold, rebase additionally requires Vin to remain within 0.02 V of the proven knee; a real Vin sag keeps the existing gentle backoff.
- Native C11 execution passed for energy control, MPPT phase search, promoted tables, advanced control, and CAN protocol. Upper-computer tests: 30/30. Parameter generator check: charger/current/voltage entries 29/13/2.
- Dual-core Renesas LLVM build passed. CPU0 text/data/bss: `189450/560/172709`; CPU1: `18846/132/9140`.
- CPU0 ELF/SREC SHA256: `594A82CA44B8072114B30FB0A3324482E7C9249E73EEC5C47D12C2EC4FAD3E1B` / `C418E67946E4CC7C83FF47370096FA60E3724E542A85073E300812534F00C7C0`.
- CPU1 ELF/SREC SHA256: `0E798F7EE25DD6C2E4AF754F6815A831280184D44486B000FCA14674B9EA7E69` / `8263DE7E4D09C846380847915FBFD27635CE0EC770F371DA9B4C28F94476B6CE`.
- The legacy full reference-parity audit still reports 10 pre-existing clone/reference configuration differences. This is recorded as a separate baseline mismatch and is not represented as a v49bj PASS.
- Pre-flash gate from the final r145 JSONL frame: `READY / CTRL OFF / PWM OFF / charge OFF / fault0`, Vout `0.388 V`, Pin `-0.11 W`.
- J-Link/SWD flash completed against device `R7KA8P1KF_CPU1` at 4 MHz. CPU0 Bank0 programmed and verified `190464 bytes`; CPU1 Bank0 programmed and verified `20480 bytes`; both `loadfile` operations returned `O.K.`, followed by reset and run. Raw log: `DAB_RA8P1_PCB_Notes/serial_logs/2026-08-21_v49bj_mppt_continuous_bumpless_dualcore_flash.log`.
- The flash operation did not open COM4 and did not access COM24/electronic load.
- State: `FLASHED_AWAITING_UPPER_IDENTITY_AND_R146_RETEST`.
