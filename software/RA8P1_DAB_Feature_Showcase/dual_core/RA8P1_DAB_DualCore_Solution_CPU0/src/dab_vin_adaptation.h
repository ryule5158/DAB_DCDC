#ifndef DAB_VIN_ADAPTATION_H
#define DAB_VIN_ADAPTATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAB_VIN_ADAPTATION_VERSION                    "2026-08-17-v1"
#define DAB_VIN_ADAPTATION_NOMINAL_V                  (24.0F)
#define DAB_VIN_ADAPTATION_MINIMUM_V                  (18.0F)
#define DAB_VIN_ADAPTATION_MAXIMUM_V                  (24.0F)
#define DAB_VIN_ADAPTATION_FILTER_ALPHA               (0.0020F)
#define DAB_VIN_ADAPTATION_FEEDFORWARD_DEFAULT        (0.50F)
#define DAB_VIN_ADAPTATION_GAIN_SCALE_MIN             (0.75F)
#define DAB_VIN_ADAPTATION_GAIN_SCALE_MAX             (1.50F)
#define DAB_VIN_ADAPTATION_FEEDFORWARD_STEP_COUNTS    (0.25F)
#define DAB_VIN_ADAPTATION_LIMIT_DEBOUNCE_S            (0.50F)
#define DAB_VIN_ADAPTATION_LIMIT_MARGIN_PERCENT       (2.0F)

typedef enum e_dab_vin_adaptation_band
{
    DAB_VIN_ADAPTATION_BAND_18V = 0,
    DAB_VIN_ADAPTATION_BAND_20V,
    DAB_VIN_ADAPTATION_BAND_22V,
    DAB_VIN_ADAPTATION_BAND_24V
} dab_vin_adaptation_band_t;

typedef struct st_dab_vin_adaptation_config
{
    bool enabled;
    float nominal_vin_v;
    float minimum_vin_v;
    float maximum_vin_v;
    float filter_alpha;
    float feedforward_strength;
    float gain_scale_minimum;
    float gain_scale_maximum;
    float limit_debounce_s;
} dab_vin_adaptation_config_t;

typedef struct st_dab_vin_adaptation_input
{
    float vin_v;
    float requested_current_a;
    float measured_current_a;
    float measured_output_power_w;
    float phase_command_counts;
    float phase_limit_counts;
    float base_kp_ns_per_a;
    float base_ki_us_per_as;
    float output_power_ceiling_w;
    float control_dt_s;
    int8_t current_control_direction;
    bool current_control_active;
} dab_vin_adaptation_input_t;

typedef struct st_dab_vin_adaptation_output
{
    float filtered_vin_v;
    float gain_scale;
    float effective_kp_ns_per_a;
    float effective_ki_us_per_as;
    float feedforward_delta_counts;
    float phase_headroom_percent;
    float estimated_max_output_power_w;
    float actuator_limit_time_s;
    dab_vin_adaptation_band_t band;
    bool active;
    bool estimate_valid;
    bool actuator_limited;
} dab_vin_adaptation_output_t;

typedef struct st_dab_vin_adaptation_state
{
    dab_vin_adaptation_config_t config;
    dab_vin_adaptation_output_t output;
    float previous_filtered_vin_v;
    bool filter_initialized;
} dab_vin_adaptation_state_t;

static inline float dab_vin_adaptation_clamp(float value,
                                              float minimum,
                                              float maximum)
{
    if (value < minimum)
    {
        return minimum;
    }
    if (value > maximum)
    {
        return maximum;
    }
    return value;
}

static inline float dab_vin_adaptation_abs(float value)
{
    return (value < 0.0F) ? -value : value;
}

