#ifndef DAB_PRODUCTION_CONFIG_H
#define DAB_PRODUCTION_CONFIG_H

/*
 * Production policy configuration for the populated DAB board.
 *
 * The fixed pin assignment is implemented in dab_board.c and dab_pwm_hw.c.
 * Do not change pins here.  Normal commissioning parameters may be adjusted
 * here or through the bounded UART commands implemented by the application.
 */

#define DAB_FW_VERSION                         "2026-07-30-production-ai-v2"

/*
 * Peripheral qualification is complete, so this build permits only the
 * existing bounded, USB-CDC-armed production sequencer.  Power remains OFF
 * after reset and every runtime hardware/measurement protection stays active.
 */
#define DAB_ALLOW_POWER_TRANSFER               (1U)

/* GPT0/1/5/8 all use the 250 MHz GPT clock. */
#define DAB_GPT_CLOCK_HZ                       (250000000UL)
#define DAB_PWM_TARGET_HZ                      (300000UL)
#define DAB_PWM_PERIOD_COUNTS                  (834UL)
#define DAB_PWM_HALF_COUNTS                    (DAB_PWM_PERIOD_COUNTS / 2UL)
#define DAB_PWM_ACTUAL_HZ                      (DAB_GPT_CLOCK_HZ / DAB_PWM_PERIOD_COUNTS)
#define DAB_PWM_COUNT_NS                       (4UL)
/* Conservative first-board margin, shared with the RA6M5 source: 160 ns. */
#define DAB_DEADTIME_NS                        (160UL)
#define DAB_DEADTIME_COUNTS                    (40UL)
#define DAB_PWM_EDGE_GUARD_COUNTS              (1UL)
#define DAB_ADC_TRIGGER_COUNTS                 (DAB_PWM_PERIOD_COUNTS / 4UL)

/* One control update for every three 299.760 ksample/s ADC-DMA frames. */
#define DAB_CONTROL_FRAME_DIVIDER              (3UL)
#define DAB_CONTROL_DT_S                       \
    ((float) DAB_CONTROL_FRAME_DIVIDER / (float) DAB_PWM_ACTUAL_HZ)
#define DAB_ADC_STALE_TIMEOUT_MS                (3UL)

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

/*
 * Absolute model limit is 480 ns.  The normal first-board limit is 320 ns;
 * 480 ns becomes available only after the explicit "unlock 200" command.
 */
#define DAB_PHASE_NORMAL_MAX_COUNTS             (80UL)
#define DAB_PHASE_ABSOLUTE_MAX_COUNTS           (120UL)
#define DAB_PHASE_RETREAT_COUNTS                (12UL)  /* 48 ns immediately. */
#define DAB_PHASE_RISE_COUNTS_PER_UPDATE        (0.020F) /* 8 ns/ms, deliberately gentle. */
#define DAB_PHASE_FALL_COUNTS_PER_UPDATE        (8.0F)

/* Cold-output clamps limit leakage-inductor current during capacitor charge. */
#define DAB_COLD_VOUT_1_V                       (5.0F)
#define DAB_COLD_VOUT_2_V                       (15.0F)
#define DAB_COLD_VOUT_3_V                       (30.0F)
#define DAB_COLD_PHASE_1_COUNTS                 (10UL)
#define DAB_COLD_PHASE_2_COUNTS                 (20UL)
#define DAB_COLD_PHASE_3_COUNTS                 (40UL)

/* Supported staged-commissioning operating envelope. */
#define DAB_VIN_START_MIN_V                     (18.0F)
#define DAB_VIN_RUN_MIN_V                       (17.0F)
#define DAB_VIN_MAX_V                           (33.0F)
#define DAB_VREF_DEFAULT_V                      (48.0F)
#define DAB_VREF_MIN_V                          (36.0F)
#define DAB_VREF_MAX_V                          (48.0F)
#define DAB_VOUT_ABSOLUTE_MAX_V                 (55.0F)
#define DAB_TEMPERATURE_MAX_C                    (70.0F)

#define DAB_POWER_DEFAULT_W                     (20.0F)
#define DAB_POWER_NORMAL_MAX_W                  (100.0F)
#define DAB_POWER_ABSOLUTE_MAX_W                (200.0F)

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
#define DAB_ISEC_ABSOLUTE_HARD_A                (14.0F)
#define DAB_BUS_CURRENT_MARGIN                  (1.50F)
#define DAB_BUS_CURRENT_EXTRA_A                 (1.0F)
#define DAB_IIN_ABSOLUTE_HARD_A                 (12.0F)
#define DAB_IOUT_ABSOLUTE_HARD_A                (7.0F)
#define DAB_POWER_TRIP_MULTIPLIER               (1.35F)

/* Power-path sequencing, all executed with phase=0 and RUN_REQ low. */
#define DAB_LATCH_RESET_LOW_MS                  (50UL)
#define DAB_LATCH_RELEASE_MS                    (20UL)
#define DAB_INPUT_PRECHARGE_MS                  (500UL)
#define DAB_MAIN_SETTLE_MS                      (100UL)
#define DAB_OUTPUT_SETTLE_MS                    (50UL)

#define DAB_CURRENT_ZERO_FRAMES                 (256UL)
#define DAB_CURRENT_ZERO_TIMEOUT_MS             (100UL)
#define DAB_CURRENT_ZERO_VALID_MS               (300000UL)
#define DAB_POWER_COMMAND_VALID_MS              (60000UL)
#define DAB_SOFTSTART_NO_RISE_TIMEOUT_MS        (5000UL)
#define DAB_PWM_UPDATE_MISS_LIMIT               (100UL)
#define DAB_TELEMETRY_PERIOD_MS                 (200UL)
#define DAB_IDLE_STREAM_PERIOD_MS               (500UL)

#if (DAB_PWM_PERIOD_COUNTS != 834UL)
#error "The DAB firmware requires the tested even 834-count PWM period."
#endif

#if (DAB_DEADTIME_COUNTS != 40UL)
#error "160 ns at 250 MHz must equal 40 GPT counts."
#endif

#if ((DAB_PWM_EDGE_GUARD_COUNTS + DAB_PHASE_ABSOLUTE_MAX_COUNTS + \
      DAB_PWM_HALF_COUNTS + DAB_DEADTIME_COUNTS) >= DAB_PWM_PERIOD_COUNTS)
#error "The maximum phase/dead-time edge schedule does not fit the PWM period."
#endif

#endif /* DAB_PRODUCTION_CONFIG_H */
