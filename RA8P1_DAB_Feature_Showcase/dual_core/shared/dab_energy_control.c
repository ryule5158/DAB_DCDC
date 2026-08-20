#include "dab_energy_control.h"

#include <stddef.h>
#include <string.h>

#define ENERGY_IPC_KEY_SHIFT       (15U)
#define ENERGY_IPC_VALUE_MASK      (0x7FFFUL)
#define ENERGY_IPC_SIGN_BIT        (0x4000L)
#define ENERGY_IPC_SIGN_EXTEND     (0x8000L)
#define ENERGY_PRECHARGE_ENTRY_DEBOUNCE_MS (500U)
#define ENERGY_PRECHARGE_STARTUP_SETTLE_MS (2500U)
#define ENERGY_PRECHARGE_RECOVERY_DEBOUNCE_MS (100U)
#define ENERGY_PRECHARGE_RECOVERY_HYSTERESIS_V (0.20F)
#define ENERGY_CV_ENTRY_DEBOUNCE_MS        (1000U)
#define ENERGY_CV_RECOVERY_DEBOUNCE_MS     (500U)
#define ENERGY_CV_RECOVERY_HYSTERESIS_V    (0.50F)
#define ENERGY_CV_ENTRY_OVERSHOOT_V        (0.50F)
#define ENERGY_CV_SENSE_COMPENSATION_V     (0.30F)
#define ENERGY_CV_TERMINATION_UNDERVOLTAGE_V (0.10F)
#define ENERGY_CV_TERMINATION_MARGIN_MIN_A (0.02F)
#define ENERGY_CV_TERMINATION_MARGIN_RATIO (0.10F)
#define ENERGY_SYSTEM_POWER_LIMIT_W          (360.0F)
#define ENERGY_LITHIUM_MAX_CURRENT_A         (10.0F)
#define ENERGY_MPPT_DP_REVERSE_THRESHOLD_W   (2.0F)
#define ENERGY_MPPT_BEST_IMPROVEMENT_W        (1.0F)
#define ENERGY_MPPT_DV_DEADBAND_V             (0.05F)
#define ENERGY_MPPT_DI_DEADBAND_A             (0.05F)
#define ENERGY_MPPT_CURRENT_STEP_A              (0.125F)
#define ENERGY_MPPT_HIGH_POWER_STEP_A            (0.025F)
#define ENERGY_MPPT_HIGH_POWER_THRESHOLD_W      (150.0F)
#define ENERGY_MPPT_UNDERVOLTAGE_STEP_A         (0.050F)
#define ENERGY_MPPT_REACQUIRE_V_DELTA_V         (0.65F)
#define ENERGY_MPPT_REACQUIRE_CONFIRM_MS        (5000U)
#define ENERGY_MPPT_DROP_MARGIN_RATIO             (0.02F)
#define ENERGY_MPPT_DROP_CURRENT_MARGIN_A         (0.25F)
#define ENERGY_MPPT_DROP_CONFIRM_SAMPLES          (4U)
#define ENERGY_MPPT_HOLD_RECOVERY_HYSTERESIS_V    (0.08F)
#define ENERGY_MPPT_HOLD_SEVERE_LOW_V              (0.65F)
#define ENERGY_MPPT_HOLD_TRIM_STEP_A               (0.010F)
#define ENERGY_MPPT_ACTUATOR_SETTLE_A              (0.20F)
#define ENERGY_MPPT_ACTUATOR_RELEASE_A             (0.05F)
#define ENERGY_MPPT_KNEE_BEND_V                    (0.12F)
#define ENERGY_MPPT_HIGH_POWER_KNEE_BEND_V         (0.04F)
#define ENERGY_MPPT_KNEE_MIN_POWER_W               (20.0F)
#define ENERGY_MPPT_REPLAY_POWER_RATIO              (0.97F)
#define ENERGY_MPPT_KNEE_RESERVE_MIN_A             (0.125F)
#define ENERGY_MPPT_KNEE_RESERVE_MAX_A             (0.375F)

static float energy_min(float a, float b)
{
    return (a < b) ? a : b;
}

static float energy_max(float a, float b)
{
    return (a > b) ? a : b;
}

static float energy_clamp(float value, float lower, float upper)
{
    return energy_min(energy_max(value, lower), upper);
}

static float energy_abs(float value)
{
    return (value < 0.0F) ? -value : value;
}

static float energy_config_scale(dab_energy_config_key_t key)
{
    switch (key)
    {
        case DAB_ENERGY_CONFIG_MANUAL_CURRENT_A:
        case DAB_ENERGY_CONFIG_BATTERY_MAX_CURRENT_A:
        case DAB_ENERGY_CONFIG_BATTERY_PRECHARGE_CURRENT_A:
        case DAB_ENERGY_CONFIG_BATTERY_TERMINATE_CURRENT_A:
        case DAB_ENERGY_CONFIG_AI_HINT_CURRENT_A:
        case DAB_ENERGY_CONFIG_CHARGE_KP_A_PER_V:
        case DAB_ENERGY_CONFIG_CHARGE_KI_A_PER_VS:
            return 100.0F;

        case DAB_ENERGY_CONFIG_MPPT_STEP_V:
        case DAB_ENERGY_CONFIG_MPPT_MIN_V:
        case DAB_ENERGY_CONFIG_MPPT_MAX_V:
        case DAB_ENERGY_CONFIG_BATTERY_PRECHARGE_V:
        case DAB_ENERGY_CONFIG_BATTERY_CV_V:
        case DAB_ENERGY_CONFIG_BATTERY_FLOAT_V:
            return 100.0F;

        case DAB_ENERGY_CONFIG_MPPT_MAX_POWER_W:
        case DAB_ENERGY_CONFIG_MPPT_FIXED_POWER_W:
        case DAB_ENERGY_CONFIG_BATTERY_CAPACITY_AH:
        case DAB_ENERGY_CONFIG_BATTERY_TEMPERATURE_C:
            return 10.0F;

        case DAB_ENERGY_CONFIG_AI_CONFIDENCE:
        case DAB_ENERGY_CONFIG_AI_MODULATION_CONFIDENCE:
            return 1000.0F;

        case DAB_ENERGY_CONFIG_MPPT_INTERVAL_MS:
        case DAB_ENERGY_CONFIG_AI_TTL_MS:
        case DAB_ENERGY_CONFIG_AI_MODULATION_TTL_MS:
        case DAB_ENERGY_CONFIG_BATTERY_TIMEOUT_S:
            return 0.1F;

        default:
            return 1.0F;
    }
}

