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

static void test_ai_hint_isolation(void)
{
    dab_energy_state_t state;
    dab_energy_initialize(&state);
    CHECK(test_abs(state.config.battery_cv_v - 43.8F) < 0.01F);
    CHECK(test_abs(state.config.mppt_max_power_w - 400.0F) < 0.01F);
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

static void test_lithium_charger(void)
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
        valid_measurement(100U, 24.0F, 1.0F, 29.0F, 0.2F);
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_PRECHARGE == state.output.charge_stage);
    CHECK(test_abs(state.output.requested_current_a - 1.0F) < 0.01F);

    measurement.now_ms = 200U;
    measurement.vout_v = 31.0F;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_CURRENT == state.output.charge_stage);
    measurement.now_ms = 300U;
    measurement.vout_v = 41.95F;
    measurement.iout_a = 0.2F;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_CONSTANT_VOLTAGE == state.output.charge_stage);
    measurement.now_ms = 310U;
    measurement.vout_v = 42.0F;
    dab_energy_update(&state, &measurement);
    CHECK(state.output.requested_current_a <= state.config.battery_max_current_a);
    measurement.now_ms = 30400U;
    dab_energy_update(&state, &measurement);
    CHECK(DAB_CHARGE_STAGE_DONE == state.output.charge_stage);
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

int main(void)
{
    test_payload_codec();
    test_ai_hint_isolation();
    test_lithium_charger();
    test_lead_and_fixed_power();
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
