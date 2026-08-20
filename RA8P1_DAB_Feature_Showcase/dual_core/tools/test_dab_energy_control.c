#include "../shared/dab_energy_control.h"

#if defined(DAB_ENERGY_TEST_WASM)
#include <stddef.h>

void * memset(void * destination, int value, size_t count)
{
    unsigned char * bytes = (unsigned char *) destination;
    for (size_t i = 0U; i < count; i++)
    {
        bytes[i] = (unsigned char) value;
    }
    return destination;
}
#else
#include <stdio.h>
#endif

static unsigned int s_failures;

static float test_abs(float value)
{
    return (value < 0.0F) ? -value : value;
}

#if defined(DAB_ENERGY_TEST_WASM)
#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            s_failures++;                                                       \
        }                                                                       \
    } while (0)
#else
#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #condition);                \
            s_failures++;                                                       \
        }                                                                       \
    } while (0)
#endif

static dab_energy_measurement_t valid_measurement(uint32_t now_ms,
                                                   float vin_v,
                                                   float iin_a,
                                                   float vout_v,
                                                   float iout_a)
{
    dab_energy_measurement_t measurement = {0};
    measurement.now_ms = now_ms;
    measurement.vin_v = vin_v;
    measurement.iin_a = iin_a;
    measurement.vout_v = vout_v;
    measurement.iout_a = iout_a;
    measurement.temp_pri_c = 25.0F;
    measurement.temp_sec_c = 25.0F;
    measurement.valid = true;
    return measurement;
}

static void test_payload_codec(void)
{
    uint32_t payload = 0U;
    dab_energy_config_key_t key = DAB_ENERGY_CONFIG_MODE;
    float value = 0.0F;
    CHECK(dab_energy_config_payload_encode(
        DAB_ENERGY_CONFIG_BATTERY_TEMPERATURE_C, -20.5F, &payload));
    CHECK(dab_energy_config_payload_decode(payload, &key, &value));
    CHECK(DAB_ENERGY_CONFIG_BATTERY_TEMPERATURE_C == key);
    CHECK(test_abs(value - (-20.5F)) < 0.051F);

    CHECK(dab_energy_config_payload_encode(
        DAB_ENERGY_CONFIG_AI_MODULATION_TTL_MS, 10000.0F, &payload));
    CHECK(dab_energy_config_payload_decode(payload, &key, &value));
    CHECK(DAB_ENERGY_CONFIG_AI_MODULATION_TTL_MS == key);
    CHECK(test_abs(value - 10000.0F) < 0.1F);
}

static void test_product_mppt_defaults_are_local(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(test_abs(state.config.mppt_min_v - 17.5F) < 0.01F);
    CHECK(test_abs(state.config.mppt_max_v - 26.0F) < 0.01F);
    CHECK(test_abs(state.config.mppt_step_v - 0.10F) < 0.01F);
    CHECK(250U == state.config.mppt_interval_ms);
    CHECK(test_abs(state.config.mppt_max_power_w - 360.0F) < 0.01F);
}

static void test_charge_start_mode_is_lower_controller_owned(void)
{
    CHECK(DAB_ENERGY_MODE_SOLAR_CHARGER == dab_energy_charge_start_mode(
        DAB_BATTERY_PROFILE_LI_ION_10S_42V,
        DAB_MPPT_MODE_PERTURB_OBSERVE));
    CHECK(DAB_ENERGY_MODE_SOLAR_CHARGER == dab_energy_charge_start_mode(
        DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V,
        DAB_MPPT_MODE_INCREMENTAL_CONDUCTANCE));
    CHECK(DAB_ENERGY_MODE_CHARGER_ONLY == dab_energy_charge_start_mode(
        DAB_BATTERY_PROFILE_LI_ION_10S_42V,
        DAB_MPPT_MODE_OFF));
    CHECK(DAB_ENERGY_MODE_CHARGER_ONLY == dab_energy_charge_start_mode(
        DAB_BATTERY_PROFILE_LEAD_ACID_18S_36V,
        DAB_MPPT_MODE_PERTURB_OBSERVE));
}