bool dab_energy_config_payload_encode(dab_energy_config_key_t key,
                                      float value,
                                      uint32_t * p_payload)
{
    if ((NULL == p_payload) || ((uint32_t) key >= (uint32_t) DAB_ENERGY_CONFIG_COUNT) ||
        ((uint32_t) key >= 32U))
    {
        return false;
    }
    float const scaled = value * energy_config_scale(key);
    if ((scaled < -16384.0F) || (scaled > 16383.0F))
    {
        return false;
    }
    int32_t const fixed = (int32_t) (scaled + ((scaled >= 0.0F) ? 0.5F : -0.5F));
    *p_payload = (((uint32_t) key) << ENERGY_IPC_KEY_SHIFT) |
                 (((uint32_t) fixed) & ENERGY_IPC_VALUE_MASK);
    return true;
}

bool dab_energy_config_payload_decode(uint32_t payload,
                                      dab_energy_config_key_t * p_key,
                                      float * p_value)
{
    if ((NULL == p_key) || (NULL == p_value))
    {
        return false;
    }
    uint32_t const key_raw = payload >> ENERGY_IPC_KEY_SHIFT;
    if (key_raw >= (uint32_t) DAB_ENERGY_CONFIG_COUNT)
    {
        return false;
    }
    int32_t fixed = (int32_t) (payload & ENERGY_IPC_VALUE_MASK);
    if (0L != (fixed & ENERGY_IPC_SIGN_BIT))
    {
        fixed -= ENERGY_IPC_SIGN_EXTEND;
    }
    *p_key = (dab_energy_config_key_t) key_raw;
    *p_value = (float) fixed / energy_config_scale(*p_key);
    return true;
}

static void energy_apply_profile(dab_energy_state_t * p_state,
                                 dab_battery_profile_t profile)
{
    /* Generic bench defaults, not a substitute for a pack datasheet/BMS:
     * Microchip AN1467: Li-ion 4.2 V/cell, LFP 3.65 V/cell, C/10
     * precharge and about C/33 termination; lead 2.4 V/cell charge.
     * GS Yuasa NP/PE: 2.275 V/cell float and 0.25 C initial maximum.
     * UART overrides exist for every pack-level voltage/current/time value. */
    dab_energy_config_t * const c = &p_state->config;
    c->battery_profile = profile;
    switch (profile)
    {
        case DAB_BATTERY_PROFILE_LI_ION_10S_42V:
            c->battery_precharge_v = 30.0F;
            c->battery_cv_v = 42.0F;
            c->battery_float_v = 0.0F;
            break;

        case DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V:
            c->battery_precharge_v = 32.4F;
            c->battery_cv_v = 43.8F;
            c->battery_float_v = 0.0F;
            break;

        case DAB_BATTERY_PROFILE_LEAD_ACID_18S_36V:
            c->battery_precharge_v = 31.5F;
            c->battery_cv_v = 43.2F;
            c->battery_float_v = 40.95F;
            break;

        case DAB_BATTERY_PROFILE_CUSTOM:
        default:
            break;
    }

    float const capacity = energy_max(c->battery_capacity_ah, 0.1F);
    if (DAB_BATTERY_PROFILE_LEAD_ACID_18S_36V == profile)
    {
        c->battery_max_current_a = energy_min(5.0F, 0.25F * capacity);
        c->battery_precharge_current_a = 0.0F;
        c->battery_terminate_current_a = energy_max(0.10F, 0.025F * capacity);
    }
    else
    {
        /* The two released lithium profiles use a 10 Ah bench default.  Their
         * bulk-current envelope is 1 C, additionally limited at runtime by
         * the converter's 360 W rating.  PRECHARGE and termination remain at
         * the conservative C/10 and C/33 levels. */
        c->battery_max_current_a = energy_min(ENERGY_LITHIUM_MAX_CURRENT_A,
                                              1.0F * capacity);
        c->battery_precharge_current_a = energy_min(1.0F, 0.1F * capacity);
        c->battery_terminate_current_a = energy_max(0.10F, 0.03F * capacity);
    }
}

void dab_energy_initialize(dab_energy_state_t * p_state)
{
    if (NULL == p_state)
    {
        return;
    }
    memset(p_state, 0, sizeof(*p_state));
    p_state->config.mode = DAB_ENERGY_MODE_OFF;
    p_state->config.manual_current_a = 1.0F;
    p_state->config.mppt_mode = DAB_MPPT_MODE_PERTURB_OBSERVE;
    p_state->config.mppt_step_v = 0.20F;
    p_state->config.mppt_interval_ms = 250U;
    /* Physical qualification remains 20/22/24 V at the source terminals.
     * r117 measured about 18.1 V on the board at nominal 20 V / 10 A after
     * source and wiring drop.  Keep that accepted point inside the standalone
     * tracker with a 17.5 V local control floor; this is not a new test point
     * and does not depend on an upper-computer range command. */
    p_state->config.mppt_min_v = 17.5F;
    p_state->config.mppt_max_v = 26.0F;
    p_state->config.mppt_max_power_w = ENERGY_SYSTEM_POWER_LIMIT_W;
    p_state->config.mppt_fixed_power_w = 100.0F;
    p_state->config.battery_capacity_ah = 10.0F;
    p_state->config.battery_absorb_s = 7200U;
    p_state->config.battery_timeout_s = 21600U;
    p_state->config.charge_kp_a_per_v = 2.0F;
    p_state->config.charge_ki_a_per_vs = 0.20F;
    p_state->config.ai_ttl_ms = 1000U;
    p_state->config.ai_modulation_ttl_ms = 1000U;
    p_state->po_direction = 1;
    energy_apply_profile(p_state, DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V);
}

dab_energy_mode_t dab_energy_charge_start_mode(
    dab_battery_profile_t battery_profile,
    dab_mppt_mode_t mppt_mode)
{
    bool const supported_lithium =
        (DAB_BATTERY_PROFILE_LI_ION_10S_42V == battery_profile) ||
        (DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V == battery_profile);
    bool const tracking_enabled =
        (DAB_MPPT_MODE_PERTURB_OBSERVE == mppt_mode) ||
        (DAB_MPPT_MODE_INCREMENTAL_CONDUCTANCE == mppt_mode);
    return (supported_lithium && tracking_enabled) ?
        DAB_ENERGY_MODE_SOLAR_CHARGER : DAB_ENERGY_MODE_CHARGER_ONLY;
}

static void energy_stage_enter(dab_energy_state_t * p_state,
                               dab_charge_stage_t stage,
                               uint32_t now_ms)
{
    p_state->output.charge_stage = stage;
    p_state->stage_started_ms = now_ms;
    p_state->below_terminate_since_ms = 0U;
    p_state->above_precharge_since_ms = 0U;
    p_state->below_precharge_since_ms = 0U;
    p_state->cv_entry_since_ms = 0U;
    p_state->below_cv_since_ms = 0U;
    p_state->charge_integral_a = 0.0F;
    p_state->cv_termination_hold_active = false;
}

static bool energy_condition_sustained(bool condition,
                                       uint32_t now_ms,
                                       uint32_t duration_ms,
                                       uint32_t * p_since_ms)
{
    if (!condition)
    {
        *p_since_ms = 0U;
        return false;
    }
    if (0U == *p_since_ms)
    {
        *p_since_ms = now_ms;
        return false;
    }
    return (uint32_t) (now_ms - *p_since_ms) >= duration_ms;
}

