#ifndef DAB_VALIDATED_CHARGER_TABLE_H
#define DAB_VALIDATED_CHARGER_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAB_VALIDATED_CHARGER_TABLE_REVISION (18UL)
#define DAB_VALIDATED_LIION_CV_PRIMARY_INNER_NS (720.0F)
#define DAB_VALIDATED_LIION_PRECHARGE_ENTRY_ID (29022001UL)
#define DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID (30022001UL)
#define DAB_VALIDATED_LIION_PRECHARGE_PRIMARY_INNER_NS (864.0F)
#define DAB_VALIDATED_LFP_PRECHARGE_PRIMARY_INNER_NS (832.0F)
#define DAB_VALIDATED_PRECHARGE_SECONDARY_INNER_NS (180.0F)
#define DAB_VALIDATED_PRECHARGE_PHASE_CAP_NS (20.0F)
#define DAB_VALIDATED_PRECHARGE_EFFECTIVE_BIAS_NS (0.0F)
#define DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_EXIT_V (41.5F)
#define DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_ENTRY_ID (33022001UL)
#define DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_PRIMARY_INNER_NS (720.0F)
#define DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_PHASE_CAP_NS (80.0F)
#define DAB_VALIDATED_LFP_MPPT_ULTRALOW_ENTRY_ID (33022002UL)
#define DAB_VALIDATED_LFP_MPPT_ULTRALOW_PRIMARY_INNER_NS (832.0F)
#define DAB_VALIDATED_LFP_MPPT_ULTRALOW_CURRENT_MAX_A (0.8F)
#define DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_CURRENT_MIN_A (0.0F)
#define DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_CURRENT_MAX_A (2.0F)
#define DAB_VALIDATED_HIGH_CURRENT_CC_SLEW_A_PER_S (0.5F)
#define DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_ENTRY_ID (33050001UL)
#define DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_EXIT_V (39.0F)
#define DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_MIN_A (2.0F)
#define DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_MAX_A (10.0F)
#define DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_PHASE_CAP_NS (120.0F)
#define DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_KP_NS_PER_A (9.785F)
#define DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_KI_US_PER_AS (0.4892F)
#define DAB_VALIDATED_LFP_PRECHARGE_BLEND_START_V (31.0F)
#define DAB_VALIDATED_LFP_PRECHARGE_BLEND_END_V (32.4F)
#define DAB_VALIDATED_LIION_PRECHARGE_BLEND_START_V (29.0F)
#define DAB_VALIDATED_LIION_PRECHARGE_BLEND_END_V (30.0F)
#define DAB_VALIDATED_LIION_PRECHARGE_BLEND_PRIMARY_INNER_NS (832.0F)

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

#include "dab_validated_charger_records.generated.h"

static inline float dab_validated_charger_abs(float value)
{
    return (value < 0.0F) ? -value : value;
}

/* FLOAT and DONE are post-CC voltage-regulation states.  Reusing a CC seed
 * after either state can reintroduce an inner-shift power floor exactly when
 * the requested charge current is falling to zero. */
static inline uint8_t dab_validated_charger_normalize_stage(uint8_t stage_id)
{
    return ((4U == stage_id) || (5U == stage_id) || (6U == stage_id)) ?
           4U : 3U;
}

/* LFP taper/termination around the validated near-unity conversion ratio can
 * remove the EPS inner shift and let the outer current PI reach zero.  Do not
 * apply that rule to the 24 V -> 42 V Li-ion operating point: physical r49
 * showed EPS 600/0 -> 0/0 at M ~= 0.91 moves back onto the board's SPS-zero
 * natural-transfer point and raises current from 0.575 A to 2.381 A.  Li-ion
 * therefore retains its validated shaped bridge and uses outer phase for CV;
 * DONE still disables PWM and all four power paths locally. */
static inline bool dab_validated_charger_zero_inner_for_taper(
    uint8_t battery_profile_id,
    uint8_t stage_id)
{
    bool const validated_lfp_profile = (2U == battery_profile_id);
    return validated_lfp_profile && ((4U == stage_id) || (6U == stage_id));
}

