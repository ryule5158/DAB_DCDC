#ifndef DAB_ADVANCED_CONTROL_H
#define DAB_ADVANCED_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAB_ADVANCED_CONTROL_VERSION                 "2026-08-17-v2"

typedef enum e_dab_autotune_stage
{
    DAB_AUTOTUNE_IDLE = 0,
    DAB_AUTOTUNE_SETTLE,
    DAB_AUTOTUNE_POSITIVE_STEP,
    DAB_AUTOTUNE_NEGATIVE_STEP,
    DAB_AUTOTUNE_COMPLETE,
    DAB_AUTOTUNE_FAILED
} dab_autotune_stage_t;

typedef enum e_dab_autotune_failure
{
    DAB_AUTOTUNE_FAILURE_NONE = 0,
    DAB_AUTOTUNE_FAILURE_INSUFFICIENT_RESPONSE,
    DAB_AUTOTUNE_FAILURE_USER_STOP,
    DAB_AUTOTUNE_FAILURE_CONTROL_RESET
} dab_autotune_failure_t;

typedef struct st_dab_autotune_config
{
    float perturbation_ns;
    float settle_time_s;
    float sample_time_s;
    float minimum_current_delta_a;
    float desired_loop_product;
    float integral_time_s;
    bool auto_apply;
} dab_autotune_config_t;

typedef struct st_dab_autotune_result
{
    float local_gain_a_per_ns;
    float positive_current_a;
    float negative_current_a;
    float estimated_min_current_a;
    float estimated_max_current_a;
    float recommended_kp_ns_per_a;
    float recommended_ki_us_per_as;
    float recommended_slew_a_per_s;
    float recommended_phase_cap_ns;
    int8_t recommended_direction;
    dab_autotune_failure_t failure_reason;
    bool valid;
    bool applied;
} dab_autotune_result_t;

typedef struct st_dab_autotune_state
{
    dab_autotune_config_t config;
    dab_autotune_result_t result;
    dab_autotune_stage_t stage;
    float stage_time_s;
    float current_sum_a;
    uint32_t current_samples;
    float anchor_phase_ns;
    float phase_cap_ns;
    float anchor_current_a;
    float injection_ns;
    bool running;
} dab_autotune_state_t;

typedef enum e_dab_modswitch_stage
{
    DAB_MODSWITCH_IDLE = 0,
    DAB_MODSWITCH_TRANSITION,
    DAB_MODSWITCH_VERIFY,
    DAB_MODSWITCH_COMPLETE,
    DAB_MODSWITCH_ROLLBACK,
    DAB_MODSWITCH_FAILED
} dab_modswitch_stage_t;

typedef enum e_dab_modswitch_failure
{
    DAB_MODSWITCH_FAILURE_NONE = 0,
    DAB_MODSWITCH_FAILURE_METRIC_ABORT,
    DAB_MODSWITCH_FAILURE_TIMEOUT,
    DAB_MODSWITCH_FAILURE_CONTROL_RESET
} dab_modswitch_failure_t;

typedef struct st_dab_modswitch_config
{
    float inner_slew_ns_per_ms;
    float allowed_metric_error_percent;
    float abort_metric_error_percent;
    float verify_time_s;
    float timeout_s;
    float minimum_dwell_s;
} dab_modswitch_config_t;

typedef struct st_dab_modswitch_state
{
    dab_modswitch_config_t config;
    dab_modswitch_stage_t stage;
    float elapsed_s;
    float verify_elapsed_s;
    float dwell_elapsed_s;
    float baseline_metric;
    float metric_error_percent;
    float target_primary_inner_ns;
    float target_secondary_inner_ns;
    uint8_t source_mode;
    uint8_t target_mode;
    int8_t source_direction;
    int8_t target_direction;
    dab_modswitch_failure_t failure_reason;
    bool rollback_requested;
} dab_modswitch_state_t;

typedef struct st_dab_cp_damping_config
{
    bool enabled;
    float target_power_w;
    float target_slew_w_per_s;
    float power_kp_a_per_w;
    float power_ki_a_per_ws;
    float virtual_conductance_a_per_v;
    float nominal_output_v;
    float minimum_output_v;
    float current_limit_a;
} dab_cp_damping_config_t;

typedef struct st_dab_cp_damping_state
{
    dab_cp_damping_config_t config;
    float active_power_target_w;
    float power_error_w;
    float integral_current_a;
    float feedforward_current_a;
    float damping_current_a;
    float requested_current_a;
    float effective_current_limit_a;
    bool saturated;
} dab_cp_damping_state_t;

