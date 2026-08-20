#ifndef DAB_FIRST_POWER_CONFIG_H
#define DAB_FIRST_POWER_CONFIG_H

#include "dab_adc_dma_config.h"

/*
 * Conservative first-power configuration for the populated DAB board.
 *
 * The fixed pin assignment is implemented in dab_board.c and dab_pwm_hw.c.
 * Do not change pins here.  Normal commissioning parameters may be adjusted
 * here or through the bounded UART commands implemented by the application.
 */

#define DAB_FW_VERSION                         "2026-08-21-dualcore-continuous-mppt-research-v49bg"

/* CP target/current startup is deliberately no faster than the already
 * hardware-qualified charge/MPPT transition rule. */
#define DAB_CP_CURRENT_REFERENCE_SLEW_A_PER_S  (0.50F)

/*
 * Peripheral qualification is complete, so this build permits only the
 * existing bounded, DEBUG-UART-armed first-power sequencer.  Power remains OFF
 * after reset and every runtime hardware/measurement protection stays active.
 */
#define DAB_ALLOW_POWER_TRANSFER               (1U)

/*
 * Single-gate diagnosis is a no-power scope aid.  The command is rejected
 * while either measured DC bus is energized, drives only one UCC21520 input,
 * and automatically returns every gate and RUN_REQ LOW after the short pulse.
 */
#define DAB_GATE_DIAG_MAX_VIN_V                (12.0F)
#define DAB_GATE_DIAG_MAX_VOUT_V               (5.0F)
#define DAB_PWM8_DIAG_MAX_VIN_V                 (2.0F)
#define DAB_PWM8_DIAG_MAX_VOUT_V                (2.0F)
#define DAB_GATE_DIAG_PULSE_MS                 (100UL)

/*
 * Primary diagnosis uses the production 300 kHz complementary GPT schedule
 * for the full Q1..Q4 bridge or one selected Q1/Q2 or Q3/Q4 half bridge.  Every
 * unselected input remains forced GPIO LOW.  Timed and manual-stop continuous
 * forms start directly without ADC, bus-voltage, or software-fault blocking.
 */
#define DAB_PRIMARY_TEST_MIN_MS                 (10UL)
#define DAB_PRIMARY_TEST_DEFAULT_MS             (100UL)
#define DAB_PRIMARY_TEST_MAX_MS                 (500UL)

/*
 * Secondary diagnosis is deliberately timed-only.  It requires both measured
 * buses below the PWM8 no-power limits, keeps all power-path MOSFETs open, and
 * always returns RUN_REQ and all eight PWM inputs LOW after the bounded pulse.
 */
#define DAB_SECONDARY_TEST_MIN_MS               (20UL)
#define DAB_SECONDARY_TEST_DEFAULT_MS           (100UL)
#define DAB_SECONDARY_TEST_MAX_MS               (200UL)
#define DAB_ADC_CROSSCHECK_SAMPLES               (32UL)
#define DAB_ADC_CROSSCHECK_INTERVAL_US           (100UL)
#define DAB_NOPOWER_DIAG_ARM_MS                  (15000UL)
#define DAB_LIVE_BURST_MAX_PHASE_NS              (80UL)
#define DAB_LIVE_BURST_MIN_MS                    (1UL)
#define DAB_LIVE_BURST_SHORT_MAX_MS              (50UL)
#define DAB_LIVE_BURST_LONG_PHASE_MAX_NS         (32UL)
#define DAB_LIVE_BURST_MAX_MS                  (15000UL)
#define DAB_LIVE_BURST_IDLE_IIN_MAX_A            (0.5F)
#define DAB_LIVE_BURST_IDLE_IOUT_MAX_A           (0.5F)
#define DAB_LIVE_BURST_IDLE_BRIDGE_MAX_A         (1.0F)

