#include "dab_measurements.h"

#include "dab_accel.h"
#include "dab_power_dsp.h"

#include <stdio.h>
#include <string.h>

#define ADC_REFERENCE_VOLTS               (3.3000F)
#define ADC_FULL_SCALE_COUNTS             (4095.0F)

/*
 * Exact populated schematic conversions:
 * VIN:  100k + 100k over 5.6k, AMC3330 gain 2, TLV333 unity.
 * VOUT: 100k + 100k over 3.3k, AMC3330 gain 2, TLV333 unity.
 * IIN:  ACS725LLCTR-50AB-T, 26.4 mV/A.
 * IOUT: ACS725LLCTR-20AB-T, 66.0 mV/A.
 * IPRI/ISEC: TMCS1133A1A, 25 mV/A and fixed 2.5 V zero, then 10k/20k
 *            divider, giving 16.6667 mV/A and about 1.6667 V at the ADC.
 */
#define VIN_VOLTS_PER_ADC_VOLT            (18.3571429F)
#define VOUT_VOLTS_PER_ADC_VOLT           (30.8030303F)
#define IIN_ADC_VOLTS_PER_AMP             (0.0264F)
#define IOUT_ADC_VOLTS_PER_AMP            (0.0660F)
#define IBRIDGE_ADC_VOLTS_PER_AMP         (0.0166667F)
#define MEASUREMENT_FILTER_ALPHA          (0.25F)

#define ACS_ZERO_MIN_V                    (1.35F)
#define ACS_ZERO_MAX_V                    (1.95F)
#define TMCS_DIVIDED_ZERO_MIN_V           (1.45F)
#define TMCS_DIVIDED_ZERO_MAX_V           (1.85F)
#define VAFE_HALF_MIN_V                   (1.35F)
#define VAFE_HALF_MAX_V                   (1.95F)

static dab_current_zero_t s_zero DAB_DTCM_INIT =
{
    .iin_v  = 1.6500F,
    .iout_v = 1.6500F,
    .ipri_v = 1.6667F,
    .isec_v = 1.6667F,
};
static dab_measurements_t s_filtered DAB_DTCM_ZERO;
static bool               s_filter_initialized DAB_DTCM_ZERO;

DAB_ITCM_CODE float dab_measurements_abs(float value)
{
    return (value < 0.0F) ? -value : value;
}

static float dab_measurements_filter(float previous, float sample)
{
    return previous + (MEASUREMENT_FILTER_ALPHA * (sample - previous));
}

void dab_measurements_initialize(void)
{
    memset(&s_filtered, 0, sizeof(s_filtered));
    s_filter_initialized = false;
}

float dab_measurements_raw_to_volts(uint16_t raw)
{
    return ((float) raw * ADC_REFERENCE_VOLTS) / ADC_FULL_SCALE_COUNTS;
}

static void dab_measurements_frame_convert(dab_adc_frame_t const * p_frame,
                                           dab_measurements_t * p_sample)
{
    memcpy(p_sample->raw, p_frame->raw, sizeof(p_sample->raw));
    (void) dab_power_dsp_u16_affine(p_frame->raw,
                                    p_sample->adc_v,
                                    DAB_ADC_CHANNEL_COUNT,
                                    ADC_REFERENCE_VOLTS / ADC_FULL_SCALE_COUNTS,
                                    0.0F);
}

float dab_measurements_isotmp35_c(float volts)
{
    if (volts < 1.500F)
    {
        return (volts - 0.500F) / 0.0100F;
    }
    if (volts < 1.7525F)
    {
        return 100.0F + ((volts - 1.500F) / 0.0101F);
    }
    return 125.0F + ((volts - 1.7525F) / 0.0106F);
}

bool dab_measurements_zero_set(dab_current_zero_t const * p_zero)
{
    if (NULL == p_zero)
    {
        return false;
    }
    if ((p_zero->iin_v < ACS_ZERO_MIN_V) || (p_zero->iin_v > ACS_ZERO_MAX_V) ||
        (p_zero->iout_v < ACS_ZERO_MIN_V) || (p_zero->iout_v > ACS_ZERO_MAX_V) ||
        (p_zero->ipri_v < TMCS_DIVIDED_ZERO_MIN_V) ||
        (p_zero->ipri_v > TMCS_DIVIDED_ZERO_MAX_V) ||
        (p_zero->isec_v < TMCS_DIVIDED_ZERO_MIN_V) ||
        (p_zero->isec_v > TMCS_DIVIDED_ZERO_MAX_V))
    {
        return false;
    }
    s_zero = *p_zero;
    return true;
}

void dab_measurements_zero_get(dab_current_zero_t * p_zero)
{
    if (NULL != p_zero)
    {
        *p_zero = s_zero;
    }
}