static inline float dab_advanced_clamp(float value, float minimum, float maximum)
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

static inline float dab_advanced_abs(float value)
{
    return (value < 0.0F) ? -value : value;
}

static inline float dab_advanced_slew(float current, float target, float step)
{
    if (target > current)
    {
        float const next = current + step;
        return (next < target) ? next : target;
    }
    float const next = current - step;
    return (next > target) ? next : target;
}

static inline void dab_autotune_initialize(dab_autotune_state_t * p_state)
{
    if (NULL == p_state)
    {
        return;
    }
    p_state->config.perturbation_ns = 8.0F;
    p_state->config.settle_time_s = 0.50F;
    p_state->config.sample_time_s = 0.30F;
    p_state->config.minimum_current_delta_a = 0.02F;
    p_state->config.desired_loop_product = 0.25F;
    p_state->config.integral_time_s = 0.020F;
    p_state->config.auto_apply = true;
    p_state->stage = DAB_AUTOTUNE_IDLE;
    p_state->stage_time_s = 0.0F;
    p_state->current_sum_a = 0.0F;
    p_state->current_samples = 0U;
    p_state->anchor_phase_ns = 0.0F;
    p_state->phase_cap_ns = 0.0F;
    p_state->anchor_current_a = 0.0F;
    p_state->injection_ns = 0.0F;
    p_state->running = false;
    p_state->result.valid = false;
    p_state->result.applied = false;
    p_state->result.failure_reason = DAB_AUTOTUNE_FAILURE_NONE;
}

static inline bool dab_autotune_start(dab_autotune_state_t * p_state,
                                      float phase_ns,
                                      float phase_cap_ns,
                                      float current_a)
{
    if ((NULL == p_state) || p_state->running ||
        (p_state->config.perturbation_ns <= 0.0F) ||
        (phase_ns <= p_state->config.perturbation_ns) ||
        ((phase_cap_ns - phase_ns) <= p_state->config.perturbation_ns))
    {
        return false;
    }
    p_state->stage = DAB_AUTOTUNE_SETTLE;
    p_state->stage_time_s = 0.0F;
    p_state->current_sum_a = 0.0F;
    p_state->current_samples = 0U;
    p_state->anchor_phase_ns = phase_ns;
    p_state->phase_cap_ns = phase_cap_ns;
    p_state->anchor_current_a = current_a;
    p_state->injection_ns = 0.0F;
    p_state->running = true;
    p_state->result.valid = false;
    p_state->result.applied = false;
    p_state->result.failure_reason = DAB_AUTOTUNE_FAILURE_NONE;
    return true;
}

static inline void dab_autotune_sample_reset(dab_autotune_state_t * p_state)
{
    p_state->stage_time_s = 0.0F;
    p_state->current_sum_a = 0.0F;
    p_state->current_samples = 0U;
}