/*
 * Software-protection profile.
 *
 * REFERENCE_TEST is bit-for-bit equivalent at run time to the long-used
 * RA8P1_Project1 test build: the application never turns power off
 * automatically.  ALL_ENABLED restores every application-level automatic
 * check below.  A build-system -D for any individual switch takes precedence,
 * so commissioning can also enable or bypass one mechanism at a time.
 *
 * These switches only select application-software checks.  They do not alter
 * the FSP GPT/POEG output-disable setting.  The current v32 reference-test FSP
 * configuration deliberately sets that clamp to PROHIBITED; restoring it is a
 * separate configuration change.  Protection outside the MCU is unaffected.
 */
#define DAB_SW_PROTECTION_PROFILE_REFERENCE_TEST       (0U)
#define DAB_SW_PROTECTION_PROFILE_ALL_ENABLED          (1U)

/*
 * Runtime shutdown is owned by the operator while all fault inputs remain
 * observable.  This changes MCU policy only; external hardware protection is
 * unaffected.
 */
#define DAB_MANUAL_STOP_TEST_MODE                       (1U)

#ifndef DAB_SW_PROTECTION_PROFILE
 #define DAB_SW_PROTECTION_PROFILE                     DAB_SW_PROTECTION_PROFILE_REFERENCE_TEST
#endif

#if (DAB_SW_PROTECTION_PROFILE == DAB_SW_PROTECTION_PROFILE_REFERENCE_TEST)
 #define DAB_SW_PROTECTION_DEFAULT                     (0U)
#elif (DAB_SW_PROTECTION_PROFILE == DAB_SW_PROTECTION_PROFILE_ALL_ENABLED)
 #define DAB_SW_PROTECTION_DEFAULT                     (1U)
#else
 #error "Unsupported DAB software-protection profile"
#endif

#ifndef DAB_SOFTWARE_AUTO_TRIP_ENABLE
 #define DAB_SOFTWARE_AUTO_TRIP_ENABLE                 DAB_SW_PROTECTION_DEFAULT
#endif

/*
 * During first-power commissioning P400/P403 are observed in status output,
 * but they do not latch a software trip.  Protection inputs remain visible in
 * telemetry; they are not application shutdown paths in manual-stop mode.
 */
#ifndef DAB_GPIO_OC_TRIP_ENABLE
 #define DAB_GPIO_OC_TRIP_ENABLE                       DAB_SW_PROTECTION_DEFAULT
#endif

/*
 * Current guards are active in v38.  The widened bus-current ceilings cover
 * the 400 W input envelope; calibration error can only cause an early trip,
 * not silently remove the trip path.
 */
#ifndef DAB_ADC_CURRENT_GUARDS_ENABLE
 #define DAB_ADC_CURRENT_GUARDS_ENABLE                 DAB_SW_PROTECTION_DEFAULT
#endif

/* Commissioning supply droop and no-rise are telemetry, not automatic trips. */
#ifndef DAB_VIN_UV_TRIP_ENABLE
 #define DAB_VIN_UV_TRIP_ENABLE                       DAB_SW_PROTECTION_DEFAULT
#endif
#ifndef DAB_NO_RISE_TRIP_ENABLE
 #define DAB_NO_RISE_TRIP_ENABLE                      DAB_SW_PROTECTION_DEFAULT
#endif

/*
 * POEG remains present in the FSP stack for observation, but it is deliberately
 * not opened or consulted by this manual-stop test image.  The generated GPT
 * configuration also prohibits POEG output disable.
 */
#ifndef DAB_POEG_PWM_SHUTDOWN_ENABLE
 #define DAB_POEG_PWM_SHUTDOWN_ENABLE                  (0U)
#endif

#if ((DAB_SOFTWARE_AUTO_TRIP_ENABLE > 1U) || \
     (DAB_GPIO_OC_TRIP_ENABLE > 1U) || \
     (DAB_ADC_CURRENT_GUARDS_ENABLE > 1U) || \
     (DAB_VIN_UV_TRIP_ENABLE > 1U) || \
     (DAB_NO_RISE_TRIP_ENABLE > 1U) || \
     (DAB_POEG_PWM_SHUTDOWN_ENABLE > 1U) || \
     (DAB_MANUAL_STOP_TEST_MODE > 1U))
 #error "DAB software-protection switches must be 0 or 1"
