#include "dab_production.h"

#include "dab_accel.h"
#include "dab_production_config.h"

#include <stddef.h>

static float dab_minf(float a, float b)
{
    return (a < b) ? a : b;
}

static float dab_maxf(float a, float b)
{
    return (a > b) ? a : b;
}

static float dab_clampf(float value, float lower, float upper)
{
    return dab_minf(dab_maxf(value, lower), upper);
}

void dab_production_controller_reset(dab_production_controller_t * p_controller)
{
    if (NULL != p_controller)
    {
        p_controller->integral_counts = 0.0F;
        p_controller->phase_counts = 0.0F;
        p_controller->current_limited = false;
        p_controller->power_limited = false;
    }
}

float dab_production_ipri_soft_limit(dab_production_request_t const * p_request)
{
    return DAB_IPRI_SOFT_BASE_A +
           (DAB_IPRI_SOFT_PER_W_A * p_request->power_limit_w);
}

float dab_production_isec_soft_limit(dab_production_request_t const * p_request)
{
    return DAB_ISEC_SOFT_BASE_A +
           (DAB_ISEC_SOFT_PER_W_A * p_request->power_limit_w);
}

float dab_production_iin_hard_limit(dab_production_request_t const * p_request,
                                    dab_measurements_t const * p_measurements)
{
    float const denominator = dab_maxf(p_measurements->vin_v,
                                       DAB_VIN_START_MIN_V);
    float const calculated = ((p_request->power_limit_w / denominator) *
                              DAB_BUS_CURRENT_MARGIN) +
                             DAB_BUS_CURRENT_EXTRA_A;
    return dab_minf(calculated, DAB_IIN_ABSOLUTE_HARD_A);
}

float dab_production_iout_hard_limit(dab_production_request_t const * p_request,
                                     dab_measurements_t const * p_measurements)
{
    float const denominator = dab_maxf(p_measurements->vout_v,
                                       DAB_VREF_MIN_V);
    float const calculated = ((p_request->power_limit_w / denominator) *
                              DAB_BUS_CURRENT_MARGIN) +
                             DAB_BUS_CURRENT_EXTRA_A;
    return dab_minf(calculated, DAB_IOUT_ABSOLUTE_HARD_A);
}

DAB_ITCM_CODE uint32_t dab_production_phase_limit(
    dab_production_request_t const * p_request,
    dab_measurements_t const * p_measurements)
{
    if (p_measurements->vout_v < DAB_COLD_VOUT_1_V)
    {
        return DAB_COLD_PHASE_1_COUNTS;
    }
    if (p_measurements->vout_v < DAB_COLD_VOUT_2_V)
    {
        return DAB_COLD_PHASE_2_COUNTS;
    }
    if (p_measurements->vout_v < DAB_COLD_VOUT_3_V)
    {
        return DAB_COLD_PHASE_3_COUNTS;
    }
    return p_request->high_power_unlocked ? DAB_PHASE_ABSOLUTE_MAX_COUNTS :
                                            DAB_PHASE_NORMAL_MAX_COUNTS;
}

