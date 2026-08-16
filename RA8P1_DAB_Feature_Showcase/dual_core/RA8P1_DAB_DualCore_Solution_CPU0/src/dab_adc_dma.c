#include "dab_adc_dma.h"

#include "dab_accel.h"

#include <stddef.h>
#include <string.h>

#define DAB_ADC_REFERENCE_MV             (3300UL)
#define DAB_ADC_FULL_SCALE_COUNTS        (4095UL)
#define DAB_ADC_DMA_PHYSICAL_WORDS       (10U)  /* ADDR[0] through ADDR[9], including unused AN005. */
#define DAB_ADC_DMA_RING_WORDS           (DAB_ADC_DMA_RING_FRAMES * DAB_ADC_DMA_PHYSICAL_WORDS)
#define DAB_ADC_DMA_RETRY_COUNT          (4U)
#define DAB_ADC_RESULT_INVALID_MASK      (1UL << 31)
#define DAB_ADC_RESULT_DATA_MASK         (0xFFFFUL)

/* Direct DMAC register fields, matching the RA8P1 hardware/FSP r_dmac driver. */
#define DAB_DMAC_DCTG_ELC_EVENT          (1U << R_DMAC0_DMTMD_DCTG_Pos)
#define DAB_DMAC_SIZE_32_BIT             (2U << R_DMAC0_DMTMD_SZ_Pos)
#define DAB_DMAC_MODE_REPEAT_BLOCK       (3U << R_DMAC0_DMTMD_MD_Pos)
#define DAB_DMAC_ADDR_INCREMENT          (2U)
#define DAB_DMAC_SOURCE_INCREMENT        (DAB_DMAC_ADDR_INCREMENT << R_DMAC0_DMAMD_SM_Pos)
#define DAB_DMAC_DEST_INCREMENT          (DAB_DMAC_ADDR_INCREMENT << R_DMAC0_DMAMD_DM_Pos)
#define DAB_GPT_ADC_TRIGGER_SHIFT        (16U)
#define DAB_GPT_ADC_TRIGGER_MASK         (0x0FUL << DAB_GPT_ADC_TRIGGER_SHIFT)

static const uint8_t s_physical_index[DAB_ADC_CHANNEL_COUNT] =
{
    0U, 1U, 2U, 3U, 4U, 6U, 7U, 8U, 9U
};

static const char * const s_pin_names[DAB_ADC_CHANNEL_COUNT] =
{
    "P000/P5.3/CN2.71", "P001/P5.5/CN2.101", "P002/P5.7/CN2.65",
    "P003/P5.9/CN2.69", "P004/P5.11/CN2.33", "P006/P5.4/CN2.73",
    "P007/P5.6/CN2.51", "P008/P5.8/CN2.67", "P009/P5.10/CN2.91"
};

/* DMA accesses the 32-bit ADC result registers, including the invalid flag. */
static uint32_t s_dma_ring[DAB_ADC_DMA_RING_FRAMES][DAB_ADC_DMA_PHYSICAL_WORDS]
    DAB_DMA_RAM;

static adc_cfg_t            s_adc_cfg;
static adc_b_extended_cfg_t s_adc_extend;
static adc_b_group_cfg_t    s_group_0;
static adc_b_group_cfg_t    s_group_1;
static adc_b_group_cfg_t  * s_groups[2];
static adc_b_scan_cfg_t     s_scan_cfg;

static const char * s_error_stage = "not_open";
static uint32_t     s_frames_before_batch;
static uint32_t     s_batch_reload_count;
static uint32_t     s_late_reload_count;
static bool         s_open;
static bool         s_running;

static uint32_t dab_adc_dma_frame_count(void)
{
    uint16_t const remaining = R_DMAC0->DMCRB_b.DMCRBL;
    return s_frames_before_batch +
           ((uint32_t) DAB_ADC_DMA_BATCH_FRAMES - (uint32_t) remaining);
}