DAB_ITCM_CODE bool dab_measurements_update(dab_adc_frame_t const * p_frame,
                                           dab_measurements_t * p_measurements)
{
    if ((NULL == p_frame) || (NULL == p_measurements) ||
        (0U != p_frame->invalid_mask))
    {
        return false;
    }

    dab_measurements_t sample;
    memset(&sample, 0, sizeof(sample));
    sample.sequence = p_frame->sequence;
    dab_measurements_frame_convert(p_frame, &sample);

    /*
     * VIN and VOUT may legitimately be exactly 0 V before first power, so a
     * zero raw code is not a general ADC fault.  The four current channels
     * and VAFE have non-zero bias references; their plausibility and all
     * engineering-unit limits are checked below and by the safety controller.
     */

    sample.vin_v         = sample.adc_v[DAB_ADC_VIN] * VIN_VOLTS_PER_ADC_VOLT;
    sample.vout_v        = sample.adc_v[DAB_ADC_VOUT] * VOUT_VOLTS_PER_ADC_VOLT;
    sample.iin_a         = (sample.adc_v[DAB_ADC_IIN] - s_zero.iin_v) /
                           IIN_ADC_VOLTS_PER_AMP;
    sample.iout_a        = (sample.adc_v[DAB_ADC_IOUT] - s_zero.iout_v) /
                           IOUT_ADC_VOLTS_PER_AMP;
    sample.ipri_a        = (sample.adc_v[DAB_ADC_IPRI] - s_zero.ipri_v) /
                           IBRIDGE_ADC_VOLTS_PER_AMP;
    sample.isec_a        = (sample.adc_v[DAB_ADC_ISEC] - s_zero.isec_v) /
                           IBRIDGE_ADC_VOLTS_PER_AMP;
    sample.vafe_half_v   = sample.adc_v[DAB_ADC_VAFE_HALF];
    sample.temp_sec_adc_v = sample.adc_v[DAB_ADC_TEMP_SEC];
    sample.temp_pri_adc_v = sample.adc_v[DAB_ADC_TEMP_PRI];
    sample.temp_sec_c     = dab_measurements_isotmp35_c(sample.temp_sec_adc_v);
    sample.temp_pri_c     = dab_measurements_isotmp35_c(sample.temp_pri_adc_v);

    if ((sample.vafe_half_v < VAFE_HALF_MIN_V) ||
        (sample.vafe_half_v > VAFE_HALF_MAX_V))
    {
        sample.valid = false;
        return false;
    }

    if (!s_filter_initialized)
    {
        s_filtered = sample;
        s_filter_initialized = true;
    }
    else
    {
        s_filtered.sequence = sample.sequence;
        memcpy(s_filtered.raw, sample.raw, sizeof(s_filtered.raw));
        memcpy(s_filtered.adc_v, sample.adc_v, sizeof(s_filtered.adc_v));
        s_filtered.vin_v   = dab_measurements_filter(s_filtered.vin_v, sample.vin_v);
        s_filtered.vout_v  = dab_measurements_filter(s_filtered.vout_v, sample.vout_v);
        s_filtered.iin_a   = dab_measurements_filter(s_filtered.iin_a, sample.iin_a);
        s_filtered.iout_a  = dab_measurements_filter(s_filtered.iout_a, sample.iout_a);
        s_filtered.ipri_a  = dab_measurements_filter(s_filtered.ipri_a, sample.ipri_a);
        s_filtered.isec_a  = dab_measurements_filter(s_filtered.isec_a, sample.isec_a);
        s_filtered.vafe_half_v = dab_measurements_filter(s_filtered.vafe_half_v,
                                                         sample.vafe_half_v);
        s_filtered.temp_sec_adc_v = dab_measurements_filter(s_filtered.temp_sec_adc_v,
                                                            sample.temp_sec_adc_v);
        s_filtered.temp_pri_adc_v = dab_measurements_filter(s_filtered.temp_pri_adc_v,
                                                            sample.temp_pri_adc_v);
        s_filtered.temp_sec_c = dab_measurements_filter(s_filtered.temp_sec_c,
                                                       sample.temp_sec_c);
        s_filtered.temp_pri_c = dab_measurements_filter(s_filtered.temp_pri_c,
                                                       sample.temp_pri_c);
    }

    s_filtered.pin_w  = s_filtered.vin_v * s_filtered.iin_a;
    s_filtered.pout_w = s_filtered.vout_v * s_filtered.iout_a;
    s_filtered.valid  = true;
    *p_measurements   = s_filtered;
    return true;
}

void dab_measurements_print(dab_measurements_t const * p_measurements)
{
    if (NULL == p_measurements)
    {
        return;
    }
    printf("[MEAS] n=%lu VIN=%.3fV VOUT=%.3fV IIN=%.3fA IOUT=%.3fA "
           "IPRI=%.3fA ISEC=%.3fA TPRI=%.1fC TSEC=%.1fC "
           "VAFE/2=%.3fV valid=%u\r\n",
           (unsigned long) p_measurements->sequence,
           (double) p_measurements->vin_v,
           (double) p_measurements->vout_v,
           (double) p_measurements->iin_a,
           (double) p_measurements->iout_a,
           (double) p_measurements->ipri_a,
           (double) p_measurements->isec_a,
           (double) p_measurements->temp_pri_c,
           (double) p_measurements->temp_sec_c,
           (double) p_measurements->vafe_half_v,
           p_measurements->valid ? 1U : 0U);
}

bool dab_measurements_helium_enabled(void)
{
    return dab_power_dsp_helium_enabled();
}
