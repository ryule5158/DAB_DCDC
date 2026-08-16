#ifndef DAB_TUNED_PARAMETER_TABLE_H
#define DAB_TUNED_PARAMETER_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Code-ready gain/modulation schedule contract.
 *
 * The storage deliberately contains no valid entry yet.  A trial becomes a
 * table entry only after its SPS/EPS/TPS parameter family is tuned, saved in
 * best_parameters_by_condition.jsonl, and explicitly promoted for firmware.
 * This prevents provisional 43.5 V data from being applied to the new 44 V /
 * high-power condition.
 */
#define DAB_TUNED_PARAMETER_TABLE_REVISION       (1UL)
#define DAB_TUNED_PARAMETER_TABLE_VERSION        "v38-empty-validated-table"
#define DAB_TUNED_PARAMETER_TABLE_ENTRY_COUNT    (0UL)

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

/* Sentinel storage keeps the generated-table ABI buildable before promotion. */
static const dab_tuned_parameter_bundle_t g_dab_tuned_parameter_storage[1] =
{
    {false, 0UL, 0U, 0U, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
     0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F}
};

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

    for (size_t i = 0U;
         i < (sizeof(g_dab_tuned_parameter_storage) /
              sizeof(g_dab_tuned_parameter_storage[0]));
         i++)
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

#endif /* DAB_TUNED_PARAMETER_TABLE_H */
