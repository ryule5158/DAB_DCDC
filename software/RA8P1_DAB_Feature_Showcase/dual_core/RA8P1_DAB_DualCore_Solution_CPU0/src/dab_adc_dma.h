#ifndef DAB_ADC_DMA_H
#define DAB_ADC_DMA_H

#include "dab_adc_dma_config.h"
#include "hal_data.h"

#include <stdbool.h>
#include <stdint.h>

/* Logical DAB signals.  The order is stable and is the public library ABI. */
typedef enum e_dab_adc_channel
{
    DAB_ADC_IPRI = 0,       /* P000/AN000 -> RA8P1 header P5.3  -> CN2.71 */
    DAB_ADC_VIN,            /* P001/AN001 -> RA8P1 header P5.5  -> CN2.101 */
    DAB_ADC_IIN,            /* P002/AN002 -> RA8P1 header P5.7  -> CN2.65 */
    DAB_ADC_VAFE_HALF,      /* P003/AN003 -> RA8P1 header P5.9  -> CN2.69 */
    DAB_ADC_TEMP_SEC,       /* P004/AN004 -> RA8P1 header P5.11 -> CN2.33 */
    DAB_ADC_ISEC,           /* P006/AN006 -> RA8P1 header P5.4  -> CN2.73 */
    DAB_ADC_VOUT,           /* P007/AN007 -> RA8P1 header P5.6  -> CN2.51 */
    DAB_ADC_IOUT,           /* P008/AN008 -> RA8P1 header P5.8  -> CN2.67 */
    DAB_ADC_TEMP_PRI,       /* P009/AN009 -> RA8P1 header P5.10 -> CN2.91 */
    DAB_ADC_CHANNEL_COUNT
} dab_adc_channel_t;

typedef struct st_dab_adc_frame
{
    uint16_t raw[DAB_ADC_CHANNEL_COUNT];
    uint16_t invalid_mask;
    uint32_t sequence;
} dab_adc_frame_t;

typedef struct st_dab_adc_dma_stats
{
    uint32_t frame_count;
    uint32_t batch_reload_count;
    uint32_t late_reload_count;
    uint16_t dma_frames_remaining;
    bool     running;
} dab_adc_dma_stats_t;

/* Opens/calibrates ADC_B and prepares DMAC0.  PWM must still be stopped here. */
fsp_err_t dab_adc_dma_open(void);

/* Enables GPT0 A/B compare triggers at the requested counter value. */
fsp_err_t dab_adc_dma_bind_pwm(gpt_instance_ctrl_t * p_gpt, uint32_t trigger_count);

/* Enables both hardware-triggered scan groups after ADC calibration. */
fsp_err_t dab_adc_dma_start(void);

/* Call at least every 20 ms.  No per-sample ISR is used. */
void dab_adc_dma_service(void);

/* Coherent copy of the newest complete frame in the DMA ring. */
bool dab_adc_dma_latest(dab_adc_frame_t * p_frame);

/* Direct result-register read for comparison with the latest DMA ring frame. */
bool dab_adc_dma_direct_read(dab_adc_channel_t channel,
                             uint16_t * p_raw,
                             bool * p_invalid);

void        dab_adc_dma_stats_get(dab_adc_dma_stats_t * p_stats);
uint32_t    dab_adc_raw_to_mv(uint16_t raw);
const char * dab_adc_channel_pin_name(dab_adc_channel_t channel);
const char * dab_adc_dma_last_error_stage(void);

#endif /* DAB_ADC_DMA_H */