bool dab_energy_apply_config(dab_energy_state_t * p_state,
                             dab_energy_config_key_t key,
                             float value,
                             uint32_t now_ms)
{
    if (NULL == p_state)
    {
        return false;
    }
    dab_energy_config_t * const c = &p_state->config;
    switch (key)
    {
        case DAB_ENERGY_CONFIG_MODE:
            if ((value < 0.0F) || (value > (float) DAB_ENERGY_MODE_SOLAR_CHARGER)) return false;
            c->mode = (dab_energy_mode_t) ((uint32_t) value);
            break;
        case DAB_ENERGY_CONFIG_MANUAL_CURRENT_A:
            if ((value < 0.0F) || (value > 18.0F)) return false;
            c->manual_current_a = value;
            break;
        case DAB_ENERGY_CONFIG_MPPT_MODE:
            if ((value < 0.0F) || (value > (float) DAB_MPPT_MODE_FIXED_POWER)) return false;
            c->mppt_mode = (dab_mppt_mode_t) ((uint32_t) value);
            p_state->mppt_initialized = false;
            break;
        case DAB_ENERGY_CONFIG_MPPT_STEP_V:
            if ((value < 0.01F) || (value > 2.0F)) return false;
            c->mppt_step_v = value;
            break;
        case DAB_ENERGY_CONFIG_MPPT_INTERVAL_MS:
            if ((value < 50.0F) || (value > 5000.0F)) return false;
            c->mppt_interval_ms = (uint32_t) value;
            break;
        case DAB_ENERGY_CONFIG_MPPT_MIN_V:
            if ((value < 5.0F) || (value >= c->mppt_max_v)) return false;
            c->mppt_min_v = value;
            break;
        case DAB_ENERGY_CONFIG_MPPT_MAX_V:
            if ((value <= c->mppt_min_v) || (value > 60.0F)) return false;
            c->mppt_max_v = value;
            break;
        case DAB_ENERGY_CONFIG_MPPT_MAX_POWER_W:
            if ((value < 1.0F) || (value > ENERGY_SYSTEM_POWER_LIMIT_W)) return false;
            c->mppt_max_power_w = value;
            break;
        case DAB_ENERGY_CONFIG_MPPT_FIXED_POWER_W:
            if ((value < 0.0F) || (value > ENERGY_SYSTEM_POWER_LIMIT_W)) return false;
            c->mppt_fixed_power_w = value;
            break;
        case DAB_ENERGY_CONFIG_BATTERY_PROFILE:
            if ((value < 0.0F) || (value > (float) DAB_BATTERY_PROFILE_LEAD_ACID_18S_36V)) return false;
            energy_apply_profile(p_state, (dab_battery_profile_t) ((uint32_t) value));
            energy_stage_enter(p_state, DAB_CHARGE_STAGE_IDLE, now_ms);
            break;
        case DAB_ENERGY_CONFIG_BATTERY_CAPACITY_AH:
            if ((value < 0.1F) || (value > 1000.0F)) return false;
            c->battery_capacity_ah = value;
            energy_apply_profile(p_state, c->battery_profile);
            break;
        case DAB_ENERGY_CONFIG_BATTERY_MAX_CURRENT_A:
            if ((value < 0.05F) || (value > 18.0F)) return false;
            c->battery_max_current_a = value;
            break;
        case DAB_ENERGY_CONFIG_BATTERY_PRECHARGE_CURRENT_A:
            if ((value < 0.01F) || (value > 5.0F)) return false;
            c->battery_precharge_current_a = value;
            break;
        case DAB_ENERGY_CONFIG_BATTERY_TERMINATE_CURRENT_A:
            if ((value < 0.01F) || (value > 5.0F)) return false;
            c->battery_terminate_current_a = value;
            break;
        case DAB_ENERGY_CONFIG_BATTERY_PRECHARGE_V:
            if ((value < 20.0F) || (value >= c->battery_cv_v)) return false;
            /* A target override tunes the selected chemistry; it must not erase it. */
            c->battery_precharge_v = value;
            break;
        case DAB_ENERGY_CONFIG_BATTERY_CV_V:
            if ((value < 36.0F) || (value > 48.0F) || (value <= c->battery_precharge_v)) return false;
            c->battery_cv_v = value;
            break;
        case DAB_ENERGY_CONFIG_BATTERY_FLOAT_V:
            if ((value < 0.0F) || (value > 48.0F)) return false;
            c->battery_float_v = value;
            break;
        case DAB_ENERGY_CONFIG_BATTERY_ABSORB_S:
            if ((value < 1.0F) || (value > 14400.0F)) return false;
            c->battery_absorb_s = (uint32_t) value;
            break;
        case DAB_ENERGY_CONFIG_BATTERY_TIMEOUT_S:
            if ((value < 60.0F) || (value > 86400.0F)) return false;
            c->battery_timeout_s = (uint32_t) value;
            break;
        case DAB_ENERGY_CONFIG_BATTERY_TEMPERATURE_C:
            if ((value < -40.0F) || (value > 100.0F)) return false;
            c->battery_temperature_c = value;
            break;
        case DAB_ENERGY_CONFIG_BATTERY_TEMPERATURE_VALID:
            c->battery_temperature_valid = (value >= 0.5F);
            break;
        case DAB_ENERGY_CONFIG_BATTERY_START:
            c->battery_start = (value >= 0.5F);
            energy_stage_enter(p_state, c->battery_start ? DAB_CHARGE_STAGE_IDLE :
                                                      DAB_CHARGE_STAGE_OFF, now_ms);
            break;
        case DAB_ENERGY_CONFIG_AI_MODE:
            if ((value < 0.0F) || (value > (float) DAB_AI_ASSIST_BOUNDED)) return false;
            c->ai_mode = (dab_ai_assist_mode_t) ((uint32_t) value);
            break;
        case DAB_ENERGY_CONFIG_AI_HINT_CURRENT_A:
            if ((value < 0.0F) || (value > 18.0F)) return false;
            c->ai_hint_current_a = value;
            p_state->ai_current_hint_received_ms = now_ms;
            p_state->ai_current_hint_valid = true;
            break;
        case DAB_ENERGY_CONFIG_AI_HINT_MODULATION:
            if ((value < 0.0F) || (value > 2.0F)) return false;
            c->ai_hint_modulation = (uint8_t) value;
            p_state->ai_modulation_hint_received_ms = now_ms;
            p_state->ai_modulation_hint_valid = true;
            break;
        case DAB_ENERGY_CONFIG_AI_CONFIDENCE:
            if ((value < 0.0F) || (value > 1.0F)) return false;
            c->ai_confidence = value;
            break;
        case DAB_ENERGY_CONFIG_AI_MODULATION_CONFIDENCE:
            if ((value < 0.0F) || (value > 1.0F)) return false;
            c->ai_modulation_confidence = value;
            break;
        case DAB_ENERGY_CONFIG_AI_TTL_MS:
            if ((value < 100.0F) || (value > 10000.0F)) return false;
            c->ai_ttl_ms = (uint32_t) value;
            break;
        case DAB_ENERGY_CONFIG_AI_MODULATION_TTL_MS:
            if ((value < 100.0F) || (value > 10000.0F)) return false;
            c->ai_modulation_ttl_ms = (uint32_t) value;
            break;
        case DAB_ENERGY_CONFIG_CHARGE_KP_A_PER_V:
            if ((value < 0.0F) || (value > 20.0F)) return false;
            c->charge_kp_a_per_v = value;
            break;
        case DAB_ENERGY_CONFIG_CHARGE_KI_A_PER_VS:
            if ((value < 0.0F) || (value > 10.0F)) return false;
            c->charge_ki_a_per_vs = value;
            break;
        default:
            return false;
    }
    return true;
}

