#ifndef RA8P1_PERIPHERAL_TEST_CONFIG_H
#define RA8P1_PERIPHERAL_TEST_CONFIG_H

#include "dab_adc_dma_config.h"

/*
 * RA8P1 board/peripheral test configuration.
 *
 * Set the switch to 1 to restore the isolated scope/ADC test image.  The
 * production DAB application is compiled while the switch is 0.
 */
#define RA8P1_PERIPHERAL_TEST_ENABLE          (0U)

/* GPT0/1/5/8 run from the 250 MHz GPT clock on this RA8P1 project. */
#define RA8P1_TEST_GPT_CLOCK_HZ               (250000000UL)

/*
 * DAB switching requirement: 300 kHz. The closest integer divider is
 * 833, but its odd period breaks exact 50/50 bridge symmetry. Round up
 * to the nearest even period: 834 counts = 299.760192 kHz (-0.08%).
 */
#define RA8P1_TEST_PWM_TARGET_HZ               (300000UL)
#define RA8P1_TEST_PWM_NEAREST_COUNTS          \
    ((RA8P1_TEST_GPT_CLOCK_HZ + (RA8P1_TEST_PWM_TARGET_HZ / 2UL)) / RA8P1_TEST_PWM_TARGET_HZ)
#define RA8P1_TEST_PWM_PERIOD_COUNTS           \
    (RA8P1_TEST_PWM_NEAREST_COUNTS + (RA8P1_TEST_PWM_NEAREST_COUNTS & 1UL))
#define RA8P1_TEST_PWM_HALF_COUNTS             (RA8P1_TEST_PWM_PERIOD_COUNTS / 2UL)

/*
 * Dead time is inserted explicitly into all eight one-shot edge positions.
 * It is not added a second time by the GPT dead-time unit.
 */
#define RA8P1_TEST_DEADTIME_NS                 (160UL)
#define RA8P1_TEST_DEADTIME_COUNTS             \
    (((RA8P1_TEST_GPT_CLOCK_HZ / 1000000UL) * RA8P1_TEST_DEADTIME_NS) / 1000UL)
#define RA8P1_TEST_EDGE_GUARD_COUNTS           (1UL)

/*
 * Positive phase means the secondary bridge lags the primary bridge.
 * Change DEFAULT to choose the boot/test phase; the three presets are exposed
 * as the serial commands "phase 0", "phase 1", and "phase 2".
 */
#define RA8P1_TEST_PHASE_COUNTS_DEFAULT        (40UL)
#define RA8P1_TEST_PHASE_PRESET_0_COUNTS       (0UL)
#define RA8P1_TEST_PHASE_PRESET_1_COUNTS       (40UL)
#define RA8P1_TEST_PHASE_PRESET_2_COUNTS       (80UL)
#define RA8P1_TEST_PHASE_MAX_COUNTS            (80UL)

/*
 * High-rate DAB acquisition:
 * - GPT0 triggers ADC0 and ADC1 once per 300 kHz PWM period.
 * - ADC0 converts AN000..AN004; ADC1 converts AN006..AN009 in parallel.
 * - DMAC0 copies ADDR[0..9] into a ring without a CPU interrupt per frame.
 * - UART only displays a DMA snapshot; its 500 ms period is not the ADC rate.
 *
 * PCLKA is 125 MHz.  PCLKA/3 gives 41.667 MHz, inside the ADC_B 25..60 MHz
 * requirement.  20 sampling clocks + 5 conversion clocks = 25 clocks/channel.
 * The slower five-channel converter therefore finishes in about 3.0 us, which
 * fits inside the 3.336 us PWM period.
 */
#define RA8P1_TEST_ADC_TRIGGER_COUNTS          (RA8P1_TEST_PWM_PERIOD_COUNTS / 4UL)
#define RA8P1_TEST_TELEMETRY_PERIOD_MS         (500UL)
#define RA8P1_TEST_ADC_RATE_DIAGNOSTICS        (0U)
#define RA8P1_TEST_PERIODIC_STATUS_ENABLE      (0U)
#define RA8P1_TEST_STATUS_PERIOD_MS            (5000UL)
#define RA8P1_TEST_GPIO_PULSE_MS               (500UL)
#define RA8P1_TEST_LED_HEARTBEAT_MS            (500UL)

/* Match the RA6M5 commissioning shell: explicit command required for PWM. */
#define RA8P1_TEST_PWM_AUTOSTART               (0U)

#if (RA8P1_TEST_PWM_TARGET_HZ != 300000UL)
#error "This DAB commissioning branch must remain at 300 kHz unless deliberately retuned."
#endif

#if (RA8P1_TEST_PWM_PERIOD_COUNTS != 834UL)
#error "The DAB test must use an even 834-count period for symmetric 300 kHz-class PWM."
#endif

#if (RA8P1_TEST_DEADTIME_COUNTS != 40UL)
#error "160 ns at 250 MHz must resolve to 40 GPT counts."
#endif

#if (RA8P1_TEST_PHASE_COUNTS_DEFAULT > RA8P1_TEST_PHASE_MAX_COUNTS)
#error "Default DAB phase exceeds the configured test limit."
#endif

#if (DAB_ADC_TRIGGER_READY_TIME_NS >= \
     ((RA8P1_TEST_PWM_PERIOD_COUNTS * 1000000000ULL) / RA8P1_TEST_GPT_CLOCK_HZ))
#error "ADC scan, data processing, and documented re-trigger gap do not fit one PWM period."
#endif

#if ((RA8P1_TEST_EDGE_GUARD_COUNTS + RA8P1_TEST_PHASE_MAX_COUNTS + \
      RA8P1_TEST_PWM_HALF_COUNTS + RA8P1_TEST_DEADTIME_COUNTS) >= \
     RA8P1_TEST_PWM_PERIOD_COUNTS)
#error "DAB edge schedule does not fit in one 300 kHz PWM period."
#endif

#endif /* RA8P1_PERIPHERAL_TEST_CONFIG_H */