/* New-MOS r51 measured a 0.548 A transfer floor at the Li-ion 24 V ->
 * 42.3 V terminal point with the promoted EPS 600/0 ns bundle and zero
 * effective outer phase.  The physical r49 sweep established the local
 * direction: reducing primary inner below 600 ns raises current monotonically
 * in the upper region.  Expand only this already-shaped Li-ion EPS taper
 * partition to 720 ns after the intermediate 680 ns r52 point reduced the
 * external current from 0.548 A to 0.382 A without a transient.  The ordinary
 * 0.04 ns/ms inner slew makes the change
 * bumpless; CC, low-input bundles and LFP's independently validated zero-inner
 * termination path remain unchanged. */
static inline float dab_validated_charger_primary_inner_for_taper(
    uint8_t battery_profile_id,
    uint8_t stage_id,
    uint8_t modulation_id,
    float scheduled_primary_inner_ns)
{
    bool const liion_cv_or_done = (1U == battery_profile_id) &&
        ((4U == stage_id) || (6U == stage_id));
    bool const shaped_eps_bundle = (1U == modulation_id) &&
        (scheduled_primary_inner_ns >= 600.0F);
    return (liion_cv_or_done && shaped_eps_bundle) ?
           DAB_VALIDATED_LIION_CV_PRIMARY_INNER_NS :
           scheduled_primary_inner_ns;
}

/* A low battery terminal voltage must not inherit either chemistry's
 * high-voltage CC bundle.  With the replacement MOSFETs those bundles have a
 * large natural-transfer floor around 29...30 V.  EPS 720/0 ns still measured
 * about 1.9 A at the locked LFP CV30 point, so PRECHARGE uses the local
 * deterministic TPS ratio mapping for M ~= 30/(2*21.4) ~= 0.70 initially
 * produced primary/secondary=720/180 ns.  Physical r55 reduced the current
 * floor to 1.65...1.67 A without a fault, while r56 at 720/360 ns rose back to
 * 1.82...1.85 A.  The signed -8/+16 ns r57/r58 probe did not reduce the
 * floor.  Continue only the independently proven primary-inner direction:
 * 760 ns is the first special-precharge point above the ordinary 720 ns
 * worst-case limit; physical r59/r60 reduced the CV30 current to
 * 1.46...1.51 A and then about 1.31 A without a fault.  Use the last
 * separately checked LFP special-precharge point, 832 ns, without changing
 * secondary inner, cap, PI, or state logic.  At the lower Li-ion CV29 point,
 * physical r64 remained at 1.33...1.34 A with the same 832/180 ns shape even
 * after the startup-state detour was removed.  Continue the same proven
 * primary-inner direction by one 32 ns step for Li-ion only.  At 864 ns the
 * compile-time and run-time edge equations include the configured 400 ns
 * dead time and retain another 400 ns before the period boundary; the LFP
 * seed remains unchanged at its accepted 832 ns point. */
static inline bool dab_validated_charger_build_precharge_seed(
    uint8_t battery_profile_id,
    uint8_t raw_stage_id,
    float measured_vin_v,
    float requested_precharge_current_a,
    float configured_iterm_a,
    dab_validated_charger_entry_t * p_entry)
{
    bool const liion = (1U == battery_profile_id);
    bool const lfp = (2U == battery_profile_id);
    bool const precharge_path = (0U == raw_stage_id) ||
                                (1U == raw_stage_id) ||
                                (2U == raw_stage_id);
    if ((!liion && !lfp) || !precharge_path || (NULL == p_entry))
    {
        return false;
    }

    *p_entry = (dab_validated_charger_entry_t)
    {
        liion ? DAB_VALIDATED_LIION_PRECHARGE_ENTRY_ID :
                DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID,
        battery_profile_id,
        2U,
        2U,
        2U,
        22.0F,
        liion ? 29.0F : 30.0F,
        liion ? DAB_VALIDATED_LIION_PRECHARGE_PRIMARY_INNER_NS :
                DAB_VALIDATED_LFP_PRECHARGE_PRIMARY_INNER_NS,
        DAB_VALIDATED_PRECHARGE_SECONDARY_INNER_NS,
        DAB_VALIDATED_PRECHARGE_PHASE_CAP_NS,
        liion ? 0.5F : 2.0F,
        liion ? 0.05F : 0.2F,
        0.05F,
        requested_precharge_current_a,
        configured_iterm_a,
        measured_vin_v,
        liion ? 29.0F : 30.0F,
        0.0F,
        0.0F,
        0.0F,
        liion ? 29.0F : 30.0F,
        0.0F,
        0.0F,
        0.0F
    };
    return true;
}