static float energy_mppt_update(dab_energy_state_t * p_state,
                                dab_energy_measurement_t const * m,
                                float current_ceiling_a)
{
    dab_energy_config_t const * const c = &p_state->config;
    float const pv_power = energy_max(0.0F, m->vin_v * m->iin_a);
    float const current_ceiling = energy_clamp(current_ceiling_a, 0.0F, 18.0F);

    /* P&O/INC no longer generates an internal power command. The source's
     * operator-set CV/CC boundary defines available power; firmware perturbs
     * only the output-current demand and observes measured Vin*Iin. */
    if (DAB_MPPT_MODE_OFF == c->mppt_mode)
    {
        p_state->output.mppt_voltage_target_v = m->vin_v;
        p_state->output.mppt_power_target_w = pv_power;
        return current_ceiling;
    }
    if (DAB_MPPT_MODE_FIXED_POWER == c->mppt_mode)
    {
        float const legacy_current = c->mppt_fixed_power_w /
            energy_max(m->vout_v, 1.0F);
        p_state->output.mppt_voltage_target_v = m->vin_v;
        p_state->output.mppt_power_target_w = pv_power;
        return energy_min(legacy_current, current_ceiling);
    }

    if (!p_state->mppt_initialized)
    {
        p_state->po_direction = 1;
        p_state->previous_pv_v = m->vin_v;
        p_state->previous_pv_i = m->iin_a;
        p_state->previous_pv_power_w = pv_power;
        p_state->output.mppt_voltage_target_v = energy_clamp(
            m->vin_v, c->mppt_min_v, c->mppt_max_v);
        p_state->mppt_current_command_a = energy_clamp(
            energy_max(m->iout_a, 0.10F), 0.0F, current_ceiling);
        p_state->mppt_best_current_command_a =
            p_state->mppt_current_command_a;
        p_state->mppt_best_power_w = pv_power;
        p_state->mppt_best_voltage_v = m->vin_v;
        p_state->mppt_hold_anchor_vin_v = m->vin_v;
        p_state->mppt_hold_started_ms = 0U;
        p_state->mppt_reacquire_since_ms = 0U;
        p_state->mppt_drop_confirm_count = 0U;
        p_state->mppt_peak_hold = false;
        p_state->mppt_knee_recovered = false;
        p_state->last_mppt_ms = m->now_ms;
        p_state->mppt_initialized = true;
    }

    if (m->vin_v < c->mppt_min_v)
    {
        /* CPU1 runs this algorithm every 100 ms, so 0.05 A per update backs
         * the request down at 0.5 A/s. This is source recovery, not a guessed
         * power ceiling. */
        p_state->mppt_current_command_a = energy_max(
            0.0F, p_state->mppt_current_command_a -
                  ENERGY_MPPT_UNDERVOLTAGE_STEP_A);
        p_state->mppt_current_command_a = energy_min(
            p_state->mppt_current_command_a, current_ceiling);
        p_state->mppt_best_current_command_a =
            p_state->mppt_current_command_a;
        p_state->mppt_best_power_w = pv_power;
        p_state->mppt_best_voltage_v = m->vin_v;
        p_state->mppt_peak_hold = false;
        p_state->mppt_knee_recovered = false;
        p_state->mppt_reacquire_since_ms = 0U;
        p_state->mppt_drop_confirm_count = 0U;
        p_state->po_direction = -1;
        p_state->previous_pv_v = m->vin_v;
        p_state->previous_pv_i = m->iin_a;
        p_state->previous_pv_power_w = pv_power;
        p_state->last_mppt_ms = m->now_ms;
        p_state->output.mppt_voltage_target_v = c->mppt_min_v;
        p_state->output.mppt_power_target_w = pv_power;
        return p_state->mppt_current_command_a;
    }

    if (p_state->mppt_peak_hold)
    {
        /* Regulate the measured knee voltage after P&O finds it. The request
         * may initially be ahead of the source-limited physical current, so
         * Vin below Vknee backs off at 0.5 A/s. If the backoff goes too far,
         * or the operator makes more source power available, Vin rises and
         * the same bounded loop increases current again. This holds MPP
         * without a watt command or a restart cycle. */
        float const knee_error_v =
            m->vin_v - p_state->mppt_hold_anchor_vin_v;
        bool const actuator_can_increase =
            p_state->mppt_current_command_a <=
            (energy_max(m->iout_a, 0.0F) +
             ENERGY_MPPT_ACTUATOR_SETTLE_A);
        /* After a knee reversal CPU0 must slew the active reference down at
         * no more than 0.5 A/s.  If CPU1 also moves its target down at the
         * same rate, the initial target/actuator gap never closes (r131 held
         * about 0.35 A of lag and Vin reached 17.85 V).  Hold each target step
         * until measured Iout has followed it, while CPU0 continues the
         * already-bounded physical current ramp. */
        bool const actuator_can_decrease =
            energy_max(m->iout_a, 0.0F) <=
            (p_state->mppt_current_command_a +
             ENERGY_MPPT_ACTUATOR_RELEASE_A);
        /* A modulation trial can change converter gain enough that Vin
         * returns inside the narrow knee deadband before the previously
         * measured MPP power is restored.  r131 then held 160 W even though
         * the same run had already measured 225 W.  Recover toward that live
         * measured best at the normal 0.1 A/s trim while Vin is not below the
         * safe knee band.  Keep the replay window close to the live best: the
         * old 90% threshold declared r135 recovered at 189 W after measuring
         * about 211 W in this energy loop, even though the phase loop had
         * proven 225.68 W in the same run.  This is not a fixed-watt command:
         * any Vin sag immediately wins and drives the existing bounded current
         * backoff. */
        bool const proven_power_not_replayed =
            (p_state->mppt_best_power_w >= ENERGY_MPPT_KNEE_MIN_POWER_W) &&
            (pv_power < (ENERGY_MPPT_REPLAY_POWER_RATIO *
                         p_state->mppt_best_power_w));
        if (!p_state->mppt_knee_recovered)
        {
            if (knee_error_v <
                -ENERGY_MPPT_HOLD_RECOVERY_HYSTERESIS_V)
            {
                if (actuator_can_decrease)
                {
                    p_state->mppt_current_command_a -=
                        ENERGY_MPPT_UNDERVOLTAGE_STEP_A;
                }
            }
            else
            {
                /* Once Vin first returns to Vknee, switch permanently to the
                 * slower damped hold loop. This prevents the full-scale
                 * 18..21 V sawtooth observed with symmetric 0.5 A/s trim. */
                p_state->mppt_knee_recovered = true;
            }
        }
        else if ((knee_error_v < -ENERGY_MPPT_HOLD_SEVERE_LOW_V) &&
                 actuator_can_decrease)
        {
            p_state->mppt_current_command_a -=
                ENERGY_MPPT_UNDERVOLTAGE_STEP_A;
        }
        else if ((knee_error_v <
                  -ENERGY_MPPT_HOLD_RECOVERY_HYSTERESIS_V) &&
                 actuator_can_decrease)
        {
            p_state->mppt_current_command_a -=
                ENERGY_MPPT_HOLD_TRIM_STEP_A;
        }
        else if (((knee_error_v >
                   ENERGY_MPPT_HOLD_RECOVERY_HYSTERESIS_V) ||
                  (proven_power_not_replayed &&
                   (knee_error_v >=
                    -ENERGY_MPPT_HOLD_RECOVERY_HYSTERESIS_V))) &&
                 actuator_can_increase)
        {
            p_state->mppt_current_command_a +=
                ENERGY_MPPT_HOLD_TRIM_STEP_A;
        }
        p_state->mppt_current_command_a = energy_clamp(
            p_state->mppt_current_command_a, 0.0F, current_ceiling);
        p_state->mppt_best_current_command_a =
            p_state->mppt_current_command_a;
        p_state->mppt_reacquire_since_ms = 0U;
        p_state->output.mppt_voltage_target_v =
            p_state->mppt_best_voltage_v;
        p_state->output.mppt_power_target_w = pv_power;
        return p_state->mppt_current_command_a;
    }

    if ((uint32_t) (m->now_ms - p_state->last_mppt_ms) >= c->mppt_interval_ms)
    {
        float const dv = m->vin_v - p_state->previous_pv_v;
        float const di = m->iin_a - p_state->previous_pv_i;
        float const dp = pv_power - p_state->previous_pv_power_w;
        bool apply_current_step = true;

        if (DAB_MPPT_MODE_PERTURB_OBSERVE == c->mppt_mode)
        {
            /* A bench supply entering CC can keep Pin rising for one sample
             * while Vin has already begun its steep fall. Waiting for four
             * below-best samples allowed r129 to move from 20.9 V to 17.8 V
             * before recovery began. Detect that local knee bend from the
             * measured trajectory before teaching the transient peak.  The
             * reserve is proportional at low current and capped at three
             * ordinary P&O steps.  Only accept the bend after the output
             * current actuator follows the requested current; otherwise a
             * natural-transfer/modulation transition can look like a source
             * knee even though the requested operating point has not been
             * reached.  CPU0 still applies recovery through the existing
             * 0.5 A/s current-reference slew. */
            float const knee_bend_v =
                (p_state->mppt_best_power_w >=
                 ENERGY_MPPT_HIGH_POWER_THRESHOLD_W) ?
                ENERGY_MPPT_HIGH_POWER_KNEE_BEND_V :
                ENERGY_MPPT_KNEE_BEND_V;
            bool const source_knee_bend =
                (p_state->mppt_best_power_w >=
                 ENERGY_MPPT_KNEE_MIN_POWER_W) &&
                (energy_abs(p_state->mppt_current_command_a -
                            energy_max(m->iout_a, 0.0F)) <=
                 ENERGY_MPPT_ACTUATOR_SETTLE_A) &&
                (m->vin_v <=
                 (p_state->previous_pv_v - knee_bend_v)) &&
                (m->iin_a >= (p_state->previous_pv_i - 0.05F)) &&
                (pv_power >= (0.90F * p_state->mppt_best_power_w)) &&
                (p_state->mppt_current_command_a >=
                 p_state->mppt_best_current_command_a);
            if (source_knee_bend)
            {
                float const reserve_a = energy_clamp(
                    0.10F * p_state->mppt_current_command_a,
                    ENERGY_MPPT_KNEE_RESERVE_MIN_A,
                    ENERGY_MPPT_KNEE_RESERVE_MAX_A);
                p_state->po_direction = -1;
                p_state->mppt_current_command_a = energy_max(
                    0.0F, p_state->mppt_current_command_a - reserve_a);
                p_state->mppt_best_current_command_a =
                    p_state->mppt_current_command_a;
                p_state->mppt_hold_anchor_vin_v =
                    p_state->mppt_best_voltage_v;
                p_state->mppt_hold_started_ms = m->now_ms;
                p_state->mppt_reacquire_since_ms = 0U;
                p_state->mppt_drop_confirm_count = 0U;
                p_state->mppt_peak_hold = true;
                p_state->mppt_knee_recovered = false;
                p_state->output.mppt_voltage_target_v =
                    p_state->mppt_best_voltage_v;
                p_state->previous_pv_v = m->vin_v;
                p_state->previous_pv_i = m->iin_a;
                p_state->previous_pv_power_w = pv_power;
                p_state->last_mppt_ms = m->now_ms;
                p_state->output.mppt_power_target_w = pv_power;
                return p_state->mppt_current_command_a;
            }
            if (pv_power > (p_state->mppt_best_power_w +
                            ENERGY_MPPT_BEST_IMPROVEMENT_W))
            {
                p_state->mppt_best_power_w = pv_power;
                p_state->mppt_best_current_command_a =
                    p_state->mppt_current_command_a;
                p_state->mppt_best_voltage_v = m->vin_v;
                p_state->mppt_drop_confirm_count = 0U;
            }
            float const drop_margin_w = energy_max(
                ENERGY_MPPT_DP_REVERSE_THRESHOLD_W,
                p_state->mppt_best_power_w *
                    ENERGY_MPPT_DROP_MARGIN_RATIO);
            bool const probed_beyond_best =
                p_state->mppt_current_command_a >=
                (p_state->mppt_best_current_command_a +
                 ENERGY_MPPT_DROP_CURRENT_MARGIN_A);
            bool const confirmed_below_best =
                pv_power <= (p_state->mppt_best_power_w - drop_margin_w);
            if (probed_beyond_best && confirmed_below_best)
            {
                if (p_state->mppt_drop_confirm_count < UINT8_MAX)
                {
                    p_state->mppt_drop_confirm_count++;
                }
            }
            else
            {
                p_state->mppt_drop_confirm_count = 0U;
            }
            if (p_state->mppt_drop_confirm_count >=
                ENERGY_MPPT_DROP_CONFIRM_SAMPLES)
            {
                /* Four consecutive probes beyond the best applied current
                 * must remain below the best measured power before the source
                 * knee is accepted. This rejects startup and modulation
                 * transients without making the physical ramp faster. */
                p_state->po_direction = -1;
                p_state->mppt_current_command_a =
                    p_state->mppt_best_current_command_a;
                p_state->mppt_hold_anchor_vin_v =
                    p_state->mppt_best_voltage_v;
                p_state->mppt_hold_started_ms = m->now_ms;
                p_state->mppt_reacquire_since_ms = 0U;
                p_state->mppt_drop_confirm_count = 0U;
                p_state->mppt_peak_hold = true;
                p_state->mppt_knee_recovered = false;
                p_state->output.mppt_voltage_target_v =
                    p_state->mppt_best_voltage_v;
                p_state->previous_pv_v = m->vin_v;
                p_state->previous_pv_i = m->iin_a;
                p_state->previous_pv_power_w = pv_power;
                p_state->last_mppt_ms = m->now_ms;
                p_state->output.mppt_power_target_w = pv_power;
                return p_state->mppt_current_command_a;
            }
            p_state->po_direction = 1;
        }
        else if (energy_abs(dv) > ENERGY_MPPT_DV_DEADBAND_V)
        {
            float const slope = (di / dv) +
                (m->iin_a / energy_max(m->vin_v, 1.0F));
            if (slope > 0.01F)
            {
                p_state->po_direction = -1;
            }
            else if (slope < -0.01F)
            {
                p_state->po_direction = 1;
            }
            else
            {
                apply_current_step = false;
            }
        }
        else if (energy_abs(dp) <= ENERGY_MPPT_DP_REVERSE_THRESHOLD_W)
        {
            p_state->po_direction = 1;
        }
        else if (dp < -ENERGY_MPPT_DP_REVERSE_THRESHOLD_W)
        {
            p_state->po_direction = (int8_t) -p_state->po_direction;
        }

        /* Do not queue current faster than the converter can physically
         * produce it.  r128b reached the real source knee near 5.2 A output,
         * but P&O kept raising the request to 8.8 A while the 480 ns actuator
         * was saturated.  That hidden backlog held maximum phase after the
         * source knee had already been crossed and pulled a nominal 22 V
         * source down to about 18 V.  A negative step is always allowed;
         * positive exploration waits until measured Iout follows the last
         * request.  This is a local lower-controller condition, not a power
         * command or an upper-computer dependency. */
        bool const actuator_can_increase =
            p_state->mppt_current_command_a <=
            (energy_max(m->iout_a, 0.0F) +
             ENERGY_MPPT_ACTUATOR_SETTLE_A);
        if ((p_state->po_direction > 0) && !actuator_can_increase)
        {
            apply_current_step = false;
        }
        if (apply_current_step)
        {
            float const step_a =
                ((p_state->po_direction > 0) &&
                 (p_state->mppt_best_power_w >=
                  ENERGY_MPPT_HIGH_POWER_THRESHOLD_W)) ?
                ENERGY_MPPT_HIGH_POWER_STEP_A :
                ENERGY_MPPT_CURRENT_STEP_A;
            p_state->mppt_current_command_a +=
                (float) p_state->po_direction * step_a;
        }
        p_state->mppt_current_command_a = energy_clamp(
            p_state->mppt_current_command_a, 0.0F, current_ceiling);
        p_state->output.mppt_voltage_target_v = energy_clamp(
            m->vin_v - ((float) p_state->po_direction * c->mppt_step_v),
            c->mppt_min_v, c->mppt_max_v);
        p_state->previous_pv_v = m->vin_v;
        p_state->previous_pv_i = m->iin_a;
        p_state->previous_pv_power_w = pv_power;
        p_state->last_mppt_ms = m->now_ms;
    }

    /* The protocol field is retained for compatibility, but it reports the
     * measured input power. It never commands converter power. */
    p_state->output.mppt_power_target_w = pv_power;
    return p_state->mppt_current_command_a;
}

