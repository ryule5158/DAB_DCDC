#ifndef DAB_ENERGY_CONTROL_H
#define DAB_ENERGY_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#define DAB_ENERGY_ALGORITHM_VERSION "2026-08-15-v39"

typedef enum e_dab_energy_mode
{
    DAB_ENERGY_MODE_OFF = 0,
    DAB_ENERGY_MODE_MANUAL_CURRENT,
    DAB_ENERGY_MODE_MPPT_ONLY,
    DAB_ENERGY_MODE_CHARGER_ONLY,
    DAB_ENERGY_MODE_SOLAR_CHARGER
} dab_energy_mode_t;

typedef enum e_dab_mppt_mode
{
    DAB_MPPT_MODE_OFF = 0,
    DAB_MPPT_MODE_PERTURB_OBSERVE,
    DAB_MPPT_MODE_INCREMENTAL_CONDUCTANCE,
    DAB_MPPT_MODE_FIXED_POWER
} dab_mppt_mode_t;

typedef enum e_dab_battery_profile
{
    DAB_BATTERY_PROFILE_CUSTOM = 0,
    DAB_BATTERY_PROFILE_LI_ION_10S_42V,
    DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V,
    DAB_BATTERY_PROFILE_LEAD_ACID_18S_36V
} dab_battery_profile_t;

typedef enum e_dab_charge_stage
{
    DAB_CHARGE_STAGE_OFF = 0,
    DAB_CHARGE_STAGE_IDLE,
    DAB_CHARGE_STAGE_PRECHARGE,
    DAB_CHARGE_STAGE_CONSTANT_CURRENT,
    DAB_CHARGE_STAGE_CONSTANT_VOLTAGE,
    DAB_CHARGE_STAGE_FLOAT,
    DAB_CHARGE_STAGE_DONE,
    DAB_CHARGE_STAGE_FAULT
} dab_charge_stage_t;

typedef enum e_dab_ai_assist_mode
{
    DAB_AI_ASSIST_OFF = 0,
    DAB_AI_ASSIST_BOUNDED
} dab_ai_assist_mode_t;

/* Values are transported as key-specific fixed point over the 20-bit IPC FIFO. */
typedef enum e_dab_energy_config_key
{
    DAB_ENERGY_CONFIG_MODE = 0,
    DAB_ENERGY_CONFIG_MANUAL_CURRENT_A,
    DAB_ENERGY_CONFIG_MPPT_MODE,
    DAB_ENERGY_CONFIG_MPPT_STEP_V,
    DAB_ENERGY_CONFIG_MPPT_INTERVAL_MS,
    DAB_ENERGY_CONFIG_MPPT_MIN_V,
    DAB_ENERGY_CONFIG_MPPT_MAX_V,
    DAB_ENERGY_CONFIG_MPPT_MAX_POWER_W,
    DAB_ENERGY_CONFIG_MPPT_FIXED_POWER_W,
    DAB_ENERGY_CONFIG_BATTERY_PROFILE,
    DAB_ENERGY_CONFIG_BATTERY_CAPACITY_AH,
    DAB_ENERGY_CONFIG_BATTERY_MAX_CURRENT_A,
    DAB_ENERGY_CONFIG_BATTERY_PRECHARGE_CURRENT_A,
    DAB_ENERGY_CONFIG_BATTERY_TERMINATE_CURRENT_A,
    DAB_ENERGY_CONFIG_BATTERY_PRECHARGE_V,
    DAB_ENERGY_CONFIG_BATTERY_CV_V,
    DAB_ENERGY_CONFIG_BATTERY_FLOAT_V,
    DAB_ENERGY_CONFIG_BATTERY_ABSORB_S,
    DAB_ENERGY_CONFIG_BATTERY_TIMEOUT_S,
    DAB_ENERGY_CONFIG_BATTERY_TEMPERATURE_C,
    DAB_ENERGY_CONFIG_BATTERY_TEMPERATURE_VALID,
    DAB_ENERGY_CONFIG_BATTERY_START,
    DAB_ENERGY_CONFIG_AI_MODE,
    DAB_ENERGY_CONFIG_AI_HINT_CURRENT_A,
    DAB_ENERGY_CONFIG_AI_HINT_MODULATION,
    DAB_ENERGY_CONFIG_AI_CONFIDENCE,
    DAB_ENERGY_CONFIG_AI_MODULATION_CONFIDENCE,
    DAB_ENERGY_CONFIG_AI_TTL_MS,
    DAB_ENERGY_CONFIG_AI_MODULATION_TTL_MS,
    DAB_ENERGY_CONFIG_CHARGE_KP_A_PER_V,
    DAB_ENERGY_CONFIG_CHARGE_KI_A_PER_VS,
    DAB_ENERGY_CONFIG_COUNT
} dab_energy_config_key_t;

