#include "../RA8P1_DAB_DualCore_Solution_CPU0/src/dab_advanced_control.h"
#include "../RA8P1_DAB_DualCore_Solution_CPU0/src/dab_vin_adaptation.h"

static int s_failures;
#define assert(condition) do { if (!(condition)) { s_failures++; } } while (0)
#define puts(message)     ((void) (message))

static bool nearf(float a, float b, float tolerance)
{
    float const difference = (a >= b) ? (a - b) : (b - a);
    return difference <= tolerance;
}

static void test_vin_adaptation_disabled_is_passthrough(void)
{
    dab_vin_adaptation_state_t state;
    dab_vin_adaptation_initialize(&state);
    dab_vin_adaptation_input_t const input =
    {
        .vin_v = 18.0F,
        .requested_current_a = 2.0F,
        .measured_current_a = 1.9F,
        .measured_output_power_w = 80.0F,
        .phase_command_counts = 50.0F,
        .phase_limit_counts = 100.0F,
        .base_kp_ns_per_a = 2.0F,
        .base_ki_us_per_as = 0.2F,
        .output_power_ceiling_w = 400.0F,
        .control_dt_s = 0.001F,
        .current_control_direction = 1,
        .current_control_active = true
    };
    dab_vin_adaptation_update(&state, &input);
    assert(!state.output.active);
    assert(nearf(state.output.effective_kp_ns_per_a, 2.0F, 0.0001F));
    assert(nearf(state.output.effective_ki_us_per_as, 0.2F, 0.0001F));
    assert(nearf(state.output.feedforward_delta_counts, 0.0F, 0.0001F));
}

static void test_vin_adaptation_schedule_and_limit_detection(void)
{
    dab_vin_adaptation_state_t state;
    dab_vin_adaptation_initialize(&state);
    state.config.enabled = true;
    state.config.filter_alpha = 1.0F;
    dab_vin_adaptation_input_t input =
    {
        .vin_v = 18.0F,
        .requested_current_a = 2.0F,
        .measured_current_a = 1.0F,
        .measured_output_power_w = 44.0F,
        .phase_command_counts = 100.0F,
        .phase_limit_counts = 100.0F,
        .base_kp_ns_per_a = 2.0F,
        .base_ki_us_per_as = 0.2F,
        .output_power_ceiling_w = 400.0F,
        .control_dt_s = 0.10F,
        .current_control_direction = 1,
        .current_control_active = true
    };
    for (unsigned int i = 0U; i < 6U; i++)
    {
        dab_vin_adaptation_update(&state, &input);
    }
    assert(state.output.active);
    assert(nearf(state.output.gain_scale, 24.0F / 18.0F, 0.001F));
    assert(nearf(state.output.effective_kp_ns_per_a, 2.6667F, 0.001F));
    assert(state.output.actuator_limited);
    assert(state.output.band == DAB_VIN_ADAPTATION_BAND_18V);
}

static void test_vin_adaptation_directional_feedforward(void)
{
    dab_vin_adaptation_state_t state;
    dab_vin_adaptation_initialize(&state);
    state.config.enabled = true;
    state.config.filter_alpha = 1.0F;
    state.config.feedforward_strength = 1.0F;
    dab_vin_adaptation_input_t input =
    {
        .vin_v = 24.0F,
        .requested_current_a = 2.0F,
        .measured_current_a = 2.0F,
        .measured_output_power_w = 88.0F,
        .phase_command_counts = 50.0F,
        .phase_limit_counts = 100.0F,
        .base_kp_ns_per_a = 2.0F,
        .base_ki_us_per_as = 0.2F,
        .output_power_ceiling_w = 400.0F,
        .control_dt_s = 0.001F,
        .current_control_direction = 1,
        .current_control_active = true
    };
    dab_vin_adaptation_update(&state, &input);
    input.vin_v = 18.0F;
    dab_vin_adaptation_update(&state, &input);
    assert(state.output.feedforward_delta_counts > 0.0F);

    dab_vin_adaptation_runtime_reset(&state);
    input.vin_v = 24.0F;
    input.current_control_direction = -1;
    dab_vin_adaptation_update(&state, &input);
    input.vin_v = 18.0F;
    dab_vin_adaptation_update(&state, &input);
    assert(state.output.feedforward_delta_counts < 0.0F);
}

