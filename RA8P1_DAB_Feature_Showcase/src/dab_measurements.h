#ifndef DAB_MEASUREMENTS_H
#define DAB_MEASUREMENTS_H

#include "dab_adc_dma.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct st_dab_measurements
{
    uint16_t raw[DAB_ADC_CHANNEL_COUNT];
    uint32_t sequence;
    float    adc_v[DAB_ADC_CHANNEL_COUNT];
    float    vin_v;
    float    vout_v;
    float    iin_a;
    float    iout_a;
    float    ipri_a;
    float    isec_a;
    float    vafe_half_v;
    float    temp_pri_adc_v;
    float    temp_sec_adc_v;
    float    temp_pri_c;
    float    temp_sec_c;
    float    pin_w;
    float    pout_w;
    bool     valid;
} dab_measurements_t;

typedef struct st_dab_current_zero
{
    float iin_v;
    float iout_v;
    float ipri_v;
    float isec_v;
} dab_current_zero_t;

void  dab_measurements_initialize(void);
float dab_measurements_raw_to_volts(uint16_t raw);

bool dab_measurements_zero_set(dab_current_zero_t const * p_zero);
void dab_measurements_zero_get(dab_current_zero_t * p_zero);

/* Converts and filters one coherent ADC-DMA frame. */
bool dab_measurements_update(dab_adc_frame_t const * p_frame,
                             dab_measurements_t * p_measurements);

float dab_measurements_abs(float value);
float dab_measurements_isotmp35_c(float volts);
void  dab_measurements_print(dab_measurements_t const * p_measurements);
bool  dab_measurements_helium_enabled(void);

#endif /* DAB_MEASUREMENTS_H */