static inline float dab_validated_charger_precharge_effective_bias_ns(
    uint32_t entry_id)
{
    return ((DAB_VALIDATED_LIION_PRECHARGE_ENTRY_ID == entry_id) ||
            (DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID == entry_id)) ?
           DAB_VALIDATED_PRECHARGE_EFFECTIVE_BIAS_NS : 0.0F;
}

static inline bool dab_validated_charger_is_precharge_entry(uint32_t entry_id)
{
    return (DAB_VALIDATED_LIION_PRECHARGE_ENTRY_ID == entry_id) ||
           (DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID == entry_id);
}

/* At the locked LFP CV31 point TPS 832/180 ns gives about 1 A, but the same
 * shape falls to about 0.67 A while Vout approaches the 32.4 V PRECHARGE/CC
 * boundary.  The separately powered CV33 repair used TPS 720/180 ns and held
 * about 1.08 A.  Move between those two measured shapes before CPU1 changes
 * stage so the current does not first collapse and then recover.  Li-ion uses
 * only the already proven adjacent 864 -> 832 ns step near its boundary. */
static inline float dab_validated_charger_precharge_primary_inner_for_voltage(
    uint32_t entry_id,
    float operating_voltage_v,
    float scheduled_primary_inner_ns)
{
    float blend_start_v = 0.0F;
    float blend_end_v = 0.0F;
    float blend_end_inner_ns = scheduled_primary_inner_ns;
    if (DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID == entry_id)
    {
        blend_start_v = DAB_VALIDATED_LFP_PRECHARGE_BLEND_START_V;
        blend_end_v = DAB_VALIDATED_LFP_PRECHARGE_BLEND_END_V;
        blend_end_inner_ns =
            DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_PRIMARY_INNER_NS;
    }
    else if (DAB_VALIDATED_LIION_PRECHARGE_ENTRY_ID == entry_id)
    {
        blend_start_v = DAB_VALIDATED_LIION_PRECHARGE_BLEND_START_V;
        blend_end_v = DAB_VALIDATED_LIION_PRECHARGE_BLEND_END_V;
        blend_end_inner_ns =
            DAB_VALIDATED_LIION_PRECHARGE_BLEND_PRIMARY_INNER_NS;
    }
    else
    {
        return scheduled_primary_inner_ns;
    }

    if (operating_voltage_v <= blend_start_v)
    {
        return scheduled_primary_inner_ns;
    }
    if (operating_voltage_v >= blend_end_v)
    {
        return blend_end_inner_ns;
    }
    float const fraction = (operating_voltage_v - blend_start_v) /
                           (blend_end_v - blend_start_v);
    return scheduled_primary_inner_ns +
           (fraction * (blend_end_inner_ns - scheduled_primary_inner_ns));
}

/* The special PRECHARGE TPS records have a weak negative local outer-phase
 * slope around their zero-effective-phase point.  A positive current error
 * must therefore retreat toward zero phase, not integrate into the +20 ns
 * boundary as v49j did during the live CV31 -> CV33 sweep. */
static inline int8_t dab_validated_charger_current_direction(uint32_t entry_id)
{
    return (dab_validated_charger_is_precharge_entry(entry_id) ||
            (DAB_VALIDATED_LFP_MPPT_ULTRALOW_ENTRY_ID == entry_id)) ? -1 : 1;
}

/* The promoted charge records were measured at a 1 A bench point and retain
 * their conservative 0.05 A/s label.  A firmware-owned 5 A CC profile would
 * need roughly 80 seconds to move from precharge current to its CC target.
 * The fused charger/MPPT plan requires a circuit-friendly transition that
 * ramps bulk current without a step: 0.5 A/s moves 1 A to 5 A in 8 seconds
 * and continues toward the local 1 C / 360 W ceiling.  PRECHARGE and
 * CV/FLOAT/DONE keep the original slow ramp/taper. */
static inline float dab_validated_charger_runtime_current_slew(
    uint8_t stage_id,
    float requested_max_current_a,
    float scheduled_slew_a_per_s)
{
    bool const high_current_cc = (3U == stage_id) &&
        (requested_max_current_a >
         DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_CURRENT_MAX_A);
    return high_current_cc ? DAB_VALIDATED_HIGH_CURRENT_CC_SLEW_A_PER_S :
                             scheduled_slew_a_per_s;
}