static void test_autotune_identifies_positive_and_negative_gain(void)
{
    dab_autotune_state_t state;
    dab_autotune_initialize(&state);
    state.config.settle_time_s = 0.1F;
    state.config.sample_time_s = 0.1F;
    assert(dab_autotune_start(&state, 100.0F, 320.0F, 2.0F));
    dab_autotune_update(&state, 2.0F, 0.1F);
    dab_autotune_update(&state, 2.2F, 0.1F);
    dab_autotune_update(&state, 1.8F, 0.1F);
    assert(state.stage == DAB_AUTOTUNE_COMPLETE);
    assert(state.result.valid);
    assert(state.result.recommended_direction == 1);
    assert(state.result.local_gain_a_per_ns > 0.0F);

    dab_autotune_initialize(&state);
    state.config.settle_time_s = 0.1F;
    state.config.sample_time_s = 0.1F;
    assert(dab_autotune_start(&state, 100.0F, 320.0F, 2.0F));
    dab_autotune_update(&state, 2.0F, 0.1F);
    dab_autotune_update(&state, 1.8F, 0.1F);
    dab_autotune_update(&state, 2.2F, 0.1F);
    assert(state.result.valid);
    assert(state.result.recommended_direction == -1);
    assert(state.result.local_gain_a_per_ns < 0.0F);
}

static void test_autotune_rejects_insufficient_response(void)
{
    dab_autotune_state_t state;
    dab_autotune_initialize(&state);
    state.config.settle_time_s = 0.1F;
    state.config.sample_time_s = 0.1F;
    state.config.minimum_current_delta_a = 0.05F;
    assert(dab_autotune_start(&state, 100.0F, 320.0F, 2.0F));
    dab_autotune_update(&state, 2.0F, 0.1F);
    dab_autotune_update(&state, 2.01F, 0.1F);
    dab_autotune_update(&state, 2.00F, 0.1F);
    assert(state.stage == DAB_AUTOTUNE_FAILED);
    assert(!state.result.valid);
    assert(state.result.failure_reason ==
           DAB_AUTOTUNE_FAILURE_INSUFFICIENT_RESPONSE);
}

static void test_modswitch_completion_and_rollback(void)
{
    dab_modswitch_state_t state;
    dab_modswitch_initialize(&state);
    assert(dab_modswitch_start(&state, 0U, 2U, 1, 1,
                               240.0F, 110.0F, 100.0F));
    dab_modswitch_update(&state, 240.0F, 110.0F, 98.0F, 0.10F);
    assert(state.stage == DAB_MODSWITCH_COMPLETE);

    dab_modswitch_initialize(&state);
    assert(dab_modswitch_start(&state, 0U, 1U, 1, 1,
                               240.0F, 0.0F, 100.0F));
    dab_modswitch_update(&state, 10.0F, 0.0F, 70.0F, 0.01F);
    assert(state.stage == DAB_MODSWITCH_ROLLBACK);
    assert(state.rollback_requested);
    assert(state.failure_reason == DAB_MODSWITCH_FAILURE_METRIC_ABORT);
}

static void test_cp_damping_virtual_conductance(void)
{
    dab_cp_damping_state_t state;
    dab_cp_damping_initialize(&state);
    state.config.target_slew_w_per_s = 1000.0F;
    state.config.power_kp_a_per_w = 0.0F;
    state.config.power_ki_a_per_ws = 0.0F;
    state.config.enabled = false;
    float current = dab_cp_damping_update(&state, 50.0F, 100.0F, 400.0F,
                                          0.1F);
    assert(nearf(current, 2.0F, 0.001F));

    dab_cp_damping_runtime_reset(&state);
    state.config.enabled = true;
    current = dab_cp_damping_update(&state, 40.0F, 100.0F, 400.0F, 0.1F);
    assert(nearf(current, 2.3F, 0.001F));
    assert(state.damping_current_a < 0.0F);
}

static void test_cp_damping_power_ceiling_and_invalid_voltage(void)
{
    dab_cp_damping_state_t state;
    dab_cp_damping_initialize(&state);
    state.config.target_power_w = 200.0F;
    state.config.target_slew_w_per_s = 1000.0F;
    state.config.power_kp_a_per_w = 0.1F;
    state.config.power_ki_a_per_ws = 1.0F;
    float current = dab_cp_damping_update(&state, 40.0F, 0.0F, 100.0F,
                                          0.2F);
    assert(nearf(state.effective_current_limit_a, 2.5F, 0.001F));
    assert(nearf(current, 2.5F, 0.001F));
    assert(state.saturated);

    current = dab_cp_damping_update(&state, 10.0F, 0.0F, 100.0F, 0.1F);
    assert(nearf(current, 100.0F / 44.0F, 0.001F));
    assert(nearf(state.active_power_target_w, 200.0F, 0.001F));
    assert(nearf(state.integral_current_a, 0.0F, 0.001F));
}

int main(void)
{
    test_vin_adaptation_disabled_is_passthrough();
    test_vin_adaptation_schedule_and_limit_detection();
    test_vin_adaptation_directional_feedforward();
    test_autotune_identifies_positive_and_negative_gain();
    test_autotune_rejects_insufficient_response();
    test_modswitch_completion_and_rollback();
    test_cp_damping_virtual_conductance();
    test_cp_damping_power_ceiling_and_invalid_voltage();
    puts("dab_advanced_control tests passed");
    return s_failures;
}
