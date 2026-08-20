#ifndef DAB_MPPT_PHASE_SEARCH_H
#define DAB_MPPT_PHASE_SEARCH_H

#include <stdbool.h>
#include <stdint.h>

#define DAB_MPPT_PHASE_SEARCH_ENTRY_ID       (33090001UL)
/* r109 swept the current nominal-20 V / CV36 point continuously and proved
 * that SPS was still increasing when the fused entry reached 320 ns: the
 * board held about 110 W / 5.8 A with fault zero, short of the independent
 * 8 A source edge.  The fused entry may therefore use the already-defined
 * platform absolute 480 ns envelope.  Current reference and inner shifts
 * remain slew limited; ordinary charging, precharge and CP entries retain
 * their own smaller caps. */
#define DAB_MPPT_PHASE_SEARCH_PHASE_CAP_NS   (480.0F)
#define DAB_MPPT_PHASE_EPS_BOOTSTRAP_NS      (360.0F)
#define DAB_MPPT_PHASE_NEAR_MATCH_MIN_M      (0.86F)
#define DAB_MPPT_PHASE_NEAR_MATCH_MAX_M      (1.14F)
#define DAB_MPPT_PHASE_SEARCH_CURRENT_KP     (2.0F)
#define DAB_MPPT_PHASE_SEARCH_CURRENT_KI     (0.2F)
#define DAB_MPPT_PHASE_SEARCH_CURRENT_SLEW   (0.5F)
#define DAB_MPPT_PHASE_SEARCH_INNER_SLEW     (0.2F)
#define DAB_MPPT_PHASE_ACQUIRE_STEP_NS       (32.0F)
#define DAB_MPPT_PHASE_ACQUIRE_STEP_MS       (1000U)
#define DAB_MPPT_PHASE_CAP_HEADROOM_NS       (16.0F)
#define DAB_MPPT_PHASE_CAP_RELEASE_STEP_NS    (8.0F)
#define DAB_MPPT_PHASE_CAP_RELEASE_MS         (250U)
#define DAB_MPPT_PHASE_TRAJECTORY_STEP_V       (0.08F)
#define DAB_MPPT_PHASE_TRAJECTORY_POWER_W      (2.0F)
#define DAB_MPPT_PHASE_TRAJECTORY_POWER_RATIO  (0.01F)
#define DAB_MPPT_PHASE_LOCAL_FLOOR_MARGIN_V     (2.0F)
#define DAB_MPPT_PHASE_RECOVERY_HIGH_NS      (832.0F)
#define DAB_MPPT_PHASE_RECOVERY_LOW_NS       (180.0F)
#define DAB_MPPT_PHASE_RECOVERY_SETTLED_NS     (8.0F)
#define DAB_MPPT_PHASE_SEQUENCED_MIN_PIN_W    (210.0F)
#define DAB_MPPT_PHASE_SEQUENCED_MIN_VIN_V     (20.3F)
#define DAB_MPPT_PHASE_SEQUENCED_MAX_VIN_V     (21.5F)

typedef enum e_dab_mppt_phase_mode
{
    DAB_MPPT_PHASE_MODE_SPS = 0,
    DAB_MPPT_PHASE_MODE_EPS,
    DAB_MPPT_PHASE_MODE_TPS
} dab_mppt_phase_mode_t;

typedef enum e_dab_mppt_phase_stage
{
    DAB_MPPT_PHASE_DISABLED = 0,
    DAB_MPPT_PHASE_WAIT_SOURCE_PEAK,
    DAB_MPPT_PHASE_SPS_OBSERVE,
    DAB_MPPT_PHASE_EPS_SETTLE,
    DAB_MPPT_PHASE_EPS_SEARCH,
    DAB_MPPT_PHASE_TPS_SETTLE,
    DAB_MPPT_PHASE_TPS_SEARCH,
    DAB_MPPT_PHASE_HOLD_BEST
} dab_mppt_phase_stage_t;

typedef struct st_dab_mppt_phase_config
{
    float vin_min_v;
    float vin_recovery_margin_v;
    uint32_t settle_ms;
    uint32_t mode_observe_ms;
    uint32_t perturb_ms;
    uint32_t hold_grace_ms;
    uint32_t condition_confirm_ms;
    uint32_t replay_confirm_ms;
    float inner_step_ns;
    float pin_equivalent_percent;
    float efficiency_gain_percent;
    float condition_voltage_delta_v;
    float condition_output_voltage_delta_v;
    float condition_power_delta_percent;
    float replay_min_percent;
} dab_mppt_phase_config_t;

typedef struct st_dab_mppt_phase_input
{
    bool enabled;
    bool valid;
    uint32_t now_ms;
    float vin_v;
    float vout_v;
    float pin_w;
    float pout_w;
    float current_reference_a;
    float current_active_a;
    float measured_output_current_a;
    bool outer_saturated;
    dab_mppt_phase_mode_t applied_mode;
    float applied_outer_phase_ns;
    float applied_primary_inner_ns;
    float applied_secondary_inner_ns;
} dab_mppt_phase_input_t;

/* Hardware-qualified mode/actuator records from r111--r141.  They are not
 * fixed operating commands: live SPS/EPS/TPS measurements remain decisive.
 * A nearby qualified record is used only to break an equivalent live result,
 * which preserves deterministic AI-OFF behavior without hiding a better MPP.
 * r115 is retained as boundary evidence but is deliberately ineligible as a
 * runtime hint because its 12 A source setting is outside the later <=10 A
 * physical-test rule and its 299.88 W search peak was not sustained. */
typedef struct st_dab_mppt_phase_validated_record
{
    uint32_t record_id;
    float vin_v;
    float iin_a;
    float vout_v;
    dab_mppt_phase_mode_t mode;
    float outer_phase_ns;
    float primary_inner_ns;
    float secondary_inner_ns;
    float selected_pin_w;
    float sustained_pin_w;
    float efficiency_percent;
    bool runtime_hint_eligible;
} dab_mppt_phase_validated_record_t;

static const dab_mppt_phase_validated_record_t
    g_dab_mppt_phase_validated_records[] =
{
    {111U, 18.5476F,  8.4310F, 36.0F, DAB_MPPT_PHASE_MODE_SPS,
     448.0F,   0.0F,   0.0F, 163.83F, 156.3741F, 90.797F, true},
    {112U, 20.6815F,  8.4544F, 36.0F, DAB_MPPT_PHASE_MODE_EPS,
     240.0F,  45.0F,   0.0F, 176.93F, 174.8499F, 90.762F, true},
    {113U, 22.6062F,  8.5263F, 36.0F, DAB_MPPT_PHASE_MODE_TPS,
      64.0F, 234.0F, 180.0F, 187.96F, 192.7479F, 90.375F, true},
    {114U, 22.6247F, 11.1994F, 36.0F, DAB_MPPT_PHASE_MODE_EPS,
     196.0F,  61.0F,   0.0F, 256.75F, 253.3830F, 88.402F, true},
    {115U, 22.1820F, 12.2025F, 36.0F, DAB_MPPT_PHASE_MODE_SPS,
     264.0F,   0.0F,   0.0F, 299.88F, 270.6757F, 86.976F, false},
    {116U, 20.9818F,  9.0866F, 36.0F, DAB_MPPT_PHASE_MODE_SPS,
     148.0F,   0.0F,   0.0F, 185.97F, 190.6529F, 90.783F, true},
    /* r139: autonomous three-mode search, then >16 s stable at the proven
     * nominal-22 V / 10 A / CV36 point with fault zero. */
    {139U, 20.9080F, 10.4680F, 36.0F, DAB_MPPT_PHASE_MODE_SPS,
     424.0F,   0.0F,   0.0F, 224.51F, 218.8600F, 89.290F, true},
    /* r141: serial-upper-computer representative point. Only high-level
     * battery/MPPT commands were used; AI was OFF and EPS held the 7 A source
     * condition for 10 s with fault zero. */
    {141U, 20.8900F,  7.1320F, 35.969F, DAB_MPPT_PHASE_MODE_EPS,
     480.0F, 141.0F,   0.0F, 154.66F, 148.9780F, 90.987F, true}
};