static float energy_battery_current_ceiling(dab_energy_config_t const * c,
                                            float battery_voltage_v)
{
    float const power_limited_current_a = ENERGY_SYSTEM_POWER_LIMIT_W /
        energy_max(battery_voltage_v, 1.0F);
    return energy_min(c->battery_max_current_a, power_limited_current_a);
}

static float energy_voltage_to_current(dab_energy_state_t * p_state,
                                       float target_v,
                                       float feedback_v,
                                       bool allow_positive_integral,
                                       float current_ceiling_a)
{
    float const dt_s = 0.1F;
    float const error_v = target_v - feedback_v;
    float const integral_delta_a = error_v *
        p_state->config.charge_ki_a_per_vs * dt_s;
    if (allow_positive_integral || (integral_delta_a <= 0.0F))
    {
        p_state->charge_integral_a += integral_delta_a;
    }
    p_state->charge_integral_a = energy_clamp(p_state->charge_integral_a,
                                              0.0F,
                                              current_ceiling_a);
    return energy_clamp((p_state->config.charge_kp_a_per_v * error_v) +
                        p_state->charge_integral_a,
                        0.0F, current_ceiling_a);
}

static float energy_charger_update(dab_energy_state_t * p_state,
                                   dab_energy_measurement_t const * m)
{
    dab_energy_config_t const * const c = &p_state->config;
    dab_charge_stage_t stage = p_state->output.charge_stage;
    if (!c->battery_start)
    {
        energy_stage_enter(p_state, DAB_CHARGE_STAGE_OFF, m->now_ms);
        p_state->output.charge_voltage_target_v = 0.0F;
        return 0.0F;
    }
    bool const lead = (DAB_BATTERY_PROFILE_LEAD_ACID_18S_36V ==
                       c->battery_profile);
    float const minimum_charge_temperature = lead ? -15.0F : 0.0F;
    float const maximum_charge_temperature = lead ? 40.0F : 45.0F;
    if (c->battery_temperature_valid &&
        ((c->battery_temperature_c < minimum_charge_temperature) ||
         (c->battery_temperature_c > maximum_charge_temperature)))
    {
        energy_stage_enter(p_state, DAB_CHARGE_STAGE_FAULT, m->now_ms);
        return 0.0F;
    }
    if ((DAB_CHARGE_STAGE_OFF == stage) || (DAB_CHARGE_STAGE_IDLE == stage))
    {
        if (lead && (m->vout_v < c->battery_precharge_v))
        {
            energy_stage_enter(p_state, DAB_CHARGE_STAGE_FAULT, m->now_ms);
            return 0.0F;
        }
        /* Always enter the two supported lithium chemistries through the
         * conservative PRECHARGE actuator.  The ordinary sustained threshold
         * below promotes a genuinely high battery terminal to CC after the
         * connection transient has settled. */
        stage = lead ? DAB_CHARGE_STAGE_CONSTANT_CURRENT :
                       DAB_CHARGE_STAGE_PRECHARGE;
        energy_stage_enter(p_state, stage, m->now_ms);
    }
    uint32_t const elapsed_s = (uint32_t) (m->now_ms - p_state->stage_started_ms) / 1000U;
    if ((elapsed_s > c->battery_timeout_s) &&
        (DAB_CHARGE_STAGE_FLOAT != stage) && (DAB_CHARGE_STAGE_DONE != stage))
    {
        energy_stage_enter(p_state, DAB_CHARGE_STAGE_FAULT, m->now_ms);
        return 0.0F;
    }
    float const current_ceiling_a = energy_battery_current_ceiling(
        c, m->vout_v);
    switch (stage)
    {
        case DAB_CHARGE_STAGE_PRECHARGE:
        {
            p_state->output.charge_voltage_target_v = c->battery_precharge_v;
            /* The output-path connection can hold an open/CV terminal above
             * Vpre for roughly one second even though the latest settled
             * battery-equivalent voltage belongs in PRECHARGE.  Keep the
             * conservative actuator for a short deterministic settling
             * interval, while still updating the voltage debounce on every
             * sample.  A genuinely high battery terminal therefore promotes
             * at 2.5 s; a startup pulse that falls back below Vpre clears the
             * debounce and never hands the bridge to a CC bundle. */
            bool const above_precharge_sustained = energy_condition_sustained(
                m->vout_v >= c->battery_precharge_v,
                m->now_ms,
                ENERGY_PRECHARGE_ENTRY_DEBOUNCE_MS,
                &p_state->above_precharge_since_ms);
            bool const startup_settled =
                ((uint32_t) (m->now_ms - p_state->stage_started_ms) >=
                 ENERGY_PRECHARGE_STARTUP_SETTLE_MS);
            if (startup_settled && above_precharge_sustained)
            {
                energy_stage_enter(p_state, DAB_CHARGE_STAGE_CONSTANT_CURRENT,
                                   m->now_ms);
            }
            return c->battery_precharge_current_a;
        }

        case DAB_CHARGE_STAGE_CONSTANT_CURRENT:
        {
            /* A CV load or open terminal can briefly report a high voltage
             * while the output path is being connected.  If the real battery
             * terminal then settles below the chemistry's precharge boundary,
             * return to PRECHARGE before a high-voltage CC bundle can take
             * ownership.  The short debounce rejects a single ADC sample; the
             * hysteresis prevents chatter at the legitimate CC boundary. */
            bool const precharge_recovery_voltage =
                !lead &&
                (m->vout_v <= (c->battery_precharge_v -
                                ENERGY_PRECHARGE_RECOVERY_HYSTERESIS_V));
            if (energy_condition_sustained(
                    precharge_recovery_voltage,
                    m->now_ms,
                    ENERGY_PRECHARGE_RECOVERY_DEBOUNCE_MS,
                    &p_state->below_precharge_since_ms))
            {
                energy_stage_enter(p_state, DAB_CHARGE_STAGE_PRECHARGE,
                                   m->now_ms);
                p_state->output.charge_voltage_target_v =
                    c->battery_precharge_v;
                return c->battery_precharge_current_a;
            }
            float cv_target = c->battery_cv_v;
            if (lead && c->battery_temperature_valid)
            {
                cv_target -= 0.090F * (c->battery_temperature_c - 25.0F);
                cv_target = energy_clamp(cv_target, 36.0F, 48.0F);
            }
            p_state->output.charge_voltage_target_v = cv_target;
            float const cv_feedback_v =
                m->vout_v + ENERGY_CV_SENSE_COMPENSATION_V;
            bool const cv_entry_voltage_valid =
                (cv_feedback_v >= (cv_target - 0.10F)) &&
                (m->vout_v <= (cv_target + ENERGY_CV_ENTRY_OVERSHOOT_V));
            if (energy_condition_sustained(cv_entry_voltage_valid,
                                           m->now_ms,
                                           ENERGY_CV_ENTRY_DEBOUNCE_MS,
                                           &p_state->cv_entry_since_ms))
            {
                energy_stage_enter(p_state, DAB_CHARGE_STAGE_CONSTANT_VOLTAGE,
                                   m->now_ms);
                p_state->charge_integral_a = energy_clamp(m->iout_a, 0.0F,
                                                          current_ceiling_a);
            }
            return current_ceiling_a;
        }

        case DAB_CHARGE_STAGE_CONSTANT_VOLTAGE:
        {
            float cv_target = c->battery_cv_v;
            if (lead && c->battery_temperature_valid)
            {
                cv_target -= 0.090F * (c->battery_temperature_c - 25.0F);
                cv_target = energy_clamp(cv_target, 36.0F, 48.0F);
            }
            p_state->output.charge_voltage_target_v = cv_target;
            float const cv_feedback_v =
                m->vout_v + ENERGY_CV_SENSE_COMPENSATION_V;
            bool const precharge_recovery_voltage =
                !lead &&
                (m->vout_v <= (c->battery_precharge_v -
                                ENERGY_PRECHARGE_RECOVERY_HYSTERESIS_V));
            if (energy_condition_sustained(
                    precharge_recovery_voltage,
                    m->now_ms,
                    ENERGY_PRECHARGE_RECOVERY_DEBOUNCE_MS,
                    &p_state->below_precharge_since_ms))
            {
                energy_stage_enter(p_state, DAB_CHARGE_STAGE_PRECHARGE,
                                   m->now_ms);
                p_state->output.charge_voltage_target_v =
                    c->battery_precharge_v;
                return c->battery_precharge_current_a;
            }
            bool const cc_recovery_voltage =
                cv_feedback_v <=
                (cv_target - ENERGY_CV_RECOVERY_HYSTERESIS_V);
            if (energy_condition_sustained(
                    cc_recovery_voltage,
                    m->now_ms,
                    ENERGY_CV_RECOVERY_DEBOUNCE_MS,
                    &p_state->below_cv_since_ms))
            {
                energy_stage_enter(p_state,
                                   DAB_CHARGE_STAGE_CONSTANT_CURRENT,
                                   m->now_ms);
                p_state->output.charge_voltage_target_v = cv_target;
                return current_ceiling_a;
            }
            bool const termination_voltage_valid =
                (cv_feedback_v >=
                 (cv_target - ENERGY_CV_TERMINATION_UNDERVOLTAGE_V)) &&
                (m->vout_v <= (cv_target + ENERGY_CV_ENTRY_OVERSHOOT_V));
            float const termination_margin_a = energy_min(
                energy_max(ENERGY_CV_TERMINATION_MARGIN_MIN_A,
                           ENERGY_CV_TERMINATION_MARGIN_RATIO *
                           c->battery_terminate_current_a),
                0.5F * c->battery_terminate_current_a);
            if ((!termination_voltage_valid) ||
                (m->iout_a > (c->battery_terminate_current_a +
                              termination_margin_a)))
            {
                p_state->cv_termination_hold_active = false;
            }
            else if (m->iout_a <= c->battery_terminate_current_a)
            {
                p_state->cv_termination_hold_active = true;
            }
            float current = energy_voltage_to_current(
                p_state, cv_target, cv_feedback_v,
                !p_state->cv_termination_hold_active,
                current_ceiling_a);
            if (p_state->cv_termination_hold_active)
            {
                /* A pack already near Vcv and below Iterm is tapering.  Do not
                 * let a small ADC/drop error wind the PI back toward CC while
                 * the 30 s termination dwell is active.  Command below Iterm
                 * so measurement noise has margin; a real undervoltage or a
                 * rise above the hysteresis band restores normal integration. */
                current = energy_min(current,
                    c->battery_terminate_current_a - termination_margin_a);
            }
            if (termination_voltage_valid &&
                (m->iout_a <= c->battery_terminate_current_a))
            {
                if (0U == p_state->below_terminate_since_ms)
                {
                    p_state->below_terminate_since_ms = m->now_ms;
                }
                else if ((uint32_t) (m->now_ms - p_state->below_terminate_since_ms) >= 30000U)
                {
                    energy_stage_enter(p_state, lead ? DAB_CHARGE_STAGE_FLOAT :
                                                       DAB_CHARGE_STAGE_DONE,
                                       m->now_ms);
                }
            }
            else
            {
                p_state->below_terminate_since_ms = 0U;
            }
            if ((DAB_BATTERY_PROFILE_LEAD_ACID_18S_36V == c->battery_profile) &&
                (elapsed_s >= c->battery_absorb_s))
            {
                energy_stage_enter(p_state, DAB_CHARGE_STAGE_FLOAT, m->now_ms);
            }
            return current;
        }

        case DAB_CHARGE_STAGE_FLOAT:
        {
            float float_target = c->battery_float_v;
            if (lead && c->battery_temperature_valid)
            {
                float_target -= 0.054F * (c->battery_temperature_c - 25.0F);
                float_target = energy_clamp(float_target, 36.0F, 48.0F);
            }
            p_state->output.charge_voltage_target_v = float_target;
            return energy_voltage_to_current(
                p_state, float_target,
                m->vout_v + ENERGY_CV_SENSE_COMPENSATION_V, true,
                current_ceiling_a);
        }

        case DAB_CHARGE_STAGE_DONE:
        case DAB_CHARGE_STAGE_FAULT:
        default:
            return 0.0F;
    }
}