typedef struct st_dab_energy_measurement
{
    float vin_v;
    float iin_a;
    float vout_v;
    float iout_a;
    float temp_pri_c;
    float temp_sec_c;
    uint32_t now_ms;
    bool valid;
} dab_energy_measurement_t;

typedef struct st_dab_energy_config
{
    dab_energy_mode_t mode;
    float manual_current_a;
    dab_mppt_mode_t mppt_mode;
    float mppt_step_v;
    uint32_t mppt_interval_ms;
    float mppt_min_v;
    float mppt_max_v;
    float mppt_max_power_w;
    float mppt_fixed_power_w;
    dab_battery_profile_t battery_profile;
    float battery_capacity_ah;
    float battery_max_current_a;
    float battery_precharge_current_a;
    float battery_terminate_current_a;
    float battery_precharge_v;
    float battery_cv_v;
    float battery_float_v;
    uint32_t battery_absorb_s;
    uint32_t battery_timeout_s;
    float battery_temperature_c;
    bool battery_temperature_valid;
    bool battery_start;
    float charge_kp_a_per_v;
    float charge_ki_a_per_vs;
    dab_ai_assist_mode_t ai_mode;
    float ai_hint_current_a;
    uint8_t ai_hint_modulation;
    float ai_confidence;
    float ai_modulation_confidence;
    uint32_t ai_ttl_ms;
    uint32_t ai_modulation_ttl_ms;
} dab_energy_config_t;

typedef struct st_dab_energy_output
{
    float requested_current_a;
    float local_requested_current_a;
    float mppt_power_target_w;
    float mppt_voltage_target_v;
    float charge_voltage_target_v;
    dab_energy_mode_t mode;
    dab_mppt_mode_t mppt_mode;
    dab_battery_profile_t battery_profile;
    dab_charge_stage_t charge_stage;
    bool mppt_limited;
    bool charger_limited;
    bool ai_applied;
    bool ai_modulation_fresh;
    bool valid;
} dab_energy_output_t;

typedef struct st_dab_energy_state
{
    dab_energy_config_t config;
    dab_energy_output_t output;
    float previous_pv_v;
    float previous_pv_i;
    float previous_pv_power_w;
    float mppt_power_command_w;
    int8_t po_direction;
    uint32_t last_mppt_ms;
    uint32_t ai_current_hint_received_ms;
    uint32_t ai_modulation_hint_received_ms;
    bool ai_current_hint_valid;
    bool ai_modulation_hint_valid;
    uint32_t stage_started_ms;
    uint32_t below_terminate_since_ms;
    float charge_integral_a;
    bool mppt_initialized;
} dab_energy_state_t;

void dab_energy_initialize(dab_energy_state_t * p_state);
bool dab_energy_apply_config(dab_energy_state_t * p_state,
                             dab_energy_config_key_t key,
                             float value,
                             uint32_t now_ms);
void dab_energy_update(dab_energy_state_t * p_state,
                       dab_energy_measurement_t const * p_measurement);
bool dab_energy_config_payload_encode(dab_energy_config_key_t key,
                                      float value,
                                      uint32_t * p_payload);
bool dab_energy_config_payload_decode(uint32_t payload,
                                      dab_energy_config_key_t * p_key,
                                      float * p_value);

#endif /* DAB_ENERGY_CONTROL_H */