static void test_ai_hint_isolation(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(test_abs(state.config.battery_cv_v - 43.8F) < 0.01F);
    CHECK(test_abs(state.config.mppt_max_power_w - 360.0F) < 0.01F);
    CHECK(!dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MAX_POWER_W,
                                   360.1F, 1000U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_MANUAL_CURRENT,
                                  1000U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MANUAL_CURRENT_A,
                                  5.0F, 1000U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_AI_MODE,
                                  (float) DAB_AI_ASSIST_BOUNDED, 1000U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_AI_HINT_CURRENT_A,
                                  3.0F, 1000U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_AI_CONFIDENCE,
                                  0.90F, 1000U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_AI_TTL_MS,
                                  500.0F, 1000U));
    dab_energy_measurement_t measurement =
        valid_measurement(1100U, 24.0F, 5.0F, 40.0F, 3.0F);
    dab_energy_update(&state, &measurement);
    CHECK(state.output.ai_applied);
    CHECK(test_abs(state.output.requested_current_a - 3.0F) < 0.01F);

    CHECK(dab_energy_apply_config(&state,
                                  DAB_ENERGY_CONFIG_AI_HINT_MODULATION,
                                  1.0F, 1150U));
    CHECK(dab_energy_apply_config(&state,
                                  DAB_ENERGY_CONFIG_AI_MODULATION_CONFIDENCE,
                                  0.80F, 1150U));
    CHECK(dab_energy_apply_config(&state,
                                  DAB_ENERGY_CONFIG_AI_MODULATION_TTL_MS,
                                  1000.0F, 1150U));
    measurement.now_ms = 1600U;
    dab_energy_update(&state, &measurement);
    CHECK(!state.output.ai_applied);
    CHECK(state.output.ai_modulation_fresh);
    CHECK(test_abs(state.output.requested_current_a - 5.0F) < 0.01F);

    measurement.now_ms = 2200U;
    dab_energy_update(&state, &measurement);
    CHECK(!state.output.ai_modulation_fresh);
}

static void test_battery_target_overrides_preserve_profile(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_PROFILE,
                                  (float) DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V,
                                  0U));
    CHECK(DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V ==
          state.config.battery_profile);
    CHECK(test_abs(state.config.battery_cv_v - 43.8F) < 0.01F);
    CHECK(test_abs(state.config.battery_max_current_a - 10.0F) < 0.01F);
    CHECK(test_abs(state.config.battery_precharge_current_a - 1.0F) < 0.01F);
    CHECK(test_abs(state.config.battery_terminate_current_a - 0.3F) < 0.01F);

    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_PRECHARGE_V,
                                  31.0F, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_CV_V,
                                  43.8F, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_FLOAT_V,
                                  0.0F, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_MAX_CURRENT_A,
                                  1.0F, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_PRECHARGE_CURRENT_A,
                                  1.0F, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_TERMINATE_CURRENT_A,
                                  0.5F, 0U));

    CHECK(DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V ==
          state.config.battery_profile);
    CHECK(test_abs(state.config.battery_cv_v - 43.8F) < 0.01F);
    CHECK(test_abs(state.config.battery_max_current_a - 1.0F) < 0.01F);
}

static void test_lithium_charger(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_PROFILE,
                                  (float) DAB_BATTERY_PROFILE_LI_ION_10S_42V,
                                  0U));
    CHECK(test_abs(state.config.battery_cv_v - 42.0F) < 0.01F);
    CHECK(test_abs(state.config.battery_max_current_a - 10.0F) < 0.01F);
    CHECK(test_abs(state.config.battery_precharge_current_a - 1.0F) < 0.01F);
    CHECK(test_abs(state.config.battery_terminate_current_a - 0.3F) < 0.01F);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_CHARGER_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_START,
                                  1.0F, 0U));
    dab_energy_measurement_t measurement =
        valid_measurement(100U, 24.0F, 1.0F, 29.0F, 0.2F);
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    CHECK(test_abs(state.output.requested_current_a - 1.0F) < 0.01F);

    measurement.now_ms = 200U;
    measurement.vout_v = 31.0F;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    measurement.now_ms = 700U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    measurement.now_ms = 2600U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_CURRENT == state.output.charge_stage);
    measurement.now_ms = 2650U;
    measurement.vout_v = 40.0F;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.output.requested_current_a - 9.0F) < 0.01F);
    measurement.now_ms = 2700U;
    measurement.vout_v = 41.95F;
    measurement.iout_a = 0.2F;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_CURRENT == state.output.charge_stage);
    measurement.now_ms = 3700U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_VOLTAGE == state.output.charge_stage);
    measurement.now_ms = 3710U;
    measurement.vout_v = 42.0F;
    dab_energy_update(&state, &measurement);
    CHECK(state.output.requested_current_a <= state.config.battery_max_current_a);
    measurement.now_ms = 33720U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_DONE == state.output.charge_stage);
}

static void test_startup_overshoot_does_not_enter_cv(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_PROFILE,
                                  (float) DAB_BATTERY_PROFILE_LI_ION_10S_42V,
                                  0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_CHARGER_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_START,
                                  1.0F, 0U));

    dab_energy_measurement_t measurement =
        valid_measurement(100U, 24.0F, 0.0F, 0.0F, 0.0F);
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);

    measurement.vout_v = 49.8F;
    measurement.now_ms = 200U;
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 700U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    measurement.now_ms = 1800U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);

    measurement.vout_v = 36.0F;
    measurement.now_ms = 3000U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_CURRENT == state.output.charge_stage);
}

