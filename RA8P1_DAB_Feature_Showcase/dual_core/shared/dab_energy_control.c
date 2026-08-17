#include "dab_energy_control.h"

#include <stddef.h>
#include <string.h>

#define ENERGY_IPC_KEY_SHIFT       (15U)
#define ENERGY_IPC_VALUE_MASK      (0x7FFFUL)
#define ENERGY_IPC_SIGN_BIT        (0x4000L)
#define ENERGY_IPC_SIGN_EXTEND     (0x8000L)
#define ENERGY_PRECHARGE_ENTRY_DEBOUNCE_MS (500U)
#define ENERGY_CV_ENTRY_DEBOUNCE_MS        (1000U)
#define ENERGY_CV_ENTRY_OVERSHOOT_V        (0.50F)

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
        c->battery_max_current_a = energy_min(5.0F, 0.5F * capacity);
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
    p_state->config.mppt_min_v = 15.0F;
    p_state->config.mppt_max_v = 30.0F;
    p_state->config.mppt_max_power_w = 400.0F;
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

static void energy_stage_enter(dab_energy_state_t * p_state,
                               dab_charge_stage_t stage,
                               uint32_t now_ms)
{
    p_state->output.charge_stage = stage;
    p_state->stage_started_ms = now_ms;
    p_state->below_terminate_since_ms = 0U;
    p_state->above_precharge_since_ms = 0U;
    p_state->cv_entry_since_ms = 0U;
    p_state->charge_integral_a = 0.0F;
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
            if ((value < 1.0F) || (value > 400.0F)) return false;
            c->mppt_max_power_w = value;
            break;
        case DAB_ENERGY_CONFIG_MPPT_FIXED_POWER_W:
            if ((value < 0.0F) || (value > 400.0F)) return false;
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
            c->battery_precharge_v = value;
            c->battery_profile = DAB_BATTERY_PROFILE_CUSTOM;
            break;
        case DAB_ENERGY_CONFIG_BATTERY_CV_V:
            if ((value < 36.0F) || (value > 48.0F) || (value <= c->battery_precharge_v)) return false;
            c->battery_cv_v = value;
            c->battery_profile = DAB_BATTERY_PROFILE_CUSTOM;
            break;
        case DAB_ENERGY_CONFIG_BATTERY_FLOAT_V:
            if ((value < 0.0F) || (value > 48.0F)) return false;
            c->battery_float_v = value;
            c->battery_profile = DAB_BATTERY_PROFILE_CUSTOM;
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
                                dab_energy_measurement_t const * m)
{
    dab_energy_config_t const * const c = &p_state->config;
    float const pv_power = energy_max(0.0F, m->vin_v * m->iin_a);
    if (DAB_MPPT_MODE_OFF == c->mppt_mode)
    {
        p_state->output.mppt_voltage_target_v = m->vin_v;
        p_state->output.mppt_power_target_w =
            (DAB_ENERGY_MODE_SOLAR_CHARGER == c->mode) ?
            c->mppt_max_power_w : 0.0F;
        return p_state->output.mppt_power_target_w;
    }
    if (DAB_MPPT_MODE_FIXED_POWER == c->mppt_mode)
    {
        p_state->output.mppt_voltage_target_v = m->vin_v;
        p_state->output.mppt_power_target_w = energy_min(c->mppt_fixed_power_w,
                                                        c->mppt_max_power_w);
        return p_state->output.mppt_power_target_w;
    }
    if (!p_state->mppt_initialized)
    {
        p_state->previous_pv_v = m->vin_v;
        p_state->previous_pv_i = m->iin_a;
        p_state->previous_pv_power_w = pv_power;
        p_state->output.mppt_voltage_target_v = energy_clamp(m->vin_v,
                                                            c->mppt_min_v,
                                                            c->mppt_max_v);
        p_state->mppt_power_command_w = energy_clamp(pv_power, 1.0F,
                                                     c->mppt_max_power_w);
        p_state->last_mppt_ms = m->now_ms;
        p_state->mppt_initialized = true;
    }
    if ((uint32_t) (m->now_ms - p_state->last_mppt_ms) >= c->mppt_interval_ms)
    {
        float const dv = m->vin_v - p_state->previous_pv_v;
        float const di = m->iin_a - p_state->previous_pv_i;
        float const dp = pv_power - p_state->previous_pv_power_w;
        if (DAB_MPPT_MODE_PERTURB_OBSERVE == c->mppt_mode)
        {
            if (dp < -0.25F)
            {
                p_state->po_direction = (int8_t) -p_state->po_direction;
            }
            p_state->output.mppt_voltage_target_v +=
                (float) p_state->po_direction * c->mppt_step_v;
        }
        else
        {
            float slope = 0.0F;
            if (energy_abs(dv) > 0.02F)
            {
                slope = (di / dv) + (m->iin_a / energy_max(m->vin_v, 1.0F));
            }
            else if (energy_abs(di) > 0.01F)
            {
                slope = (di > 0.0F) ? 1.0F : -1.0F;
            }
            if (slope > 0.01F)
            {
                p_state->output.mppt_voltage_target_v += c->mppt_step_v;
            }
            else if (slope < -0.01F)
            {
                p_state->output.mppt_voltage_target_v -= c->mppt_step_v;
            }
        }
        p_state->output.mppt_voltage_target_v = energy_clamp(
            p_state->output.mppt_voltage_target_v, c->mppt_min_v, c->mppt_max_v);
        p_state->previous_pv_v = m->vin_v;
        p_state->previous_pv_i = m->iin_a;
        p_state->previous_pv_power_w = pv_power;
        p_state->last_mppt_ms = m->now_ms;
    }

    /* The MPPT loop supplies a slow power ceiling; CPU0 owns the fast current PI. */
    float const input_voltage_error = m->vin_v - p_state->output.mppt_voltage_target_v;
    p_state->mppt_power_command_w += 0.05F * input_voltage_error;
    p_state->mppt_power_command_w = energy_clamp(p_state->mppt_power_command_w,
                                                0.0F, c->mppt_max_power_w);
    p_state->output.mppt_power_target_w = p_state->mppt_power_command_w;
    return p_state->mppt_power_command_w;
}