#endif

#if DAB_MANUAL_STOP_TEST_MODE && \
    (DAB_SOFTWARE_AUTO_TRIP_ENABLE || DAB_GPIO_OC_TRIP_ENABLE || \
     DAB_ADC_CURRENT_GUARDS_ENABLE || DAB_VIN_UV_TRIP_ENABLE || \
     DAB_NO_RISE_TRIP_ENABLE || DAB_POEG_PWM_SHUTDOWN_ENABLE)
 #error "Manual-stop test mode requires every MCU automatic shutdown path disabled"
#endif

/* GPT0/1/5/8 all use the 250 MHz GPT clock. */
#define DAB_GPT_CLOCK_HZ                       (250000000UL)
#define DAB_PWM_TARGET_HZ                      (300000UL)
#define DAB_PWM_PERIOD_COUNTS                  (834UL)
#define DAB_PWM_HALF_COUNTS                    (DAB_PWM_PERIOD_COUNTS / 2UL)
#define DAB_PWM_ACTUAL_HZ                      (DAB_GPT_CLOCK_HZ / DAB_PWM_PERIOD_COUNTS)
#define DAB_PWM_COUNT_NS                       (4UL)
/* Commissioning margin for both normal ON and primary-only diagnosis: 400 ns. */
#define DAB_DEADTIME_NS                        (400UL)
#define DAB_DEADTIME_COUNTS                    (100UL)
#define DAB_PWM_EDGE_GUARD_COUNTS              (1UL)
#define DAB_ADC_TRIGGER_COUNTS                 (DAB_PWM_PERIOD_COUNTS / 4UL)

/* One control update for every three coherent ADC-DMA frames. */
#define DAB_ADC_FRAME_RATE_HZ                  \
    (DAB_PWM_ACTUAL_HZ / DAB_ADC_TRIGGER_DIVIDER)
#define DAB_CONTROL_FRAME_DIVIDER              (3UL)
#define DAB_CONTROL_DT_S                       \
    ((float) DAB_CONTROL_FRAME_DIVIDER / (float) DAB_ADC_FRAME_RATE_HZ)
#define DAB_ADC_STALE_TIMEOUT_MS                (20UL)

/*
 * The Simulink outer loop uses 20 ns/V and 5 us/(V*s).  At a 4 ns GPT
 * quantum these become 5 counts/V and 1250 counts/(V*s).
 */
#define DAB_KP_DEFAULT_NS_PER_V                 (20.0F)
#define DAB_KI_DEFAULT_US_PER_VS                (5.0F)
#define DAB_KP_MIN_NS_PER_V                     (0.0F)
#define DAB_KP_MAX_NS_PER_V                     (40.0F)
#define DAB_KI_MIN_US_PER_VS                    (0.0F)
#define DAB_KI_MAX_US_PER_VS                    (10.0F)
#define DAB_KD_DEFAULT_NS_S_PER_V               (0.0F)
#define DAB_KD_MIN_NS_S_PER_V                   (0.0F)
#define DAB_KD_MAX_NS_S_PER_V                   (0.1000F)
/* Derivative is taken on filtered Vout to avoid a Vref-change kick. */
#define DAB_DERIVATIVE_FILTER_ALPHA             (0.0500F)

/* Direct secondary/output-current PI.  CPU0 executes this in the same ITCM
 * frame-synchronous path as the original voltage PI. */
#define DAB_CURRENT_KP_DEFAULT_NS_PER_A          (8.0F)
#define DAB_CURRENT_KI_DEFAULT_US_PER_AS         (1.0F)
#define DAB_CURRENT_KP_MIN_NS_PER_A              (0.0F)
#define DAB_CURRENT_KP_MAX_NS_PER_A              (80.0F)
#define DAB_CURRENT_KI_MIN_US_PER_AS             (0.0F)
#define DAB_CURRENT_KI_MAX_US_PER_AS             (20.0F)
#define DAB_CURRENT_REFERENCE_MAX_A              (18.0F)
#define DAB_CURRENT_REFERENCE_SLEW_DEFAULT_A_PER_S (2.0F)
#define DAB_SCHEDULE_VALIDATION_SLEW_MIN_A_PER_S   (1.0F)
#define DAB_CURRENT_REFERENCE_SLEW_MAX_A_PER_S   (100.0F)
#define DAB_ENERGY_REFERENCE_STALE_MS            (500UL)

