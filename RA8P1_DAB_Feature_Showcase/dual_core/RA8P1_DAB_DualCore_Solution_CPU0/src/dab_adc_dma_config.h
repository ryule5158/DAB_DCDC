#ifndef DAB_ADC_DMA_CONFIG_H
#define DAB_ADC_DMA_CONFIG_H

/*
 * Project-wide ADC acquisition contract.  This file is shared by the
 * commissioning firmware and the later production control state machine.
 */
#define DAB_ADC_PCLKA_HZ                (125000000UL)
#define DAB_ADC_CLOCK_DIVISOR           (3UL)
#define DAB_ADC_CLOCK_HZ                (DAB_ADC_PCLKA_HZ / DAB_ADC_CLOCK_DIVISOR)
#define DAB_ADC_SAMPLE_CYCLES           (20UL)
#define DAB_ADC_CONVERSION_CYCLES       (5UL)
#define DAB_ADC_CHANNEL_CYCLES          (DAB_ADC_SAMPLE_CYCLES + DAB_ADC_CONVERSION_CYCLES)
/* ADSYCYC must remain 5..255 even when both ADC synchronous-operation bits are disabled. */
#define DAB_ADC_DISABLED_SYNC_PERIOD_CYCLES (5UL)
#define DAB_ADC_SLOW_GROUP_CHANNELS     (5UL)
#define DAB_ADC_FRAME_CYCLES            (DAB_ADC_CHANNEL_CYCLES * DAB_ADC_SLOW_GROUP_CHANNELS)
#define DAB_ADC_FRAME_TIME_NS           \
    ((DAB_ADC_FRAME_CYCLES * 1000000000ULL + DAB_ADC_CLOCK_HZ - 1UL) / DAB_ADC_CLOCK_HZ)
/* Worst-case SAR data processing (7 ADCLK + 6 PCLKA), followed by the
 * documented no-sync re-trigger gap (2 ADCLK + 2 PCLKA). */
#define DAB_ADC_TRIGGER_READY_PCLKA_CYCLES \
    ((DAB_ADC_FRAME_CYCLES * DAB_ADC_CLOCK_DIVISOR) + \
     (7UL * DAB_ADC_CLOCK_DIVISOR) + 6UL + \
     (2UL * DAB_ADC_CLOCK_DIVISOR) + 2UL)
#define DAB_ADC_TRIGGER_READY_TIME_NS \
    ((DAB_ADC_TRIGGER_READY_PCLKA_CYCLES * 1000000000ULL + DAB_ADC_PCLKA_HZ - 1UL) / \
     DAB_ADC_PCLKA_HZ)

/* Keep every second GPT0 ADC event.  PWM remains 300 kHz while the coherent
 * ADC0+ADC1 frame rate becomes 149.88 ksample/s with ample re-trigger margin. */
#define DAB_ADC_TRIGGER_DIVIDER         (2UL)

#define DAB_ADC_DMA_CHANNEL             (0U)
#define DAB_ADC_DMA_RING_FRAMES         (64U)
#define DAB_ADC_DMA_BATCH_FRAMES        (65535U)
/* 20000 frames are about 133 ms at 149.88 kHz, longer than the worst test I2C wait. */
#define DAB_ADC_DMA_REARM_THRESHOLD     (20000U)
#define DAB_ADC_CAL_TIMEOUT_MS          (1000UL)

#if ((DAB_ADC_CLOCK_HZ < 25000000UL) || (DAB_ADC_CLOCK_HZ > 60000000UL))
#error "RA8P1 ADC_B clock must remain within the documented 25..60 MHz range."
#endif

#if ((DAB_ADC_DISABLED_SYNC_PERIOD_CYCLES < 5UL) || \
     (DAB_ADC_DISABLED_SYNC_PERIOD_CYCLES > 255UL))
#error "ADC_B ADSYCYC must remain within the documented 5..255 cycle range."
#endif

#if ((DAB_ADC_DMA_RING_FRAMES == 0U) || \
     ((DAB_ADC_DMA_RING_FRAMES & (DAB_ADC_DMA_RING_FRAMES - 1U)) != 0U))
#error "ADC DMA ring frame count must be a power of two."
#endif

#if (DAB_ADC_DMA_REARM_THRESHOLD >= DAB_ADC_DMA_BATCH_FRAMES)
#error "ADC DMA rearm threshold must be less than the batch size."
#endif

#if ((DAB_ADC_TRIGGER_DIVIDER < 1UL) || (DAB_ADC_TRIGGER_DIVIDER > 8UL))
#error "GPT ADC trigger divider must remain in the hardware-supported 1..8 range."
#endif

#endif /* DAB_ADC_DMA_CONFIG_H */
