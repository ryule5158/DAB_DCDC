# RA8P1 DAB Feature Showcase

This project is an independent copy of `RA8P1_DAB_Production`.  It exists so
RA8P1-specific experiments cannot contaminate the live commissioning project
or the clean production baseline.

## Implemented in this copy

| RA8P1 capability | DAB use | Implementation | Default state |
| --- | --- | --- | --- |
| Cortex-M85 Helium/MVE-F | Eight-lane ADC conversion and four-lane power-signal window statistics | `dab_power_dsp.c`; scalar fallback and deterministic self-test included; the ADC measurement path calls this driver | On when the compiler defines MVE-F |
| ITCM/DTCM/non-cache SRAM | Low-jitter hot code/state and coherent DMA buffers | Existing `dab_accel.h` section policy | On |
| Cortex-M33 second core | Slow monitoring, communications, logging and independent status/fault reporting without adding jitter to CPU0 PWM control | `drv_ra8p1_ipc.c`; hardware FIFO messages, sequence, heartbeat, stale-peer detection and fault report | Off until a matching CPU1 project is built |
| External SDRAM controller | Long waveform capture, trend history and larger AI working buffers | `drv_ra8p1_sdram.c`; controller/status checks, aligned bounded allocator, and save/write/read/restore test | Off until the board memory device and timing are confirmed |
| Hardware CRC | Fast integrity checks for parameter records, logs and model blobs | `drv_ra8p1_crc.c`; CRC-32/ISO-HDLC and boot/command self-test against `ABCDEFGHIJKLMNOP` | On |
| Ethos-U55 | MPPT, PV-fault and thermal advisory inference | Existing generated-model bridge and asynchronous supervisor | Driver ready; application opt-in |
| GPT + ADC + DMAC + POEG | Deterministic PWM/ADC chain and hardware output shutdown | Existing production drivers | On |

## Safety and activation gates

- `src/dab_feature_config.h` is the only feature-policy switch file.
- SDRAM remains at zero capacity and disabled.  Do not enable it from the MCU
  data sheet alone: confirm the CPKHMI schematic/BOM, data width, address pins,
  clock pin, capacity and timing, then configure SDRAM and pins in FSP first.
- `R_BSP_SdramInit()` is owned by FSP startup and must run only once.  The
  application driver never calls it again.
- The SDRAM test is available only through `features sdram-test`, and the shell
  requires READY, no fault, control OFF and all power paths forced OFF.
- IPC remains disabled until a separate M33/CPU1 image implements the same
  32-bit protocol.  CPU0 keeps PWM, ADC, POEG and all final protection authority.
- The M33 is intended for CAN/Ethernet/USB supervision, logging and slow health
  checks.  It must not generate gate timing or override CPU0/hardware trips.

## Shell commands

```text
features
features dsp-test
features crc-test
features sdram-test
features ipc-ping
```

`features` reports compile-time acceleration and runtime hardware readiness;
it does not claim that an unpopulated peripheral has been physically tested.

## Other RA8P1 features screened for this power project

- High value next steps: IWDT for application-liveness shutdown, CAN FD for
  BMS/EMS, data-flash/MRAM redundant parameter records with CRC, and signed
  update/configuration verification using the security engine.
- Useful only if board routing is confirmed: OSPI for model/log storage and
  Ethernet/RMII for plant telemetry.
- Not justified for the DAB controller: camera/MIPI, display/GLCDC, audio/PDM
  and graphics-oriented peripherals.  They add no control or safety value.

Build success proves source/toolchain integration only.  Instruction inspection
can prove that MVE opcodes were emitted.  Neither replaces timing measurements,
SDRAM hardware tests, a running M33 image, or powered DAB validation.

## Official dual-core derivative

The independent `dual_core` derivative now contains an FSP 6.4.0 Flat/FreeRTOS
Solution with buildable CPU0 and CPU1 images.  CPU0 preserves the authoritative
test firmware and adds only safe-start CPU1 launch, IPC service and LLVM stdio
routing.  CPU1 runs IPC, hardware-CRC and SDRAM-service responsibilities as
separate RTOS work.  SDRAM remains disabled pending the populated part and
timing data.  See `dual_core/RA8P1_DAB_DUAL_CORE_IMPLEMENTATION.md` for the
source-equivalence, clock, pin, build and Helium-disassembly evidence.