/*
 * Absolute model limit is 480 ns.  The normal first-board limit is 320 ns;
 * 480 ns becomes available only after the explicit "unlock 400" command.
 */
#define DAB_PHASE_NORMAL_MAX_COUNTS             (80UL)
#define DAB_PHASE_ABSOLUTE_MAX_COUNTS           (120UL)
#define DAB_PHASE_RETREAT_COUNTS                (12UL)  /* 48 ns immediately. */
#define DAB_PHASE_RISE_NS_PER_MS                (0.04F)
#define DAB_PHASE_RISE_COUNTS_PER_UPDATE        \
    ((DAB_PHASE_RISE_NS_PER_MS * 1000.0F * DAB_CONTROL_DT_S) / \
     (float) DAB_PWM_COUNT_NS)
#define DAB_PHASE_FALL_COUNTS_PER_UPDATE        (8.0F)

/*
 * Hard startup envelope.  This is a control constraint, not an automatic
 * shutdown path, and therefore remains active in manual-stop bench mode.
 * Every ON starts in SPS with zero inner shift.  After the hold, an explicit
 * manual EPS/TPS target may slew in SOFTSTART to avoid a high-ratio startup
 * deadlock; AUTO still waits for RUN.  Effective outer phase is limited to one
 * 4 ns hardware count until RUN and then released one count at a time.
 */
#define DAB_STARTUP_PHASE_MIN_COUNTS             (1UL)
#define DAB_STARTUP_PHASE_HOLD_MS                 (1000UL)
#define DAB_STARTUP_PHASE_STEP_INTERVAL_MS        (100UL)
#define DAB_STARTUP_INNER_SLOW_WINDOW_MS          (3000UL)
#define DAB_STARTUP_INNER_SLEW_NS_PER_MS          (0.020F)

/*
 * Board-local charger scheduling.  The locked 18 V/CV43 V repair keeps the
 * proven TPS 100/564 ns bootstrap partition until RUN is established, then
 * slews to the 100/536 ns steady candidate without any host command.  This
 * candidate is intentionally restricted to the 1 A LFP workpoint and must be
 * physically revalidated before promotion into the PASS table.
 */
#define DAB_CHARGER_RUNTIME_RESELECT_MS             (250UL)
#define DAB_CHARGER_RUNTIME_CONFIRM_MS              (3000UL)
#define DAB_CHARGER_LOW_INPUT_MAX_V                 (18.0F)
#define DAB_CHARGER_LOW_OUTPUT_MIN_V                (42.5F)
#define DAB_CHARGER_LOW_OUTPUT_MAX_V                (44.0F)
#define DAB_CHARGER_LOW_CURRENT_MIN_A                (0.8F)
#define DAB_CHARGER_LOW_CURRENT_MAX_A                (1.2F)
#define DAB_CHARGER_LOW_START_PRIMARY_NS            (100.0F)
#define DAB_CHARGER_LOW_START_SECONDARY_NS          (564.0F)
#define DAB_CHARGER_LOW_RUN_PRIMARY_NS              (100.0F)
#define DAB_CHARGER_LOW_RUN_SECONDARY_NS            (536.0F)
#define DAB_CHARGER_LOW_RUN_SETTLE_MS                (500UL)
#define DAB_CHARGER_LOW_PHASE_CAP_NS                 (320.0F)
#define DAB_CHARGER_LOW_ENTRY_ID                  (48018001UL)

/* Physical endpoints at 22 V/CV40 V are 1.443 A for SPS 0/0 at zero phase
 * and about zero transfer for EPS 600/0 at its compensated zero.  The latter
 * consumes 300 ns of a 480 ns raw-phase range and reaches only 0.243 A at an
 * effective 120 ns.  Use the normal 240 ns inner limit as an interpolated,
 * lower-start-power partition with enough forward actuator headroom. */