static void test_startup_spike_recovers_precharge_for_both_lithium_profiles(void)
{
    dab_energy_state_t state;
    dab_energy_measurement_t measurement;

    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_PROFILE,
                                  (float) DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V,
                                  0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_CHARGER_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_START,
                                  1.0F, 0U));
    measurement = valid_measurement(100U, 22.0F, 0.0F, 40.5F, 0.0F);
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    measurement.now_ms = 200U;
    measurement.vout_v = 30.0F;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    measurement.now_ms = 300U;
    measurement.vout_v = 40.5F;
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 900U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    measurement.now_ms = 1000U;
    measurement.vout_v = 30.0F;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    measurement.now_ms = 1110U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    CHECK(test_abs(state.output.requested_current_a -
                   state.config.battery_precharge_current_a) < 0.01F);

    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_PROFILE,
                                  (float) DAB_BATTERY_PROFILE_LI_ION_10S_42V,
                                  0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_CHARGER_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_START,
                                  1.0F, 0U));
    measurement = valid_measurement(100U, 22.0F, 0.0F, 40.0F, 0.0F);
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    measurement.now_ms = 200U;
    measurement.vout_v = 29.0F;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    measurement.now_ms = 300U;
    measurement.vout_v = 40.0F;
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 900U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    measurement.now_ms = 1000U;
    measurement.vout_v = 29.0F;
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 1110U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    CHECK(test_abs(state.output.requested_current_a -
                   state.config.battery_precharge_current_a) < 0.01F);
}

static void test_cv_termination_hold_and_undervoltage_recovery(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_CHARGER_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state,
                                  DAB_ENERGY_CONFIG_BATTERY_MAX_CURRENT_A,
                                  1.0F, 0U));
    CHECK(dab_energy_apply_config(&state,
                                  DAB_ENERGY_CONFIG_BATTERY_TERMINATE_CURRENT_A,
                                  0.5F, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_START,
                                  1.0F, 0U));

    dab_energy_measurement_t measurement =
        valid_measurement(100U, 24.0F, 1.0F, 43.75F, 0.42F);
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    measurement.now_ms = 2600U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_CURRENT == state.output.charge_stage);
    measurement.now_ms = 2700U;
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 3700U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_VOLTAGE == state.output.charge_stage);

    measurement.now_ms = 3800U;
    measurement.iout_a = 0.49F;
    dab_energy_update(&state, &measurement);
    CHECK(state.cv_termination_hold_active);
    CHECK(state.output.requested_current_a <= 0.451F);
    CHECK(0U != state.below_terminate_since_ms);

    measurement.now_ms = 3900U;
    measurement.iout_a = 0.51F;
    dab_energy_update(&state, &measurement);
    CHECK(state.cv_termination_hold_active);
    CHECK(state.output.requested_current_a <= 0.451F);
    CHECK(0U == state.below_terminate_since_ms);

    measurement.iout_a = 0.45F;
    for (uint32_t now_ms = 4000U; now_ms <= 34100U; now_ms += 100U)
    {
        measurement.now_ms = now_ms;
        dab_energy_update(&state, &measurement);
        if (DAB_CHARGE_STAGE_CONSTANT_VOLTAGE == state.output.charge_stage)
        {
            CHECK(state.cv_termination_hold_active);
            CHECK(state.output.requested_current_a <= 0.451F);
        }
    }
    CHECK(DAB_CHARGE_STAGE_DONE == state.output.charge_stage);

    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_CHARGER_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state,
                                  DAB_ENERGY_CONFIG_BATTERY_MAX_CURRENT_A,
                                  1.0F, 0U));
    CHECK(dab_energy_apply_config(&state,
                                  DAB_ENERGY_CONFIG_BATTERY_TERMINATE_CURRENT_A,
                                  0.5F, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_START,
                                  1.0F, 0U));
    measurement = valid_measurement(100U, 18.0F, 1.0F, 43.75F, 0.42F);
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 2600U;
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 2700U;
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 3700U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_VOLTAGE == state.output.charge_stage);

    measurement.now_ms = 3800U;
    dab_energy_update(&state, &measurement);
    CHECK(state.cv_termination_hold_active);
    measurement.now_ms = 3900U;
    measurement.vout_v = 43.0F;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_VOLTAGE == state.output.charge_stage);
    CHECK(!state.cv_termination_hold_active);
    CHECK(0U == state.below_terminate_since_ms);
    CHECK(state.output.requested_current_a >
          state.config.battery_terminate_current_a);
    measurement.now_ms = 4500U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_CURRENT == state.output.charge_stage);
}