static inline void dab_autotune_update(dab_autotune_state_t * p_state,
                                       float measured_current_a,
                                       float control_dt_s)
{
    if ((NULL == p_state) || !p_state->running)
    {
        return;
    }
    p_state->stage_time_s += control_dt_s;
    switch (p_state->stage)
    {
        case DAB_AUTOTUNE_SETTLE:
            p_state->injection_ns = 0.0F;
            if (p_state->stage_time_s >= p_state->config.settle_time_s)
            {
                p_state->stage = DAB_AUTOTUNE_POSITIVE_STEP;
                p_state->injection_ns = p_state->config.perturbation_ns;
                dab_autotune_sample_reset(p_state);
            }
            break;

        case DAB_AUTOTUNE_POSITIVE_STEP:
            p_state->injection_ns = p_state->config.perturbation_ns;
            p_state->current_sum_a += measured_current_a;
            p_state->current_samples++;
            if (p_state->stage_time_s >= p_state->config.sample_time_s)
            {
                p_state->result.positive_current_a = p_state->current_sum_a /
                    (float) p_state->current_samples;
                p_state->stage = DAB_AUTOTUNE_NEGATIVE_STEP;
                p_state->injection_ns = -p_state->config.perturbation_ns;
                dab_autotune_sample_reset(p_state);
            }
            break;

        case DAB_AUTOTUNE_NEGATIVE_STEP:
            p_state->injection_ns = -p_state->config.perturbation_ns;
            p_state->current_sum_a += measured_current_a;
            p_state->current_samples++;
            if (p_state->stage_time_s >= p_state->config.sample_time_s)
            {
                p_state->result.negative_current_a = p_state->current_sum_a /
                    (float) p_state->current_samples;
                float const current_delta_a =
                    p_state->result.positive_current_a -
                    p_state->result.negative_current_a;
                p_state->result.local_gain_a_per_ns = current_delta_a /
                    (2.0F * p_state->config.perturbation_ns);
                p_state->injection_ns = 0.0F;
                p_state->running = false;
                if (dab_advanced_abs(current_delta_a) <
                    p_state->config.minimum_current_delta_a)
                {
                    p_state->stage = DAB_AUTOTUNE_FAILED;
                    p_state->result.valid = false;
                    p_state->result.failure_reason =
                        DAB_AUTOTUNE_FAILURE_INSUFFICIENT_RESPONSE;
                    break;
                }

                float const gain_abs = dab_advanced_abs(
                    p_state->result.local_gain_a_per_ns);
                p_state->result.recommended_direction =
                    (p_state->result.local_gain_a_per_ns >= 0.0F) ? 1 : -1;
                p_state->result.recommended_kp_ns_per_a = dab_advanced_clamp(
                    p_state->config.desired_loop_product / gain_abs,
                    0.50F, 80.0F);
                p_state->result.recommended_ki_us_per_as = dab_advanced_clamp(
                    p_state->result.recommended_kp_ns_per_a /
                    (1000.0F * p_state->config.integral_time_s),
                    0.01F, 20.0F);
                p_state->result.recommended_slew_a_per_s = dab_advanced_clamp(
                    gain_abs * p_state->config.perturbation_ns * 2.0F,
                    0.05F, 5.0F);
                p_state->result.recommended_phase_cap_ns = dab_advanced_clamp(
                    p_state->anchor_phase_ns +
                    (4.0F * p_state->config.perturbation_ns),
                    p_state->anchor_phase_ns + p_state->config.perturbation_ns,
                    p_state->phase_cap_ns);
                float const current_at_zero = p_state->anchor_current_a -
                    (p_state->result.local_gain_a_per_ns *
                     p_state->anchor_phase_ns);
                float const current_at_cap = p_state->anchor_current_a +
                    (p_state->result.local_gain_a_per_ns *
                     (p_state->phase_cap_ns - p_state->anchor_phase_ns));
                p_state->result.estimated_min_current_a =
                    (current_at_zero < current_at_cap) ? current_at_zero :
                                                        current_at_cap;
                p_state->result.estimated_max_current_a =
                    (current_at_zero > current_at_cap) ? current_at_zero :
                                                        current_at_cap;
                p_state->result.estimated_min_current_a = dab_advanced_clamp(
                    p_state->result.estimated_min_current_a, 0.0F, 18.0F);
                p_state->result.estimated_max_current_a = dab_advanced_clamp(
                    p_state->result.estimated_max_current_a, 0.0F, 18.0F);
                p_state->result.valid = true;
                p_state->result.failure_reason = DAB_AUTOTUNE_FAILURE_NONE;
                p_state->stage = DAB_AUTOTUNE_COMPLETE;
            }
            break;

        default:
            p_state->injection_ns = 0.0F;
            p_state->running = false;
            break;
    }
}

static inline void dab_modswitch_initialize(dab_modswitch_state_t * p_state)
{
    if (NULL == p_state)
    {
        return;
    }
    p_state->config.inner_slew_ns_per_ms = 1.0F;
    p_state->config.allowed_metric_error_percent = 5.0F;
    p_state->config.abort_metric_error_percent = 20.0F;
    p_state->config.verify_time_s = 0.10F;
    p_state->config.timeout_s = 2.0F;
    p_state->config.minimum_dwell_s = 3.0F;
    p_state->stage = DAB_MODSWITCH_IDLE;
    p_state->elapsed_s = 0.0F;
    p_state->verify_elapsed_s = 0.0F;
    p_state->dwell_elapsed_s = 1000.0F;
    p_state->metric_error_percent = 0.0F;
    p_state->failure_reason = DAB_MODSWITCH_FAILURE_NONE;
    p_state->rollback_requested = false;
}