#define DAB_CHARGER_LIION_MID_INPUT_MAX_V          (22.0F)
#define DAB_CHARGER_LIION_MID_OUTPUT_MIN_V         (39.0F)
#define DAB_CHARGER_LIION_MID_OUTPUT_MAX_V         (42.5F)
#define DAB_CHARGER_LIION_MID_PRIMARY_NS           (240.0F)
#define DAB_CHARGER_LIION_MID_SECONDARY_NS         (0.0F)
#define DAB_CHARGER_LIION_MID_PHASE_CAP_NS         (124.0F)
#define DAB_CHARGER_LIION_MID_ENTRY_ID          (22040009UL)
#define DAB_CHARGER_LIION_LOW_INPUT_MAX_V           (20.5F)
#define DAB_CHARGER_LIION_LOW_PRIMARY_NS            (100.0F)
#define DAB_CHARGER_LIION_LOW_SECONDARY_NS          (536.0F)
#define DAB_CHARGER_LIION_LOW_PHASE_CAP_NS          (260.0F)
#define DAB_CHARGER_LIION_LOW_ENTRY_ID            (22040014UL)

/* EPS/TPS shift is narrowed so the 400 ns dead-time schedule always fits. */
#define DAB_INNER_SHIFT_MAX_COUNTS              (180UL)
#define DAB_PRECHARGE_PRIMARY_INNER_MAX_COUNTS  (216UL)
#define DAB_INNER_NORMAL_MAX_COUNTS             (60UL)
/* Keep inner-shift motion slow enough for the outer voltage loop to follow. */
#define DAB_INNER_SLEW_NS_PER_MS                (0.04F)
#define DAB_AUTO_SPS_M_MIN                      (0.90F)
#define DAB_AUTO_SPS_M_MAX                      (1.10F)
#define DAB_AUTO_EPS_M_MIN                      (0.80F)
#define DAB_AUTO_EPS_M_MAX                      (1.25F)
#define DAB_AUTO_M_HYSTERESIS                   (0.02F)

/* Cold-output clamps limit leakage-inductor current during capacitor charge. */
#define DAB_COLD_VOUT_1_V                       (5.0F)
#define DAB_COLD_VOUT_2_V                       (15.0F)
#define DAB_COLD_VOUT_3_V                       (30.0F)
#define DAB_COLD_PHASE_1_COUNTS                 (2UL)
#define DAB_COLD_PHASE_2_COUNTS                 (20UL)
#define DAB_COLD_PHASE_3_COUNTS                 (40UL)
#define DAB_NO_RISE_TIMEOUT_MS                  (60000UL)
#define DAB_START_ADC_WAIT_MS                   (100UL)

/* Supported first-power operating envelope. */
#define DAB_VIN_START_MIN_V                     (16.0F)
#define DAB_VIN_RUN_MIN_V                       (15.5F)
#define DAB_VIN_MAX_V                           (33.0F)
#define DAB_VREF_DEFAULT_V                      (44.0F)
#define DAB_VREF_MIN_V                          (36.0F)
#define DAB_VREF_MAX_V                          (48.0F)
#define DAB_VOUT_ABSOLUTE_MAX_V                 (55.0F)
#define DAB_TEMPERATURE_MAX_C                    (70.0F)

#define DAB_POWER_DEFAULT_W                     (20.0F)
#define DAB_POWER_NORMAL_MAX_W                  (100.0F)
#define DAB_POWER_ABSOLUTE_MAX_W                (360.0F)

/* Training-condition metadata.  These values never energize the converter. */
#define DAB_BENCH_VIN_SET_DEFAULT_V             (24.0F)
#define DAB_BENCH_VIN_SET_MIN_V                 (18.0F)
#define DAB_BENCH_VIN_SET_MAX_V                 (24.0F)
#define DAB_LOAD_NOMINAL_DEFAULT_OHM            (10.0F)
#define DAB_LOAD_NOMINAL_MIN_OHM                (1.0F)
#define DAB_LOAD_NOMINAL_MAX_OHM                (100.0F)
#define DAB_POWER_LAYER_DEFAULT_W               (200.0F)
#define DAB_POWER_LAYER_MIN_W                   (20.0F)
#define DAB_POWER_LAYER_MAX_W                   (360.0F)