static void test_cv_sense_compensation_reproduces_v45_failed_point(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_CHARGER_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state,
                                  DAB_ENERGY_CONFIG_BATTERY_MAX_CURRENT_A,
                                  1.0F, 0U));
    CHECK(dab_energy_apply_config(&state,
                                  DAB_ENERGY_CONFIG_BATTERY_TERMINATE_CURRENT_A,
                                  0.5F, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_START,
                                  1.0F, 0U));

    /* v45 stayed in CC because CPU1 saw 43.6 V after 0.1 V IPC
     * quantization while the CV load measured 43.99 V. */
    dab_energy_measurement_t measurement =
        valid_measurement(100U, 24.0F, 1.0F, 43.6F, 0.90F);
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    measurement.now_ms = 2600U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_CURRENT == state.output.charge_stage);
    measurement.now_ms = 2700U;
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 3700U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_VOLTAGE == state.output.charge_stage);

    /* Model only the slow supervisory loop here: measured current follows the
     * prior request.  The compensated feedback must taper below Iterm and
     * complete the unchanged 30 s dwell without changing the physical point. */
    for (uint32_t now_ms = 3800U; now_ms <= 54900U; now_ms += 100U)
    {
        measurement.now_ms = now_ms;
        measurement.iout_a = state.output.requested_current_a;
        dab_energy_update(&state, &measurement);
    }
    CHECK(DAB_CHARGE_STAGE_DONE == state.output.charge_stage);

    /* The same compensation must not classify a real 0.8 V undervoltage as
     * near-CV; the existing recovery behavior remains available. */
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_CHARGER_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_START,
                                  1.0F, 0U));
    measurement = valid_measurement(100U, 18.0F, 1.0F, 43.0F, 0.90F);
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 2600U;
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 3700U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_CURRENT == state.output.charge_stage);
}

static void test_lead_and_fixed_power(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_PROFILE,
                                  (float) DAB_BATTERY_PROFILE_LEAD_ACID_18S_36V,
                                  0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_CHARGER_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_START,
                                  1.0F, 0U));
    dab_energy_measurement_t measurement =
        valid_measurement(100U, 24.0F, 1.0F, 30.0F, 0.0F);
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_FAULT == state.output.charge_stage);
    CHECK(test_abs(state.output.requested_current_a) < 0.01F);

    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_MPPT_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MODE,
                                  (float) DAB_MPPT_MODE_FIXED_POWER, 0U));
    CHECK(dab_energy_apply_config(&state,
                                  DAB_ENERGY_CONFIG_MPPT_FIXED_POWER_W,
                                  100.0F, 0U));
    measurement = valid_measurement(100U, 24.0F, 4.0F, 40.0F, 2.5F);
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.output.requested_current_a - 2.5F) < 0.01F);
}

static void test_solar_charger_runs_mppt_only_in_cc(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_PROFILE,
                                  (float) DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V,
                                  0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_SOLAR_CHARGER, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MODE,
                                  (float) DAB_MPPT_MODE_PERTURB_OBSERVE, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MAX_POWER_W,
                                  360.0F, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_START,
                                  1.0F, 0U));

    dab_energy_measurement_t measurement =
        valid_measurement(100U, 22.0F, 6.0F, 31.0F, 1.0F);
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    CHECK(!state.mppt_initialized);
    CHECK(test_abs(state.output.requested_current_a - 1.0F) < 0.01F);
    CHECK(state.output.charger_limited);
    CHECK(!state.output.mppt_limited);

    measurement.vout_v = 36.0F;
    measurement.now_ms = 2600U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    CHECK(!state.mppt_initialized);

    measurement.now_ms = 3100U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_CURRENT == state.output.charge_stage);
    CHECK(state.mppt_initialized);

    measurement.now_ms = 3200U;
    dab_energy_update(&state, &measurement);
    CHECK(state.mppt_initialized);
    CHECK(state.output.requested_current_a <=
          state.config.battery_max_current_a);
    CHECK(state.output.requested_current_a > 0.0F);

    measurement.vout_v = 43.75F;
    measurement.iout_a = 0.2F;
    measurement.now_ms = 3300U;
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 4300U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_VOLTAGE == state.output.charge_stage);
    measurement.now_ms = 4400U;
    dab_energy_update(&state, &measurement);
    CHECK(!state.mppt_initialized);
    CHECK(state.output.requested_current_a <=
          state.config.battery_max_current_a);
    CHECK(state.output.charger_limited);
}

static void test_360w_fixed_power_respects_battery_ceiling(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_PROFILE,
                                  (float) DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V,
                                  0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_SOLAR_CHARGER, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MODE,
                                  (float) DAB_MPPT_MODE_FIXED_POWER, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MAX_POWER_W,
                                  360.0F, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_FIXED_POWER_W,
                                  360.0F, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_BATTERY_START,
                                  1.0F, 0U));

    dab_energy_measurement_t measurement =
        valid_measurement(100U, 24.0F, 2.0F, 31.0F, 1.0F);
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    CHECK(test_abs(state.output.requested_current_a - 1.0F) < 0.01F);

    measurement.vout_v = 36.0F;
    measurement.now_ms = 2600U;
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 3100U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_CURRENT == state.output.charge_stage);
    measurement.now_ms = 3200U;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.output.requested_current_a - 10.0F) < 0.01F);
    /* The legacy fixed-power selector still limits current, but MPPT telemetry
     * exposes measured input power rather than an internal power command. */
    CHECK(test_abs(state.output.mppt_power_target_w - 48.0F) < 0.01F);

    measurement.vout_v = 40.0F;
    measurement.now_ms = 3300U;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.output.requested_current_a - 9.0F) < 0.01F);
    CHECK(state.output.charger_limited);
    CHECK(state.output.mppt_limited);
}

