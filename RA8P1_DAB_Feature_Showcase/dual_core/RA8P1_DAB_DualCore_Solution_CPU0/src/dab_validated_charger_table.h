#ifndef DAB_VALIDATED_CHARGER_TABLE_H
#define DAB_VALIDATED_CHARGER_TABLE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Physically validated charger/current-loop records.
 *
 * This table is deliberately record-only.  It is separate from
 * dab_tuned_parameter_table.h because that schedule contains voltage-loop
 * gains, whereas these entries contain output-current PI gains supervised by
 * the CPU1 battery state machine.  No entry is applied automatically at boot
 * or by `sched on`; a future charger lookup must perform explicit bounds and
 * profile/stage checks before using one.
 *
 * ID contract shared with existing telemetry:
 * battery_profile=2 -> LIFEPO4_12S_43P8V
 * charge_stage=3/4  -> CC/CV
 * modulation=1     -> EPS
 * architecture=2   -> CPU1_ENERGY_SUPERVISOR_CURRENT_PI
 */
#define DAB_VALIDATED_CHARGER_TABLE_REVISION    (1UL)
#define DAB_VALIDATED_CHARGER_TABLE_VERSION     "v43-20260816-lfp12-eps"
#define DAB_VALIDATED_CHARGER_TABLE_ENTRY_COUNT (5UL)

typedef struct st_dab_validated_charger_entry
{
    uint32_t entry_id;
    uint8_t  battery_profile_id;
    uint8_t  charge_stage_id;
    uint8_t  modulation_id;
    uint8_t  control_architecture_id;
    float    nominal_input_v;
    float    external_cv_set_v;
    float    primary_inner_ns;
    float    secondary_inner_ns;
    float    phase_cap_ns;
    float    current_kp_ns_per_a;
    float    current_ki_us_per_as;
    float    current_slew_a_per_s;
    float    configured_imax_a;
    float    configured_iterm_a;
    float    board_vin_v;
    float    board_vout_v;
    float    board_iout_a;
    float    board_pin_w;
    float    board_pout_w;
    float    external_voltage_v;
    float    external_current_a;
    float    external_power_w;
    float    external_to_board_input_eff_pct;
} dab_validated_charger_entry_t;

static const dab_validated_charger_entry_t g_dab_validated_charger_table[] =
{
    {1UL, 2U, 3U, 1U, 2U, 24.0F, 42.0F, 240.0F, 0.0F, 80.0F,
     2.0F, 0.2F, 0.05F, 1.0F, 0.5F,
     23.07536F, 41.82335F, 0.99888F, 48.3833F, 41.7767F,
     42.0110F, 1.02925F, 43.2395F, 89.366F},
    {2UL, 2U, 3U, 1U, 2U, 24.0F, 43.0F, 240.0F, 0.0F, 120.0F,
     2.0F, 0.2F, 0.05F, 1.0F, 0.5F,
     22.94452F, 42.60344F, 0.99805F, 48.8895F, 42.5205F,
     43.0020F, 1.0340F, 44.4640F, 90.9480F},
    {3UL, 2U, 3U, 1U, 2U, 24.0F, 43.5F, 240.0F, 0.0F, 120.0F,
     2.0F, 0.2F, 0.05F, 1.0F, 0.5F,
     23.08464F, 43.30539F, 0.99983F, 49.5561F, 43.2969F,
     43.5070F, 1.0332F, 44.9508F, 90.7069F},
    {4UL, 2U, 3U, 1U, 2U, 24.0F, 43.7F, 240.0F, 0.0F, 120.0F,
     2.0F, 0.2F, 0.05F, 1.0F, 0.5F,
     23.06668F, 43.45808F, 0.99969F, 49.7034F, 43.4452F,
     43.7130F, 1.0310F, 45.0674F, 90.6727F},
    {5UL, 2U, 4U, 1U, 2U, 24.0F, 44.0F, 240.0F, 0.0F, 120.0F,
     2.0F, 0.2F, 0.05F, 1.0F, 0.5F,
     23.14036F, 43.79035F, 0.42055F, 23.8992F, 18.4181F,
     44.0080F, 0.4556F, 20.0494F, 83.8915F}
};

_Static_assert(
    DAB_VALIDATED_CHARGER_TABLE_ENTRY_COUNT ==
    (sizeof(g_dab_validated_charger_table) /
     sizeof(g_dab_validated_charger_table[0])),
    "validated charger table count mismatch");

#endif /* DAB_VALIDATED_CHARGER_TABLE_H */