#define DAB_MPPT_PHASE_VALIDATED_RECORD_COUNT \
    ((uint32_t) (sizeof(g_dab_mppt_phase_validated_records) / \
                 sizeof(g_dab_mppt_phase_validated_records[0])))

typedef struct st_dab_mppt_phase_output
{
    bool active;
    bool changed;
    bool selection_complete;
    dab_mppt_phase_stage_t stage;
    dab_mppt_phase_mode_t mode;
    float primary_inner_ns;
    float secondary_inner_ns;
    float selected_outer_phase_ns;
    float selected_pin_w;
    float selected_efficiency_percent;
    float phase_cap_ns;
} dab_mppt_phase_output_t;

typedef struct st_dab_mppt_phase_metric
{
    float pin_w;
    float efficiency_percent;
    float outer_phase_ns;
    float primary_inner_ns;
    float secondary_inner_ns;
    bool valid;
} dab_mppt_phase_metric_t;

typedef struct st_dab_mppt_phase_search
{
    dab_mppt_phase_config_t config;
    dab_mppt_phase_output_t output;
    dab_mppt_phase_metric_t mode_best[3];
    uint32_t stage_started_ms;
    uint32_t bucket_started_ms;
    uint32_t hold_confirm_started_ms;
    uint32_t replay_bad_started_ms;
    uint32_t acquisition_adjust_started_ms;
    uint32_t phase_cap_adjust_started_ms;
    float bucket_pin_sum_w;
    float bucket_pout_sum_w;
    float bucket_outer_phase_sum_ns;
    float bucket_primary_inner_sum_ns;
    float bucket_secondary_inner_sum_ns;
    uint32_t bucket_samples;
    float previous_current_reference_a;
    float source_best_pin_w;
    float source_best_vin_v;
    float search_floor_vin_v;
    float high_inner_ns;
    float low_inner_ns;
    int8_t high_direction;
    int8_t low_direction;
    bool source_peak_seen;
    bool source_reversal_seen;
    bool sps_infeasible;
    bool eps_infeasible;
    bool tps_infeasible;
    bool sequenced_recovery_inner;
    bool local_research;
    bool hold_anchor_valid;
    float hold_anchor_vin_v;
    float hold_anchor_vout_v;
    float hold_anchor_pin_w;
    bool trajectory_anchor_valid;
    float trajectory_anchor_vout_v;
    float trajectory_anchor_pin_w;
} dab_mppt_phase_search_t;

static inline float dab_mppt_phase_abs(float value)
{
    return (value < 0.0F) ? -value : value;
}

static inline float dab_mppt_phase_clamp(float value, float lower, float upper)
{
    if (value < lower)
    {
        return lower;
    }
    if (value > upper)
    {
        return upper;
    }
    return value;
}

static inline float dab_mppt_phase_efficiency(float pin_w, float pout_w)
{
    if ((pin_w <= 1.0F) || (pout_w < 0.0F))
    {
        return 0.0F;
    }
    return dab_mppt_phase_clamp(100.0F * pout_w / pin_w, 0.0F, 100.0F);
}

static inline bool dab_mppt_phase_validated_preferred_mode(
    dab_mppt_phase_input_t const * p_input,
    dab_mppt_phase_mode_t * p_mode)
{
    if ((!p_input) || (!p_mode) || (p_input->vin_v <= 1.0F))
    {
        return false;
    }
    float const iin_a = p_input->pin_w / p_input->vin_v;
    float best_score = 1000000.0F;
    bool found = false;
    for (uint32_t i = 0U; i < DAB_MPPT_PHASE_VALIDATED_RECORD_COUNT; i++)
    {
        dab_mppt_phase_validated_record_t const * const p_record =
            &g_dab_mppt_phase_validated_records[i];
        float const dv = dab_mppt_phase_abs(p_input->vin_v - p_record->vin_v);
        float const di = dab_mppt_phase_abs(iin_a - p_record->iin_a);
        float const dvo = dab_mppt_phase_abs(p_input->vout_v - p_record->vout_v);
        if (!p_record->runtime_hint_eligible || (dv > 1.25F) ||
            (di > 1.25F) || (dvo > 2.0F))
        {
            continue;
        }
        float const score = dv + (0.75F * di) + (0.25F * dvo);
        if (score < best_score)
        {
            best_score = score;
            *p_mode = p_record->mode;
            found = true;
        }
    }
    return found;
}

static inline void dab_mppt_phase_metric_clear(
    dab_mppt_phase_metric_t * p_metric)
{
    p_metric->pin_w = 0.0F;
    p_metric->efficiency_percent = 0.0F;
    p_metric->outer_phase_ns = 0.0F;
    p_metric->primary_inner_ns = 0.0F;
    p_metric->secondary_inner_ns = 0.0F;
    p_metric->valid = false;
}

static inline bool dab_mppt_phase_metric_better(
    dab_mppt_phase_search_t const * p_state,
    float pin_w,
    float efficiency_percent,
    dab_mppt_phase_metric_t const * p_reference)
{
    if (!p_reference->valid)
    {
        return true;
    }
    float const pin_margin = p_reference->pin_w *
        (p_state->config.pin_equivalent_percent / 100.0F);
    if (pin_w > (p_reference->pin_w + pin_margin))
    {
        return true;
    }
    return (pin_w >= (p_reference->pin_w - pin_margin)) &&
           (efficiency_percent >
            (p_reference->efficiency_percent +
             p_state->config.efficiency_gain_percent));
}

static inline void dab_mppt_phase_bucket_reset(
    dab_mppt_phase_search_t * p_state,
    uint32_t now_ms)
{
    p_state->bucket_started_ms = now_ms;
    p_state->bucket_pin_sum_w = 0.0F;
    p_state->bucket_pout_sum_w = 0.0F;
    p_state->bucket_outer_phase_sum_ns = 0.0F;
    p_state->bucket_primary_inner_sum_ns = 0.0F;
    p_state->bucket_secondary_inner_sum_ns = 0.0F;
    p_state->bucket_samples = 0U;
}

static inline bool dab_mppt_phase_sample_eligible(
    dab_mppt_phase_search_t const * p_state,
    dab_mppt_phase_input_t const * p_input)
{
    float const zero_tolerance_ns = 8.0F;
    if (DAB_MPPT_PHASE_MODE_SPS == p_state->output.mode)
    {
        return (dab_mppt_phase_abs(p_input->applied_primary_inner_ns) <=
                zero_tolerance_ns) &&
               (dab_mppt_phase_abs(p_input->applied_secondary_inner_ns) <=
                zero_tolerance_ns);
    }
    if (DAB_MPPT_PHASE_MODE_EPS == p_state->output.mode)
    {
        /* Allow the active inner phase to sweep, but require the bridge which
         * is nominally zero to have actually reached zero.  Every measured
         * power sample is then paired with the phase applied by the PWM. */
        bool const primary_is_zero =
            p_state->output.primary_inner_ns <= zero_tolerance_ns;
        return primary_is_zero ?
            (dab_mppt_phase_abs(p_input->applied_primary_inner_ns) <=
             zero_tolerance_ns) :
            (dab_mppt_phase_abs(p_input->applied_secondary_inner_ns) <=
             zero_tolerance_ns);
    }
    return true;
}