static void dab_adc_dma_configure(void)
{
    uint32_t const source_address = (uint32_t) (uintptr_t) &R_ADC_B->ADDR[0];
    uint32_t const dest_address   = (uint32_t) (uintptr_t) &s_dma_ring[0][0];
    uint32_t const block_words    = DAB_ADC_DMA_PHYSICAL_WORDS;
    uint32_t const ring_words     = DAB_ADC_DMA_RING_WORDS;

    R_BSP_MODULE_START(FSP_IP_DMAC, DAB_ADC_DMA_CHANNEL);

    R_ICU->DELSR[DAB_ADC_DMA_CHANNEL] = ELC_EVENT_NONE;
    R_DMAC0->DMCNT = 0U;
    R_DMA->DMAST   = 1U;

    R_DMAC0->DMSAR = source_address;
    R_DMAC0->DMDAR = dest_address;
    R_DMAC0->DMSRR = source_address;
    R_DMAC0->DMDRR = dest_address;
    R_DMAC0->DMCRA = block_words | (block_words << R_DMAC0_DMCRA_DMCRAH_Pos);
    R_DMAC0->DMCRB = DAB_ADC_DMA_BATCH_FRAMES;
    R_DMAC0->DMSBS = block_words | (block_words << R_DMAC0_DMSBS_DMSBSH_Pos);
    R_DMAC0->DMDBS = ring_words | (ring_words << R_DMAC0_DMDBS_DMDBSH_Pos);
    R_DMAC0->DMTMD = (uint16_t) (DAB_DMAC_DCTG_ELC_EVENT |
                                  DAB_DMAC_SIZE_32_BIT |
                                  DAB_DMAC_MODE_REPEAT_BLOCK);
    R_DMAC0->DMAMD = (uint16_t) (DAB_DMAC_SOURCE_INCREMENT |
                                  DAB_DMAC_DEST_INCREMENT);
    R_DMAC0->DMOFR = 0U;
    R_DMAC0->DMINT = 0U;
    R_DMAC0->DMREQ = 0U;
    R_DMAC0->DMBWR = 0U;

    R_ICU->DELSR[DAB_ADC_DMA_CHANNEL] = ELC_EVENT_ADC_ADI0;
    R_DMAC0->DMCNT = 1U;
}

fsp_err_t dab_adc_dma_open(void)
{
    if (s_open)
    {
        return FSP_ERR_ALREADY_OPEN;
    }

#if BSP_CFG_DCACHE_ENABLED
 #error "dab_adc_dma requires explicit cache maintenance when D-cache is enabled."
#endif

    memset(s_dma_ring, 0, sizeof(s_dma_ring));
    s_frames_before_batch = 0U;
    s_batch_reload_count  = 0U;
    s_late_reload_count   = 0U;
    s_running             = false;

    /* Own private copies: FSP-generated/production configuration stays untouched. */
    s_adc_cfg    = g_adc0_cfg;
    s_adc_extend = *((adc_b_extended_cfg_t const *) g_adc0_cfg.p_extend);
    s_group_0    = *g_adc0_scan_cfg.p_adc_groups[0];
    s_group_1    = *g_adc0_scan_cfg.p_adc_groups[1];

    s_adc_extend.clock_control_bits.divider = ADC_B_CLOCK_DIV_3;
    /* GPT0 emits A0 and B0 at the same compare count, so ADC0 and ADC1 still
     * begin together.  ADC_B synchronous operation is deliberately disabled:
     * the RA8P1 requires a 2 * ADSYCYC post-scan trigger gap when it is enabled,
     * which rejected every other trigger with five channels at 300 kHz. */
    s_adc_extend.sync_operation_control_bits.period_cycle = DAB_ADC_DISABLED_SYNC_PERIOD_CYCLES;
    s_adc_extend.sync_operation_control_bits.adc_0_disable_sync = 1U;
    s_adc_extend.sync_operation_control_bits.adc_1_disable_sync = 1U;
    for (uint32_t i = 0U; i < 15U; i++)
    {
        s_adc_extend.sampling_state_table[i] = DAB_ADC_SAMPLE_CYCLES;
    }
    s_adc_cfg.p_extend = &s_adc_extend;

    s_group_0.scan_end_interrupt_enable = true;
    s_group_0.gpt_trigger_enable_mask   = ADC_B_GPT_TRIGGER_A0;
    s_group_1.scan_end_interrupt_enable = false;
    s_group_1.gpt_trigger_enable_mask   = ADC_B_GPT_TRIGGER_B0;
    s_groups[0] = &s_group_0;
    s_groups[1] = &s_group_1;
    s_scan_cfg.group_count  = 2U;
    s_scan_cfg.p_adc_groups = s_groups;

    s_error_stage = "open";
    fsp_err_t err = R_ADC_B_Open(&g_adc0_ctrl, &s_adc_cfg);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    s_error_stage = "scan_cfg";
    err = R_ADC_B_ScanCfg(&g_adc0_ctrl, &s_scan_cfg);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    s_error_stage = "cal_start";
    err = R_ADC_B_Calibrate(&g_adc0_ctrl, NULL);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    uint32_t waited_us = 0U;
    adc_status_t status = {.state = ADC_STATE_CALIBRATION_IN_PROGRESS};
    while (waited_us < (DAB_ADC_CAL_TIMEOUT_MS * 1000UL))
    {
        s_error_stage = "cal_status";
        err = R_ADC_B_StatusGet(&g_adc0_ctrl, &status);
        if (FSP_SUCCESS != err)
        {
            return err;
        }
        if (ADC_STATE_CALIBRATION_IN_PROGRESS != status.state)
        {
            break;
        }
        R_BSP_SoftwareDelay(100U, BSP_DELAY_UNITS_MICROSECONDS);
        waited_us += 100U;
    }
    if (ADC_STATE_CALIBRATION_IN_PROGRESS == status.state)
    {
        s_error_stage = "cal_timeout";
        return FSP_ERR_TIMEOUT;
    }
    if (ADC_B_CONVERTER_STATE_READY != g_adc0_ctrl.adc_state)
    {
        s_error_stage = "cal_result";
        return FSP_ERR_INVALID_STATE;
    }

    s_error_stage = "dma_config";
    dab_adc_dma_configure();
    s_open        = true;
    s_error_stage = "ready";
    return FSP_SUCCESS;
}