static inline bool dab_modswitch_start(dab_modswitch_state_t * p_state,
                                       uint8_t source_mode,
                                       uint8_t target_mode,
                                       int8_t source_direction,
                                       int8_t target_direction,
                                       float target_primary_inner_ns,
                                       float target_secondary_inner_ns,
                                       float baseline_metric)
{
    if ((NULL == p_state) ||
        ((DAB_MODSWITCH_TRANSITION == p_state->stage) ||
         (DAB_MODSWITCH_VERIFY == p_state->stage) ||
         (DAB_MODSWITCH_ROLLBACK == p_state->stage)) ||
        (p_state->dwell_elapsed_s < p_state->config.minimum_dwell_s) ||
        (baseline_metric <= 0.0F))
    {
        return false;
    }
    p_state->source_mode = source_mode;
    p_state->target_mode = target_mode;
    p_state->source_direction = source_direction;
    p_state->target_direction = target_direction;
    p_state->target_primary_inner_ns = target_primary_inner_ns;
    p_state->target_secondary_inner_ns = target_secondary_inner_ns;
    p_state->baseline_metric = baseline_metric;
    p_state->metric_error_percent = 0.0F;
    p_state->elapsed_s = 0.0F;
    p_state->verify_elapsed_s = 0.0F;
    p_state->dwell_elapsed_s = 0.0F;
    p_state->rollback_requested = false;
    p_state->failure_reason = DAB_MODSWITCH_FAILURE_NONE;
    p_state->stage = DAB_MODSWITCH_TRANSITION;
    return true;
}

static inline void dab_modswitch_update(dab_modswitch_state_t * p_state,
                                        float primary_inner_ns,
                                        float secondary_inner_ns,
                                        float current_metric,
                                        float control_dt_s)
{
    if (NULL == p_state)
    {
        return;
    }
    p_state->dwell_elapsed_s += control_dt_s;
    if ((DAB_MODSWITCH_TRANSITION != p_state->stage) &&
        (DAB_MODSWITCH_VERIFY != p_state->stage) &&
        (DAB_MODSWITCH_ROLLBACK != p_state->stage))
    {
        return;
    }
    p_state->elapsed_s += control_dt_s;
    p_state->metric_error_percent = 100.0F *
        dab_advanced_abs(current_metric - p_state->baseline_metric) /
        dab_advanced_clamp(p_state->baseline_metric, 0.001F, 100000.0F);
    if ((DAB_MODSWITCH_ROLLBACK != p_state->stage) &&
        (p_state->metric_error_percent >
         p_state->config.abort_metric_error_percent))
    {
        p_state->rollback_requested = true;
        p_state->failure_reason = DAB_MODSWITCH_FAILURE_METRIC_ABORT;
        p_state->stage = DAB_MODSWITCH_ROLLBACK;
        return;
    }
    if (p_state->elapsed_s >= p_state->config.timeout_s)
    {
        p_state->rollback_requested = true;
        if (DAB_MODSWITCH_FAILURE_NONE == p_state->failure_reason)
        {
            p_state->failure_reason = DAB_MODSWITCH_FAILURE_TIMEOUT;
        }
        p_state->stage = DAB_MODSWITCH_ROLLBACK;
        return;
    }

    bool const target_reached =
        (dab_advanced_abs(primary_inner_ns -
                          p_state->target_primary_inner_ns) <= 4.0F) &&
        (dab_advanced_abs(secondary_inner_ns -
                          p_state->target_secondary_inner_ns) <= 4.0F);
    if ((DAB_MODSWITCH_TRANSITION == p_state->stage) && target_reached)
    {
        p_state->stage = DAB_MODSWITCH_VERIFY;
        p_state->verify_elapsed_s = 0.0F;
    }
    if (DAB_MODSWITCH_VERIFY == p_state->stage)
    {
        if (p_state->metric_error_percent <=
            p_state->config.allowed_metric_error_percent)
        {
            p_state->verify_elapsed_s += control_dt_s;
            if (p_state->verify_elapsed_s >= p_state->config.verify_time_s)
            {
                p_state->stage = DAB_MODSWITCH_COMPLETE;
            }
        }
        else
        {
            p_state->verify_elapsed_s = 0.0F;
        }
    }
}

static inline void dab_cp_damping_initialize(dab_cp_damping_state_t * p_state)
{
    if (NULL == p_state)
    {
        return;
    }
    p_state->config.enabled = false;
    p_state->config.target_power_w = 100.0F;
    p_state->config.target_slew_w_per_s = 50.0F;
    p_state->config.power_kp_a_per_w = 0.005F;
    p_state->config.power_ki_a_per_ws = 0.050F;
    p_state->config.virtual_conductance_a_per_v = 0.050F;
    p_state->config.nominal_output_v = 44.0F;
    p_state->config.minimum_output_v = 18.0F;
    p_state->config.current_limit_a = 18.0F;
    p_state->active_power_target_w = 0.0F;
    p_state->power_error_w = 0.0F;
    p_state->integral_current_a = 0.0F;
    p_state->feedforward_current_a = 0.0F;
    p_state->damping_current_a = 0.0F;
    p_state->requested_current_a = 0.0F;
    p_state->effective_current_limit_a = 0.0F;
    p_state->saturated = false;
}

