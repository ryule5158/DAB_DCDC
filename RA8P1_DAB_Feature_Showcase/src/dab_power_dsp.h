#ifndef DAB_POWER_DSP_H
#define DAB_POWER_DSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct st_dab_power_dsp_window
{
    float mean;
    float rms;
    float minimum;
    float maximum;
} dab_power_dsp_window_t;

/* Affine ADC/sample conversion: output[i] = input[i] * scale + offset. */
bool dab_power_dsp_u16_affine(uint16_t const * p_input,
                              float * p_output,
                              size_t count,
                              float scale,
                              float offset);

/* Mean/RMS/min/max are useful for current, voltage and ripple windows. */
bool dab_power_dsp_window_analyze(float const * p_samples,
                                  size_t count,
                                  dab_power_dsp_window_t * p_result);

bool dab_power_dsp_self_test(void);
bool dab_power_dsp_helium_enabled(void);

#endif /* DAB_POWER_DSP_H */