static inline void dab_mppt_phase_bucket_add(
    dab_mppt_phase_search_t * p_state,
    dab_mppt_phase_input_t const * p_input)
{
    if ((p_input->pin_w > 1.0F) && (p_input->pout_w >= 0.0F) &&
        dab_mppt_phase_sample_eligible(p_state, p_input))
    {
        p_state->bucket_pin_sum_w += p_input->pin_w;
        p_state->bucket_pout_sum_w += p_input->pout_w;
        p_state->bucket_outer_phase_sum_ns +=
            p_input->applied_outer_phase_ns;
        p_state->bucket_primary_inner_sum_ns +=
            p_input->applied_primary_inner_ns;
        p_state->bucket_secondary_inner_sum_ns +=
            p_input->applied_secondary_inner_ns;
        p_state->bucket_samples++;
    }
}

static inline bool dab_mppt_phase_bucket_commit(
    dab_mppt_phase_search_t * p_state,
    dab_mppt_phase_mode_t mode)
{
    if (0U == p_state->bucket_samples)
    {
        return false;
    }
    float const pin_w = p_state->bucket_pin_sum_w /
                        (float) p_state->bucket_samples;
    float const pout_w = p_state->bucket_pout_sum_w /
                         (float) p_state->bucket_samples;
    float const outer_phase_ns =
        p_state->bucket_outer_phase_sum_ns /
        (float) p_state->bucket_samples;
    float const primary_inner_ns =
        p_state->bucket_primary_inner_sum_ns /
        (float) p_state->bucket_samples;
    float const secondary_inner_ns =
        p_state->bucket_secondary_inner_sum_ns /
        (float) p_state->bucket_samples;
    float const efficiency = dab_mppt_phase_efficiency(pin_w, pout_w);
    dab_mppt_phase_metric_t * const p_best = &p_state->mode_best[(uint8_t) mode];
    bool const improved = dab_mppt_phase_metric_better(
        p_state, pin_w, efficiency, p_best);
    if (improved)
    {
        p_best->pin_w = pin_w;
        p_best->efficiency_percent = efficiency;
        p_best->outer_phase_ns = outer_phase_ns;
        p_best->primary_inner_ns = primary_inner_ns;
        p_best->secondary_inner_ns = secondary_inner_ns;
        p_best->valid = true;
    }
    return improved;
}

static inline void dab_mppt_phase_source_probe_record(
    dab_mppt_phase_search_t * p_state,
    dab_mppt_phase_input_t const * p_input)
{
    if ((p_input->vin_v < p_state->config.vin_min_v) ||
        (p_input->pin_w <= 1.0F) || (p_input->pout_w < 0.0F))
    {
        return;
    }
    float const zero_tolerance_ns = 8.0F;
    bool const primary_zero =
        dab_mppt_phase_abs(p_input->applied_primary_inner_ns) <=
        zero_tolerance_ns;
    bool const secondary_zero =
        dab_mppt_phase_abs(p_input->applied_secondary_inner_ns) <=
        zero_tolerance_ns;
    dab_mppt_phase_mode_t const applied_mode =
        (primary_zero && secondary_zero) ? DAB_MPPT_PHASE_MODE_SPS :
        ((primary_zero || secondary_zero) ? DAB_MPPT_PHASE_MODE_EPS :
                                            DAB_MPPT_PHASE_MODE_TPS);
    float const efficiency = dab_mppt_phase_efficiency(p_input->pin_w,
                                                        p_input->pout_w);
    dab_mppt_phase_metric_t * const p_best =
        &p_state->mode_best[(uint8_t) applied_mode];
    if (dab_mppt_phase_metric_better(p_state, p_input->pin_w,
                                     efficiency, p_best))
    {
        p_best->pin_w = p_input->pin_w;
        p_best->efficiency_percent = efficiency;
        p_best->outer_phase_ns = p_input->applied_outer_phase_ns;
        p_best->primary_inner_ns = p_input->applied_primary_inner_ns;
        p_best->secondary_inner_ns = p_input->applied_secondary_inner_ns;
        p_best->valid = true;
    }
}

static inline float dab_mppt_phase_generic_high_inner_ns(float vin_v,
                                                          float vout_v)
{
    if (vin_v < 1.0F)
    {
        return 0.0F;
    }
    float const m = vout_v / (2.0F * vin_v);
    /* A ratio-derived 242 ns seed fell into a low-power basin in r108.  r109
     * then proved that forcing the historical 360 ns EPS seed at m about 0.94
     * reduced Pin from about 110 W SPS to about 30 W.  Near voltage match,
     * begin with one degree of freedom and let the +16 ns EPS/TPS probes prove
     * whether another shift helps; this naturally classifies the point as SPS
     * when it does not.  Away from match, retain the same-board 360 ns CV36
     * bootstrap which remains appropriate for the nominal 22/24 V ratios. */
    if ((m >= DAB_MPPT_PHASE_NEAR_MATCH_MIN_M) &&
        (m <= DAB_MPPT_PHASE_NEAR_MATCH_MAX_M))
    {
        return 0.0F;
    }
    if ((m >= 0.55F) && (m <= 1.25F))
    {
        return DAB_MPPT_PHASE_EPS_BOOTSTRAP_NS;
    }
    float const low_over_high = (m >= 1.0F) ? (1.0F / m) : m;
    float const mismatch = dab_mppt_phase_clamp(1.0F - low_over_high,
                                                 0.0F, 0.30F);
    return dab_mppt_phase_clamp(
        mismatch * (5000.0F - (8000.0F * mismatch)), 0.0F, 800.0F);
}

static inline bool dab_mppt_phase_high_is_primary(float vin_v, float vout_v)
{
    return (vout_v / (2.0F * ((vin_v < 1.0F) ? 1.0F : vin_v))) < 1.0F;
}

static inline void dab_mppt_phase_apply_inner(
    dab_mppt_phase_search_t * p_state,
    bool high_is_primary)
{
    if (DAB_MPPT_PHASE_MODE_SPS == p_state->output.mode)
    {
        p_state->output.primary_inner_ns = 0.0F;
        p_state->output.secondary_inner_ns = 0.0F;
    }
    else if (high_is_primary)
    {
        p_state->output.primary_inner_ns = p_state->high_inner_ns;
        p_state->output.secondary_inner_ns =
            (DAB_MPPT_PHASE_MODE_TPS == p_state->output.mode) ?
            p_state->low_inner_ns : 0.0F;
    }
    else
    {
        p_state->output.secondary_inner_ns = p_state->high_inner_ns;
        p_state->output.primary_inner_ns =
            (DAB_MPPT_PHASE_MODE_TPS == p_state->output.mode) ?
            p_state->low_inner_ns : 0.0F;
    }
}

