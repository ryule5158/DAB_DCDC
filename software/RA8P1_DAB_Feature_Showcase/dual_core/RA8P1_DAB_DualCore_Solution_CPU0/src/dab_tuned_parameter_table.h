#ifndef DAB_TUNED_PARAMETER_TABLE_H
#define DAB_TUNED_PARAMETER_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Runtime contracts.  Storage is generated exclusively from powered PASS
 * records by tools/ai_training/promote_validated_to_firmware.py. */
#define DAB_TUNED_PARAMETER_TABLE_REVISION (4UL)

typedef struct st_dab_tuned_parameter_bundle
{
    bool     valid;
    uint32_t entry_id;
    uint8_t  modulation_id;
    uint8_t  control_architecture_id;
    float    bench_vin_set_v;
    float    voltage_ref_v;
    float    load_nominal_ohm;
    float    requested_power_layer_w;
    float    power_command_label_w;
    float    outer_kp_ns_per_v;
    float    outer_ki_us_per_vs;
    float    outer_kd_ns_s_per_v;
    float    derivative_filter_alpha;
    float    phase_cap_ns;
    float    feedforward_effective_phase_ns;
    float    primary_inner_ns;
    float    secondary_inner_ns;
} dab_tuned_parameter_bundle_t;

typedef struct st_dab_tuned_current_bundle
{
    bool     valid;
    uint32_t entry_id;
    uint8_t  modulation_id;            /* SPS=0, EPS=1, TPS=2 */
    uint8_t  control_architecture_id;   /* OUTPUT_CURRENT_PI=1 */
    float    measured_vin_v;
    float    voltage_ref_v;
    float    current_ref_a;
    int8_t   current_direction;
    float    current_kp_ns_per_a;
    float    current_ki_us_per_as;
    float    current_slew_a_per_s;
    float    phase_cap_ns;
    float    primary_inner_ns;
    float    secondary_inner_ns;
    float    proven_relative_efficiency_pct;
} dab_tuned_current_bundle_t;

#include "dab_tuned_parameter_records.generated.h"

static inline float dab_tuned_parameter_abs(float value)
{
    return (value < 0.0F) ? -value : value;
}

static inline bool dab_tuned_parameter_lookup_exact(
    float bench_vin_set_v,
    float voltage_ref_v,
    float load_nominal_ohm,
    float requested_power_layer_w,
    uint8_t modulation_id,
    uint8_t control_architecture_id,
    dab_tuned_parameter_bundle_t * p_bundle)
{
    if (NULL == p_bundle)
    {
        return false;
    }

    for (size_t i = 0U; i < DAB_TUNED_PARAMETER_TABLE_ENTRY_COUNT; i++)
    {
        dab_tuned_parameter_bundle_t const * p_entry =
            &g_dab_tuned_parameter_storage[i];
        if (p_entry->valid &&
            (p_entry->modulation_id == modulation_id) &&
            (p_entry->control_architecture_id == control_architecture_id) &&
            (dab_tuned_parameter_abs(p_entry->bench_vin_set_v -
                                     bench_vin_set_v) <= 0.25F) &&
            (dab_tuned_parameter_abs(p_entry->voltage_ref_v -
                                     voltage_ref_v) <= 0.01F) &&
            (dab_tuned_parameter_abs(p_entry->load_nominal_ohm -
                                     load_nominal_ohm) <= 0.05F) &&
            (dab_tuned_parameter_abs(p_entry->requested_power_layer_w -
                                     requested_power_layer_w) <= 0.5F))
        {
            *p_bundle = *p_entry;
            return true;
        }
    }
    return false;
}