/* The 22 V / 43 V LFP CC entry is physically validated near 43 V, but r54
 * showed that applying its EPS 0/44 ns shape at a 30 V terminal raises Iout
 * to 4.13 A.  From Vpre through the lower CC region, retain the separately
 * validated TPS precharge shape while CPU1 still reports and controls the CC
 * stage.  The ordinary powered CC table takes ownership only near its tested
 * high-voltage region. */
static inline bool dab_validated_charger_lfp_low_voltage_cc_shape(
    uint8_t battery_profile_id,
    uint8_t normalized_stage_id,
    float operating_voltage_v)
{
    return (2U == battery_profile_id) &&
           (3U == normalized_stage_id) &&
           (operating_voltage_v <
            DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_EXIT_V);
}

/* r68 proved TPS 832/180 ns at LFP CV33 as a safe approximately 0.725 A /
 * 31 W low-power actuator.  r69 proved TPS 720/180 ns at the same terminal as
 * the approximately 1.08 A / 43 W actuator.  Keep both as distinct entry IDs
 * so a live MPPT reference below 0.8 A cannot escape into an unrelated
 * high-voltage table row.  For the 22 V / 2.5 A pseudo-PV knee, a 60 W input
 * ceiling maps to less than 2 A at the CV33 output.  Retain TPS 720/180 ns
 * through that low-power search region so the MPPT demand cannot cross the old
 * 1.5 A scheduler boundary and enter the 184 W SPS natural-transfer floor seen
 * in r83.  The ordinary 0.5 A/s reference still limits output-current movement. */
static inline bool dab_validated_charger_build_lfp_low_voltage_cc_seed(
    uint8_t battery_profile_id,
    uint8_t normalized_stage_id,
    float measured_vin_v,
    float operating_voltage_v,
    float requested_current_a,
    float configured_iterm_a,
    dab_validated_charger_entry_t * p_entry)
{
    if (!dab_validated_charger_lfp_low_voltage_cc_shape(
            battery_profile_id, normalized_stage_id, operating_voltage_v) ||
        (requested_current_a <
         DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_CURRENT_MIN_A) ||
        (requested_current_a >
         DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_CURRENT_MAX_A) ||
        (NULL == p_entry))
    {
        return false;
    }

    if (!dab_validated_charger_build_precharge_seed(
            battery_profile_id,
            2U,
            measured_vin_v,
            requested_current_a,
            configured_iterm_a,
            p_entry))
    {
        return false;
    }

    bool const ultralow_mppt =
        requested_current_a <=
        DAB_VALIDATED_LFP_MPPT_ULTRALOW_CURRENT_MAX_A;
    p_entry->entry_id = ultralow_mppt ?
        DAB_VALIDATED_LFP_MPPT_ULTRALOW_ENTRY_ID :
        DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_ENTRY_ID;
    p_entry->charge_stage_id = 3U;
    p_entry->external_cv_set_v = operating_voltage_v;
    p_entry->primary_inner_ns = ultralow_mppt ?
        DAB_VALIDATED_LFP_MPPT_ULTRALOW_PRIMARY_INNER_NS :
        DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_PRIMARY_INNER_NS;
    /* r87 proved that the 20 ns cap leaves the TPS720 actuator saturated at
     * Pin about 42 W while INC correctly requests 60 W.  Give only the normal
     * 0.8...2.0 A low-voltage CC partition one additional two-count (8 ns)
     * phase window.  PRECHARGE and the TPS832 ultralow partition keep their
     * separately proven 20 ns cap. */
    p_entry->phase_cap_ns = ultralow_mppt ?
        DAB_VALIDATED_PRECHARGE_PHASE_CAP_NS :
        DAB_VALIDATED_LFP_LOW_VOLTAGE_CC_PHASE_CAP_NS;
    p_entry->board_vout_v = operating_voltage_v;
    p_entry->external_voltage_v = operating_voltage_v;
    return true;
}

