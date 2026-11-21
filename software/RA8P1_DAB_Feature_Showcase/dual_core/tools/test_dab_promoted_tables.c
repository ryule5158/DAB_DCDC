#include "../RA8P1_DAB_DualCore_Solution_CPU0/src/dab_tuned_parameter_table.h"
#include "../RA8P1_DAB_DualCore_Solution_CPU0/src/dab_validated_charger_table.h"

static int s_failures;
#define assert(condition) do { if (!(condition)) { s_failures++; } } while (0)
#define puts(message)     ((void) (message))

int main(void)
{
    assert(DAB_TUNED_PARAMETER_TABLE_ENTRY_COUNT >= 2U);
    assert(DAB_TUNED_CURRENT_ENTRY_COUNT >= 13U);
    assert(DAB_VALIDATED_CHARGER_TABLE_ENTRY_COUNT >= 25U);
    assert(3U == dab_validated_charger_normalize_stage(3U));
    assert(4U == dab_validated_charger_normalize_stage(4U));
    assert(4U == dab_validated_charger_normalize_stage(5U));
    assert(4U == dab_validated_charger_normalize_stage(6U));
    assert(3U == dab_validated_charger_normalize_stage(2U));
    assert(!dab_validated_charger_zero_inner_for_taper(1U, 4U));
    assert(!dab_validated_charger_zero_inner_for_taper(1U, 6U));
    assert(dab_validated_charger_zero_inner_for_taper(2U, 4U));
    assert(dab_validated_charger_zero_inner_for_taper(2U, 6U));
    assert(!dab_validated_charger_zero_inner_for_taper(2U, 3U));
    assert(!dab_validated_charger_zero_inner_for_taper(3U, 4U));
    assert(720.0F == dab_validated_charger_primary_inner_for_taper(
        1U, 4U, 1U, 600.0F));
    assert(720.0F == dab_validated_charger_primary_inner_for_taper(
        1U, 6U, 1U, 600.0F));
    assert(600.0F == dab_validated_charger_primary_inner_for_taper(
        1U, 3U, 1U, 600.0F));
    assert(536.0F == dab_validated_charger_primary_inner_for_taper(
        1U, 4U, 2U, 536.0F));
    assert(600.0F == dab_validated_charger_primary_inner_for_taper(
        2U, 4U, 1U, 600.0F));

    dab_validated_charger_entry_t precharge = {0};
    assert(dab_validated_charger_build_precharge_seed(
        2U, 0U, 21.4F, 1.0F, 0.3F, &precharge));
    assert((precharge.entry_id == DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID) &&
           (precharge.charge_stage_id == 2U) &&
           (precharge.modulation_id == 2U) &&
           (precharge.primary_inner_ns == 832.0F) &&
           (precharge.secondary_inner_ns == 180.0F) &&
           (precharge.phase_cap_ns == 20.0F) &&
           (precharge.configured_imax_a == 1.0F));
    assert(dab_validated_charger_build_precharge_seed(
        2U, 2U, 24.0F, 0.8F, 0.2F, &precharge));
    assert(dab_validated_charger_build_precharge_seed(
        1U, 2U, 22.0F, 1.0F, 0.3F, &precharge));
    assert((precharge.entry_id == DAB_VALIDATED_LIION_PRECHARGE_ENTRY_ID) &&
           (precharge.battery_profile_id == 1U) &&
           (precharge.charge_stage_id == 2U) &&
           (precharge.modulation_id == 2U) &&
           (precharge.primary_inner_ns == 864.0F) &&
           (precharge.secondary_inner_ns == 180.0F) &&
           (precharge.phase_cap_ns == 20.0F) &&
           (precharge.current_kp_ns_per_a == 0.5F) &&
           (precharge.current_ki_us_per_as == 0.05F));
    assert(!dab_validated_charger_build_precharge_seed(
        2U, 3U, 24.0F, 1.0F, 0.3F, &precharge));
    assert(!dab_validated_charger_build_precharge_seed(
        3U, 2U, 24.0F, 1.0F, 0.3F, &precharge));
    assert(0.0F == dab_validated_charger_precharge_effective_bias_ns(
        DAB_VALIDATED_LIION_PRECHARGE_ENTRY_ID));
    assert(0.0F == dab_validated_charger_precharge_effective_bias_ns(
        DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID));
    assert(0.0F == dab_validated_charger_precharge_effective_bias_ns(1234U));
    assert(dab_validated_charger_is_precharge_entry(
        DAB_VALIDATED_LIION_PRECHARGE_ENTRY_ID));
    assert(dab_validated_charger_is_precharge_entry(
        DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID));
    assert(!dab_validated_charger_is_precharge_entry(
        DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_ENTRY_ID));
    assert(-1 == dab_validated_charger_current_direction(
        DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID));
    assert(-1 == dab_validated_charger_current_direction(
        DAB_VALIDATED_LIION_PRECHARGE_ENTRY_ID));
    assert(1 == dab_validated_charger_current_direction(
        DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_ENTRY_ID));
    assert(832.0F ==
        dab_validated_charger_precharge_primary_inner_for_voltage(
            DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID, 31.0F, 832.0F));
    assert(776.0F ==
        dab_validated_charger_precharge_primary_inner_for_voltage(
            DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID, 31.7F, 832.0F));
    assert(720.0F ==
        dab_validated_charger_precharge_primary_inner_for_voltage(
            DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID, 32.4F, 832.0F));
    assert(848.0F ==
        dab_validated_charger_precharge_primary_inner_for_voltage(
            DAB_VALIDATED_LIION_PRECHARGE_ENTRY_ID, 29.5F, 864.0F));
    assert(832.0F ==
        dab_validated_charger_precharge_primary_inner_for_voltage(
            DAB_VALIDATED_LIION_PRECHARGE_ENTRY_ID, 30.0F, 864.0F));
    assert(600.0F ==
        dab_validated_charger_precharge_primary_inner_for_voltage(
            1234U, 32.4F, 600.0F));
    assert(0.05F == dab_validated_charger_runtime_current_slew(
        2U, 5.0F, 0.05F));
    assert(0.5F == dab_validated_charger_runtime_current_slew(
        3U, 5.0F, 0.05F));
    assert(0.5F == dab_validated_charger_runtime_current_slew(
        3U, 10.0F, 0.05F));
    assert(0.05F == dab_validated_charger_runtime_current_slew(
        3U, 1.0F, 0.05F));
    assert(0.05F == dab_validated_charger_runtime_current_slew(
        4U, 5.0F, 0.05F));
    assert(dab_validated_charger_lfp_low_voltage_cc_shape(
        2U, 3U, 33.0F));
    assert(dab_validated_charger_lfp_low_voltage_cc_shape(
        2U, 3U, 41.49F));
    assert(!dab_validated_charger_lfp_low_voltage_cc_shape(
        2U, 3U, 41.5F));
    assert(!dab_validated_charger_lfp_low_voltage_cc_shape(
        1U, 3U, 33.0F));
    assert(!dab_validated_charger_lfp_low_voltage_cc_shape(
        2U, 4U, 33.0F));
    dab_validated_charger_entry_t low_cc = {0};
    assert(dab_validated_charger_build_lfp_low_voltage_cc_seed(
        2U, 3U, 21.4F, 33.0F, 1.0F, 0.3F, &low_cc));
    assert((low_cc.entry_id ==
            DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_ENTRY_ID) &&
           (low_cc.battery_profile_id == 2U) &&
           (low_cc.charge_stage_id == 3U) &&
           (low_cc.modulation_id == 2U) &&
           (low_cc.primary_inner_ns == 720.0F) &&
           (low_cc.secondary_inner_ns == 180.0F) &&
           (low_cc.phase_cap_ns == 80.0F) &&
           (low_cc.current_kp_ns_per_a == 2.0F) &&
           (low_cc.current_ki_us_per_as == 0.2F) &&
           (low_cc.configured_imax_a == 1.0F) &&
           (low_cc.external_cv_set_v == 33.0F));
    assert(!dab_validated_charger_build_lfp_low_voltage_cc_seed(
        2U, 3U, 21.4F, 41.5F, 1.0F, 0.3F, &low_cc));
    assert(!dab_validated_charger_build_lfp_low_voltage_cc_seed(
        1U, 3U, 21.4F, 33.0F, 1.0F, 0.3F, &low_cc));
    assert(!dab_validated_charger_build_lfp_low_voltage_cc_seed(
        2U, 4U, 21.4F, 33.0F, 1.0F, 0.3F, &low_cc));
    assert(!dab_validated_charger_build_lfp_low_voltage_cc_seed(
        2U, 3U, 21.4F, 33.0F, 5.0F, 0.3F, &low_cc));
    assert(dab_validated_charger_build_lfp_low_voltage_cc_seed(
        2U, 3U, 21.4F, 33.0F, 1.25F, 0.3F, &low_cc));
    assert(low_cc.configured_imax_a == 1.25F);
    assert(dab_validated_charger_build_lfp_low_voltage_cc_seed(
        2U, 3U, 21.4F, 33.0F, 2.0F, 0.3F, &low_cc));
    assert(!dab_validated_charger_build_lfp_low_voltage_cc_seed(
        2U, 3U, 21.4F, 33.0F, 2.01F, 0.3F, &low_cc));
    assert(dab_validated_charger_build_lfp_low_voltage_cc_seed(
        2U, 3U, 21.4F, 33.0F, 0.8F, 0.3F, &low_cc));
    assert((low_cc.entry_id ==
            DAB_VALIDATED_LFP_MPPT_ULTRALOW_ENTRY_ID) &&
           (low_cc.primary_inner_ns == 832.0F) &&
           (low_cc.phase_cap_ns == 20.0F) &&
           (dab_validated_charger_current_direction(low_cc.entry_id) == -1));
    assert(dab_validated_charger_build_lfp_low_voltage_cc_seed(
        2U, 3U, 21.4F, 33.0F, 0.81F, 0.3F, &low_cc));
    assert((low_cc.entry_id ==
            DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_ENTRY_ID) &&
           (low_cc.primary_inner_ns == 720.0F));

    dab_validated_charger_entry_t high_current_bridge = {0};
    assert(dab_validated_charger_build_low_voltage_high_current_cc_seed(
        2U, 3U, 21.4F, 33.0F, 5.0F, 0.3F,
        &high_current_bridge));
    assert((high_current_bridge.entry_id ==
            DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_ENTRY_ID) &&
           (high_current_bridge.battery_profile_id == 2U) &&
           (high_current_bridge.charge_stage_id == 3U) &&
           (high_current_bridge.modulation_id == 0U) &&
           (high_current_bridge.primary_inner_ns == 0.0F) &&
           (high_current_bridge.secondary_inner_ns == 0.0F) &&
           (high_current_bridge.phase_cap_ns == 120.0F) &&
           (high_current_bridge.current_kp_ns_per_a == 9.785F) &&
           (high_current_bridge.current_ki_us_per_as == 0.4892F) &&
           (high_current_bridge.current_slew_a_per_s == 0.5F) &&
           (high_current_bridge.configured_imax_a == 5.0F));
    assert(dab_validated_charger_build_low_voltage_high_current_cc_seed(
        1U, 3U, 21.4F, 30.0F, 5.0F, 0.3F,
        &high_current_bridge));
    assert(dab_validated_charger_build_low_voltage_high_current_cc_seed(
        2U, 3U, 24.0F, 36.0F, 10.0F, 0.3F,
        &high_current_bridge));
    assert(high_current_bridge.configured_imax_a == 10.0F);
    assert(!dab_validated_charger_build_low_voltage_high_current_cc_seed(
        2U, 3U, 24.0F, 36.0F, 10.1F, 0.3F,
        &high_current_bridge));
    assert(!dab_validated_charger_build_low_voltage_high_current_cc_seed(
        2U, 3U, 21.4F, 39.0F, 5.0F, 0.3F,
        &high_current_bridge));
    assert(!dab_validated_charger_build_low_voltage_high_current_cc_seed(
        2U, 3U, 21.4F, 33.0F, 1.0F, 0.3F,
        &high_current_bridge));
    assert(!dab_validated_charger_build_low_voltage_high_current_cc_seed(
        2U, 3U, 21.4F, 33.0F, 1.25F, 0.3F,
        &high_current_bridge));
    assert(dab_validated_charger_build_low_voltage_high_current_cc_seed(
        2U, 3U, 21.4F, 33.0F, 2.01F, 0.3F,
        &high_current_bridge));
    assert(!dab_validated_charger_build_low_voltage_high_current_cc_seed(
        2U, 4U, 21.4F, 33.0F, 5.0F, 0.3F,
        &high_current_bridge));

    dab_tuned_parameter_bundle_t voltage = {0};
    assert(dab_tuned_parameter_lookup_exact(24.0F, 43.5F, 18.0F, 200.0F,
                                             0U, 0U, &voltage));
    assert(voltage.valid && (voltage.phase_cap_ns == 160.0F));

    dab_tuned_current_bundle_t current = {0};
    float score = 0.0F;
    assert(dab_tuned_current_parameter_lookup_exact(
        19.2F, 48.0F, 0.5F, 1U, 1U, &current, &score));
    assert(current.valid && (current.secondary_inner_ns == 520.0F));
    assert(score < 0.1F);
    assert(!dab_tuned_current_parameter_lookup_exact(
        19.0F, 48.0F, 0.5F, 0U, 1U, &current, &score));
    assert(!dab_tuned_current_parameter_lookup_exact(
        24.0F, 42.0F, 4.0F, 0U, 1U, &current, &score));

    assert(dab_tuned_current_parameter_lookup_cp_nearest(
        23.1F, 42.0F, 50.0F / 42.0F, &current, &score));
    assert(current.valid);
    assert(current.modulation_id <= 2U);
    assert(current.entry_id == 721910276UL);
    assert(current.proven_relative_efficiency_pct > 87.0F);

    dab_validated_charger_entry_t charger = {0};
    assert(dab_validated_charger_lookup_nearest(
        1U, 3U, 23.0F, 42.0F, 1.0F, &charger, &score));
    assert((charger.battery_profile_id == 1U) &&
           (charger.charge_stage_id == 3U));
    assert(dab_validated_charger_lookup_nearest(
        1U, 4U, 23.0F, 36.0F, 5.0F, &charger, &score));
    assert((charger.battery_profile_id == 1U) &&
           (charger.charge_stage_id == 4U));
    assert(dab_validated_charger_lookup_nearest(
        2U, 4U, 23.1F, 43.8F, 1.0F, &charger, &score));
    assert((charger.battery_profile_id == 2U) &&
           (charger.charge_stage_id == 4U));
    assert(dab_validated_charger_lookup_nearest(
        2U, 3U, 21.13F, 43.0F, 1.0F, &charger, &score));
    assert((charger.modulation_id == 1U) &&
           (charger.primary_inner_ns == 0.0F) &&
           (charger.secondary_inner_ns == 44.0F) &&
           (charger.phase_cap_ns == 120.0F));
    assert(dab_validated_charger_lookup_nearest(
        2U, 3U, 19.06F, 43.0F, 1.0F, &charger, &score));
    assert((charger.modulation_id == 2U) &&
           (charger.primary_inner_ns == 100.0F) &&
           (charger.secondary_inner_ns == 564.0F) &&
           (charger.phase_cap_ns == 260.0F));

    puts("promoted table tests passed");
    return s_failures;
}