DAB_ITCM_CODE uint32_t dab_production_faults_evaluate(
    dab_production_safety_snapshot_t const * p_snapshot,
    dab_production_request_t const * p_request,
    bool evaluate_dynamic_power)
{
    if ((NULL == p_snapshot) || (NULL == p_request))
    {
        return DAB_FAULT_INIT;
    }

    uint32_t faults = DAB_FAULT_NONE;
    dab_board_inputs_t const * p_inputs = &p_snapshot->inputs;
    dab_measurements_t const * p_meas = &p_snapshot->measurements;

    if (!p_inputs->raw_fault_n)    { faults |= DAB_FAULT_RAW_N; }
    if (!p_inputs->ipri_oc_n)      { faults |= DAB_FAULT_IPRI_OC_N; }
    if (!p_inputs->isec_oc_n)      { faults |= DAB_FAULT_ISEC_OC_N; }
    if (!p_inputs->pri_hw_ok)      { faults |= DAB_FAULT_PRI_HW; }
    if (!p_inputs->sec_hw_ok)      { faults |= DAB_FAULT_SEC_HW; }
    if (!p_inputs->temp_amb_ot_n)  { faults |= DAB_FAULT_AMBIENT_OT; }
    if (!p_snapshot->poeg_ok)      { faults |= DAB_FAULT_POEG; }
    if (!p_meas->valid)            { faults |= DAB_FAULT_ADC; }
    if (!p_snapshot->adc_fresh)    { faults |= DAB_FAULT_ADC_STALE; }

    if (!p_meas->valid)
    {
        return faults;
    }

    if (p_meas->vin_v < DAB_VIN_RUN_MIN_V) { faults |= DAB_FAULT_VIN_UV; }
    if (p_meas->vin_v > DAB_VIN_MAX_V)     { faults |= DAB_FAULT_VIN_OV; }

    float const vout_trip = dab_minf(DAB_VOUT_ABSOLUTE_MAX_V,
                                     p_request->vref_v + 4.0F);
    if (p_meas->vout_v > vout_trip) { faults |= DAB_FAULT_VOUT_OV; }

    if ((p_meas->temp_pri_c > DAB_TEMPERATURE_MAX_C) ||
        (p_meas->temp_sec_c > DAB_TEMPERATURE_MAX_C))
    {
        faults |= DAB_FAULT_TEMP;
    }

    if (evaluate_dynamic_power)
    {
        float const ipri_hard = dab_minf(
            dab_production_ipri_soft_limit(p_request) +
            DAB_IPRI_HARD_MARGIN_A, DAB_IPRI_ABSOLUTE_HARD_A);
        float const isec_hard = dab_minf(
            dab_production_isec_soft_limit(p_request) +
            DAB_ISEC_HARD_MARGIN_A, DAB_ISEC_ABSOLUTE_HARD_A);

        if (dab_measurements_abs(p_meas->ipri_a) > ipri_hard)
        {
            faults |= DAB_FAULT_IPRI;
        }
        if (dab_measurements_abs(p_meas->isec_a) > isec_hard)
        {
            faults |= DAB_FAULT_ISEC;
        }
        if (dab_measurements_abs(p_meas->iin_a) >
            dab_production_iin_hard_limit(p_request, p_meas))
        {
            faults |= DAB_FAULT_IIN;
        }
        if (dab_measurements_abs(p_meas->iout_a) >
            dab_production_iout_hard_limit(p_request, p_meas))
        {
            faults |= DAB_FAULT_IOUT;
        }
        if ((dab_measurements_abs(p_meas->pin_w) >
             (p_request->power_limit_w * DAB_POWER_TRIP_MULTIPLIER)) ||
            (dab_measurements_abs(p_meas->pout_w) >
             (p_request->power_limit_w * DAB_POWER_TRIP_MULTIPLIER)))
        {
            faults |= DAB_FAULT_POWER;
        }
    }

    return faults;
}

bool dab_production_start_ready(
    dab_production_safety_snapshot_t const * p_snapshot,
    dab_start_block_t * p_block)
{
    dab_start_block_t block = DAB_START_OK;
    if (NULL == p_snapshot)
    {
        block = DAB_START_BLOCK_ADC;
    }
    else if (!p_snapshot->inputs.raw_fault_n ||
             !p_snapshot->inputs.ipri_oc_n ||
             !p_snapshot->inputs.isec_oc_n ||
             !p_snapshot->inputs.pri_hw_ok ||
             !p_snapshot->inputs.sec_hw_ok ||
             !p_snapshot->inputs.temp_amb_ot_n)
    {
        block = DAB_START_BLOCK_HARDWARE;
    }
    else if (!p_snapshot->poeg_ok)
    {
        block = DAB_START_BLOCK_POEG;
    }
    else if (!p_snapshot->measurements.valid)
    {
        block = DAB_START_BLOCK_ADC;
    }
    else if (!p_snapshot->adc_fresh)
    {
        block = DAB_START_BLOCK_ADC_STALE;
    }
    else if ((p_snapshot->measurements.vin_v < DAB_VIN_START_MIN_V) ||
             (p_snapshot->measurements.vin_v > DAB_VIN_MAX_V))
    {
        block = DAB_START_BLOCK_VIN;
    }
    else if ((dab_measurements_abs(p_snapshot->measurements.ipri_a) > 2.0F) ||
             (dab_measurements_abs(p_snapshot->measurements.isec_a) > 2.0F) ||
             (dab_measurements_abs(p_snapshot->measurements.iin_a) > 1.0F) ||
             (dab_measurements_abs(p_snapshot->measurements.iout_a) > 1.0F))
    {
        block = DAB_START_BLOCK_CURRENT;
    }
    else if ((p_snapshot->measurements.temp_pri_c > DAB_TEMPERATURE_MAX_C) ||
             (p_snapshot->measurements.temp_sec_c > DAB_TEMPERATURE_MAX_C))
    {
        block = DAB_START_BLOCK_TEMPERATURE;
    }

    if (NULL != p_block)
    {
        *p_block = block;
    }
    return DAB_START_OK == block;
}