static inline void dab_mppt_phase_apply_recovery_baseline(
    dab_mppt_phase_search_t * p_state,
    dab_mppt_phase_input_t const * p_input)
{
    (void) p_input;
    p_state->output.mode = DAB_MPPT_PHASE_MODE_TPS;
    p_state->high_inner_ns = DAB_MPPT_PHASE_RECOVERY_HIGH_NS;
    p_state->low_inner_ns = DAB_MPPT_PHASE_RECOVERY_LOW_NS;
    /* r120 exposed a self-lock which the ratio-based orientation created.
     * While a nominal 22 V source collapsed through Vout/(2*Vin) == 1, the
     * recovery target flipped from the commissioned primary-high 832/180 ns
     * waveform to 180/832 ns.  The secondary target then exceeded its 720 ns
     * hardware envelope while the old 480 ns effective command was still
     * present.  Atomic PWM updates no longer fitted the edge schedule, so the
     * last high-power raw phase was retained and Vin stayed near 14--15 V.
     *
     * Recovery is a source-unload waveform, not a live mode-ranking sample.
     * Keep its physical bridge orientation fixed to the same primary-high
     * 832/180 ns baseline qualified by r93 and by both battery precharge
     * paths.  Normal EPS/TPS discovery remains ratio-directed after Vin has
     * recovered.
     *
     * Do not slew both recovery inners up from zero together. r136 proved
     * that the intermediate 136/136 ns waveform does not unload the source:
     * Vin fell from the 20.9 V knee to 18.37 V even though the final 832/180
     * ns waveform is safe. Open the primary-to-secondary separation first;
     * the WAIT update releases the secondary 180 ns target only after the
     * primary 832 ns target has physically arrived. */
    p_state->output.primary_inner_ns = p_state->high_inner_ns;
    p_state->output.secondary_inner_ns =
        p_state->sequenced_recovery_inner ? 0.0F : p_state->low_inner_ns;
}

static inline bool dab_mppt_phase_sequence_recovery_inner(
    dab_mppt_phase_search_t * p_state,
    dab_mppt_phase_input_t const * p_input)
{
    p_state->output.primary_inner_ns = DAB_MPPT_PHASE_RECOVERY_HIGH_NS;
    bool const primary_arrived =
        dab_mppt_phase_abs(p_input->applied_primary_inner_ns -
                           DAB_MPPT_PHASE_RECOVERY_HIGH_NS) <=
        DAB_MPPT_PHASE_RECOVERY_SETTLED_NS;
    p_state->output.secondary_inner_ns = primary_arrived ?
        DAB_MPPT_PHASE_RECOVERY_LOW_NS : 0.0F;
    return primary_arrived &&
           (dab_mppt_phase_abs(p_input->applied_secondary_inner_ns -
                               DAB_MPPT_PHASE_RECOVERY_LOW_NS) <=
            DAB_MPPT_PHASE_RECOVERY_SETTLED_NS);
}

static inline void dab_mppt_phase_initialize(
    dab_mppt_phase_search_t * p_state)
{
    /* Physical source settings are still qualified only at 20/22/24 V.  r117
     * proved that the nominal 20 V / 10 A point can read about 18.1 V on the
     * board after source and wiring drop.  The 17.5 V local search floor gives
     * that accepted point control margin; it is not another physical point. */
    p_state->config.vin_min_v = 17.5F;
    p_state->config.vin_recovery_margin_v = 0.50F;
    /* Complete one SPS/EPS/TPS audit inside the operator's 10 s dwell.  The
     * physical inner-phase slew remains 0.2 ns/ms, so this shortens only idle
     * observation windows; it does not make a waveform edge jump. */
    p_state->config.settle_ms = 1500U;
    p_state->config.mode_observe_ms = 1500U;
    p_state->config.perturb_ms = 500U;
    p_state->config.hold_grace_ms = 1500U;
    p_state->config.condition_confirm_ms = 500U;
    p_state->config.replay_confirm_ms = 500U;
    p_state->config.inner_step_ns = 16.0F;
    p_state->config.pin_equivalent_percent = 2.0F;
    p_state->config.efficiency_gain_percent = 0.30F;
    p_state->config.condition_voltage_delta_v = 1.00F;
    p_state->config.condition_output_voltage_delta_v = 2.00F;
    p_state->config.condition_power_delta_percent = 10.0F;
    p_state->config.replay_min_percent = 90.0F;
    p_state->output = (dab_mppt_phase_output_t) {0};
    p_state->output.stage = DAB_MPPT_PHASE_DISABLED;
    p_state->output.mode = DAB_MPPT_PHASE_MODE_SPS;
    for (uint8_t i = 0U; i < 3U; i++)
    {
        dab_mppt_phase_metric_clear(&p_state->mode_best[i]);
    }
    p_state->stage_started_ms = 0U;
    p_state->bucket_started_ms = 0U;
    p_state->hold_confirm_started_ms = 0U;
    p_state->replay_bad_started_ms = 0U;
    p_state->acquisition_adjust_started_ms = 0U;
    p_state->phase_cap_adjust_started_ms = 0U;
    p_state->bucket_pin_sum_w = 0.0F;
    p_state->bucket_pout_sum_w = 0.0F;
    p_state->bucket_outer_phase_sum_ns = 0.0F;
    p_state->bucket_primary_inner_sum_ns = 0.0F;
    p_state->bucket_secondary_inner_sum_ns = 0.0F;
    p_state->bucket_samples = 0U;
    p_state->previous_current_reference_a = 0.0F;
    p_state->source_best_pin_w = 0.0F;
    p_state->source_best_vin_v = 0.0F;
    p_state->search_floor_vin_v = 0.0F;
    p_state->high_inner_ns = 0.0F;
    p_state->low_inner_ns = 0.0F;
    p_state->high_direction = 1;
    p_state->low_direction = 1;
    p_state->source_peak_seen = false;
    p_state->source_reversal_seen = false;
    p_state->sps_infeasible = false;
    p_state->eps_infeasible = false;
    p_state->tps_infeasible = false;
    p_state->sequenced_recovery_inner = false;
    p_state->local_research = false;
    p_state->hold_anchor_valid = false;
    p_state->hold_anchor_vin_v = 0.0F;
    p_state->hold_anchor_vout_v = 0.0F;
    p_state->hold_anchor_pin_w = 0.0F;
    p_state->trajectory_anchor_valid = false;
    p_state->trajectory_anchor_vout_v = 0.0F;
    p_state->trajectory_anchor_pin_w = 0.0F;
}

static inline void dab_mppt_phase_begin(
    dab_mppt_phase_search_t * p_state,
    dab_mppt_phase_input_t const * p_input)
{
    for (uint8_t i = 0U; i < 3U; i++)
    {
        dab_mppt_phase_metric_clear(&p_state->mode_best[i]);
    }
    p_state->output.active = true;
    p_state->output.changed = true;
    p_state->output.selection_complete = false;
    p_state->output.stage = DAB_MPPT_PHASE_WAIT_SOURCE_PEAK;
    p_state->output.selected_outer_phase_ns = 0.0F;
    p_state->output.selected_pin_w = 0.0F;
    p_state->output.selected_efficiency_percent = 0.0F;
    p_state->output.phase_cap_ns = DAB_MPPT_PHASE_SEARCH_PHASE_CAP_NS;
    p_state->stage_started_ms = p_input->now_ms;
    p_state->previous_current_reference_a = p_input->current_reference_a;
    p_state->source_best_pin_w = p_input->pin_w;
    p_state->source_best_vin_v = p_input->vin_v;
    p_state->search_floor_vin_v = 0.0F;
    p_state->hold_confirm_started_ms = 0U;
    p_state->replay_bad_started_ms = 0U;
    p_state->acquisition_adjust_started_ms = 0U;
    p_state->phase_cap_adjust_started_ms = 0U;
    bool const high_is_primary = dab_mppt_phase_high_is_primary(
        p_input->vin_v, p_input->vout_v);
    p_state->high_inner_ns = high_is_primary ?
        p_input->applied_primary_inner_ns :
        p_input->applied_secondary_inner_ns;
    p_state->low_inner_ns = high_is_primary ?
        p_input->applied_secondary_inner_ns :
        p_input->applied_primary_inner_ns;
    p_state->high_direction = 1;
    p_state->low_direction = 1;
    p_state->source_peak_seen = false;
    p_state->source_reversal_seen = false;
    p_state->sps_infeasible = false;
    p_state->eps_infeasible = false;
    p_state->tps_infeasible = false;
    p_state->sequenced_recovery_inner = false;
    p_state->local_research = false;
    p_state->hold_anchor_valid = false;
    p_state->hold_anchor_vin_v = 0.0F;
    p_state->hold_anchor_vout_v = 0.0F;
    p_state->hold_anchor_pin_w = 0.0F;
    p_state->trajectory_anchor_valid = false;
    p_state->trajectory_anchor_vout_v = 0.0F;
    p_state->trajectory_anchor_pin_w = 0.0F;
    /* Enter fused CC without changing the waveform which was already
     * transferring precharge current.  The current MPPT reference and outer
     * current loop remain live; if this inherited TPS/EPS waveform runs out
     * of phase authority, WAIT progressively removes inner shift instead of
     * making the measured 1 A -> 5 A SPS step seen in r143. */
    p_state->output.mode =
        (p_input->applied_mode <= DAB_MPPT_PHASE_MODE_TPS) ?
        p_input->applied_mode : DAB_MPPT_PHASE_MODE_SPS;
    p_state->output.primary_inner_ns =
        p_input->applied_primary_inner_ns;
    p_state->output.secondary_inner_ns =
        p_input->applied_secondary_inner_ns;
    dab_mppt_phase_bucket_reset(p_state, p_input->now_ms);
}

