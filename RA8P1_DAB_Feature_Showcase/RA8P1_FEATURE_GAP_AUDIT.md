# RA8P1 Feature and Report Gap Audit

## Executive result

The report already names Cortex-M85, Helium/MVE, ITCM/DTCM/non-cache SRAM,
Ethos-U55, GPT, dual ADC, DMAC and POEG.  The strongest implemented RA8P1
advantages are the deterministic PWM/ADC/protection chain, memory placement,
and the generated U55 model bridge.  Before this showcase branch, however,
Helium was only a local eight-channel conversion block, and there was no SDRAM
driver or CPU0/CPU1 application IPC protocol.

This independent project closes the software-driver portion of those gaps.  It
does not claim physical SDRAM, M33, NPU-latency or powered-DAB validation.

## Claim-to-evidence matrix

| Capability | Current report wording/effect | Evidence now in this project | Remaining gap before the report can claim full use |
| --- | --- | --- | --- |
| Helium/MVE-F | Report body page 3 and appendix describe vector ADC conversion | `dab_power_dsp.c` is called by `dab_measurements.c`; the clean Cortex-M85 build emits MVE `q`-register convert/multiply/add/min/max instructions; scalar fallback and deterministic self-test exist | Run `features dsp-test` on the board and record cycle counts versus scalar code under the same clock/cache state |
| ITCM/DTCM/non-cache SRAM | Report describes hot code/state/DMA placement | Link map places DSP and control hot functions in `.itcm_code_from_flash`, CPU-owned state in DTCM, and DMA buffers in non-cache SRAM | Capture worst-case control-loop timing and jitter on hardware; map placement alone is not timing proof |
| Ethos-U55 | Report says three INT8 models and asynchronous inference | Generated MPPT/PV-fault/thermal command streams and `ethosu_invoke_async()` bridge are linked; deterministic control remains authoritative | Record actual RA8P1 inference status, output and latency for all three models; PC/RUHMI results are not U55 board proof |
| Dual core M85+M33 | Not materially demonstrated in the report | The official `dual_core` FSP Solution builds both CPU0 and CPU1; CPU0 safely starts CPU1, and both images contain the bidirectional IPC protocol | Flash both images with the official multicore debug flow, then capture bidirectional heartbeat/fault evidence |
| SDRAM | Not currently a substantiated project advantage | `drv_ra8p1_sdram.c` provides status checks, bounded aligned allocation and a save/write/read/restore diagnostic | Confirm that the CPKHMI board actually populates SDRAM and obtain part number, capacity, width, pins and timing; configure FSP/linker; then run address/data/retention and bandwidth tests |
| Hardware CRC | Previously not highlighted | `drv_ra8p1_crc.c` performs hardware CRC-32 with a known-answer boot/command self-test | Use the CRC in the eventual MRAM parameter record and model/log headers; board execution is still required |
| GPT + ADC-B + DMAC + POEG | Central deterministic-control claim | Production code and FSP instances are present and linked | Existing build/source evidence does not replace safe powered waveform and protection-trip measurements |
| CAN FD | Pins and TCAN3413 transceiver are present; transport is not active | Board routing is documented, but only the transceiver strap helper exists | Enable CANFDCLK, add `r_canfd`, bit timing, filters, queues and a BMS/EMS protocol only after bus requirements are fixed |
| Ethernet/OSPI | Mentioned as possible communication/storage expansion | Pin and low-level placeholders exist; data planes are disabled | Confirm PHY/flash population, clocks and pin ownership before adding FSP instances |
| IWDT | Not used | Option settings leave automatic IWDT start disabled | Define which task owns liveness, startup grace, legal refresh window and reset recovery before enabling; premature enabling can create reset loops |
| MRAM and secure update | Not used as a delivered feature | No application-owned write partition or key lifecycle exists | Reserve a linker region, use redundant versioned records plus hardware CRC, and define signing/key provisioning before writing nonvolatile memory |

## Recommended dual-core ownership

CPU0/M85 keeps all hard-real-time and final safety authority:

- GPT complementary PWM and phase updates;
- ADC/DMAC frame consumption and Helium DSP;
- POEG and fast GPIO fault handling;
- deterministic voltage/current/power limits.

CPU1/M33 is useful for slow, separable work:

- CAN FD, USB/Ethernet supervisory communication;
- logging and SDRAM history management;
- slow thermal/health monitoring;
- heartbeat and fault/status reporting.

CPU1 must never be able to raise the CPU0 operator power ceiling, clear a
hardware trip, or directly generate gate timing.

## Feature activation order

1. Keep all power paths off and flash the dual-core CPU0/CPU1 images with the
   official multicore flow; record CPU1 start and bidirectional IPC heartbeat.
2. Run UART command and PWM/ADC regression against the authoritative test image.
3. Confirm the exact SDRAM device and board routing; then enable FSP SDRAM and
   set a real capacity in `dab_feature_config.h`.
4. Validate SDRAM diagnostic, aliasing, retention and bandwidth while the DAB
   remains unpowered.
5. Add CAN FD, then IWDT, then MRAM/security only with explicit communication,
   reset-recovery, storage and key-management requirements.

## Features deliberately not added

MIPI camera/display, GLCDC graphics, PDM/audio and similar multimedia blocks do
not improve DAB control, measurement, protection or maintainability.  Adding
them would consume pins, memory and verification effort without project value.

## Evidence boundary

The clean build proves source, FSP and linker integration.  Disassembly proves
that Helium/MVE instructions were emitted.  Source-level driver presence proves
that activation software exists.  None of these proves board population,
electrical timing, real-time margin, inference latency, SDRAM integrity, M33
operation or powered converter performance.