static float energy_voltage_to_current(dab_energy_state_t * p_state,
                                       float target_v,
                                       dab_energy_measurement_t const * m)
{
    float const dt_s = 0.1F;
    float const error_v = target_v - m->vout_v;
    p_state->charge_integral_a += error_v *
        p_state->config.charge_ki_a_per_vs * dt_s;
    p_state->charge_integral_a = energy_clamp(p_state->charge_integral_a,
                                              0.0F,
                                              p_state->config.battery_max_current_a);
    return energy_clamp((p_state->config.charge_kp_a_per_v * error_v) +
                        p_state->charge_integral_a,
                        0.0F, p_state->config.battery_max_current_a);
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
        stage = (m->vout_v < c->battery_precharge_v) ?
                DAB_CHARGE_STAGE_PRECHARGE : DAB_CHARGE_STAGE_CONSTANT_CURRENT;
        energy_stage_enter(p_state, stage, m->now_ms);
    }
    uint32_t const elapsed_s = (uint32_t) (m->now_ms - p_state->stage_started_ms) / 1000U;
    if ((elapsed_s > c->battery_timeout_s) &&
        (DAB_CHARGE_STAGE_FLOAT != stage) && (DAB_CHARGE_STAGE_DONE != stage))
    {
        energy_stage_enter(p_state, DAB_CHARGE_STAGE_FAULT, m->now_ms);
        return 0.0F;
    }
    switch (stage)
    {
        case DAB_CHARGE_STAGE_PRECHARGE:
            p_state->output.charge_voltage_target_v = c->battery_precharge_v;
            if (energy_condition_sustained(
                    m->vout_v >= c->battery_precharge_v,
                    m->now_ms,
                    ENERGY_PRECHARGE_ENTRY_DEBOUNCE_MS,
                    &p_state->above_precharge_since_ms))
            {
                energy_stage_enter(p_state, DAB_CHARGE_STAGE_CONSTANT_CURRENT,
                                   m->now_ms);
            }
            return c->battery_precharge_current_a;

        case DAB_CHARGE_STAGE_CONSTANT_CURRENT:
        {
            float cv_target = c->battery_cv_v;
            if (lead && c->battery_temperature_valid)
            {
                cv_target -= 0.090F * (c->battery_temperature_c - 25.0F);
                cv_target = energy_clamp(cv_target, 36.0F, 48.0F);
            }
            p_state->output.charge_voltage_target_v = cv_target;
            bool const cv_entry_voltage_valid =
                (m->vout_v >= (cv_target - 0.10F)) &&
                (m->vout_v <= (cv_target + ENERGY_CV_ENTRY_OVERSHOOT_V));
            if (energy_condition_sustained(cv_entry_voltage_valid,
                                           m->now_ms,
                                           ENERGY_CV_ENTRY_DEBOUNCE_MS,
                                           &p_state->cv_entry_since_ms))
            {
                energy_stage_enter(p_state, DAB_CHARGE_STAGE_CONSTANT_VOLTAGE,
                                   m->now_ms);
                p_state->charge_integral_a = energy_clamp(m->iout_a, 0.0F,
                                                          c->battery_max_current_a);
            }
            return c->battery_max_current_a;
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
            float const current = energy_voltage_to_current(p_state, cv_target, m);
            if (m->iout_a <= c->battery_terminate_current_a)
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
            return energy_voltage_to_current(p_state, float_target, m);
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

    float mppt_current = 18.0F;
    float charge_current = 18.0F;
    if ((DAB_ENERGY_MODE_MPPT_ONLY == p_state->config.mode) ||
        (DAB_ENERGY_MODE_SOLAR_CHARGER == p_state->config.mode))
    {
        float const available_power = energy_mppt_update(p_state, m);
        mppt_current = available_power / energy_max(m->vout_v, 1.0F);
    }
    if ((DAB_ENERGY_MODE_CHARGER_ONLY == p_state->config.mode) ||
        (DAB_ENERGY_MODE_SOLAR_CHARGER == p_state->config.mode))
    {
        charge_current = energy_charger_update(p_state, m);
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