static void test_incremental_conductance_noise_and_vin_recovery(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_MPPT_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MODE,
                                  (float) DAB_MPPT_MODE_INCREMENTAL_CONDUCTANCE,
                                  0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MIN_V,
                                  20.0F, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MAX_V,
                                  24.0F, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MAX_POWER_W,
                                  60.0F, 0U));

    dab_energy_measurement_t measurement =
        valid_measurement(0U, 21.0F, 2.0F, 33.0F, 1.0F);
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.output.mppt_power_target_w - 42.0F) < 0.01F);
    CHECK(test_abs(state.output.mppt_voltage_target_v - 21.0F) < 0.01F);

    /* ADC-scale dV/dI noise causes one bounded search step, not an
     * unreachable-voltage integrator wind-down. */
    measurement.now_ms = 250U;
    measurement.vin_v = 21.01F;
    measurement.iin_a = 2.02F;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.output.mppt_power_target_w -
                   (21.01F * 2.02F)) < 0.01F);
    CHECK(test_abs(state.mppt_current_command_a - 1.125F) < 0.01F);
    CHECK(state.output.mppt_voltage_target_v >= 20.0F);
    CHECK(state.output.mppt_voltage_target_v <= 24.0F);

    /* r86 carried a negative direction out of the CC-entry transient.  A
     * neutral stiff-source plateau must reseed the search upward instead of
     * walking the requested power toward zero. */
    state.po_direction = -1;

    /* Reproduce the bench-source plateau before the current-limit knee. The
     * current perturbation must keep climbing; no power target is generated. */
    float previous_current_command_a = state.mppt_current_command_a;
    for (uint32_t now_ms = 500U; now_ms <= 4000U; now_ms += 250U)
    {
        measurement.now_ms = now_ms;
        measurement.vin_v = 21.01F;
        measurement.iin_a = 2.0F;
        measurement.iout_a = state.mppt_current_command_a;
        dab_energy_update(&state, &measurement);
        CHECK(state.mppt_current_command_a >= previous_current_command_a);
        CHECK(state.po_direction > 0);
        previous_current_command_a = state.mppt_current_command_a;
    }
    CHECK(state.mppt_current_command_a > 2.5F);

    /* Below Vmin, every real 100 ms CPU1 algorithm update reduces the current
     * demand by 0.05 A, preserving the physical 0.5 A/s limit. */
    float const current_before_undervoltage =
        state.mppt_current_command_a;
    measurement.now_ms = 4100U;
    measurement.vin_v = 19.0F;
    measurement.iin_a = 2.0F;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.output.mppt_voltage_target_v - 20.0F) < 0.01F);
    CHECK(test_abs(state.mppt_current_command_a -
                   (current_before_undervoltage - 0.050F)) < 0.001F);
    measurement.now_ms = 4200U;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.mppt_current_command_a -
                   (current_before_undervoltage - 0.100F)) < 0.001F);
}

static void test_po_keeps_best_point_during_modulation_trials(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_MPPT_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MODE,
                                  (float) DAB_MPPT_MODE_PERTURB_OBSERVE, 0U));

    dab_energy_measurement_t measurement =
        valid_measurement(0U, 23.5F, 2.0F, 36.0F, 1.0F);
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.output.mppt_power_target_w - 47.0F) < 0.01F);

    /* A stiff source improves while the requested current climbs at exactly
     * 0.125 A per 250 ms, matching the independent 0.5 A/s slew. */
    for (uint32_t now_ms = 250U; now_ms <= 2000U; now_ms += 250U)
    {
        measurement.now_ms = now_ms;
        measurement.iin_a += 0.15F;
        measurement.iout_a = state.mppt_current_command_a;
        dab_energy_update(&state, &measurement);
    }
    float const proven_current_a = state.mppt_best_current_command_a;
    CHECK(proven_current_a > 1.7F);

    /* One startup/mode transient cannot lock the tracker. It continues with
     * the same bounded upward probe. */
    measurement.now_ms = 2250U;
    measurement.iin_a = 2.82F;
    measurement.iout_a = state.mppt_current_command_a;
    dab_energy_update(&state, &measurement);
    CHECK(!state.mppt_peak_hold);
    CHECK(state.mppt_current_command_a > proven_current_a);

    /* A sustained loss beyond the best current is confirmed over four
     * samples, then the tracker returns to the proven point and holds it. */
    for (uint32_t now_ms = 2500U; now_ms <= 6000U; now_ms += 250U)
    {
        measurement.now_ms = now_ms;
        measurement.iin_a = 2.70F;
        measurement.iout_a = state.mppt_current_command_a;
        dab_energy_update(&state, &measurement);
    }
    CHECK(state.mppt_peak_hold);
    /* With replay assist the controller may already be walking back toward
     * the proven live power, but only at the 0.01 A/update trim rate. */
    CHECK(state.mppt_current_command_a >= proven_current_a);
    CHECK(state.mppt_current_command_a <= (proven_current_a + 0.30F));

    /* r117's board-side 18.1 V remains in the accepted range. While below the
     * measured knee it backs the request down by one 100 ms / 0.05 A recovery
     * step, rather than releasing the hold and searching upward. */
    float const command_before_collapse =
        state.mppt_current_command_a;
    measurement.now_ms = 6010U;
    measurement.vin_v = 18.1F;
    measurement.iin_a = 2.0F;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.mppt_current_command_a -
                   (command_before_collapse - 0.050F)) < 0.001F);

    /* A genuine collapse below the local 17.5 V floor still releases the best
     * command so a reduced source limit can be reacquired autonomously. */
    measurement.now_ms = 6020U;
    measurement.vin_v = 17.0F;
    dab_energy_update(&state, &measurement);
    CHECK(state.mppt_current_command_a <
          (command_before_collapse - 0.050F));
    CHECK(test_abs(state.mppt_best_current_command_a -
                   state.mppt_current_command_a) < 0.01F);
}