void dab_energy_update(dab_energy_state_t * p_state,
                       dab_energy_measurement_t const * m)
{
    if ((NULL == p_state) || (NULL == m))
    {
        return;
    }
    p_state->output.mode = p_state->config.mode;
    p_state->output.mppt_mode = p_state->config.mppt_mode;
    p_state->output.battery_profile = p_state->config.battery_profile;
    p_state->output.mppt_limited = false;
    p_state->output.charger_limited = false;
    p_state->output.ai_applied = false;
    p_state->output.ai_modulation_fresh =
        p_state->ai_modulation_hint_valid &&
        (p_state->config.ai_modulation_ttl_ms > 0U) &&
        ((uint32_t) (m->now_ms - p_state->ai_modulation_hint_received_ms) <=
         p_state->config.ai_modulation_ttl_ms);
    p_state->output.valid = m->valid;
    if (!m->valid)
    {
        p_state->output.requested_current_a = 0.0F;
        return;
    }

    float charge_current = 18.0F;
    if ((DAB_ENERGY_MODE_CHARGER_ONLY == p_state->config.mode) ||
        (DAB_ENERGY_MODE_SOLAR_CHARGER == p_state->config.mode))
    {
        charge_current = energy_charger_update(p_state, m);
    }

    float mppt_current = 18.0F;
    if (DAB_ENERGY_MODE_MPPT_ONLY == p_state->config.mode)
    {
        mppt_current = energy_mppt_update(p_state, m, 18.0F);
    }
    else if (DAB_ENERGY_MODE_SOLAR_CHARGER == p_state->config.mode)
    {
        /* MPPT is useful only while the battery can absorb bulk CC power.
         * PRECHARGE and CV/taper must never be driven upward by a tracker
         * trying to harvest more input power.  Pause/reinitialize the slow
         * tracker outside CC and expose only the charger-required power for
         * telemetry; the final min() below therefore remains charger-owned. */
        if (DAB_CHARGE_STAGE_CONSTANT_CURRENT ==
            p_state->output.charge_stage)
        {
            mppt_current = energy_mppt_update(p_state, m, charge_current);
        }
        else
        {
            p_state->mppt_initialized = false;
            p_state->output.mppt_voltage_target_v = energy_clamp(
                m->vin_v, p_state->config.mppt_min_v,
                p_state->config.mppt_max_v);
            p_state->output.mppt_power_target_w = energy_max(
                0.0F, m->vin_v * m->iin_a);
        }
    }

    float local = 0.0F;
    switch (p_state->config.mode)
    {
        case DAB_ENERGY_MODE_MANUAL_CURRENT:
            local = p_state->config.manual_current_a;
            break;
        case DAB_ENERGY_MODE_MPPT_ONLY:
            local = mppt_current;
            p_state->output.mppt_limited = true;
            break;
        case DAB_ENERGY_MODE_CHARGER_ONLY:
            local = charge_current;
            p_state->output.charger_limited = true;
            break;
        case DAB_ENERGY_MODE_SOLAR_CHARGER:
            local = energy_min(mppt_current, charge_current);
            p_state->output.mppt_limited = (mppt_current <= charge_current);
            p_state->output.charger_limited = (charge_current <= mppt_current);
            break;
        case DAB_ENERGY_MODE_OFF:
        default:
            local = 0.0F;
            break;
    }
    p_state->output.local_requested_current_a = energy_clamp(local, 0.0F, 18.0F);

    float requested = p_state->output.local_requested_current_a;
    bool const ai_fresh = p_state->ai_current_hint_valid &&
                          (p_state->config.ai_ttl_ms > 0U) &&
                          ((uint32_t) (m->now_ms -
                           p_state->ai_current_hint_received_ms) <=
                           p_state->config.ai_ttl_ms);
    if ((DAB_AI_ASSIST_BOUNDED == p_state->config.ai_mode) &&
        (p_state->config.ai_confidence >= 0.70F) && ai_fresh)
    {
        /* AI is advisory: it can reduce stress, never exceed the local ceiling. */
        requested = energy_min(requested, p_state->config.ai_hint_current_a);
        p_state->output.ai_applied = true;
    }
    p_state->output.requested_current_a = energy_clamp(requested, 0.0F, 18.0F);
}