static inline void dab_cp_damping_runtime_reset(
    dab_cp_damping_state_t * p_state)
{
    if (NULL == p_state)
    {
        return;
    }
    p_state->active_power_target_w = 0.0F;
    p_state->power_error_w = 0.0F;
    p_state->integral_current_a = 0.0F;
    p_state->feedforward_current_a = 0.0F;
    p_state->damping_current_a = 0.0F;
    p_state->requested_current_a = 0.0F;
    p_state->effective_current_limit_a = 0.0F;
    p_state->saturated = false;
}

static inline float dab_cp_damping_update(dab_cp_damping_state_t * p_state,
                                          float measured_output_v,
                                          float measured_output_power_w,
                                          float output_power_limit_w,
                                          float control_dt_s)
{
    if (NULL == p_state)
    {
        return 0.0F;
    }
    if ((output_power_limit_w <= 0.0F) || (control_dt_s <= 0.0F))
    {
        dab_cp_damping_runtime_reset(p_state);
        return 0.0F;
    }

    /* A CP load does not establish the output bus.  Below the normal CP
     * calculation floor, bootstrap from Ptarget/Vnom instead of requesting
     * zero current forever.  Both the power-target slew and CPU0 current slew
     * remain active, so crossing into the ordinary P/Vout loop is continuous. */
    if (measured_output_v < p_state->config.minimum_output_v)
    {
        float const slew_step = p_state->config.target_slew_w_per_s *
                                control_dt_s;
        p_state->active_power_target_w = dab_advanced_slew(
            p_state->active_power_target_w,
            p_state->config.target_power_w,
            slew_step);
        p_state->power_error_w = 0.0F;
        p_state->integral_current_a = 0.0F;
        p_state->feedforward_current_a =
            p_state->active_power_target_w /
            dab_advanced_clamp(p_state->config.nominal_output_v, 1.0F, 1000.0F);
        p_state->damping_current_a = 0.0F;
        p_state->effective_current_limit_a = dab_advanced_clamp(
            output_power_limit_w /
            dab_advanced_clamp(p_state->config.nominal_output_v, 1.0F, 1000.0F),
            0.0F,
            p_state->config.current_limit_a);
        p_state->requested_current_a = dab_advanced_clamp(
            p_state->feedforward_current_a,
            0.0F,
            p_state->effective_current_limit_a);
        p_state->saturated =
            p_state->feedforward_current_a > p_state->effective_current_limit_a;
        return p_state->requested_current_a;
    }
    float const power_current_limit_a = output_power_limit_w /
                                        measured_output_v;
    p_state->effective_current_limit_a = dab_advanced_clamp(
        power_current_limit_a, 0.0F, p_state->config.current_limit_a);
    float const slew_step = p_state->config.target_slew_w_per_s * control_dt_s;
    p_state->active_power_target_w = dab_advanced_slew(
        p_state->active_power_target_w,
        p_state->config.target_power_w,
        slew_step);
    p_state->power_error_w = p_state->active_power_target_w -
                             measured_output_power_w;
    p_state->feedforward_current_a = p_state->active_power_target_w /
                                     measured_output_v;
    /* Positive virtual conductance: a falling bus reduces the requested
     * current, counteracting the negative incremental impedance of CP. */
    p_state->damping_current_a = p_state->config.enabled ?
        p_state->config.virtual_conductance_a_per_v *
        (measured_output_v - p_state->config.nominal_output_v) : 0.0F;
    float integral_candidate = p_state->integral_current_a +
        (p_state->config.power_ki_a_per_ws * p_state->power_error_w *
         control_dt_s);
    integral_candidate = dab_advanced_clamp(
        integral_candidate,
        -p_state->effective_current_limit_a,
        p_state->effective_current_limit_a);
    float const raw_current = p_state->feedforward_current_a +
        (p_state->config.power_kp_a_per_w * p_state->power_error_w) +
        integral_candidate + p_state->damping_current_a;
    p_state->requested_current_a = dab_advanced_clamp(
        raw_current, 0.0F, p_state->effective_current_limit_a);
    p_state->saturated = (raw_current < 0.0F) ||
                         (raw_current > p_state->effective_current_limit_a);
    if (!p_state->saturated)
    {
        p_state->integral_current_a = integral_candidate;
    }
    return p_state->requested_current_a;
}

#endif /* DAB_ADVANCED_CONTROL_H */