static void test_po_locks_crossed_peak_during_modulation_search(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_MPPT_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MODE,
                                  (float) DAB_MPPT_MODE_PERTURB_OBSERVE, 0U));

    dab_energy_measurement_t measurement =
        valid_measurement(0U, 21.4F, 1.0F, 36.0F, 0.5F);
    dab_energy_update(&state, &measurement);

    measurement.now_ms = 250U;
    measurement.vin_v = 21.35F;
    measurement.iin_a = 2.0F;
    measurement.iout_a = state.mppt_current_command_a;
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 500U;
    measurement.vin_v = 21.30F;
    measurement.iin_a = 3.0F;
    measurement.iout_a = state.mppt_current_command_a;
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 750U;
    measurement.vin_v = 21.25F;
    measurement.iin_a = 4.0F;
    measurement.iout_a = state.mppt_current_command_a;
    dab_energy_update(&state, &measurement);

    float const best_current_a = state.mppt_best_current_command_a;
    float const best_voltage_v = state.mppt_best_voltage_v;
    CHECK(best_current_a > 0.5F);

    /* Four consecutive probes beyond the best point confirm the source knee;
     * a single startup or modulation transient is not enough. */
    for (uint32_t now_ms = 1000U; now_ms <= 2000U; now_ms += 250U)
    {
        measurement.now_ms = now_ms;
        measurement.vin_v = 19.8F;
        measurement.iin_a = 3.5F;
        measurement.iout_a = state.mppt_current_command_a;
        dab_energy_update(&state, &measurement);
        if (now_ms < 2000U)
        {
            CHECK(!state.mppt_peak_hold);
        }
    }
    CHECK(state.mppt_peak_hold);
    CHECK(test_abs(state.mppt_current_command_a - best_current_a) < 0.01F);
    CHECK(test_abs(state.output.mppt_voltage_target_v - best_voltage_v) < 0.01F);

    /* CPU0 mode trials may temporarily lose power, but cannot move the held
     * source command or teach that transient as a new MPP. */
    measurement.now_ms = 3000U;
    measurement.vin_v = best_voltage_v - 0.05F;
    measurement.iin_a = 1.0F;
    dab_energy_update(&state, &measurement);
    CHECK(state.mppt_peak_hold);
    CHECK(state.mppt_knee_recovered);
    CHECK(test_abs(state.mppt_current_command_a - best_current_a) < 0.01F);

    /* More available source power raises Vin at the held current. The local
     * knee-voltage loop remains in hold and increases demand at 0.5 A/s; it
     * does not need a restart or a watt command. */
    float const held_current_a = state.mppt_current_command_a;
    measurement.now_ms = 4000U;
    measurement.vin_v = best_voltage_v + 1.0F;
    dab_energy_update(&state, &measurement);
    CHECK(state.mppt_initialized);
    CHECK(state.mppt_peak_hold);
    CHECK(test_abs(state.mppt_current_command_a -
                   (held_current_a + 0.01F)) < 0.001F);
}

static void test_mppt_waits_for_current_actuator(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_MPPT_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MODE,
                                  (float) DAB_MPPT_MODE_PERTURB_OBSERVE, 0U));

    dab_energy_measurement_t measurement =
        valid_measurement(0U, 22.0F, 2.0F, 36.0F, 1.0F);
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 250U;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.mppt_current_command_a - 1.125F) < 0.001F);

    /* The next positive probe is held while the physical output current is
     * more than 0.2 A behind the request. */
    measurement.now_ms = 500U;
    measurement.iout_a = 0.50F;
    measurement.iin_a = 2.2F;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.mppt_current_command_a - 1.125F) < 0.001F);

    /* Once the actuator catches up, autonomous exploration resumes with the
     * same 0.125 A / 250 ms bound. */
    measurement.now_ms = 750U;
    measurement.iout_a = state.mppt_current_command_a;
    measurement.iin_a = 2.3F;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.mppt_current_command_a - 1.250F) < 0.001F);
}