/*
 * Software current limits are intentionally far below the fitted hardware
 * comparator limits used by the simulation.  They are increased gradually
 * with the requested power but remain capped for first-board work.
 */
#define DAB_IPRI_SOFT_BASE_A                    (5.0F)
#define DAB_IPRI_SOFT_PER_W_A                   (0.10F)
#define DAB_IPRI_HARD_MARGIN_A                  (4.0F)
#define DAB_IPRI_ABSOLUTE_HARD_A                (22.0F)
#define DAB_ISEC_SOFT_BASE_A                    (3.0F)
#define DAB_ISEC_SOFT_PER_W_A                   (0.06F)
#define DAB_ISEC_HARD_MARGIN_A                  (3.0F)
#define DAB_ISEC_ABSOLUTE_HARD_A                (30.0F)
#define DAB_BUS_CURRENT_MARGIN                  (1.50F)
#define DAB_BUS_CURRENT_EXTRA_A                 (1.0F)
#define DAB_IIN_ABSOLUTE_HARD_A                 (26.0F)
#define DAB_IOUT_ABSOLUTE_HARD_A                (18.0F)
#define DAB_INPUT_POWER_SOFT_MULTIPLIER          (1.25F)
#define DAB_POWER_TRIP_MULTIPLIER               (1.35F)
#define DAB_START_IPRI_MAX_A                    (4.0F)
#define DAB_START_ISEC_MAX_A                    (4.0F)
#define DAB_START_IIN_MAX_A                     (2.0F)
#define DAB_START_IOUT_MAX_A                    (2.0F)

/* Direct four-MOS power-path sequencing, executed with phase=0 and RUN_REQ low. */
#define DAB_LATCH_RESET_LOW_MS                  (50UL)
#define DAB_LATCH_RELEASE_MS                    (20UL)
#define DAB_DIRECT_PATH_SETTLE_MS               (100UL)
#define DAB_OUTPUT_SETTLE_MS                    (50UL)

#define DAB_CURRENT_ZERO_SETTLE_MS              (3000UL)
#define DAB_CURRENT_ZERO_FRAMES                 (256UL)
#define DAB_CURRENT_ZERO_TIMEOUT_MS             (100UL)
#define DAB_TELEMETRY_PERIOD_MS                 (200UL)
#define DAB_IDLE_STREAM_PERIOD_MS               (500UL)
#define DAB_OBSERVE_PERIOD_MS                   (1000UL)
#define DAB_VOFA_PERIOD_MS                      (50UL)

#if (DAB_PWM_PERIOD_COUNTS != 834UL)
#error "The DAB firmware requires the tested even 834-count PWM period."
#endif

#if (DAB_DEADTIME_COUNTS != 100UL)
#error "400 ns at 250 MHz must equal 100 GPT counts."
#endif

#if ((DAB_PWM_EDGE_GUARD_COUNTS + DAB_PHASE_ABSOLUTE_MAX_COUNTS + \
      DAB_INNER_SHIFT_MAX_COUNTS + \
      DAB_PWM_HALF_COUNTS + DAB_DEADTIME_COUNTS) >= DAB_PWM_PERIOD_COUNTS)
#error "The maximum phase/dead-time edge schedule does not fit the PWM period."
#endif

#if ((DAB_PWM_EDGE_GUARD_COUNTS + \
      DAB_PRECHARGE_PRIMARY_INNER_MAX_COUNTS + DAB_PWM_HALF_COUNTS + \
      DAB_DEADTIME_COUNTS) >= DAB_PWM_PERIOD_COUNTS)
#error "The special precharge primary-inner edge schedule does not fit."
#endif

#endif /* DAB_FIRST_POWER_CONFIG_H */