static inline void dab_mppt_phase_begin_local_research(
    dab_mppt_phase_search_t * p_state,
    dab_mppt_phase_input_t const * p_input)
{
    dab_mppt_phase_begin(p_state, p_input);
    /* The energy P&O loop is already sitting at its present source knee.
     * Re-ranking a changed battery/source condition must not wait for another
     * artificial reversal and must not command the low-power recovery
     * waveform. Preserve the live waveform for the settle interval, then
     * perform the current-regulated SPS/EPS/TPS comparison. */
    p_state->local_research = true;
    p_state->source_peak_seen = true;
    p_state->search_floor_vin_v = dab_mppt_phase_clamp(
        p_input->vin_v - DAB_MPPT_PHASE_LOCAL_FLOOR_MARGIN_V,
        p_state->config.vin_min_v,
        p_input->vin_v);
}

static inline void dab_mppt_phase_return_to_source_wait(
    dab_mppt_phase_search_t * p_state,
    dab_mppt_phase_input_t const * p_input)
{
    if (DAB_MPPT_PHASE_MODE_SPS == p_state->output.mode)
    {
        p_state->sps_infeasible = true;
    }
    else if (DAB_MPPT_PHASE_MODE_EPS == p_state->output.mode)
    {
        p_state->eps_infeasible = true;
    }
    else
    {
        p_state->tps_infeasible = true;
    }
    p_state->output.stage = DAB_MPPT_PHASE_WAIT_SOURCE_PEAK;
    p_state->output.selected_outer_phase_ns = 0.0F;
    p_state->output.selected_pin_w = 0.0F;
    p_state->output.selected_efficiency_percent = 0.0F;
    p_state->output.selection_complete = false;
    p_state->output.changed = true;
    p_state->stage_started_ms = p_input->now_ms;
    p_state->previous_current_reference_a = p_input->current_reference_a;
    p_state->source_best_pin_w = p_input->pin_w;
    p_state->source_best_vin_v = p_input->vin_v;
    p_state->high_inner_ns = 0.0F;
    p_state->low_inner_ns = 0.0F;
    p_state->high_direction = 1;
    p_state->low_direction = 1;
    p_state->source_peak_seen = true;
    p_state->source_reversal_seen = false;
    dab_mppt_phase_apply_recovery_baseline(p_state, p_input);
    dab_mppt_phase_bucket_reset(p_state, p_input->now_ms);
}

static inline void dab_mppt_phase_choose_best(
    dab_mppt_phase_search_t * p_state,
    dab_mppt_phase_input_t const * p_input)
{
    dab_mppt_phase_mode_t selected = DAB_MPPT_PHASE_MODE_SPS;
    dab_mppt_phase_metric_t best = p_state->mode_best[0];
    for (uint8_t i = 1U; i < 3U; i++)
    {
        if (p_state->mode_best[i].valid &&
            dab_mppt_phase_metric_better(p_state,
                                         p_state->mode_best[i].pin_w,
                                         p_state->mode_best[i].efficiency_percent,
                                         &best))
        {
            best = p_state->mode_best[i];
            selected = (dab_mppt_phase_mode_t) i;
        }
    }
    dab_mppt_phase_mode_t validated_mode = DAB_MPPT_PHASE_MODE_SPS;
    if (dab_mppt_phase_validated_preferred_mode(p_input, &validated_mode) &&
        (validated_mode != selected))
    {
        dab_mppt_phase_metric_t const validated =
            p_state->mode_best[(uint8_t) validated_mode];
        float const pin_margin = best.pin_w *
            (p_state->config.pin_equivalent_percent / 100.0F);
        bool const equivalent_live_power = validated.valid &&
            (validated.pin_w >= (best.pin_w - pin_margin));
        bool const no_efficiency_regression =
            validated.efficiency_percent >=
            (best.efficiency_percent -
             p_state->config.efficiency_gain_percent);
        if (equivalent_live_power && no_efficiency_regression)
        {
            best = validated;
            selected = validated_mode;
        }
    }
    p_state->output.mode = selected;
    p_state->output.primary_inner_ns = best.primary_inner_ns;
    p_state->output.secondary_inner_ns = best.secondary_inner_ns;
    p_state->output.selected_outer_phase_ns = best.outer_phase_ns;
    p_state->output.selected_pin_w = best.pin_w;
    p_state->output.selected_efficiency_percent = best.efficiency_percent;
    p_state->output.phase_cap_ns = dab_mppt_phase_clamp(
        best.outer_phase_ns + DAB_MPPT_PHASE_CAP_HEADROOM_NS,
        DAB_MPPT_PHASE_CAP_HEADROOM_NS,
        DAB_MPPT_PHASE_SEARCH_PHASE_CAP_NS);
    p_state->output.stage = DAB_MPPT_PHASE_HOLD_BEST;
    p_state->output.selection_complete = true;
    p_state->output.changed = true;
    p_state->stage_started_ms = p_input->now_ms;
    p_state->hold_confirm_started_ms = 0U;
    p_state->replay_bad_started_ms = 0U;
    p_state->phase_cap_adjust_started_ms = 0U;
    p_state->hold_anchor_valid = false;
    p_state->hold_anchor_vin_v = 0.0F;
    p_state->hold_anchor_vout_v = 0.0F;
    p_state->hold_anchor_pin_w = 0.0F;
    p_state->trajectory_anchor_valid = true;
    p_state->trajectory_anchor_vout_v = p_input->vout_v;
    p_state->trajectory_anchor_pin_w = p_input->pin_w;
}