static void test_po_catches_source_knee_before_deep_collapse(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_MPPT_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MODE,
                                  (float) DAB_MPPT_MODE_PERTURB_OBSERVE, 0U));

    dab_energy_measurement_t measurement =
        valid_measurement(0U, 22.0F, 2.0F, 36.0F, 1.0F);
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 250U;
    measurement.vin_v = 21.9F;
    measurement.iin_a = 3.0F;
    measurement.iout_a = state.mppt_current_command_a;
    dab_energy_update(&state, &measurement);
    CHECK(!state.mppt_peak_hold);
    CHECK(test_abs(state.mppt_current_command_a - 1.125F) < 0.001F);

    /* Pin is still high, but Vin has bent down by more than 0.12 V while
     * input current is not falling: enter the local peak hold now instead of
     * waiting until the source has already collapsed. */
    measurement.now_ms = 500U;
    measurement.vin_v = 21.7F;
    measurement.iin_a = 3.2F;
    measurement.iout_a = state.mppt_current_command_a;
    dab_energy_update(&state, &measurement);
    CHECK(state.mppt_peak_hold);
    CHECK(state.po_direction < 0);
    CHECK(test_abs(state.mppt_current_command_a - 1.000F) < 0.001F);
    CHECK(test_abs(state.mppt_hold_anchor_vin_v - 21.9F) < 0.01F);
}

static void test_po_ignores_knee_until_current_actuator_tracks(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_MPPT_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MODE,
                                  (float) DAB_MPPT_MODE_PERTURB_OBSERVE, 0U));

    dab_energy_measurement_t measurement =
        valid_measurement(0U, 22.0F, 2.0F, 36.0F, 1.0F);
    dab_energy_update(&state, &measurement);
    measurement.now_ms = 250U;
    measurement.vin_v = 21.9F;
    measurement.iin_a = 3.0F;
    measurement.iout_a = state.mppt_current_command_a;
    dab_energy_update(&state, &measurement);

    /* Reproduce r130: physical current is still far above the requested
     * current while the modulation actuator is settling.  Its Vin bend must
     * not latch the source peak. */
    measurement.now_ms = 500U;
    measurement.vin_v = 21.7F;
    measurement.iin_a = 3.2F;
    measurement.iout_a = state.mppt_current_command_a + 1.0F;
    dab_energy_update(&state, &measurement);
    CHECK(!state.mppt_peak_hold);
}

static void test_peak_hold_restores_proven_power_inside_voltage_deadband(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_MPPT_ONLY, 0U));
    state.mppt_initialized = true;
    state.mppt_peak_hold = true;
    state.mppt_knee_recovered = true;
    state.mppt_current_command_a = 4.0F;
    state.mppt_best_current_command_a = 5.4F;
    state.mppt_best_power_w = 220.0F;
    state.mppt_best_voltage_v = 21.0F;
    state.mppt_hold_anchor_vin_v = 21.0F;

    /* r131 replayed only 160 W at Vin=21.03 V: inside the old 0.08 V
     * deadband, but safely above its lower edge and with the actuator caught
     * up.  Restore only one normal 0.01 A / 100 ms trim step. */
    dab_energy_measurement_t measurement =
        valid_measurement(100U, 21.03F, 160.0F / 21.03F, 36.0F, 4.0F);
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.mppt_current_command_a - 4.01F) < 0.001F);

    /* r135 stopped replaying at the former 90% threshold.  Even 95% of the
     * live measured best must keep the bounded 0.1 A/s recovery active. */
    measurement.now_ms = 200U;
    measurement.iin_a = (0.95F * 220.0F) / measurement.vin_v;
    measurement.iout_a = state.mppt_current_command_a;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.mppt_current_command_a - 4.02F) < 0.001F);

    /* Once replay is within 2% of the measured live best, voltage—not a watt
     * command—again becomes the only reason to move the current request. */
    measurement.now_ms = 300U;
    measurement.iin_a = (0.98F * 220.0F) / measurement.vin_v;
    measurement.iout_a = state.mppt_current_command_a;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.mppt_current_command_a - 4.02F) < 0.001F);

    /* Source voltage below the safe knee band always takes priority over the
     * replay assist and backs current down instead. */
    measurement.now_ms = 400U;
    measurement.vin_v = 20.90F;
    measurement.iin_a = 8.0F;
    measurement.iout_a = state.mppt_current_command_a;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.mppt_current_command_a - 4.01F) < 0.001F);
}