fsp_err_t dab_adc_dma_bind_pwm(gpt_instance_ctrl_t * p_gpt, uint32_t trigger_count)
{
    if ((NULL == p_gpt) || (NULL == p_gpt->p_reg))
    {
        return FSP_ERR_ASSERTION;
    }
    /* Use the opened timer's real period so the library is not coupled to a
     * commissioning-only PWM macro. */
    if ((0U == p_gpt->p_reg->GTPR) || (trigger_count >= p_gpt->p_reg->GTPR))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    p_gpt->p_reg->GTADTRA = trigger_count;
    p_gpt->p_reg->GTADTRB = trigger_count;

    /* Count GPT overflow/underflow events and suppress ADC A and B together.
     * With divider 2, the eight PWM outputs still switch every 3.336 us while
     * both ADC units receive one coherent trigger every 6.672 us. */
    p_gpt->p_reg->GTITC = 0U;
#if (DAB_ADC_TRIGGER_DIVIDER > 1UL)
    p_gpt->p_reg->GTITC =
        ((uint32_t) GPT_INTERRUPT_SKIP_SOURCE_OVERFLOW_UNDERFLOW << R_GPT0_GTITC_IVTC_Pos) |
        ((uint32_t) (DAB_ADC_TRIGGER_DIVIDER - 1UL) << R_GPT0_GTITC_IVTT_Pos) |
        ((uint32_t) GPT_INTERRUPT_SKIP_ADC_A_AND_B << R_GPT0_GTITC_ADTAL_Pos);
#endif

    uint32_t gtintad = p_gpt->p_reg->GTINTAD & ~DAB_GPT_ADC_TRIGGER_MASK;
    gtintad |= ((uint32_t) (GPT_ADC_TRIGGER_UP_COUNT_START_ADC_A |
                            GPT_ADC_TRIGGER_UP_COUNT_START_ADC_B) << DAB_GPT_ADC_TRIGGER_SHIFT);
    p_gpt->p_reg->GTINTAD = gtintad;
    return FSP_SUCCESS;
}

fsp_err_t dab_adc_dma_start(void)
{
    if (!s_open)
    {
        return FSP_ERR_NOT_OPEN;
    }
    fsp_err_t const err = R_ADC_B_ScanStart(&g_adc0_ctrl);
    if (FSP_SUCCESS == err)
    {
        s_running = true;
    }
    else
    {
        s_error_stage = "scan_start";
    }
    return err;
}