static inline void dab_vin_adaptation_runtime_reset(
    dab_vin_adaptation_state_t * p_state)
{
    if (NULL == p_state)
    {
        return;
    }
    bool const enabled = p_state->config.enabled;
    p_state->output.filtered_vin_v = 0.0F;
    p_state->output.gain_scale = 1.0F;
    p_state->output.effective_kp_ns_per_a = 0.0F;
    p_state->output.effective_ki_us_per_as = 0.0F;
    p_state->output.feedforward_delta_counts = 0.0F;
    p_state->output.phase_headroom_percent = 100.0F;
    p_state->output.estimated_max_output_power_w = 0.0F;
    p_state->output.actuator_limit_time_s = 0.0F;
    p_state->output.band = DAB_VIN_ADAPTATION_BAND_24V;
    p_state->output.active = false;
    p_state->output.estimate_valid = false;
    p_state->output.actuator_limited = false;
    p_state->previous_filtered_vin_v = 0.0F;
    p_state->filter_initialized = false;
    p_state->config.enabled = enabled;
}

static inline void dab_vin_adaptation_initialize(
    dab_vin_adaptation_state_t * p_state)
{
    if (NULL == p_state)
    {
        return;
    }
    p_state->config.enabled = false;
    p_state->config.nominal_vin_v = DAB_VIN_ADAPTATION_NOMINAL_V;
    p_state->config.minimum_vin_v = DAB_VIN_ADAPTATION_MINIMUM_V;
    p_state->config.maximum_vin_v = DAB_VIN_ADAPTATION_MAXIMUM_V;
    p_state->config.filter_alpha = DAB_VIN_ADAPTATION_FILTER_ALPHA;
    p_state->config.feedforward_strength =
        DAB_VIN_ADAPTATION_FEEDFORWARD_DEFAULT;
    p_state->config.gain_scale_minimum =
        DAB_VIN_ADAPTATION_GAIN_SCALE_MIN;
    p_state->config.gain_scale_maximum =
        DAB_VIN_ADAPTATION_GAIN_SCALE_MAX;
    p_state->config.limit_debounce_s =
        DAB_VIN_ADAPTATION_LIMIT_DEBOUNCE_S;
    dab_vin_adaptation_runtime_reset(p_state);
}

static inline dab_vin_adaptation_band_t dab_vin_adaptation_band_get(
    float vin_v)
{
    if (vin_v >= 23.0F)
    {
        return DAB_VIN_ADAPTATION_BAND_24V;
    }
    if (vin_v >= 21.0F)
    {
        return DAB_VIN_ADAPTATION_BAND_22V;
    }
    if (vin_v >= 19.0F)
    {
        return DAB_VIN_ADAPTATION_BAND_20V;
    }
    return DAB_VIN_ADAPTATION_BAND_18V;
}

/*
 * This deterministic layer never changes the requested current, modulation,
 * phase cap, or stored base PI gains.  It only schedules the effective current
 * PI gains and adds a rate-limited, direction-aware phase-integrator
 * feed-forward term while explicitly enabled.  OFF is therefore bit-for-bit
 * equivalent to the original current loop.
 */