static void test_peak_hold_waits_for_actuator_before_next_backoff(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_MPPT_ONLY, 0U));
    state.mppt_initialized = true;
    state.mppt_peak_hold = true;
    state.mppt_knee_recovered = false;
    state.mppt_current_command_a = 4.28F;
    state.mppt_best_current_command_a = 4.28F;
    state.mppt_best_power_w = 223.0F;
    state.mppt_best_voltage_v = 20.80F;
    state.mppt_hold_anchor_vin_v = 20.80F;

    /* r131 had Iout about 0.15 A above the already-reversed target.  Keep the
     * target fixed so CPU0's 0.5 A/s active reference can catch it. */
    dab_energy_measurement_t measurement =
        valid_measurement(100U, 20.40F, 10.7F, 36.0F, 4.43F);
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.mppt_current_command_a - 4.28F) < 0.001F);

    /* Once measured Iout is within 0.05 A, release exactly one bounded
     * 0.05 A / 100 ms recovery step. */
    measurement.now_ms = 200U;
    measurement.iout_a = 4.28F;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.mppt_current_command_a - 4.23F) < 0.001F);
}

static void test_peak_hold_rebases_selected_waveform_floor(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_MPPT_ONLY, 0U));
    state.mppt_initialized = true;
    state.mppt_peak_hold = true;
    state.mppt_knee_recovered = false;
    state.mppt_actuator_rebased = false;
    state.mppt_current_command_a = 1.79F;
    state.mppt_best_current_command_a = 1.79F;
    state.mppt_best_power_w = 173.0F;
    state.mppt_best_voltage_v = 22.9F;
    state.mppt_hold_anchor_vin_v = 22.9F;

    /* v49bg selected an EPS waveform which physically delivered 4.6 A even
     * though the old P&O request was only 1.79 A. Adopt the already-flowing
     * current as the new target; the lower controller's active reference still
     * retains its <=0.5 A/s slew, so this is a bumpless logical handoff. */
    dab_energy_measurement_t measurement =
        valid_measurement(100U, 22.9F, 7.60F, 33.0F, 4.60F);
    dab_energy_update(&state, &measurement);
    CHECK(state.mppt_actuator_rebased);
    CHECK(test_abs(state.mppt_current_command_a - 4.60F) < 0.001F);
    CHECK(test_abs(state.output.requested_current_a - 4.60F) < 0.001F);

    /* The handoff occurs once. A later real Vin sag must use the ordinary
     * gentle knee-hold backoff instead of snapping back to measured Iout. */
    measurement.now_ms = 200U;
    measurement.vin_v = 22.70F;
    measurement.iin_a = 7.60F;
    measurement.iout_a = 4.60F;
    dab_energy_update(&state, &measurement);
    CHECK(test_abs(state.mppt_current_command_a - 4.59F) < 0.001F);
}

static void test_high_power_positive_probe_is_gentle(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MODE,
                                  (float) DAB_ENERGY_MODE_MPPT_ONLY, 0U));
    CHECK(dab_energy_apply_config(&state, DAB_ENERGY_CONFIG_MPPT_MODE,
                                  (float) DAB_MPPT_MODE_PERTURB_OBSERVE, 0U));

    dab_energy_measurement_t measurement =
        valid_measurement(0U, 22.0F, 8.0F, 36.0F, 4.0F);
    dab_energy_update(&state, &measurement);
    float const before_a = state.mppt_current_command_a;
    measurement.now_ms = 250U;
    measurement.vin_v = 21.98F;
    measurement.iin_a = 8.2F;
    measurement.iout_a = before_a;
    dab_energy_update(&state, &measurement);

    /* Above 150 W, positive exploration is 0.025 A/250 ms = 0.1 A/s.
     * Negative source recovery remains separately bounded at 0.5 A/s. */
    CHECK(test_abs(state.mppt_current_command_a -
                   (before_a + 0.025F)) < 0.001F);
}

int main(void)
{
    test_payload_codec();
    test_product_mppt_defaults_are_local();
    test_charge_start_mode_is_lower_controller_owned();
    test_ai_hint_isolation();
    test_battery_target_overrides_preserve_profile();
    test_lithium_charger();
    test_startup_overshoot_does_not_enter_cv();
    test_startup_spike_recovers_precharge_for_both_lithium_profiles();
    test_cv_termination_hold_and_undervoltage_recovery();
    test_cv_sense_compensation_reproduces_v45_failed_point();
    test_lead_and_fixed_power();
    test_solar_charger_runs_mppt_only_in_cc();
    test_360w_fixed_power_respects_battery_ceiling();
    test_incremental_conductance_noise_and_vin_recovery();
    test_po_keeps_best_point_during_modulation_trials();
    test_po_locks_crossed_peak_during_modulation_search();
    test_mppt_waits_for_current_actuator();
    test_po_catches_source_knee_before_deep_collapse();
    test_po_ignores_knee_until_current_actuator_tracks();
    test_peak_hold_restores_proven_power_inside_voltage_deadband();
    test_peak_hold_waits_for_actuator_before_next_backoff();
    test_peak_hold_rebases_selected_waveform_floor();
    test_high_power_positive_probe_is_gentle();
    if (0U != s_failures)
    {
#if !defined(DAB_ENERGY_TEST_WASM)
        printf("dab_energy_control: %u failure(s)\n", s_failures);
#endif
        return 1;
    }
#if !defined(DAB_ENERGY_TEST_WASM)
    puts("dab_energy_control: all host tests passed");
#endif
    return 0;
}