static inline bool dab_tuned_current_parameter_lookup_exact(
    float measured_vin_v,
    float voltage_ref_v,
    float current_ref_a,
    uint8_t modulation_id,
    uint8_t control_architecture_id,
    dab_tuned_current_bundle_t * p_bundle,
    float * p_match_score)
{
    if (NULL == p_bundle)
    {
        return false;
    }

    bool found = false;
    float best_score = 1000000.0F;
    for (size_t i = 0U; i < DAB_TUNED_CURRENT_ENTRY_COUNT; i++)
    {
        dab_tuned_current_bundle_t const * p_entry =
            &g_dab_tuned_current_storage[i];
        float const vin_error = dab_tuned_parameter_abs(
            p_entry->measured_vin_v - measured_vin_v);
        float const voltage_error = dab_tuned_parameter_abs(
            p_entry->voltage_ref_v - voltage_ref_v);
        float const current_error = dab_tuned_parameter_abs(
            p_entry->current_ref_a - current_ref_a);
        if (!p_entry->valid ||
            (p_entry->modulation_id != modulation_id) ||
            (p_entry->control_architecture_id != control_architecture_id) ||
            (vin_error > 1.25F) || (voltage_error > 0.25F) ||
            (current_error > 0.05F))
        {
            continue;
        }

        float const score = (3.0F * vin_error) +
                            (2.0F * voltage_error) + current_error;
        if ((!found) || (score < best_score))
        {
            found = true;
            best_score = score;
            *p_bundle = *p_entry;
        }
    }

    if ((NULL != p_match_score) && found)
    {
        *p_match_score = best_score;
    }
    return found;
}

/* CP is a high-level product function: the host supplies the bus-voltage and
 * power targets, while the lower controller selects a previously powered
 * current-loop seed.  The exact current table was collected at discrete
 * current points, so requiring an exact current/modulation match would force
 * the host to choose the modulation and would reject ordinary CP targets such
 * as 50 W / 42 V.  Keep reuse local to the measured 20--24 V, 42--44 V
 * neighbourhood and rank all SPS/EPS/TPS records by plant proximity, using
 * proven relative efficiency only as a small tie breaker. */
static inline bool dab_tuned_current_parameter_lookup_cp_nearest(
    float measured_vin_v,
    float nominal_output_v,
    float requested_current_a,
    dab_tuned_current_bundle_t * p_bundle,
    float * p_match_score)
{
    if ((NULL == p_bundle) || (measured_vin_v < 17.5F) ||
        (measured_vin_v > 26.0F) || (nominal_output_v < 36.0F) ||
        (nominal_output_v > 48.0F) || (requested_current_a < 0.0F) ||
        (requested_current_a > 7.0F))
    {
        return false;
    }

    bool found = false;
    float best_score = 1000000.0F;
    for (size_t i = 0U; i < DAB_TUNED_CURRENT_ENTRY_COUNT; i++)
    {
        dab_tuned_current_bundle_t const * p_entry =
            &g_dab_tuned_current_storage[i];
        float const vin_error = dab_tuned_parameter_abs(
            p_entry->measured_vin_v - measured_vin_v);
        float const voltage_error = dab_tuned_parameter_abs(
            p_entry->voltage_ref_v - nominal_output_v);
        float const current_error = dab_tuned_parameter_abs(
            p_entry->current_ref_a - requested_current_a);
        if (!p_entry->valid ||
            (p_entry->control_architecture_id != 1U) ||
            (p_entry->modulation_id > 2U) ||
            (vin_error > 2.50F) || (voltage_error > 2.00F))
        {
            continue;
        }

        float const score = (3.0F * vin_error) +
                            (2.0F * voltage_error) + current_error -
                            (0.002F * p_entry->proven_relative_efficiency_pct);
        if ((!found) || (score < best_score))
        {
            found = true;
            best_score = score;
            *p_bundle = *p_entry;
        }
    }

    if ((NULL != p_match_score) && found)
    {
        *p_match_score = best_score;
    }
    return found;
}

_Static_assert(DAB_TUNED_PARAMETER_TABLE_ENTRY_COUNT ==
               (sizeof(g_dab_tuned_parameter_storage) /
                sizeof(g_dab_tuned_parameter_storage[0])),
               "generated voltage table count mismatch");
_Static_assert(DAB_TUNED_CURRENT_ENTRY_COUNT ==
               (sizeof(g_dab_tuned_current_storage) /
                sizeof(g_dab_tuned_current_storage[0])),
               "generated current table count mismatch");

#endif /* DAB_TUNED_PARAMETER_TABLE_H */