DAB_ITCM_CODE void dab_adc_dma_service(void)
{
    if (!s_running)
    {
        return;
    }

    uint16_t const remaining = R_DMAC0->DMCRB_b.DMCRBL;
    if (remaining > DAB_ADC_DMA_REARM_THRESHOLD)
    {
        return;
    }

    R_ICU->DELSR[DAB_ADC_DMA_CHANNEL] = ELC_EVENT_NONE;
    R_DMAC0->DMCNT = 0U;
    while (0U != R_DMAC0->DMSTS_b.ACT)
    {
        /* A block is only ten 32-bit reads; this wait is bounded to a few bus clocks. */
    }

    uint16_t const stopped_remaining = R_DMAC0->DMCRB_b.DMCRBL;
    s_frames_before_batch += ((uint32_t) DAB_ADC_DMA_BATCH_FRAMES -
                              (uint32_t) stopped_remaining);
    if (0U == stopped_remaining)
    {
        s_late_reload_count++;
    }
    s_batch_reload_count++;
    R_DMAC0->DMCRB = DAB_ADC_DMA_BATCH_FRAMES;
    R_ICU->DELSR[DAB_ADC_DMA_CHANNEL] = ELC_EVENT_ADC_ADI0;
    R_DMAC0->DMCNT = 1U;
}

DAB_ITCM_CODE bool dab_adc_dma_latest(dab_adc_frame_t * p_frame)
{
    if ((!s_running) || (NULL == p_frame) || (0U == dab_adc_dma_frame_count()))
    {
        return false;
    }

    uint32_t physical[DAB_ADC_DMA_PHYSICAL_WORDS];
    for (uint32_t attempt = 0U; attempt < DAB_ADC_DMA_RETRY_COUNT; attempt++)
    {
        uint32_t const before = R_DMAC0->DMDAR;
        uint32_t const base   = (uint32_t) (uintptr_t) &s_dma_ring[0][0];
        uint32_t word_offset  = (before - base) / sizeof(uint32_t);
        if (word_offset >= DAB_ADC_DMA_RING_WORDS)
        {
            word_offset = 0U;
        }
        uint32_t const next_frame = word_offset / DAB_ADC_DMA_PHYSICAL_WORDS;
        uint32_t const latest = (next_frame + DAB_ADC_DMA_RING_FRAMES - 1U) &
                                (DAB_ADC_DMA_RING_FRAMES - 1U);

        memcpy(physical, s_dma_ring[latest], sizeof(physical));
        __DMB();
        if (before == R_DMAC0->DMDAR)
        {
            uint16_t invalid_mask = 0U;
            for (uint32_t channel = 0U; channel < DAB_ADC_CHANNEL_COUNT; channel++)
            {
                uint32_t const result = physical[s_physical_index[channel]];
                p_frame->raw[channel] = (uint16_t) (result & DAB_ADC_RESULT_DATA_MASK);
                if (0U != (result & DAB_ADC_RESULT_INVALID_MASK))
                {
                    invalid_mask |= (uint16_t) (1U << channel);
                }
            }
            p_frame->invalid_mask = invalid_mask;
            p_frame->sequence     = dab_adc_dma_frame_count();
            return true;
        }
    }
    return false;
}

void dab_adc_dma_stats_get(dab_adc_dma_stats_t * p_stats)
{
    if (NULL == p_stats)
    {
        return;
    }
    p_stats->frame_count          = s_running ? dab_adc_dma_frame_count() : 0U;
    p_stats->batch_reload_count   = s_batch_reload_count;
    p_stats->late_reload_count    = s_late_reload_count;
    p_stats->dma_frames_remaining = R_DMAC0->DMCRB_b.DMCRBL;
    p_stats->running              = s_running;
}

uint32_t dab_adc_raw_to_mv(uint16_t raw)
{
    return (((uint32_t) raw * DAB_ADC_REFERENCE_MV) + (DAB_ADC_FULL_SCALE_COUNTS / 2UL)) /
           DAB_ADC_FULL_SCALE_COUNTS;
}

const char * dab_adc_channel_pin_name(dab_adc_channel_t channel)
{
    if ((uint32_t) channel >= DAB_ADC_CHANNEL_COUNT)
    {
        return "invalid";
    }
    return s_pin_names[channel];
}

const char * dab_adc_dma_last_error_stage(void)
{
    return s_error_stage;
}
