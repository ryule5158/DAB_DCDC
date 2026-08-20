# v49bg continuous MPPT re-search build

- Firmware: `2026-08-21-dualcore-continuous-mppt-research-v49bg`.
- r143 root cause: v49bf treated the winning outer phase as a permanent cap and disabled Vout/Pin-driven mode re-ranking. After EPS was selected near 34.7 V, the 172 ns cap prevented the current loop from following the P&O reference as battery voltage rose; Iout fell from about 4.8 A to 1.3 A before CV.
- PRECHARGE -> CC now inherits the actually applied TPS/EPS/SPS waveform. The fused CC scheduler uses the commissioned TPS832/180 ns seed instead of requesting SPS at entry.
- While acquiring the first source knee, an inherited TPS/EPS waveform is opened only when the outer loop is saturated and measured Iout trails the P&O current request; the inner target changes by at most 32 ns per second and still passes through the existing 0.2 ns/ms physical slew.
- A healthy P&O current reversal no longer unloads to the TPS recovery waveform. The recovery waveform remains mandatory only for a genuine source-voltage collapse.
- The selected outer phase is now the initial HOLD cap plus 16 ns headroom, not a permanent ceiling. If the current loop is saturated below the P&O request, the cap is released by 8 ns every 250 ms up to the existing 480 ns platform limit.
- Local mode re-ranking is restored after a persistent material Vin, Vout, or available-power change, or after the full 480 ns authority remains insufficient. It inherits the live current reference and applied waveform; it does not restart from a fixed power command or low-power host state.
- No fixed-watt MPPT command, host phase command, current slew increase, hardware limit increase, or electronic-load access was added.
- Updated phase-search test source passed Renesas ARM C11 `-Wall -Wextra -Werror` compilation. The current Windows environment still has no executable native C test compiler, so phase assertions are not claimed as executed.
- Unchanged energy-state-machine host assertions passed. Promoted-table host assertions passed. Parameter generator `--check` passed with charger/current/voltage entries `29/13/2`.
- Upper-computer regression: `30/30` passed.
- Dual-core build passed. CPU0 text/data/bss: `188850/560/172697`; CPU1: `18658/132/9136`.
- CPU0 ELF/SREC SHA256: `155B5E30FFF0075DF3DB605979AC3B56AFD06EBD88B8DF39D375EFD4A2C440F7` / `9A8FF2EF1E3D6217009F8BB924E483085F60E52365DB0F25EEE6AD92F3C018E4`.
- CPU1 ELF/SREC SHA256: `3568EEE0B4666968A11F12BF60618D86EC79F357FE5654D973D321692FC58945` / `0D89F3ADFB83E54459BDEAC34FFF979727443E4B4007CA4A3312A6220429E25E`.
- J-Link dual-core flash and verify passed. CPU0 Bank0 programmed and verified `190464 bytes`; CPU1 contents matched.
- Post-flash query confirmed v49bg, `READY / CTRL OFF / PWM=0 / GPT_RUN=0`, every power path off, `fault=0`, LFP `Vpre=32.40 V / Vcv=43.80 V`, and deterministic local AI-OFF fallback. COM4 was released immediately and COM24 was not accessed.
- Hardware state: `FLASHED_READY_AWAITING_R143_SAME_TRAJECTORY_RETEST`; r143 remains failed until the same continuous LFP trajectory passes physically.