static inline void dab_vin_adaptation_update(
    dab_vin_adaptation_state_t * p_state,
    dab_vin_adaptation_input_t const * p_input)
{
    if ((NULL == p_state) || (NULL == p_input))
    {
        return;
    }

    dab_vin_adaptation_output_t * const o = &p_state->output;
    o->effective_kp_ns_per_a = p_input->base_kp_ns_per_a;
    o->effective_ki_us_per_as = p_input->base_ki_us_per_as;
    o->gain_scale = 1.0F;
    o->feedforward_delta_counts = 0.0F;
    o->active = p_state->config.enabled &&
                p_input->current_control_active &&
                (p_input->vin_v > 1.0F) &&
                (p_input->phase_limit_counts > 0.0F);
    if (!o->active)
    {
        o->actuator_limit_time_s = 0.0F;
        o->actuator_limited = false;
        o->estimate_valid = false;
        return;
    }

    if (!p_state->filter_initialized)
    {
        o->filtered_vin_v = p_input->vin_v;
        p_state->previous_filtered_vin_v = p_input->vin_v;
        p_state->filter_initialized = true;
    }
    else
    {
        float const alpha = dab_vin_adaptation_clamp(
            p_state->config.filter_alpha, 0.0001F, 1.0F);
        o->filtered_vin_v += alpha *
            (p_input->vin_v - o->filtered_vin_v);
    }

    float const scheduled_vin_v = dab_vin_adaptation_clamp(
        o->filtered_vin_v,
        p_state->config.minimum_vin_v,
        p_state->config.maximum_vin_v);
    float const raw_scale = p_state->config.nominal_vin_v / scheduled_vin_v;
    o->gain_scale = dab_vin_adaptation_clamp(
        raw_scale,
        p_state->config.gain_scale_minimum,
        p_state->config.gain_scale_maximum);
    o->effective_kp_ns_per_a = p_input->base_kp_ns_per_a * o->gain_scale;
    o->effective_ki_us_per_as = p_input->base_ki_us_per_as * o->gain_scale;
    o->band = dab_vin_adaptation_band_get(scheduled_vin_v);

    float const vin_delta_ratio =
        (p_state->previous_filtered_vin_v - o->filtered_vin_v) /
        dab_vin_adaptation_clamp(o->filtered_vin_v, 1.0F, 1000.0F);
    float const direction = (p_input->current_control_direction < 0) ?
                            -1.0F : 1.0F;
    float feedforward_delta = p_input->phase_command_counts *
        vin_delta_ratio * p_state->config.feedforward_strength * direction;
    feedforward_delta = dab_vin_adaptation_clamp(
        feedforward_delta,
        -DAB_VIN_ADAPTATION_FEEDFORWARD_STEP_COUNTS,
        DAB_VIN_ADAPTATION_FEEDFORWARD_STEP_COUNTS);
    o->feedforward_delta_counts = feedforward_delta;
    p_state->previous_filtered_vin_v = o->filtered_vin_v;

    float const phase = dab_vin_adaptation_clamp(
        p_input->phase_command_counts, 0.0F, p_input->phase_limit_counts);
    float const phase_headroom = (p_input->current_control_direction < 0) ?
                                 phase :
                                 (p_input->phase_limit_counts - phase);
    o->phase_headroom_percent = 100.0F * phase_headroom /
                                p_input->phase_limit_counts;

    float const error_a = p_input->requested_current_a -
                          p_input->measured_current_a;
    float const error_tolerance_a = dab_vin_adaptation_clamp(
        0.03F * p_input->requested_current_a, 0.05F, 0.50F);
    float const directional_error_a = error_a * direction;
    bool const at_upper_limit =
        phase >= (0.98F * p_input->phase_limit_counts);
    bool const at_lower_limit = phase <=
        (0.01F * p_input->phase_limit_counts);
    bool const blocked_by_actuator =
        ((directional_error_a > error_tolerance_a) && at_upper_limit) ||
        ((directional_error_a < -error_tolerance_a) && at_lower_limit);
    if (blocked_by_actuator)
    {
        o->actuator_limit_time_s += p_input->control_dt_s;
    }
    else
    {
        o->actuator_limit_time_s = 0.0F;
    }
    o->actuator_limited =
        o->actuator_limit_time_s >= p_state->config.limit_debounce_s;

    o->estimate_valid = (p_input->current_control_direction >= 0) &&
                        (phase >= 1.0F) &&
                        (p_input->measured_output_power_w > 0.0F);
    if (o->estimate_valid)
    {
        float estimate_w = p_input->measured_output_power_w *
            p_input->phase_limit_counts / phase;
        estimate_w = dab_vin_adaptation_clamp(
            estimate_w,
            p_input->measured_output_power_w,
            p_input->output_power_ceiling_w);
        o->estimated_max_output_power_w = estimate_w;
    }
    else
    {
        o->estimated_max_output_power_w = 0.0F;
    }
}

#endif /* DAB_VIN_ADAPTATION_H */