DAB_ITCM_CODE void dab_production_control_step(
    dab_production_controller_t * p_controller,
    dab_production_request_t const * p_request,
    dab_measurements_t const * p_measurements,
    uint32_t phase_limit_counts,
    dab_production_control_result_t * p_result)
{
    if ((NULL == p_controller) || (NULL == p_request) ||
        (NULL == p_measurements) || (NULL == p_result))
    {
        return;
    }

    float const error_v = p_request->vref_v - p_measurements->vout_v;
    float const kp_counts_per_v = p_request->kp_ns_per_v / 4.0F;
    float const ki_counts_per_vs = p_request->ki_us_per_vs * 250.0F;
    float const phase_limit = (float) phase_limit_counts;

    float integral_candidate = p_controller->integral_counts +
        (error_v * ki_counts_per_vs * DAB_CONTROL_DT_S);
    integral_candidate = dab_clampf(integral_candidate, 0.0F, phase_limit);
    float target = dab_clampf((kp_counts_per_v * error_v) +
                              integral_candidate, 0.0F, phase_limit);

    bool const current_soft =
        (dab_measurements_abs(p_measurements->ipri_a) >
         dab_production_ipri_soft_limit(p_request)) ||
        (dab_measurements_abs(p_measurements->isec_a) >
         dab_production_isec_soft_limit(p_request));
    bool const power_soft =
        (dab_measurements_abs(p_measurements->pin_w) > p_request->power_limit_w) ||
        (dab_measurements_abs(p_measurements->pout_w) > p_request->power_limit_w);

    if (current_soft || power_soft)
    {
        target = dab_maxf(0.0F, p_controller->phase_counts -
                          (float) DAB_PHASE_RETREAT_COUNTS);
        integral_candidate = dab_minf(integral_candidate, target);
        p_controller->phase_counts = target;
    }
    else if (target > p_controller->phase_counts)
    {
        p_controller->phase_counts = dab_minf(
            target, p_controller->phase_counts +
                    DAB_PHASE_RISE_COUNTS_PER_UPDATE);
    }
    else
    {
        p_controller->phase_counts = dab_maxf(
            target, p_controller->phase_counts -
                    DAB_PHASE_FALL_COUNTS_PER_UPDATE);
    }

    p_controller->integral_counts = integral_candidate;
    p_controller->phase_counts = dab_clampf(p_controller->phase_counts,
                                             0.0F, phase_limit);
    p_controller->current_limited = current_soft;
    p_controller->power_limited = power_soft;

    p_result->phase_counts = (uint32_t) (p_controller->phase_counts + 0.5F);
    p_result->current_limited = current_soft;
    p_result->power_limited = power_soft;
}

const char * dab_production_fault_name(uint32_t fault_mask)
{
    static const char * const names[] =
    {
        "INIT", "ADC", "ADC_STALE", "RAW_FAULT_N", "IPRI_OC_N",
        "ISEC_OC_N", "POEG", "VIN_UV", "VIN_OV", "VOUT_OV",
        "IPRI", "ISEC", "IIN", "IOUT", "POWER", "NO_RISE",
        "PWM_UPDATE", "PRI_HW", "SEC_HW", "AMBIENT_OT", "TEMP",
        "USB_LINK"
    };
    if (0U == fault_mask)
    {
        return "NONE";
    }
    for (uint32_t bit = 0U; bit < (sizeof(names) / sizeof(names[0])); bit++)
    {
        if (0U != (fault_mask & (1UL << bit)))
        {
            return names[bit];
        }
    }
    return "UNKNOWN";
}

const char * dab_production_start_block_name(dab_start_block_t block)
{
    static const char * const names[] =
    {
        "OK", "HARDWARE", "POEG", "ADC", "ADC_STALE", "VIN",
        "CURRENT", "TEMPERATURE"
    };
    uint32_t const index = (uint32_t) block;
    return (index < (sizeof(names) / sizeof(names[0]))) ? names[index] :
                                                          "UNKNOWN";
}