static inline void dab_mppt_phase_update(
    dab_mppt_phase_search_t * p_state,
    dab_mppt_phase_input_t const * p_input)
{
    p_state->output.changed = false;
    if (!p_input->enabled || !p_input->valid)
    {
        if (DAB_MPPT_PHASE_DISABLED != p_state->output.stage)
        {
            dab_mppt_phase_initialize(p_state);
        }
        return;
    }
    if (DAB_MPPT_PHASE_DISABLED == p_state->output.stage)
    {
        dab_mppt_phase_begin(p_state, p_input);
        return;
    }

    if (p_input->pin_w > p_state->source_best_pin_w)
    {
        p_state->source_best_pin_w = p_input->pin_w;
        p_state->source_best_vin_v = p_input->vin_v;
    }
    /* Rank every physically applied waveform throughout discovery and mode
     * comparison, not only the initial WAIT state.  r97 proved that a startup
     * target reversal can move the state machine into SPS_OBSERVE before the
     * MOS-friendly inner slew reaches the real 5.47 A / 113.55 W source
     * point.  The applied phase still identifies a valid EPS/TPS candidate
     * and a later low-power trial must never erase it. */
    if (DAB_MPPT_PHASE_HOLD_BEST != p_state->output.stage)
    {
        dab_mppt_phase_source_probe_record(p_state, p_input);
    }
    bool const high_is_primary = dab_mppt_phase_high_is_primary(
        p_input->vin_v, p_input->vout_v);
    uint32_t const elapsed = p_input->now_ms - p_state->stage_started_ms;

    /* A mode-complexity trial must never continue below the standalone search
     * floor.  The 22 V / CV33 hardware point proves that
     * SPS at zero phase still has a substantial natural-transfer floor, so
     * recovery uses the already commissioned low-duty TPS baseline.  The
     * existing runtime inner slew and external-phase compensation keep the
     * transition continuous.  The mode which caused the collapse is skipped
     * for the remainder of this search. */
    float const active_search_floor_v =
        (p_state->search_floor_vin_v > p_state->config.vin_min_v) ?
        p_state->search_floor_vin_v : p_state->config.vin_min_v;
    if ((DAB_MPPT_PHASE_WAIT_SOURCE_PEAK != p_state->output.stage) &&
        (DAB_MPPT_PHASE_HOLD_BEST != p_state->output.stage) &&
        (p_input->vin_v < active_search_floor_v))
    {
        dab_mppt_phase_return_to_source_wait(p_state, p_input);
        return;
    }

    if (DAB_MPPT_PHASE_WAIT_SOURCE_PEAK == p_state->output.stage)
    {
        /* Candidate capture is performed for the complete search above;
         * WAIT is responsible only for detecting and leaving the edge. */
        bool const target_reversed =
            p_input->current_reference_a <
            (p_state->previous_current_reference_a - 0.02F);
        if (target_reversed)
        {
            /* CPU1 changes the target once per 100 ms, while CPU0 needs
             * several bounded slew updates to catch it. r129 required
             * target_reversed and current_settled in the same update, an
             * impossible conjunction after a real source-knee reversal.
             * Latch the event until the actuator has settled. */
            p_state->source_reversal_seen = true;
        }
        bool const source_edge =
            p_input->vin_v <= (p_state->config.vin_min_v + 0.15F);
        bool const current_settled =
            dab_mppt_phase_abs(p_input->current_reference_a -
                               p_input->current_active_a) <= 0.15F;

        /* The inherited precharge waveform is the safest CC entry, but it
         * may not have enough transfer authority to let P&O reach the source
         * knee. Only when the outer actuator is actually saturated and the
         * measured current still trails the current reference do we open the
         * waveform by one bounded inner-phase step per second. This is the
         * standalone MCU acquisition path; no host power command is used. */
        bool const current_tracking_deficit =
            p_input->current_reference_a >
            (p_input->measured_output_current_a + 0.20F);
        if (!p_state->source_peak_seen && p_input->outer_saturated &&
            current_tracking_deficit)
        {
            if (0U == p_state->acquisition_adjust_started_ms)
            {
                p_state->acquisition_adjust_started_ms = p_input->now_ms;
            }
            else if ((p_input->now_ms -
                      p_state->acquisition_adjust_started_ms) >=
                     DAB_MPPT_PHASE_ACQUIRE_STEP_MS)
            {
                if (DAB_MPPT_PHASE_MODE_TPS == p_state->output.mode)
                {
                    p_state->high_inner_ns = dab_mppt_phase_clamp(
                        p_state->high_inner_ns -
                            DAB_MPPT_PHASE_ACQUIRE_STEP_NS,
                        32.0F, 800.0F);
                    dab_mppt_phase_apply_inner(p_state, high_is_primary);
                }
                else if (DAB_MPPT_PHASE_MODE_EPS == p_state->output.mode)
                {
                    p_state->high_inner_ns = dab_mppt_phase_clamp(
                        p_state->high_inner_ns -
                            DAB_MPPT_PHASE_ACQUIRE_STEP_NS,
                        0.0F, 800.0F);
                    if (p_state->high_inner_ns <= 0.0F)
                    {
                        p_state->output.mode = DAB_MPPT_PHASE_MODE_SPS;
                    }
                    dab_mppt_phase_apply_inner(p_state, high_is_primary);
                }
                p_state->acquisition_adjust_started_ms = p_input->now_ms;
                p_state->output.changed = true;
            }
        }
        else
        {
            p_state->acquisition_adjust_started_ms = 0U;
        }
        bool const peak_triggered = source_edge ||
            (p_state->source_reversal_seen && current_settled &&
             (elapsed >= p_state->config.settle_ms));
        if (!p_state->source_peak_seen && peak_triggered)
        {
            p_state->source_peak_seen = true;
            p_state->source_reversal_seen = false;
            /* Preserve every commissioned low/mid-power path exactly. The
             * sequenced transition is enabled only for the r136/r137 class
             * measured at the nominal-22 V high-power knee. */
            p_state->sequenced_recovery_inner = source_edge &&
                (p_state->source_best_pin_w >=
                 DAB_MPPT_PHASE_SEQUENCED_MIN_PIN_W) &&
                (p_state->source_best_vin_v >=
                 DAB_MPPT_PHASE_SEQUENCED_MIN_VIN_V) &&
                (p_state->source_best_vin_v <=
                 DAB_MPPT_PHASE_SEQUENCED_MAX_VIN_V);
            /* Bind mode trials to this live source edge.  The global 17.5 V
             * floor keeps nominal-20 V points valid, but r133 showed that a
             * nominal-22 V trial could still reach 18.0 V before crossing it. */
            p_state->search_floor_vin_v = dab_mppt_phase_clamp(
                p_state->source_best_vin_v -
                    DAB_MPPT_PHASE_LOCAL_FLOOR_MARGIN_V,
                p_state->config.vin_min_v,
                p_state->source_best_vin_v);
            /* A genuine source collapse still uses the commissioned unload
             * waveform. A healthy P&O current reversal is already the MPP;
             * keep that operating point and let the current loop regulate the
             * later mode comparison instead of first throwing power away. */
            if (source_edge)
            {
                dab_mppt_phase_apply_recovery_baseline(p_state, p_input);
            }
            p_state->stage_started_ms = p_input->now_ms;
            dab_mppt_phase_bucket_reset(p_state, p_input->now_ms);
            p_state->output.changed = true;
            p_state->previous_current_reference_a =
                p_input->current_reference_a;
            return;
        }
        float const source_recovery_v = dab_mppt_phase_clamp(
            p_state->search_floor_vin_v +
                (0.5F * p_state->config.vin_recovery_margin_v),
            p_state->config.vin_min_v +
                p_state->config.vin_recovery_margin_v,
            1000.0F);
        bool const source_recovered =
            p_input->vin_v >= source_recovery_v;
        bool const recovery_inner_settled =
            p_state->sequenced_recovery_inner ?
            dab_mppt_phase_sequence_recovery_inner(p_state, p_input) : true;
        if (p_state->source_peak_seen && source_recovered &&
            current_settled && recovery_inner_settled &&
            (elapsed >= p_state->config.settle_ms))
        {
            if (p_state->tps_infeasible)
            {
                /* TPS is the final comparison stage. If it crosses the live
                 * floor, SPS/EPS have already been observed; finalize those
                 * retained measurements instead of restarting at SPS. */
                dab_mppt_phase_choose_best(p_state, p_input);
                return;
            }
            if (!p_state->sps_infeasible)
            {
                p_state->output.mode = DAB_MPPT_PHASE_MODE_SPS;
                p_state->output.primary_inner_ns = 0.0F;
                p_state->output.secondary_inner_ns = 0.0F;
                p_state->output.stage = DAB_MPPT_PHASE_SPS_OBSERVE;
            }
            else if (!p_state->eps_infeasible)
            {
                p_state->output.mode = DAB_MPPT_PHASE_MODE_EPS;
                p_state->high_inner_ns = dab_mppt_phase_generic_high_inner_ns(
                    p_input->vin_v, p_input->vout_v);
                p_state->low_inner_ns = 0.0F;
                p_state->high_direction = 1;
                dab_mppt_phase_apply_inner(p_state, high_is_primary);
                p_state->output.stage = DAB_MPPT_PHASE_EPS_SETTLE;
            }
            else if (!p_state->tps_infeasible)
            {
                dab_mppt_phase_apply_recovery_baseline(p_state, p_input);
                p_state->low_direction = 1;
                p_state->output.stage = DAB_MPPT_PHASE_TPS_SETTLE;
            }
            else
            {
                /* r137: once TPS also crosses the live mode floor, do not
                 * recover and retry it forever. Select the best physically
                 * measured SPS/EPS/TPS sample already retained this run. */
                dab_mppt_phase_choose_best(p_state, p_input);
                return;
            }
            p_state->stage_started_ms = p_input->now_ms;
            dab_mppt_phase_bucket_reset(p_state, p_input->now_ms);
            p_state->output.changed = true;
        }
        p_state->previous_current_reference_a =
            p_input->current_reference_a;
        return;
    }

    if (DAB_MPPT_PHASE_HOLD_BEST == p_state->output.stage)
    {
        bool const current_settled =
            dab_mppt_phase_abs(p_input->current_reference_a -
                               p_input->current_active_a) <= 0.15F;
        bool const inner_settled =
            (dab_mppt_phase_abs(p_input->applied_primary_inner_ns -
                                p_state->output.primary_inner_ns) <= 8.0F) &&
            (dab_mppt_phase_abs(p_input->applied_secondary_inner_ns -
                                p_state->output.secondary_inner_ns) <= 8.0F);
        bool const current_tracking =
            dab_mppt_phase_abs(p_input->current_reference_a -
                               p_input->measured_output_current_a) <= 0.25F;
        bool const current_tracking_deficit =
            p_input->current_reference_a >
            (p_input->measured_output_current_a + 0.15F);

        /* A charging demonstration advances the battery-equivalent CV point
         * in 0.1 V steps. Do not wait for a multi-volt condition restart while
         * the selected waveform loses gain. Every real 0.1 V step therefore
         * performs a light, mode-preserving MPPT retune: when rising Vout is
         * accompanied by falling Pin or a current deficit, release one bounded
         * 8 ns slice of outer authority. The physical current reference and
         * inner phases retain their existing slew limits, so this cannot make
         * a hard power or topology transition. Full SPS/EPS/TPS efficiency
         * ranking remains the slower cumulative-condition operation below. */
        if (!p_state->trajectory_anchor_valid)
        {
            p_state->trajectory_anchor_valid = true;
            p_state->trajectory_anchor_vout_v = p_input->vout_v;
            p_state->trajectory_anchor_pin_w = p_input->pin_w;
        }
        else if (dab_mppt_phase_abs(p_input->vout_v -
                                    p_state->trajectory_anchor_vout_v) >=
                 DAB_MPPT_PHASE_TRAJECTORY_STEP_V)
        {
            float const power_drop_margin_w =
                (DAB_MPPT_PHASE_TRAJECTORY_POWER_RATIO *
                 p_state->trajectory_anchor_pin_w) >
                DAB_MPPT_PHASE_TRAJECTORY_POWER_W ?
                (DAB_MPPT_PHASE_TRAJECTORY_POWER_RATIO *
                 p_state->trajectory_anchor_pin_w) :
                DAB_MPPT_PHASE_TRAJECTORY_POWER_W;
            bool const rising_output =
                p_input->vout_v > p_state->trajectory_anchor_vout_v;
            bool const power_receding =
                p_input->pin_w <
                (p_state->trajectory_anchor_pin_w - power_drop_margin_w);
            if (rising_output && (power_receding || current_tracking_deficit) &&
                (p_state->output.phase_cap_ns <
                 DAB_MPPT_PHASE_SEARCH_PHASE_CAP_NS))
            {
                p_state->output.phase_cap_ns = dab_mppt_phase_clamp(
                    p_state->output.phase_cap_ns +
                        DAB_MPPT_PHASE_CAP_RELEASE_STEP_NS,
                    0.0F, DAB_MPPT_PHASE_SEARCH_PHASE_CAP_NS);
                p_state->output.changed = true;
            }
            p_state->trajectory_anchor_vout_v = p_input->vout_v;
            p_state->trajectory_anchor_pin_w = p_input->pin_w;
        }

        /* selected_outer_phase_ns is the measured winning point, not a
         * permanent actuator ceiling. If battery voltage rises and the
         * current loop reaches that ceiling, release 8 ns every 250 ms until
         * the requested current is recovered. P&O therefore remains the
         * continuous MPPT layer throughout the charge trajectory. */
        if (p_input->outer_saturated && current_tracking_deficit &&
            (p_state->output.phase_cap_ns <
             DAB_MPPT_PHASE_SEARCH_PHASE_CAP_NS))
        {
            if (0U == p_state->phase_cap_adjust_started_ms)
            {
                p_state->phase_cap_adjust_started_ms = p_input->now_ms;
            }
            else if ((p_input->now_ms -
                      p_state->phase_cap_adjust_started_ms) >=
                     DAB_MPPT_PHASE_CAP_RELEASE_MS)
            {
                p_state->output.phase_cap_ns = dab_mppt_phase_clamp(
                    p_state->output.phase_cap_ns +
                        DAB_MPPT_PHASE_CAP_RELEASE_STEP_NS,
                    0.0F, DAB_MPPT_PHASE_SEARCH_PHASE_CAP_NS);
                p_state->phase_cap_adjust_started_ms = p_input->now_ms;
                p_state->output.changed = true;
            }
        }
        else
        {
            p_state->phase_cap_adjust_started_ms = 0U;
        }
        if (!p_state->hold_anchor_valid)
        {
            /* The selected mode may require about 1 s of inner-phase slew and
             * several seconds of the MOS-friendly 0.5 A/s current ramp.  Do
             * not mistake that intentional transition for a new condition. */
            if ((elapsed >= p_state->config.hold_grace_ms) &&
                current_settled && inner_settled)
            {
                float const replay_percent =
                    (p_state->output.selected_pin_w > 1.0F) ?
                    (100.0F * p_input->pin_w /
                     p_state->output.selected_pin_w) : 100.0F;
                if (replay_percent < p_state->config.replay_min_percent)
                {
                    if (0U == p_state->replay_bad_started_ms)
                    {
                        p_state->replay_bad_started_ms = p_input->now_ms;
                    }
                    else if ((p_input->now_ms -
                              p_state->replay_bad_started_ms) >=
                             p_state->config.replay_confirm_ms)
                    {
                        /* Never lock the low-power replay of a transient
                         * sample.  Repeat the live, current-regulated local
                         * search from the waveform which is already applied. */
                        dab_mppt_phase_begin_local_research(p_state, p_input);
                    }
                    return;
                }
                p_state->replay_bad_started_ms = 0U;
                p_state->hold_anchor_vin_v = p_input->vin_v;
                p_state->hold_anchor_vout_v = p_input->vout_v;
                p_state->hold_anchor_pin_w = p_input->pin_w;
                p_state->hold_anchor_valid = true;
                p_state->hold_confirm_started_ms = 0U;
                p_state->output.changed = true;
            }
            return;
        }

        /* Continuous P&O handles ordinary movement. A materially new Vin,
         * Vout or available-power region still deserves a fresh efficiency
         * comparison, but only after it persists and current is tracking.
         * The local restart inherits the live waveform/reference. */
        bool const source_voltage_changed =
            dab_mppt_phase_abs(p_input->vin_v -
                               p_state->hold_anchor_vin_v) >
            p_state->config.condition_voltage_delta_v;
        bool const output_voltage_changed =
            dab_mppt_phase_abs(p_input->vout_v -
                               p_state->hold_anchor_vout_v) >
            p_state->config.condition_output_voltage_delta_v;
        float const source_power_delta_w =
            dab_mppt_phase_abs(p_input->pin_w -
                               p_state->hold_anchor_pin_w);
        bool const source_power_changed =
            (p_state->hold_anchor_pin_w > 1.0F) &&
            (source_power_delta_w >
             (p_state->hold_anchor_pin_w *
              p_state->config.condition_power_delta_percent / 100.0F));
        float const source_power_floor_w =
            p_state->hold_anchor_pin_w *
            (100.0F - p_state->config.condition_power_delta_percent) /
            100.0F;
        bool const source_power_available =
            (p_state->hold_anchor_pin_w <= 1.0F) ||
            (p_input->pin_w >= source_power_floor_w);
        bool const phase_authority_exhausted =
            p_input->outer_saturated && current_tracking_deficit &&
            (p_state->output.phase_cap_ns >=
             DAB_MPPT_PHASE_SEARCH_PHASE_CAP_NS);
        bool const source_power_degraded =
            (p_state->hold_anchor_pin_w > 1.0F) &&
            !source_power_available;
        bool const changed_condition = phase_authority_exhausted ||
            ((source_voltage_changed || output_voltage_changed ||
              source_power_changed) && current_settled &&
             (current_tracking || source_power_degraded));
        if (changed_condition)
        {
            if (0U == p_state->hold_confirm_started_ms)
            {
                p_state->hold_confirm_started_ms = p_input->now_ms;
            }
            else if ((p_input->now_ms - p_state->hold_confirm_started_ms) >=
                     p_state->config.condition_confirm_ms)
            {
                dab_mppt_phase_begin_local_research(p_state, p_input);
            }
        }
        else
        {
            p_state->hold_confirm_started_ms = 0U;
        }
        return;
    }

    if ((DAB_MPPT_PHASE_EPS_SETTLE == p_state->output.stage) ||
        (DAB_MPPT_PHASE_TPS_SETTLE == p_state->output.stage))
    {
        /* Record the real trajectory during the MOS-friendly inner slew.
         * r94 proved that the useful point can lie between old and requested
         * targets, so the bucket stores applied rather than target phase. */
        dab_mppt_phase_bucket_add(p_state, p_input);
        if ((p_input->now_ms - p_state->bucket_started_ms) >=
            p_state->config.perturb_ms)
        {
            (void) dab_mppt_phase_bucket_commit(p_state,
                                                 p_state->output.mode);
            dab_mppt_phase_bucket_reset(p_state, p_input->now_ms);
        }
        if (elapsed >= p_state->config.settle_ms)
        {
            p_state->output.stage =
                (DAB_MPPT_PHASE_EPS_SETTLE == p_state->output.stage) ?
                DAB_MPPT_PHASE_EPS_SEARCH : DAB_MPPT_PHASE_TPS_SEARCH;
            p_state->stage_started_ms = p_input->now_ms;
            dab_mppt_phase_bucket_reset(p_state, p_input->now_ms);
            p_state->output.changed = true;
        }
        return;
    }

    dab_mppt_phase_bucket_add(p_state, p_input);
    bool const bucket_due =
        (p_input->now_ms - p_state->bucket_started_ms) >=
        p_state->config.perturb_ms;
    if (bucket_due)
    {
        dab_mppt_phase_mode_t const mode = p_state->output.mode;
        bool const improved = dab_mppt_phase_bucket_commit(p_state, mode);
        if (DAB_MPPT_PHASE_EPS_SEARCH == p_state->output.stage)
        {
            if (!improved)
            {
                p_state->high_direction = (int8_t) -p_state->high_direction;
            }
            p_state->high_inner_ns = dab_mppt_phase_clamp(
                p_state->high_inner_ns +
                ((float) p_state->high_direction *
                 p_state->config.inner_step_ns), 0.0F, 800.0F);
            dab_mppt_phase_apply_inner(p_state, high_is_primary);
        }
        else if (DAB_MPPT_PHASE_TPS_SEARCH == p_state->output.stage)
        {
            if (!improved)
            {
                p_state->low_direction = (int8_t) -p_state->low_direction;
            }
            p_state->low_inner_ns = dab_mppt_phase_clamp(
                p_state->low_inner_ns +
                ((float) p_state->low_direction *
                 p_state->config.inner_step_ns), 0.0F, 400.0F);
            dab_mppt_phase_apply_inner(p_state, high_is_primary);
        }
        dab_mppt_phase_bucket_reset(p_state, p_input->now_ms);
        p_state->output.changed = true;
    }

    if (elapsed < p_state->config.mode_observe_ms)
    {
        return;
    }

    (void) dab_mppt_phase_bucket_commit(p_state, p_state->output.mode);
    if (DAB_MPPT_PHASE_SPS_OBSERVE == p_state->output.stage)
    {
        p_state->output.mode = DAB_MPPT_PHASE_MODE_EPS;
        p_state->high_inner_ns = dab_mppt_phase_generic_high_inner_ns(
            p_input->vin_v, p_input->vout_v);
        p_state->low_inner_ns = 0.0F;
        p_state->high_direction = 1;
        dab_mppt_phase_apply_inner(p_state, high_is_primary);
        p_state->output.stage = DAB_MPPT_PHASE_EPS_SETTLE;
    }
    else if (DAB_MPPT_PHASE_EPS_SEARCH == p_state->output.stage)
    {
        dab_mppt_phase_metric_t const eps_best = p_state->mode_best[1];
        p_state->output.mode = DAB_MPPT_PHASE_MODE_TPS;
        p_state->high_inner_ns = high_is_primary ?
            eps_best.primary_inner_ns : eps_best.secondary_inner_ns;
        p_state->low_inner_ns = 0.0F;
        p_state->low_direction = 1;
        dab_mppt_phase_apply_inner(p_state, high_is_primary);
        p_state->output.stage = DAB_MPPT_PHASE_TPS_SETTLE;
    }
    else
    {
        dab_mppt_phase_choose_best(p_state, p_input);
        return;
    }
    p_state->stage_started_ms = p_input->now_ms;
    dab_mppt_phase_bucket_reset(p_state, p_input->now_ms);
    p_state->output.changed = true;
}

#endif /* DAB_MPPT_PHASE_SEARCH_H */