/* The 43 V / 1 A EPS 0/44 ns record has a +22 ns natural effective phase.
 * At the live 33 V / 5 A point it produced a 5.89 A floor even with the outer
 * command at zero.  Use the physically identified low-voltage SPS current
 * controller as a zero-inner bridge for both supported lithium chemistries.
 * Its 120 ns phase envelope covers the 165 W 33 V / 5 A workpoint while the
 * firmware-owned 0.5 A/s reference keeps the transition circuit-friendly. */
static inline bool
dab_validated_charger_build_low_voltage_high_current_cc_seed(
    uint8_t battery_profile_id,
    uint8_t normalized_stage_id,
    float measured_vin_v,
    float operating_voltage_v,
    float requested_current_a,
    float configured_iterm_a,
    dab_validated_charger_entry_t * p_entry)
{
    bool const supported_profile = (1U == battery_profile_id) ||
                                   (2U == battery_profile_id);
    if (!supported_profile || (3U != normalized_stage_id) ||
        (operating_voltage_v >=
         DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_EXIT_V) ||
        (requested_current_a <=
         DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_MIN_A) ||
        (requested_current_a >
         DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_MAX_A) ||
        (NULL == p_entry))
    {
        return false;
    }

    *p_entry = (dab_validated_charger_entry_t)
    {
        DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_ENTRY_ID,
        battery_profile_id,
        3U,
        0U,
        2U,
        22.0F,
        operating_voltage_v,
        0.0F,
        0.0F,
        DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_PHASE_CAP_NS,
        DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_KP_NS_PER_A,
        DAB_VALIDATED_LOW_VOLTAGE_HIGH_CURRENT_CC_KI_US_PER_AS,
        DAB_VALIDATED_HIGH_CURRENT_CC_SLEW_A_PER_S,
        requested_current_a,
        configured_iterm_a,
        measured_vin_v,
        operating_voltage_v,
        0.0F,
        0.0F,
        0.0F,
        operating_voltage_v,
        0.0F,
        0.0F,
        0.0F
    };
    return true;
}

static inline bool dab_validated_charger_lookup_nearest(
    uint8_t battery_profile_id,
    uint8_t preferred_charge_stage_id,
    float measured_vin_v,
    float requested_voltage_v,
    float requested_current_a,
    dab_validated_charger_entry_t * p_entry,
    float * p_score)
{
    if (NULL == p_entry)
    {
        return false;
    }

    bool exact_stage_available = false;
    for (size_t i = 0U; i < DAB_VALIDATED_CHARGER_TABLE_ENTRY_COUNT; i++)
    {
        dab_validated_charger_entry_t const * candidate =
            &g_dab_validated_charger_table[i];
        if ((candidate->battery_profile_id == battery_profile_id) &&
            (candidate->charge_stage_id == preferred_charge_stage_id))
        {
            exact_stage_available = true;
            break;
        }
    }

    bool found = false;
    float best_score = 1000000.0F;
    for (size_t i = 0U; i < DAB_VALIDATED_CHARGER_TABLE_ENTRY_COUNT; i++)
    {
        dab_validated_charger_entry_t const * candidate =
            &g_dab_validated_charger_table[i];
        if (candidate->battery_profile_id != battery_profile_id)
        {
            continue;
        }
        if (exact_stage_available &&
            (candidate->charge_stage_id != preferred_charge_stage_id))
        {
            continue;
        }
        float const vin_error = dab_validated_charger_abs(
            candidate->board_vin_v - measured_vin_v);
        float const voltage_error = dab_validated_charger_abs(
            candidate->external_cv_set_v - requested_voltage_v);
        float const current_error = dab_validated_charger_abs(
            candidate->configured_imax_a - requested_current_a);
        float const stage_penalty =
            (candidate->charge_stage_id == preferred_charge_stage_id) ?
            0.0F : 4.0F;
        float const score = (2.0F * vin_error) +
                            (2.0F * voltage_error) +
                            current_error + stage_penalty;
        if ((!found) || (score < best_score))
        {
            found = true;
            best_score = score;
            *p_entry = *candidate;
        }
    }
    if (found && (NULL != p_score))
    {
        *p_score = best_score;
    }
    return found;
}

_Static_assert(DAB_VALIDATED_CHARGER_TABLE_ENTRY_COUNT ==
               (sizeof(g_dab_validated_charger_table) /
                sizeof(g_dab_validated_charger_table[0])),
               "generated charger table count mismatch");

#endif /* DAB_VALIDATED_CHARGER_TABLE_H */
