#include "ra8p1_peripheral_test_config.h"

#if !RA8P1_PERIPHERAL_TEST_ENABLE

/*
 * First-power DAB application for the final 2026-07-22 hardware.
 *
 * The power application is deliberately separate from
 * ra8p1_dab_peripheral_test.c.  The tested PWM/ADC commissioning image can be
 * restored by changing one switch in ra8p1_peripheral_test_config.h.
 *
 * Manual commissioning policy:
 * - no automatic power transfer after reset;
 * - RUN_REQ and every path control start LOW;
 * - power transfer requires an explicit DEBUG-UART on command;
 * - phase starts at zero and rises slowly;
 * - runtime-selectable NORMAL/BENCH guards preserve absolute hardware limits;
 * - explicit UART stop commands remove PWM, RUN_REQ, and every power path.
 */

#include "User_Init_Task.h"
#include "dab_advanced_control.h"
#include "dab_accel.h"
#include "dab_adc_dma.h"
#include "dab_board.h"
#include "dab_can_transport.h"
#include "dab_dual_core_cpu0.h"
#include "dab_ethernet.h"
#include "dab_first_power_config.h"
#include "dab_measurements.h"
#include "dab_mppt_phase_search.h"
#include "dab_pwm_hw.h"
#include "dab_ra8p1_scope_test.h"
#include "dab_remote_control.h"
#include "dab_tuned_parameter_table.h"
#include "dab_validated_charger_table.h"
#include "dab_vin_adaptation.h"
#include "drv_tcan3413.h"
#include "drv_tmp1075.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UART_TX_QUEUE_DEPTH       (16U)
#define UART_TX_MESSAGE_LENGTH    (512U)
#define UART_RX_RING_LENGTH       (128U)
#define UART_COMMAND_LENGTH       (96U)

typedef enum e_dab_state
{
    DAB_STATE_BOOT = 0,
    DAB_STATE_READY,
    DAB_STATE_RESET_LOW,
    DAB_STATE_LATCH_RELEASE,
    DAB_STATE_MAIN_SETTLE,
    DAB_STATE_OUTPUT_SETTLE,
    DAB_STATE_SOFTSTART,
    DAB_STATE_RUN,
    DAB_STATE_PAUSED,
    DAB_STATE_PATH_DIRECT,
    DAB_STATE_TEST_MODE,
    DAB_STATE_FAULT
} dab_state_t;

typedef enum e_dab_modulation_mode
{
    DAB_MODULATION_SPS = 0,
    DAB_MODULATION_EPS,
    DAB_MODULATION_TPS,
    DAB_MODULATION_AUTO
} dab_modulation_mode_t;

typedef enum e_dab_control_architecture
{
    DAB_CONTROL_ARCH_VOLTAGE_PI_SINGLE = 0,
    DAB_CONTROL_ARCH_OUTPUT_CURRENT_PI,
    DAB_CONTROL_ARCH_ENERGY_SUPERVISOR,
    DAB_CONTROL_ARCH_CP_DAMPED_CASCADE
} dab_control_architecture_t;

typedef enum e_dab_guard_profile
{
    DAB_GUARD_PROFILE_NORMAL = 0,
    DAB_GUARD_PROFILE_BENCH
} dab_guard_profile_t;

typedef enum e_dab_fault
{
    DAB_FAULT_NONE          = 0U,
    DAB_FAULT_INIT          = 1UL << 0,
    DAB_FAULT_ADC           = 1UL << 1,
    DAB_FAULT_ADC_STALE     = 1UL << 2,
    DAB_FAULT_RAW_N         = 1UL << 3,
    DAB_FAULT_IPRI_OC_N     = 1UL << 4,
    DAB_FAULT_ISEC_OC_N     = 1UL << 5,
    DAB_FAULT_POEG          = 1UL << 6,
    DAB_FAULT_VIN_UV        = 1UL << 7,
    DAB_FAULT_VIN_OV        = 1UL << 8,
    DAB_FAULT_VOUT_OV       = 1UL << 9,
    DAB_FAULT_IPRI          = 1UL << 10,
    DAB_FAULT_ISEC          = 1UL << 11,
    DAB_FAULT_IIN           = 1UL << 12,
    DAB_FAULT_IOUT          = 1UL << 13,
    DAB_FAULT_POWER         = 1UL << 14,
    DAB_FAULT_NO_RISE       = 1UL << 15,
    DAB_FAULT_PWM_UPDATE    = 1UL << 16,
    DAB_FAULT_PRI_HW        = 1UL << 17,
    DAB_FAULT_SEC_HW        = 1UL << 18,
    DAB_FAULT_AMBIENT_OT    = 1UL << 19,
    DAB_FAULT_TEMP          = 1UL << 20
} dab_fault_t;

typedef enum e_dab_primary_diag_target
{
    DAB_PRIMARY_DIAG_FULL = 0,
    DAB_PRIMARY_DIAG_A,
    DAB_PRIMARY_DIAG_B
} dab_primary_diag_target_t;

typedef enum e_dab_secondary_diag_target
{
    DAB_SECONDARY_DIAG_FULL = 0,
    DAB_SECONDARY_DIAG_A,
    DAB_SECONDARY_DIAG_B
} dab_secondary_diag_target_t;

static volatile bool    s_uart_tx_busy;
static volatile uint8_t s_uart_tx_head;
static volatile uint8_t s_uart_tx_tail;
static char             s_uart_tx_queue[UART_TX_QUEUE_DEPTH][UART_TX_MESSAGE_LENGTH];
static volatile uint8_t s_rx_head;
static volatile uint8_t s_rx_tail;
static uint8_t          s_rx_ring[UART_RX_RING_LENGTH];

static dab_state_t        s_state = DAB_STATE_BOOT;
static TickType_t         s_state_tick;
static TickType_t         s_last_adc_tick;
static TickType_t         s_last_valid_adc_tick;
static uint32_t           s_fault_mask;
static uint32_t           s_last_control_sequence;
static dab_measurements_t s_measurements;
static bool               s_measurements_ready;
static bool               s_control_enabled;
static bool               s_primary_diag_running;
static TickType_t         s_nopower_diag_arm_deadline;
static bool               s_nopower_diag_arm_available;
static bool               s_power_command_received;
static bool               s_unlock_high_power = true;
static bool               s_force_telemetry;
static bool               s_stream;
static bool               s_vofa_stream;
static bool               s_algorithm_logging_enabled;
static bool               s_test_mode;
static bool               s_tmp1075_ok;
static tmp1075_info_t      s_tmp1075;
static float              s_vref_v       = DAB_VREF_DEFAULT_V;
static float              s_power_limit_w = DAB_POWER_DEFAULT_W;
static uint32_t           s_closed_loop_phase_cap_counts =
    DAB_PHASE_NORMAL_MAX_COUNTS;
static float              s_kp_ns_per_v  = DAB_KP_DEFAULT_NS_PER_V;
static float              s_ki_us_per_vs = DAB_KI_DEFAULT_US_PER_VS;
static float              s_kd_ns_s_per_v = DAB_KD_DEFAULT_NS_S_PER_V;
static float              s_pi_integral_counts;
static float              s_current_pi_integral_counts;
static float              s_current_reference_command_a = 1.0F;
static float              s_current_reference_active_a;
static float              s_current_error_a;
static int8_t             s_current_control_direction = 1;
static float              s_current_kp_ns_per_a = DAB_CURRENT_KP_DEFAULT_NS_PER_A;
static float              s_current_ki_us_per_as = DAB_CURRENT_KI_DEFAULT_US_PER_AS;
static float              s_current_reference_slew_a_per_s =
    DAB_CURRENT_REFERENCE_SLEW_DEFAULT_A_PER_S;
static float              s_phase_command_counts;
static float              s_previous_vout_v;
static float              s_vout_derivative_v_per_s;
static bool               s_derivative_initialized;
static bool               s_outer_saturated;
static uint32_t           s_outer_saturation_updates;
static bool               s_manual_phase_override;
static uint32_t           s_manual_phase_counts;
static float              s_primary_inner_command_counts;
static float              s_secondary_inner_command_counts;
static bool               s_inner_manual_override;
static float              s_primary_inner_manual_target_counts;
static float              s_secondary_inner_manual_target_counts;
static uint32_t           s_pwm_update_miss_count;
static dab_modulation_mode_t s_modulation_requested = DAB_MODULATION_SPS;
static dab_modulation_mode_t s_modulation_effective = DAB_MODULATION_SPS;
static char                  s_load_mode[DAB_REMOTE_TEXT_LENGTH] = "resistive_load";
static dab_control_architecture_t s_control_architecture =
    DAB_CONTROL_ARCH_VOLTAGE_PI_SINGLE;
static dab_guard_profile_t s_guard_profile = DAB_GUARD_PROFILE_BENCH;
static dab_energy_state_t s_energy_shadow;
static float s_bench_vin_set_v = DAB_BENCH_VIN_SET_DEFAULT_V;
static float s_load_nominal_ohm = DAB_LOAD_NOMINAL_DEFAULT_OHM;
static float s_requested_power_layer_w = DAB_POWER_LAYER_DEFAULT_W;
static bool  s_schedule_enabled;
static bool  s_schedule_autoload_enabled;
static bool  s_configuration_start_blocked;
static uint32_t s_schedule_entry_id;
static bool  s_charger_low_input_runtime;
static uint32_t s_charger_runtime_candidate_id;
static bool  s_charger_runtime_candidate_low_input;
static TickType_t s_charger_runtime_candidate_tick;
static TickType_t s_charger_runtime_last_select_tick;
static bool  s_feedforward_enabled;
static float s_feedforward_effective_phase_ns;
static dab_vin_adaptation_state_t s_vin_adaptation;
static dab_autotune_state_t s_autotune;
static dab_modswitch_state_t s_modswitch;
static dab_mppt_phase_search_t s_mppt_phase_search;
static dab_cp_damping_state_t s_cp_damping;
static float s_modswitch_source_primary_inner_counts;
static float s_modswitch_source_secondary_inner_counts;
static bool s_modswitch_completion_reported;

static void dab_log(const char * fmt, ...);
static void dab_uart_service(void);
static void dab_process_uart(void);
static void dab_print_status(void);
static void dab_print_observe(void);
static void dab_print_vofa(void);
static void dab_print_pins(void);
static void dab_print_drivers(void);
static void dab_print_adc_diag(void);
static void dab_print_adc_crosscheck(void);
static void dab_print_pwm_hw_diag(void);
static void dab_single_gate_diag(const char * argument);
static void dab_primary_bridge_diag(const char * argument,
                                    dab_primary_diag_target_t target);
static void dab_secondary_bridge_diag(const char * argument,
                                      dab_secondary_diag_target_t target);
static void dab_all_gates_pwm_diag(const char * argument);
static void dab_low_voltage_live_burst(const char * argument);
static void dab_nopower_diag_arm(const char * argument);
static bool dab_nopower_diag_vin_accept(float vin_abs,
                                        bool timed_only,
                                        const char * command_name);
static void dab_force_all_safe(void);
static void dab_fault_latch(uint32_t fault_mask);
static bool dab_adc_and_pwm_initialize(void);
static bool dab_measurements_refresh_from_adc(void);
static bool dab_current_zero_calibrate(void);
static void dab_state_service(void);
static void dab_control_update(void);
static uint32_t dab_state_elapsed_ms(void);
static bool dab_startup_hold_active(void);
static bool dab_startup_inner_slow_active(void);
static uint32_t dab_startup_phase_limit_counts(void);
static void dab_remote_command_service(void);
static void dab_remote_snapshot_publish_now(void);
static void dab_can_command_service(void);
static void dab_can_snapshot_build(dab_can_application_status_t * p_status,
                                   dab_can_measurements_t * p_measurements);
static uint32_t dab_detect_run_faults(void);
static void dab_control_reset(void);
static bool dab_schedule_apply_current(void);
static void dab_schedule_mark_manual(void);
static bool dab_charger_bundle_select(float measured_vin_v,
                                      float operating_voltage_v,
                                      uint8_t raw_stage,
                                      float requested_current_a,
                                      dab_validated_charger_entry_t * p_charger,
                                      float * p_score,
                                      bool * p_low_input_runtime);
static void dab_charger_runtime_schedule_service(void);
static void dab_mppt_phase_search_service(void);
static bool dab_charger_completion_service(void);
static void dab_print_parameter_bundle(void);
static void dab_print_energy_status(void);
static void dab_print_vin_adaptation_status(void);
static void dab_print_autotune_status(void);
static void dab_print_modswitch_status(void);
static void dab_print_cp_damping_status(void);
static bool dab_energy_parameter_set(dab_energy_config_key_t key, float value);
#if DAB_ALLOW_POWER_TRANSFER
static bool dab_start_preconditions_ok(void);
static void dab_path_direct_start(void);
#endif
int _write(int fd, char * p_buffer, int size);

static float dab_minf(float a, float b)
{
    return (a < b) ? a : b;
}

static float dab_maxf(float a, float b)
{
    return (a > b) ? a : b;
}

static float dab_clampf(float value, float lower, float upper)
{
    return dab_minf(dab_maxf(value, lower), upper);
}

static const char * dab_modulation_name(dab_modulation_mode_t mode)
{
    static const char * const names[] = {"SPS", "EPS", "TPS", "AUTO"};
    uint32_t const index = (uint32_t) mode;
    return (index < (sizeof(names) / sizeof(names[0]))) ? names[index] : "?";
}

static const char * dab_control_architecture_name(
    dab_control_architecture_t architecture)
{
    static const char * const names[] =
    {
        "VOLTAGE_PI_SINGLE_LOOP",
        "OUTPUT_CURRENT_PI",
        "CPU1_ENERGY_SUPERVISOR_CURRENT_PI",
        "CP_POWER_PI_CURRENT_PI_DAMPED"
    };
    uint32_t const index = (uint32_t) architecture;
    return (index < (sizeof(names) / sizeof(names[0]))) ? names[index] : "?";
}

static const char * dab_guard_profile_name(dab_guard_profile_t profile)
{
    return (DAB_GUARD_PROFILE_BENCH == profile) ? "BENCH_ABSOLUTE_ONLY" :
                                                  "NORMAL_LAYERED";
}

static const char * dab_vin_adaptation_band_name(
    dab_vin_adaptation_band_t band)
{
    static const char * const names[] = {"18V", "20V", "22V", "24V"};
    uint32_t const index = (uint32_t) band;
    return (index < (sizeof(names) / sizeof(names[0]))) ? names[index] : "?";
}

static const char * dab_autotune_stage_name(dab_autotune_stage_t stage)
{
    static const char * const names[] =
    {
        "IDLE", "SETTLE", "STEP_POS", "STEP_NEG", "COMPLETE", "FAILED"
    };
    uint32_t const index = (uint32_t) stage;
    return (index < (sizeof(names) / sizeof(names[0]))) ? names[index] : "?";
}

static const char * dab_modswitch_stage_name(dab_modswitch_stage_t stage)
{
    static const char * const names[] =
    {
        "IDLE", "TRANSITION", "VERIFY", "COMPLETE", "ROLLBACK", "FAILED"
    };
    uint32_t const index = (uint32_t) stage;
    return (index < (sizeof(names) / sizeof(names[0]))) ? names[index] : "?";
}

static float dab_slew(float current, float target, float step)
{
    if (target > current)
    {
        return dab_minf(target, current + step);
    }
    return dab_maxf(target, current - step);
}

/*
 * The differential-voltage pulse center of a bridge moves by half of its
 * inner shift.  Keep the controller/manual command expressed as effective
 * bridge-to-bridge center phase and compensate the raw secondary-A offset:
 *
 *   effective = raw + secondary_inner/2 - primary_inner/2
 */
static uint32_t dab_raw_phase_from_signed_effective(
    int32_t effective_counts,
    uint32_t primary_inner_counts,
    uint32_t secondary_inner_counts)
{
    int32_t const raw_x2 = (effective_counts * 2) +
                           (int32_t) primary_inner_counts -
                           (int32_t) secondary_inner_counts;
    if (raw_x2 <= 0)
    {
        return 0U;
    }

    uint32_t const raw_counts = (uint32_t) (raw_x2 + 1) / 2U;
    return (raw_counts > DAB_PHASE_ABSOLUTE_MAX_COUNTS) ?
           DAB_PHASE_ABSOLUTE_MAX_COUNTS : raw_counts;
}

static int32_t dab_charger_effective_bias_counts(void)
{
    float const bias_ns =
        dab_validated_charger_precharge_effective_bias_ns(s_schedule_entry_id);
    return (int32_t) (bias_ns / (float) DAB_PWM_COUNT_NS);
}

static int32_t dab_effective_phase_ns(uint32_t raw_counts,
                                      uint32_t primary_inner_counts,
                                      uint32_t secondary_inner_counts)
{
    int32_t const effective_x2 = ((int32_t) raw_counts * 2) +
                                 (int32_t) secondary_inner_counts -
                                 (int32_t) primary_inner_counts;
    return effective_x2 * ((int32_t) DAB_PWM_COUNT_NS / 2);
}

static void dab_modulation_targets(float * p_primary_counts,
                                   float * p_secondary_counts)
{
    *p_primary_counts = 0.0F;
    *p_secondary_counts = 0.0F;

    /* Every control mode starts as SPS with zero inner shift.  After the
     * initial hold, an explicit manual EPS/TPS inner target may slew while
     * still in SOFTSTART so a high conversion-ratio point can reach RUN.
     * AUTO partitioning remains RUN-only and no path bypasses the hold. */
    bool const manual_softstart_release =
        (DAB_STATE_SOFTSTART == s_state) &&
        (dab_state_elapsed_ms() >= DAB_STARTUP_PHASE_HOLD_MS) &&
        s_inner_manual_override &&
        ((DAB_MODULATION_EPS == s_modulation_requested) ||
         (DAB_MODULATION_TPS == s_modulation_requested));
    bool const startup_hold_required =
        dab_startup_hold_active() && !manual_softstart_release;
    if (((DAB_STATE_RUN != s_state) && !manual_softstart_release) ||
        startup_hold_required ||
        !s_measurements.valid ||
        (s_measurements.vin_v < 1.0F) ||
        (DAB_MODULATION_SPS == s_modulation_requested))
    {
        s_modulation_effective = DAB_MODULATION_SPS;
        return;
    }

    float const m = s_measurements.vout_v / (2.0F * s_measurements.vin_v);
    dab_modulation_mode_t effective = s_modulation_requested;
    if (DAB_MODULATION_AUTO == effective)
    {
        float const h = DAB_AUTO_M_HYSTERESIS;
        if ((DAB_MODULATION_SPS == s_modulation_effective) &&
            (m >= (DAB_AUTO_SPS_M_MIN - h)) &&
            (m <= (DAB_AUTO_SPS_M_MAX + h)))
        {
            effective = DAB_MODULATION_SPS;
        }
        else if ((DAB_MODULATION_EPS == s_modulation_effective) &&
                 (m >= (DAB_AUTO_EPS_M_MIN - h)) &&
                 (m <= (DAB_AUTO_EPS_M_MAX + h)) &&
                 ((m < (DAB_AUTO_SPS_M_MIN + h)) ||
                  (m > (DAB_AUTO_SPS_M_MAX - h))))
        {
            effective = DAB_MODULATION_EPS;
        }
        else if ((DAB_MODULATION_TPS == s_modulation_effective) &&
                 ((m < (DAB_AUTO_EPS_M_MIN + h)) ||
                  (m > (DAB_AUTO_EPS_M_MAX - h))))
        {
            effective = DAB_MODULATION_TPS;
        }
        else if ((m >= DAB_AUTO_SPS_M_MIN) && (m <= DAB_AUTO_SPS_M_MAX))
        {
            effective = DAB_MODULATION_SPS;
        }
        else if ((m >= DAB_AUTO_EPS_M_MIN) && (m <= DAB_AUTO_EPS_M_MAX))
        {
            effective = DAB_MODULATION_EPS;
        }
        else
        {
            effective = DAB_MODULATION_TPS;
        }
    }
    s_modulation_effective = effective;
    if (DAB_MODULATION_SPS == effective)
    {
        return;
    }

    if (s_inner_manual_override)
    {
        bool const zero_inner_for_taper = s_schedule_enabled &&
            (DAB_CONTROL_ARCH_ENERGY_SUPERVISOR == s_control_architecture) &&
            g_dab_cpu0_dual_core_status.energy_reference_valid &&
            dab_validated_charger_zero_inner_for_taper(
                (uint8_t) g_dab_cpu0_dual_core_status.battery_profile,
                (uint8_t) g_dab_cpu0_dual_core_status.charge_stage);
        if (zero_inner_for_taper)
        {
            /* Preserve the effective-phase/current PI command while the
             * ordinary inner slew removes the Vin-specific transfer floor. */
            return;
        }
        if (s_charger_low_input_runtime && s_schedule_enabled &&
            (DAB_CONTROL_ARCH_ENERGY_SUPERVISOR == s_control_architecture))
        {
            bool const steady_partition =
                (DAB_STATE_RUN == s_state) &&
                (dab_state_elapsed_ms() >= DAB_CHARGER_LOW_RUN_SETTLE_MS);
            *p_primary_counts =
                (steady_partition ? DAB_CHARGER_LOW_RUN_PRIMARY_NS :
                                    DAB_CHARGER_LOW_START_PRIMARY_NS) /
                (float) DAB_PWM_COUNT_NS;
            *p_secondary_counts =
                (steady_partition ? DAB_CHARGER_LOW_RUN_SECONDARY_NS :
                                    DAB_CHARGER_LOW_START_SECONDARY_NS) /
                (float) DAB_PWM_COUNT_NS;
            return;
        }
        float const scheduled_primary_inner_ns =
            s_primary_inner_manual_target_counts * (float) DAB_PWM_COUNT_NS;
        float const transition_primary_inner_ns =
            dab_validated_charger_precharge_primary_inner_for_voltage(
                s_schedule_entry_id,
                s_measurements.vout_v,
                scheduled_primary_inner_ns);
        float const taper_primary_inner_ns =
            dab_validated_charger_primary_inner_for_taper(
                (uint8_t) g_dab_cpu0_dual_core_status.battery_profile,
                (uint8_t) g_dab_cpu0_dual_core_status.charge_stage,
                (uint8_t) s_modulation_requested,
                transition_primary_inner_ns);
        *p_primary_counts = taper_primary_inner_ns /
                            (float) DAB_PWM_COUNT_NS;
        *p_secondary_counts = s_secondary_inner_manual_target_counts;
        return;
    }

    bool const secondary_is_higher = (m >= 1.0F);
    float const low_over_high = secondary_is_higher ? (1.0F / m) : m;
    float const mismatch = dab_clampf(1.0F - low_over_high, 0.0F, 0.30F);
    float const target_ns = mismatch * (5000.0F - (8000.0F * mismatch));
    float const high_counts = dab_clampf(target_ns /
                                         (float) DAB_PWM_COUNT_NS,
                                         0.0F,
                                         (float) DAB_INNER_SHIFT_MAX_COUNTS);
    float const low_counts = (DAB_MODULATION_TPS == effective) ?
                             (0.25F * high_counts) : 0.0F;
    if (secondary_is_higher)
    {
        *p_primary_counts = low_counts;
        *p_secondary_counts = high_counts;
    }
    else
    {
        *p_primary_counts = high_counts;
        *p_secondary_counts = low_counts;
    }
}

static const char * dab_state_name(dab_state_t state)
{
    static const char * const names[] =
    {
        "BOOT", "READY", "RESET", "RELEASE", "MAIN",
        "OUTPUT", "SOFTSTART", "RUN", "PAUSED", "PATH", "TEST", "FAULT"
    };
    uint32_t const index = (uint32_t) state;
    return (index < (sizeof(names) / sizeof(names[0]))) ? names[index] : "?";
}

static void dab_enter_state(dab_state_t state)
{
    s_state      = state;
    s_state_tick = xTaskGetTickCount();
    dab_log("[DAB][STATE] %s\r\n", dab_state_name(state));
}

static uint32_t dab_state_elapsed_ms(void)
{
    return (uint32_t) ((xTaskGetTickCount() - s_state_tick) * portTICK_PERIOD_MS);
}

static bool dab_startup_hold_active(void)
{
    return (DAB_STATE_SOFTSTART == s_state) ||
           ((DAB_STATE_RUN == s_state) &&
            (dab_state_elapsed_ms() < DAB_STARTUP_PHASE_HOLD_MS));
}

static bool dab_startup_inner_slow_active(void)
{
    return (DAB_STATE_SOFTSTART == s_state) ||
           ((DAB_STATE_RUN == s_state) &&
            (dab_state_elapsed_ms() < DAB_STARTUP_INNER_SLOW_WINDOW_MS));
}

static uint32_t dab_startup_phase_limit_counts(void)
{
    uint32_t configured = s_closed_loop_phase_cap_counts;
    uint32_t const maximum = s_unlock_high_power ?
        DAB_PHASE_ABSOLUTE_MAX_COUNTS : DAB_PHASE_NORMAL_MAX_COUNTS;
    if (configured > maximum)
    {
        configured = maximum;
    }

    if ((DAB_STATE_SOFTSTART != s_state) && (DAB_STATE_RUN != s_state))
    {
        return configured;
    }

    /* A promoted autonomous charger bundle is already constrained by its
     * measured phase cap and current loop.  Let that trusted path use the
     * existing startup ramp after the unconditional low-power hold; otherwise
     * a high-ratio point can be stuck in SOFTSTART forever because RUN itself
     * requires 90 percent of Vref.  Manual and non-charger paths retain the
     * original SOFTSTART minimum-phase envelope. */
    bool const autonomous_charger_softstart =
        (DAB_STATE_SOFTSTART == s_state) &&
        s_schedule_autoload_enabled && s_schedule_enabled &&
        (DAB_CONTROL_ARCH_ENERGY_SUPERVISOR == s_control_architecture) &&
        s_energy_shadow.config.battery_start &&
        s_inner_manual_override &&
        ((DAB_MODULATION_EPS == s_modulation_requested) ||
         (DAB_MODULATION_TPS == s_modulation_requested));
    bool const autonomous_cp_softstart =
        (DAB_STATE_SOFTSTART == s_state) &&
        s_schedule_autoload_enabled && s_schedule_enabled &&
        (DAB_CONTROL_ARCH_CP_DAMPED_CASCADE == s_control_architecture) &&
        (s_modulation_requested <= DAB_MODULATION_TPS);

    uint32_t allowed = DAB_STARTUP_PHASE_MIN_COUNTS;
    if ((DAB_STATE_RUN == s_state) || autonomous_charger_softstart ||
        autonomous_cp_softstart)
    {
        uint32_t const elapsed_ms = dab_state_elapsed_ms();
        if (elapsed_ms > DAB_STARTUP_PHASE_HOLD_MS)
        {
            allowed += (elapsed_ms - DAB_STARTUP_PHASE_HOLD_MS) /
                       DAB_STARTUP_PHASE_STEP_INTERVAL_MS;
        }
    }
    return (configured < allowed) ? configured : allowed;
}

static bool dab_gate_driver_enable(void)
{
    dab_board_outputs_t outputs;

    dab_board_run_request_set(true);
    R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MICROSECONDS);
    dab_board_outputs_get(&outputs);
    if (!outputs.run_request)
    {
        dab_log("[DAB][FORCE] RUN_REQ P707/CN2.57 readback LOW, but MCU PWM will still run\r\n");
    }
    else
    {
        dab_log("[DAB][DRV] RUN_REQ P707/CN2.57=HIGH; expect DIS_BUS=LOW and UCC21520 enabled\r\n");
    }
    return true;
}

static bool dab_required_digital_faults_clear(void)
{
    if (!DAB_SOFTWARE_AUTO_TRIP_ENABLE)
    {
        return true;
    }

    dab_board_inputs_t inputs;
    dab_board_inputs_get(&inputs);

    if (!inputs.raw_fault_n)
    {
        return false;
    }
#if DAB_GPIO_OC_TRIP_ENABLE
    if (!inputs.ipri_oc_n || !inputs.isec_oc_n)
    {
        return false;
    }
#endif
    return true;
}

void uart_callback(uart_callback_args_t * p_args)
{
    if (NULL == p_args)
    {
        return;
    }

    if (UART_EVENT_TX_COMPLETE == p_args->event)
    {
        s_uart_tx_tail = (uint8_t) ((s_uart_tx_tail + 1U) &
                                    (UART_TX_QUEUE_DEPTH - 1U));
        s_uart_tx_busy = false;
    }
    else if (UART_EVENT_RX_CHAR == p_args->event)
    {
        uint8_t const next = (uint8_t) ((s_rx_head + 1U) &
                                        (UART_RX_RING_LENGTH - 1U));
        if (next != s_rx_tail)
        {
            s_rx_ring[s_rx_head] = (uint8_t) p_args->data;
            s_rx_head = next;
        }
    }
}

static void dab_uart_service(void)
{
    if (s_uart_tx_busy || (s_uart_tx_head == s_uart_tx_tail))
    {
        return;
    }

    char * p_message = s_uart_tx_queue[s_uart_tx_tail];
    s_uart_tx_busy = true;
    if (FSP_SUCCESS != R_SCI_B_UART_Write(&g_uart9_ctrl,
                                          (uint8_t const *) p_message,
                                          (uint32_t) strlen(p_message)))
    {
        s_uart_tx_busy = false;
    }
}

static int dab_uart_enqueue(char const * p_data, size_t length)
{
    uint8_t const next = (uint8_t) ((s_uart_tx_head + 1U) &
                                    (UART_TX_QUEUE_DEPTH - 1U));
    if (next == s_uart_tx_tail)
    {
        return 0;
    }
    if (length >= UART_TX_MESSAGE_LENGTH)
    {
        length = UART_TX_MESSAGE_LENGTH - 1U;
    }
    memcpy(s_uart_tx_queue[s_uart_tx_head], p_data, length);
    s_uart_tx_queue[s_uart_tx_head][length] = '\0';
    s_uart_tx_head = next;
    dab_uart_service();
    return (int) length;
}

int _write(int fd, char * p_buffer, int size)
{
    FSP_PARAMETER_NOT_USED(fd);
    if ((NULL == p_buffer) || (size <= 0))
    {
        return 0;
    }
    return dab_uart_enqueue(p_buffer, (size_t) size);
}

static void dab_log(const char * fmt, ...)
{
    uint8_t const next = (uint8_t) ((s_uart_tx_head + 1U) &
                                    (UART_TX_QUEUE_DEPTH - 1U));
    if (next == s_uart_tx_tail)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    int const length = vsnprintf(s_uart_tx_queue[s_uart_tx_head],
                                 UART_TX_MESSAGE_LENGTH, fmt, args);
    va_end(args);
    if (length < 0)
    {
        return;
    }
    s_uart_tx_queue[s_uart_tx_head][UART_TX_MESSAGE_LENGTH - 1U] = '\0';
    s_uart_tx_head = next;
    dab_uart_service();
}

static void dab_control_reset(void)
{
    s_pi_integral_counts = s_feedforward_enabled ?
        (s_feedforward_effective_phase_ns / (float) DAB_PWM_COUNT_NS) : 0.0F;
    s_current_pi_integral_counts = s_pi_integral_counts;
    s_current_reference_active_a = 0.0F;
    s_current_error_a = 0.0F;
    s_phase_command_counts = 0.0F;
    s_manual_phase_override = false;
    s_manual_phase_counts = 0U;
    s_previous_vout_v = 0.0F;
    s_vout_derivative_v_per_s = 0.0F;
    s_derivative_initialized = false;
    s_outer_saturated = false;
    s_outer_saturation_updates = 0U;
    dab_vin_adaptation_runtime_reset(&s_vin_adaptation);
    s_autotune.running = false;
    s_autotune.injection_ns = 0.0F;
    if ((DAB_AUTOTUNE_SETTLE == s_autotune.stage) ||
        (DAB_AUTOTUNE_POSITIVE_STEP == s_autotune.stage) ||
        (DAB_AUTOTUNE_NEGATIVE_STEP == s_autotune.stage))
    {
        s_autotune.stage = DAB_AUTOTUNE_FAILED;
        s_autotune.result.failure_reason =
            DAB_AUTOTUNE_FAILURE_CONTROL_RESET;
    }
    if ((DAB_MODSWITCH_TRANSITION == s_modswitch.stage) ||
        (DAB_MODSWITCH_VERIFY == s_modswitch.stage) ||
        (DAB_MODSWITCH_ROLLBACK == s_modswitch.stage))
    {
        s_modswitch.stage = DAB_MODSWITCH_FAILED;
        s_modswitch.failure_reason = DAB_MODSWITCH_FAILURE_CONTROL_RESET;
    }
    dab_cp_damping_runtime_reset(&s_cp_damping);
    s_primary_inner_command_counts = 0.0F;
    s_secondary_inner_command_counts = 0.0F;
    s_modulation_effective = DAB_MODULATION_SPS;
    s_pwm_update_miss_count = 0U;
    (void) dab_pwm_hw_modulation_set(0U, 0U, 0U);
}

static bool dab_control_prearm_autonomous_charger(void)
{
    bool const promoted_inner_bundle = s_schedule_autoload_enabled &&
        s_schedule_enabled &&
        (DAB_CONTROL_ARCH_ENERGY_SUPERVISOR == s_control_architecture) &&
        s_inner_manual_override &&
        ((DAB_MODULATION_EPS == s_modulation_requested) ||
         (DAB_MODULATION_TPS == s_modulation_requested));
    if (!promoted_inner_bundle)
    {
        return true;
    }

    bool const taper_requires_zero_inner =
        g_dab_cpu0_dual_core_status.energy_reference_valid &&
        dab_validated_charger_zero_inner_for_taper(
            (uint8_t) g_dab_cpu0_dual_core_status.battery_profile,
            (uint8_t) g_dab_cpu0_dual_core_status.charge_stage);
    if (taper_requires_zero_inner)
    {
        return true;
    }

    bool const special_precharge_entry =
        (DAB_VALIDATED_LIION_PRECHARGE_ENTRY_ID == s_schedule_entry_id) ||
        (DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID == s_schedule_entry_id) ||
        (DAB_VALIDATED_LFP_MPPT_ULTRALOW_ENTRY_ID == s_schedule_entry_id);
    uint32_t const maximum_primary_inner_counts =
        (s_unlock_high_power && special_precharge_entry) ?
        DAB_PRECHARGE_PRIMARY_INNER_MAX_COUNTS :
        (s_unlock_high_power ? DAB_INNER_SHIFT_MAX_COUNTS :
                               DAB_INNER_NORMAL_MAX_COUNTS);
    uint32_t const maximum_secondary_inner_counts = s_unlock_high_power ?
        DAB_INNER_SHIFT_MAX_COUNTS : DAB_INNER_NORMAL_MAX_COUNTS;
    uint32_t const primary_inner = (uint32_t) (dab_clampf(
        s_primary_inner_manual_target_counts, 0.0F,
        (float) maximum_primary_inner_counts) + 0.5F);
    uint32_t const secondary_inner = (uint32_t) (dab_clampf(
        s_secondary_inner_manual_target_counts, 0.0F,
        (float) maximum_secondary_inner_counts) + 0.5F);
    int32_t const effective_bias_counts =
        dab_charger_effective_bias_counts();
    uint32_t const raw_phase = dab_raw_phase_from_signed_effective(
        effective_bias_counts, primary_inner, secondary_inner);

    s_phase_command_counts = 0.0F;
    s_primary_inner_command_counts = (float) primary_inner;
    s_secondary_inner_command_counts = (float) secondary_inner;
    s_modulation_effective = s_modulation_requested;
    if (!dab_pwm_hw_modulation_set(raw_phase, primary_inner,
                                   secondary_inner))
    {
        dab_log("[DAB][CHGPREARM][BLOCK] promoted bundle could not be loaded while PWM was OFF\r\n");
        return false;
    }

    dab_log("[DAB][CHGPREARM] entry=%lu mod=%s raw=%luns inner=%lu/%luns effective=%ldns PWM=0; trusted bundle loaded before gate enable\r\n",
            (unsigned long) s_schedule_entry_id,
            dab_modulation_name(s_modulation_requested),
            (unsigned long) (raw_phase * DAB_PWM_COUNT_NS),
            (unsigned long) (primary_inner * DAB_PWM_COUNT_NS),
            (unsigned long) (secondary_inner * DAB_PWM_COUNT_NS),
            (long) (effective_bias_counts * (int32_t) DAB_PWM_COUNT_NS));
    return true;
}

static DAB_ITCM_CODE void dab_force_all_safe(void)
{
    /* The first write disables the gate-driver chain. */
    dab_board_run_request_set(false);
    dab_pwm_hw_outputs_disable();
    dab_board_force_path_safe();
    dab_board_fan_set(false);
    s_control_enabled = false;
    s_primary_diag_running = false;
    dab_control_reset();
}

static DAB_ITCM_CODE void dab_fault_latch(uint32_t fault_mask)
{
    if (0U == fault_mask)
    {
        return;
    }
    if (!DAB_SOFTWARE_AUTO_TRIP_ENABLE)
    {
        return;
    }
    s_fault_mask |= fault_mask;
    dab_force_all_safe();
    dab_enter_state(DAB_STATE_FAULT);
    dab_log("[DAB][TRIP] mask=0x%08lX; power OFF; use status, fix cause, then clear\r\n",
            (unsigned long) s_fault_mask);
    s_force_telemetry = true;
}

static bool dab_measurements_refresh_from_adc(void)
{
    dab_adc_frame_t frame;
    if (!dab_adc_dma_latest(&frame) ||
        !dab_measurements_update(&frame, &s_measurements))
    {
        return false;
    }

    s_measurements_ready = true;
    s_last_adc_tick = xTaskGetTickCount();
    if (s_measurements.valid)
    {
        s_last_valid_adc_tick = s_last_adc_tick;
    }
    return true;
}

static bool dab_current_zero_calibrate(void)
{
    dab_current_zero_t sum = {0};
    dab_adc_frame_t frame;
    uint32_t samples = 0U;
    uint32_t last_sequence = UINT32_MAX;
    TickType_t const deadline = xTaskGetTickCount() +
                                pdMS_TO_TICKS(DAB_CURRENT_ZERO_TIMEOUT_MS);

    while ((samples < DAB_CURRENT_ZERO_FRAMES) &&
           ((int32_t) (deadline - xTaskGetTickCount()) > 0))
    {
        dab_adc_dma_service();
        if (dab_adc_dma_latest(&frame) &&
            (frame.sequence != last_sequence) &&
            (0U == frame.invalid_mask))
        {
            last_sequence = frame.sequence;
            sum.iin_v  += dab_measurements_raw_to_volts(frame.raw[DAB_ADC_IIN]);
            sum.iout_v += dab_measurements_raw_to_volts(frame.raw[DAB_ADC_IOUT]);
            sum.ipri_v += dab_measurements_raw_to_volts(frame.raw[DAB_ADC_IPRI]);
            sum.isec_v += dab_measurements_raw_to_volts(frame.raw[DAB_ADC_ISEC]);
            samples++;
        }
    }

    if (samples != DAB_CURRENT_ZERO_FRAMES)
    {
        dab_adc_dma_stats_t stats;
        dab_adc_dma_stats_get(&stats);
        dab_log("[DAB][ZERO] ACQ FAIL samples=%lu/%lu run=%u frames=%lu rem=%u "
                "reload=%lu late=%lu stage=%s\r\n",
                (unsigned long) samples,
                (unsigned long) DAB_CURRENT_ZERO_FRAMES,
                stats.running ? 1U : 0U,
                (unsigned long) stats.frame_count,
                (unsigned int) stats.dma_frames_remaining,
                (unsigned long) stats.batch_reload_count,
                (unsigned long) stats.late_reload_count,
                dab_adc_dma_last_error_stage());
        return false;
    }
    float const divisor = (float) samples;
    sum.iin_v  /= divisor;
    sum.iout_v /= divisor;
    sum.ipri_v /= divisor;
    sum.isec_v /= divisor;
    if (!dab_measurements_zero_set(&sum))
    {
        dab_log("[DAB][ZERO] FAIL IIN=%.3fV IOUT=%.3fV IPRI=%.3fV ISEC=%.3fV\r\n",
                (double) sum.iin_v, (double) sum.iout_v,
                (double) sum.ipri_v, (double) sum.isec_v);
        return false;
    }

    dab_measurements_initialize();
    s_measurements_ready = false;
    s_measurements.valid = false;
    dab_log("[DAB][ZERO] PASS IIN=%.3fV IOUT=%.3fV IPRI=%.3fV ISEC=%.3fV\r\n",
            (double) sum.iin_v, (double) sum.iout_v,
            (double) sum.ipri_v, (double) sum.isec_v);
    return true;
}

static bool dab_adc_and_pwm_initialize(void)
{
    fsp_err_t err = dab_pwm_hw_open();
    if (FSP_SUCCESS != err)
    {
        dab_log("[DAB][INIT] PWM/POEG open FAIL err=%ld\r\n", (long) err);
        return false;
    }

    err = dab_adc_dma_open();
    if (FSP_SUCCESS != err)
    {
        dab_log("[DAB][INIT] ADC-DMA FAIL stage=%s err=%ld\r\n",
                dab_adc_dma_last_error_stage(), (long) err);
        return false;
    }
    err = dab_adc_dma_bind_pwm(&g_pwm_pri_a_ctrl, DAB_ADC_TRIGGER_COUNTS);
    if (FSP_SUCCESS != err)
    {
        dab_log("[DAB][INIT] ADC trigger bind FAIL err=%ld\r\n", (long) err);
        return false;
    }
    err = dab_adc_dma_start();
    if (FSP_SUCCESS != err)
    {
        dab_log("[DAB][INIT] ADC scan start FAIL err=%ld\r\n", (long) err);
        return false;
    }
    err = dab_pwm_hw_clock_start();
    if (FSP_SUCCESS != err)
    {
        dab_log("[DAB][INIT] safe GPT clock start FAIL err=%ld\r\n", (long) err);
        return false;
    }

    /* The isolated secondary current-sense bias rises more slowly than the
     * MCU and ADC.  Keep every power output OFF while ADC+DMA continue to run,
     * then calibrate only after the analog chain has settled. */
    dab_log("[DAB][ZERO] wait %lums for current-sense bias; PWM/RUN/four MOS OFF\r\n",
            (unsigned long) DAB_CURRENT_ZERO_SETTLE_MS);
    TickType_t const zero_settle_started = xTaskGetTickCount();
    while ((xTaskGetTickCount() - zero_settle_started) <
           pdMS_TO_TICKS(DAB_CURRENT_ZERO_SETTLE_MS))
    {
        dab_adc_dma_service();
        vTaskDelay(pdMS_TO_TICKS(1UL));
    }

    if (!dab_current_zero_calibrate())
    {
        return false;
    }

    TickType_t const measurement_deadline = xTaskGetTickCount() +
                                             pdMS_TO_TICKS(20UL);
    while (!dab_measurements_refresh_from_adc() &&
           ((int32_t) (measurement_deadline - xTaskGetTickCount()) > 0))
    {
        dab_adc_dma_service();
        taskYIELD();
    }
    if (!s_measurements_ready)
    {
        dab_log("[DAB][INIT] ADC engineering conversion timeout\r\n");
        return false;
    }
    s_last_control_sequence = s_measurements.sequence;
    return true;
}

static float dab_ipri_soft_limit(void)
{
    return DAB_IPRI_SOFT_BASE_A +
           (DAB_IPRI_SOFT_PER_W_A * s_power_limit_w);
}

static float dab_isec_soft_limit(void)
{
    return DAB_ISEC_SOFT_BASE_A +
           (DAB_ISEC_SOFT_PER_W_A * s_power_limit_w);
}

static float dab_iin_hard_limit(void)
{
    float const denominator = dab_maxf(s_measurements.vin_v, DAB_VIN_START_MIN_V);
    float const calculated = ((s_power_limit_w / denominator) *
                              DAB_BUS_CURRENT_MARGIN) +
                             DAB_BUS_CURRENT_EXTRA_A;
    return dab_minf(calculated, DAB_IIN_ABSOLUTE_HARD_A);
}

static float dab_iout_hard_limit(void)
{
    float const denominator = dab_maxf(s_measurements.vout_v, DAB_VREF_MIN_V);
    float const calculated = ((s_power_limit_w / denominator) *
                              DAB_BUS_CURRENT_MARGIN) +
                             DAB_BUS_CURRENT_EXTRA_A;
    return dab_minf(calculated, DAB_IOUT_ABSOLUTE_HARD_A);
}

static DAB_ITCM_CODE uint32_t dab_detect_run_faults(void)
{
    if (!DAB_SOFTWARE_AUTO_TRIP_ENABLE)
    {
        return DAB_FAULT_NONE;
    }

    uint32_t faults = DAB_FAULT_NONE;
    dab_board_inputs_t inputs;
    dab_board_inputs_get(&inputs);
    if (!inputs.raw_fault_n)
    {
        faults |= DAB_FAULT_RAW_N;
    }
#if DAB_GPIO_OC_TRIP_ENABLE
    if (!inputs.ipri_oc_n)
    {
        faults |= DAB_FAULT_IPRI_OC_N;
    }
    if (!inputs.isec_oc_n)
    {
        faults |= DAB_FAULT_ISEC_OC_N;
    }
#endif
    if (!inputs.pri_hw_ok)
    {
        faults |= DAB_FAULT_PRI_HW;
    }
    if (!inputs.sec_hw_ok)
    {
        faults |= DAB_FAULT_SEC_HW;
    }
    if (!inputs.temp_amb_ot_n)
    {
        faults |= DAB_FAULT_AMBIENT_OT;
    }
    if (!dab_pwm_hw_poeg_ok())
    {
        faults |= DAB_FAULT_POEG;
    }
    if (!s_measurements.valid &&
        ((xTaskGetTickCount() - s_last_valid_adc_tick) >
         pdMS_TO_TICKS(DAB_ADC_STALE_TIMEOUT_MS)))
    {
        faults |= DAB_FAULT_ADC;
    }
    if ((xTaskGetTickCount() - s_last_adc_tick) >
        pdMS_TO_TICKS(DAB_ADC_STALE_TIMEOUT_MS))
    {
        faults |= DAB_FAULT_ADC_STALE;
    }
    if (!s_measurements.valid)
    {
        return faults;
    }
    if (DAB_VIN_UV_TRIP_ENABLE &&
        (s_measurements.vin_v < DAB_VIN_RUN_MIN_V))
    {
        faults |= DAB_FAULT_VIN_UV;
    }
    if (s_measurements.vin_v > DAB_VIN_MAX_V)
    {
        faults |= DAB_FAULT_VIN_OV;
    }

    float const vout_trip = (DAB_GUARD_PROFILE_BENCH == s_guard_profile) ?
        DAB_VOUT_ABSOLUTE_MAX_V :
        dab_minf(DAB_VOUT_ABSOLUTE_MAX_V, s_vref_v + 4.0F);
    if (s_measurements.vout_v > vout_trip)
    {
        faults |= DAB_FAULT_VOUT_OV;
    }

    if (DAB_ADC_CURRENT_GUARDS_ENABLE)
    {
        float const ipri_hard = (DAB_GUARD_PROFILE_BENCH == s_guard_profile) ?
            DAB_IPRI_ABSOLUTE_HARD_A :
            dab_minf(dab_ipri_soft_limit() + DAB_IPRI_HARD_MARGIN_A,
                     DAB_IPRI_ABSOLUTE_HARD_A);
        float const isec_hard = (DAB_GUARD_PROFILE_BENCH == s_guard_profile) ?
            DAB_ISEC_ABSOLUTE_HARD_A :
            dab_minf(dab_isec_soft_limit() + DAB_ISEC_HARD_MARGIN_A,
                     DAB_ISEC_ABSOLUTE_HARD_A);
        if (dab_measurements_abs(s_measurements.ipri_a) > ipri_hard)
        {
            faults |= DAB_FAULT_IPRI;
        }
        if (dab_measurements_abs(s_measurements.isec_a) > isec_hard)
        {
            faults |= DAB_FAULT_ISEC;
        }
        float const iin_hard = (DAB_GUARD_PROFILE_BENCH == s_guard_profile) ?
            DAB_IIN_ABSOLUTE_HARD_A : dab_iin_hard_limit();
        float const iout_hard = (DAB_GUARD_PROFILE_BENCH == s_guard_profile) ?
            DAB_IOUT_ABSOLUTE_HARD_A : dab_iout_hard_limit();
        if (dab_measurements_abs(s_measurements.iin_a) > iin_hard)
        {
            faults |= DAB_FAULT_IIN;
        }
        if (dab_measurements_abs(s_measurements.iout_a) > iout_hard)
        {
            faults |= DAB_FAULT_IOUT;
        }
    }
    if ((s_measurements.temp_pri_c > DAB_TEMPERATURE_MAX_C) ||
        (s_measurements.temp_sec_c > DAB_TEMPERATURE_MAX_C))
    {
        faults |= DAB_FAULT_TEMP;
    }
    if ((DAB_GUARD_PROFILE_NORMAL == s_guard_profile) &&
        DAB_ADC_CURRENT_GUARDS_ENABLE &&
        ((dab_measurements_abs(s_measurements.pin_w) >
          (s_power_limit_w * DAB_POWER_TRIP_MULTIPLIER)) ||
         (dab_measurements_abs(s_measurements.pout_w) >
          (s_power_limit_w * DAB_POWER_TRIP_MULTIPLIER))))
    {
        faults |= DAB_FAULT_POWER;
    }
    return faults;
}

static uint32_t dab_cold_phase_limit(void)
{
    /* The startup phase envelope is unconditional, including BENCH and
     * manual-stop builds.  The optional cold/protection limits below may only
     * reduce this value further. */
    uint32_t configured = dab_startup_phase_limit_counts();

    if (!DAB_SOFTWARE_AUTO_TRIP_ENABLE ||
        (DAB_GUARD_PROFILE_BENCH == s_guard_profile))
    {
        return configured;
    }

    if (s_measurements.vout_v < DAB_COLD_VOUT_1_V)
    {
        return (configured < DAB_COLD_PHASE_1_COUNTS) ?
               configured : DAB_COLD_PHASE_1_COUNTS;
    }
    if (s_measurements.vout_v < DAB_COLD_VOUT_2_V)
    {
        return (configured < DAB_COLD_PHASE_2_COUNTS) ?
               configured : DAB_COLD_PHASE_2_COUNTS;
    }
    if (s_measurements.vout_v < DAB_COLD_VOUT_3_V)
    {
        return (configured < DAB_COLD_PHASE_3_COUNTS) ?
               configured : DAB_COLD_PHASE_3_COUNTS;
    }
    return configured;
}

static DAB_ITCM_CODE void dab_control_update(void)
{
    if (DAB_SOFTWARE_AUTO_TRIP_ENABLE)
    {
        uint32_t const faults = dab_detect_run_faults();
        if (DAB_FAULT_NONE != faults)
        {
            dab_fault_latch(faults);
            return;
        }
    }
    if (DAB_SOFTWARE_AUTO_TRIP_ENABLE && !s_measurements.valid)
    {
        return;
    }

    float const error_v = s_vref_v - s_measurements.vout_v;
    float const kp_counts_per_v = s_kp_ns_per_v / 4.0F;
    float const ki_counts_per_vs = s_ki_us_per_vs * 250.0F;
    float const kd_counts_s_per_v = s_kd_ns_s_per_v / 4.0F;
    uint32_t const phase_limit_u32 = dab_cold_phase_limit();
    /* Manual phase commands are also constrained during every ON startup. */
    float const phase_limit = (float) phase_limit_u32;

    float integral_candidate = (DAB_CONTROL_ARCH_VOLTAGE_PI_SINGLE ==
                                s_control_architecture) ?
                               s_pi_integral_counts :
                               s_current_pi_integral_counts;
    float target = (float) s_manual_phase_counts;
    if (!s_manual_phase_override)
    {
        if (DAB_CONTROL_ARCH_VOLTAGE_PI_SINGLE == s_control_architecture)
        {
            if (!s_derivative_initialized)
            {
                s_previous_vout_v = s_measurements.vout_v;
                s_vout_derivative_v_per_s = 0.0F;
                s_derivative_initialized = true;
            }
            else
            {
                float const raw_derivative =
                    (s_measurements.vout_v - s_previous_vout_v) /
                    DAB_CONTROL_DT_S;
                s_previous_vout_v = s_measurements.vout_v;
                s_vout_derivative_v_per_s +=
                    DAB_DERIVATIVE_FILTER_ALPHA *
                    (raw_derivative - s_vout_derivative_v_per_s);
            }
            integral_candidate += error_v * ki_counts_per_vs * DAB_CONTROL_DT_S;
            integral_candidate = dab_clampf(integral_candidate, 0.0F, phase_limit);
            target = (kp_counts_per_v * error_v) + integral_candidate -
                     (kd_counts_s_per_v * s_vout_derivative_v_per_s);
        }
        else
        {
            float requested_current = s_current_reference_command_a;
            if (DAB_CONTROL_ARCH_ENERGY_SUPERVISOR == s_control_architecture)
            {
                requested_current =
                    g_dab_cpu0_dual_core_status.energy_reference_valid ?
                    g_dab_cpu0_dual_core_status.energy_current_reference_a : 0.0F;
            }
            else if (DAB_CONTROL_ARCH_CP_DAMPED_CASCADE ==
                     s_control_architecture)
            {
                if (!s_measurements.valid)
                {
                    dab_cp_damping_runtime_reset(&s_cp_damping);
                    requested_current = 0.0F;
                }
                else
                {
                    requested_current = dab_cp_damping_update(
                        &s_cp_damping,
                        s_measurements.vout_v,
                        s_measurements.pout_w,
                        s_power_limit_w,
                        DAB_CONTROL_DT_S);
                }
            }
            requested_current = dab_clampf(requested_current, 0.0F,
                                           DAB_CURRENT_REFERENCE_MAX_A);
            float const current_slew_step =
                s_current_reference_slew_a_per_s * DAB_CONTROL_DT_S;
            s_current_reference_active_a = dab_slew(
                s_current_reference_active_a, requested_current,
                current_slew_step);

            dab_autotune_update(&s_autotune,
                                s_measurements.iout_a,
                                DAB_CONTROL_DT_S);
            if ((DAB_AUTOTUNE_COMPLETE == s_autotune.stage) &&
                s_autotune.result.valid &&
                s_autotune.config.auto_apply &&
                !s_autotune.result.applied)
            {
                s_current_control_direction =
                    s_autotune.result.recommended_direction;
                s_current_kp_ns_per_a =
                    s_autotune.result.recommended_kp_ns_per_a;
                s_current_ki_us_per_as =
                    s_autotune.result.recommended_ki_us_per_as;
                s_current_reference_slew_a_per_s =
                    s_autotune.result.recommended_slew_a_per_s;
                s_closed_loop_phase_cap_counts = (uint32_t)
                    ((s_autotune.result.recommended_phase_cap_ns /
                      (float) DAB_PWM_COUNT_NS) + 0.5F);
                s_autotune.result.applied = true;
                dab_schedule_mark_manual();
                s_force_telemetry = true;
            }

            s_current_error_a = s_current_reference_active_a -
                                s_measurements.iout_a;
            bool const precharge_reference_still_ramping =
                dab_validated_charger_is_precharge_entry(
                    s_schedule_entry_id) &&
                (s_current_reference_active_a <
                 (0.95F * requested_current));
            float const current_control_error_a =
                precharge_reference_still_ramping ? 0.0F :
                (s_current_error_a * (float) s_current_control_direction);

            dab_vin_adaptation_input_t vin_adaptation_input =
            {
                .vin_v = s_measurements.vin_v,
                .requested_current_a = s_current_reference_active_a,
                .measured_current_a = s_measurements.iout_a,
                .measured_output_power_w = s_measurements.pout_w,
                .phase_command_counts = s_phase_command_counts,
                .phase_limit_counts = phase_limit,
                .base_kp_ns_per_a = s_current_kp_ns_per_a,
                .base_ki_us_per_as = s_current_ki_us_per_as,
                .output_power_ceiling_w = dab_clampf(
                    s_power_limit_w, DAB_POWER_DEFAULT_W,
                    DAB_POWER_ABSOLUTE_MAX_W),
                .control_dt_s = DAB_CONTROL_DT_S,
                .current_control_direction = s_current_control_direction,
                .current_control_active = true
            };
            dab_vin_adaptation_update(&s_vin_adaptation,
                                      &vin_adaptation_input);
            float const current_kp_counts_per_a =
                s_vin_adaptation.output.effective_kp_ns_per_a /
                (float) DAB_PWM_COUNT_NS;
            float const current_ki_counts_per_as =
                (s_autotune.running ? 0.0F :
                 s_vin_adaptation.output.effective_ki_us_per_as) * 250.0F;
            if (!s_autotune.running)
            {
                integral_candidate +=
                    s_vin_adaptation.output.feedforward_delta_counts;
                integral_candidate += current_control_error_a *
                    current_ki_counts_per_as * DAB_CONTROL_DT_S;
            }
            integral_candidate = dab_clampf(integral_candidate, 0.0F,
                                            phase_limit);
            if (s_autotune.running)
            {
                /* Identify the local plant slope around a frozen controller
                 * output.  Letting the existing proportional path react to
                 * the perturbation would measure a closed-loop attenuation
                 * and produce an invalid PI recommendation. */
                target = (s_autotune.anchor_phase_ns +
                          s_autotune.injection_ns) /
                         (float) DAB_PWM_COUNT_NS;
            }
            else
            {
                target = (current_kp_counts_per_a * current_control_error_a) +
                         integral_candidate;
            }
        }
        float const unclamped_target = target;
        s_outer_saturated = (unclamped_target <= 0.0F) ||
                            (unclamped_target >= phase_limit);
        if (s_outer_saturated && (s_outer_saturation_updates < UINT32_MAX))
        {
            s_outer_saturation_updates++;
        }
        target = dab_clampf(target, 0.0F, phase_limit);
    }
    else
    {
        s_outer_saturated = false;
    }

    bool const current_soft = (DAB_GUARD_PROFILE_NORMAL == s_guard_profile) &&
        DAB_ADC_CURRENT_GUARDS_ENABLE &&
        ((dab_measurements_abs(s_measurements.ipri_a) > dab_ipri_soft_limit()) ||
         (dab_measurements_abs(s_measurements.isec_a) > dab_isec_soft_limit()));
    bool const power_soft = (DAB_GUARD_PROFILE_NORMAL == s_guard_profile) &&
        DAB_ADC_CURRENT_GUARDS_ENABLE &&
        ((dab_measurements_abs(s_measurements.pin_w) >
          (s_power_limit_w * DAB_INPUT_POWER_SOFT_MULTIPLIER)) ||
         (dab_measurements_abs(s_measurements.pout_w) > s_power_limit_w));

    if (current_soft || power_soft)
    {
        target = dab_maxf(0.0F, s_phase_command_counts -
                         (float) DAB_PHASE_RETREAT_COUNTS);
        integral_candidate = dab_minf(integral_candidate, target);
        s_phase_command_counts = target;
    }
    else if (target > s_phase_command_counts)
    {
        s_phase_command_counts = dab_minf(target,
                                          s_phase_command_counts +
                                          DAB_PHASE_RISE_COUNTS_PER_UPDATE);
    }
    else
    {
        s_phase_command_counts = dab_maxf(target,
                                          s_phase_command_counts -
                                          DAB_PHASE_FALL_COUNTS_PER_UPDATE);
    }
    if (DAB_CONTROL_ARCH_VOLTAGE_PI_SINGLE == s_control_architecture)
    {
        s_pi_integral_counts = integral_candidate;
    }
    else
    {
        s_current_pi_integral_counts = integral_candidate;
    }
    s_phase_command_counts = dab_clampf(s_phase_command_counts, 0.0F,
                                        phase_limit);

    float primary_target = 0.0F;
    float secondary_target = 0.0F;
    dab_modulation_targets(&primary_target, &secondary_target);
    bool const modulation_transition_active =
        (DAB_MODSWITCH_TRANSITION == s_modswitch.stage) ||
        (DAB_MODSWITCH_VERIFY == s_modswitch.stage) ||
        (DAB_MODSWITCH_ROLLBACK == s_modswitch.stage);
    float const ordinary_inner_slew_ns_per_ms =
        s_mppt_phase_search.output.active ?
        DAB_MPPT_PHASE_SEARCH_INNER_SLEW : DAB_INNER_SLEW_NS_PER_MS;
    float const inner_slew_ns_per_ms = dab_startup_inner_slow_active() ?
        DAB_STARTUP_INNER_SLEW_NS_PER_MS :
        (modulation_transition_active ?
         s_modswitch.config.inner_slew_ns_per_ms :
         ordinary_inner_slew_ns_per_ms);
    float const inner_step = (inner_slew_ns_per_ms *
                              DAB_CONTROL_DT_S * 1000.0F) /
                             (float) DAB_PWM_COUNT_NS;
    s_primary_inner_command_counts = dab_slew(
        s_primary_inner_command_counts, primary_target, inner_step);
    s_secondary_inner_command_counts = dab_slew(
        s_secondary_inner_command_counts, secondary_target, inner_step);

    float const switch_metric =
        (DAB_CONTROL_ARCH_VOLTAGE_PI_SINGLE == s_control_architecture) ?
        s_measurements.pout_w : s_measurements.iout_a;
    dab_modswitch_update(&s_modswitch,
        s_primary_inner_command_counts * (float) DAB_PWM_COUNT_NS,
        s_secondary_inner_command_counts * (float) DAB_PWM_COUNT_NS,
        switch_metric,
        DAB_CONTROL_DT_S);
    if ((DAB_MODSWITCH_ROLLBACK == s_modswitch.stage) &&
        s_modswitch.rollback_requested)
    {
        s_modulation_requested =
            (dab_modulation_mode_t) s_modswitch.source_mode;
        s_current_control_direction = s_modswitch.source_direction;
        if (DAB_MODULATION_SPS == s_modulation_requested)
        {
            s_inner_manual_override = false;
            s_primary_inner_manual_target_counts = 0.0F;
            s_secondary_inner_manual_target_counts = 0.0F;
        }
        else
        {
            s_inner_manual_override = true;
            s_primary_inner_manual_target_counts =
                s_modswitch_source_primary_inner_counts;
            s_secondary_inner_manual_target_counts =
                s_modswitch_source_secondary_inner_counts;
        }
        s_modswitch.rollback_requested = false;
    }
    if (DAB_MODSWITCH_ROLLBACK == s_modswitch.stage)
    {
        bool const rollback_reached =
            (dab_measurements_abs(s_primary_inner_command_counts -
                                  s_modswitch_source_primary_inner_counts) <=
             1.0F) &&
            (dab_measurements_abs(s_secondary_inner_command_counts -
                                  s_modswitch_source_secondary_inner_counts) <=
             1.0F);
        if (rollback_reached)
        {
            s_modswitch.stage = DAB_MODSWITCH_FAILED;
            s_modswitch_completion_reported = true;
            s_force_telemetry = true;
        }
    }
    else if ((DAB_MODSWITCH_COMPLETE == s_modswitch.stage) &&
             !s_modswitch_completion_reported)
    {
        s_modswitch_completion_reported = true;
        s_force_telemetry = true;
    }

    int32_t const requested_effective =
        (int32_t) (s_phase_command_counts + 0.5F) +
        dab_charger_effective_bias_counts();
    uint32_t const primary_inner =
        (uint32_t) (s_primary_inner_command_counts + 0.5F);
    uint32_t const secondary_inner =
        (uint32_t) (s_secondary_inner_command_counts + 0.5F);
    uint32_t const requested_raw =
        dab_raw_phase_from_signed_effective(requested_effective,
                                            primary_inner,
                                            secondary_inner);
    if ((requested_raw != dab_pwm_hw_phase_get()) ||
        (primary_inner != dab_pwm_hw_primary_inner_get()) ||
        (secondary_inner != dab_pwm_hw_secondary_inner_get()))
    {
        if (!dab_pwm_hw_modulation_set(requested_raw,
                                       primary_inner,
                                       secondary_inner))
        {
            if (s_pwm_update_miss_count < UINT32_MAX)
            {
                s_pwm_update_miss_count++;
            }
            if (s_pwm_update_miss_count > 100U)
            {
                if (DAB_SOFTWARE_AUTO_TRIP_ENABLE)
                {
                    dab_fault_latch(DAB_FAULT_PWM_UPDATE);
                }
                else if (101U == s_pwm_update_miss_count)
                {
                    dab_log("[DAB][WARN] PWM update missed >100 times; retaining last applied phase; manual-stop mode remains ON\r\n");
                }
            }
        }
        else
        {
            s_pwm_update_miss_count = 0U;
        }
    }

    /* A low-terminal PRECHARGE point is intentionally below 0.90*Vref and
     * must not depend on a connection overshoot to leave SOFTSTART.  A fully
     * bounded autonomous charger bundle is pre-armed with PWM off and then
     * held through the existing slow-inner startup window, so completion of
     * that window is the deterministic RUN criterion for this path.  Manual,
     * SPS and non-charger paths retain the original voltage-rise criterion. */
    bool const autonomous_charger_startup_complete =
        s_schedule_autoload_enabled && s_schedule_enabled &&
        (DAB_CONTROL_ARCH_ENERGY_SUPERVISOR == s_control_architecture) &&
        s_energy_shadow.config.battery_start && s_inner_manual_override &&
        ((DAB_MODULATION_EPS == s_modulation_requested) ||
         (DAB_MODULATION_TPS == s_modulation_requested)) &&
        (dab_state_elapsed_ms() >= DAB_STARTUP_INNER_SLOW_WINDOW_MS);
    bool const autonomous_cp_startup_complete =
        s_schedule_autoload_enabled && s_schedule_enabled &&
        (DAB_CONTROL_ARCH_CP_DAMPED_CASCADE == s_control_architecture) &&
        (s_modulation_requested <= DAB_MODULATION_TPS) &&
        (dab_state_elapsed_ms() >= DAB_STARTUP_INNER_SLOW_WINDOW_MS);
    if ((DAB_STATE_SOFTSTART == s_state) &&
        ((s_measurements.vout_v >= (0.90F * s_vref_v)) ||
         autonomous_charger_startup_complete ||
         autonomous_cp_startup_complete))
    {
        dab_enter_state(DAB_STATE_RUN);
        dab_log("[DAB][STARTUP] envelope: promoted autonomous EPS/TPS may be prearmed with effective phase 0 while PWM is OFF and ramp after hold; other paths use SPS/inner=0 hold=%lums; initial effective phase<=%luns then +%luns/%lums, inner slew=%.3fns/ms for %lums\r\n",
                (unsigned long) DAB_STARTUP_PHASE_HOLD_MS,
                (unsigned long) (DAB_STARTUP_PHASE_MIN_COUNTS *
                                 DAB_PWM_COUNT_NS),
                (unsigned long) DAB_PWM_COUNT_NS,
                (unsigned long) DAB_STARTUP_PHASE_STEP_INTERVAL_MS,
                (double) DAB_STARTUP_INNER_SLEW_NS_PER_MS,
                (unsigned long) DAB_STARTUP_INNER_SLOW_WINDOW_MS);
    }
    if ((DAB_GUARD_PROFILE_NORMAL == s_guard_profile) &&
        DAB_NO_RISE_TRIP_ENABLE &&
        (DAB_STATE_SOFTSTART == s_state) &&
        (dab_state_elapsed_ms() > DAB_NO_RISE_TIMEOUT_MS) &&
        (s_measurements.vout_v < DAB_COLD_VOUT_1_V))
    {
        dab_fault_latch(DAB_FAULT_NO_RISE);
    }
}

#if DAB_ALLOW_POWER_TRANSFER
static bool dab_start_preconditions_ok(void)
{
    if (!DAB_SOFTWARE_AUTO_TRIP_ENABLE)
    {
        return true;
    }

    if (!s_measurements_ready || !s_measurements.valid)
    {
        TickType_t const deadline = xTaskGetTickCount() +
                                    pdMS_TO_TICKS(DAB_START_ADC_WAIT_MS);
        do
        {
            dab_adc_dma_service();
            if (dab_measurements_refresh_from_adc() &&
                s_measurements.valid)
            {
                break;
            }
            taskYIELD();
        }
        while ((int32_t) (deadline - xTaskGetTickCount()) > 0);
    }

    dab_board_inputs_t inputs;
    dab_board_inputs_get(&inputs);
    if (!dab_required_digital_faults_clear() ||
        !inputs.pri_hw_ok || !inputs.sec_hw_ok || !inputs.temp_amb_ot_n ||
        !dab_pwm_hw_poeg_ok())
    {
        dab_log("[DAB][BLOCK] hardware fault/OK/POEG precondition not healthy\r\n");
        return false;
    }
    if (!s_measurements_ready || !s_measurements.valid)
    {
        dab_log("[DAB][BLOCK] ADC still invalid after %lums; send adcdiag\r\n",
                (unsigned long) DAB_START_ADC_WAIT_MS);
        return false;
    }
    if (s_measurements.vin_v > DAB_VIN_MAX_V)
    {
        dab_log("[DAB][BLOCK] post-switch Vin %.2fV exceeds %.1fV\r\n",
                (double) s_measurements.vin_v, (double) DAB_VIN_MAX_V);
        return false;
    }
    if (DAB_ADC_CURRENT_GUARDS_ENABLE &&
        ((dab_measurements_abs(s_measurements.ipri_a) > DAB_START_IPRI_MAX_A) ||
         (dab_measurements_abs(s_measurements.isec_a) > DAB_START_ISEC_MAX_A) ||
         (dab_measurements_abs(s_measurements.iin_a) > DAB_START_IIN_MAX_A) ||
         (dab_measurements_abs(s_measurements.iout_a) > DAB_START_IOUT_MAX_A)))
    {
        dab_log("[DAB][BLOCK] start-current sample too large IIN=%.2fA IOUT=%.2fA IPRI=%.2fA ISEC=%.2fA\r\n",
                (double) s_measurements.iin_a,
                (double) s_measurements.iout_a,
                (double) s_measurements.ipri_a,
                (double) s_measurements.isec_a);
        return false;
    }
    return true;
}

static void dab_path_direct_start(void)
{
    if (s_control_enabled || s_test_mode)
    {
        dab_log("[DAB][BLOCK] send off before forced mos on\r\n");
        return;
    }

    dab_force_all_safe();
    dab_log("[DAB][PATH] forced path-control test only; PWM remains OFF; on force-starts PWM without POEG/ADC gating\r\n");
    R_BSP_SoftwareDelay(DAB_LATCH_RESET_LOW_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);
    dab_board_protection_reset_set(true);
    R_BSP_SoftwareDelay(DAB_LATCH_RELEASE_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);

    dab_board_input_precharge_set(false);
    dab_board_input_main_set(true);
    dab_board_output_protection_set(true);
    dab_board_run_request_set(false);
    dab_pwm_hw_outputs_disable();

    dab_board_outputs_t outputs;
    dab_board_outputs_get(&outputs);
    dab_enter_state(DAB_STATE_PATH_DIRECT);
    dab_log("[DAB][PATH] ON MAIN(PB02/CN2.99)=%u OUT(P706/.45)=%u RESET_N(PB03/.47)=%u PRE=%u RUN=%u PWM=%u\r\n",
            outputs.input_main_en ? 1U : 0U,
            outputs.output_protection_en ? 1U : 0U,
            outputs.protection_reset_n ? 1U : 0U,
            outputs.input_precharge_en ? 1U : 0U,
            outputs.run_request ? 1U : 0U,
            dab_pwm_hw_outputs_are_enabled() ? 1U : 0U);
}
#endif

static void dab_state_service(void)
{
    bool const control_path =
        (DAB_STATE_RESET_LOW == s_state) ||
        (DAB_STATE_LATCH_RELEASE == s_state) ||
        (DAB_STATE_MAIN_SETTLE == s_state) ||
        (DAB_STATE_OUTPUT_SETTLE == s_state) ||
        (DAB_STATE_SOFTSTART == s_state) ||
        (DAB_STATE_RUN == s_state) ||
        (DAB_STATE_PAUSED == s_state);
    if (control_path && !s_control_enabled)
    {
        dab_force_all_safe();
        dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY : DAB_STATE_FAULT);
        return;
    }

    bool const energized_path =
        (DAB_STATE_MAIN_SETTLE == s_state) ||
        (DAB_STATE_OUTPUT_SETTLE == s_state) ||
        (DAB_STATE_PAUSED == s_state);
    if (DAB_SOFTWARE_AUTO_TRIP_ENABLE && energized_path)
    {
        uint32_t faults = DAB_FAULT_NONE;
        dab_board_inputs_t inputs;
        dab_board_inputs_get(&inputs);
        if (!inputs.raw_fault_n)
        {
            faults |= DAB_FAULT_RAW_N;
        }
#if DAB_GPIO_OC_TRIP_ENABLE
        if (!inputs.ipri_oc_n)
        {
            faults |= DAB_FAULT_IPRI_OC_N;
        }
        if (!inputs.isec_oc_n)
        {
            faults |= DAB_FAULT_ISEC_OC_N;
        }
#endif
        if (!inputs.pri_hw_ok)
        {
            faults |= DAB_FAULT_PRI_HW;
        }
        if (!inputs.sec_hw_ok)
        {
            faults |= DAB_FAULT_SEC_HW;
        }
        if (!inputs.temp_amb_ot_n)
        {
            faults |= DAB_FAULT_AMBIENT_OT;
        }
        if (!dab_pwm_hw_poeg_ok())
        {
            faults |= DAB_FAULT_POEG;
        }
        if (DAB_STATE_PATH_DIRECT != s_state)
        {
            if (!s_measurements.valid &&
                ((xTaskGetTickCount() - s_last_valid_adc_tick) >
                 pdMS_TO_TICKS(DAB_ADC_STALE_TIMEOUT_MS)))
            {
                faults |= DAB_FAULT_ADC;
            }
            if ((xTaskGetTickCount() - s_last_adc_tick) >
                pdMS_TO_TICKS(DAB_ADC_STALE_TIMEOUT_MS))
            {
                faults |= DAB_FAULT_ADC_STALE;
            }
            if (s_measurements.valid)
            {
                if (DAB_VIN_UV_TRIP_ENABLE &&
                    (DAB_STATE_MAIN_SETTLE != s_state) &&
                    (s_measurements.vin_v < DAB_VIN_RUN_MIN_V))
                {
                    faults |= DAB_FAULT_VIN_UV;
                }
                if (s_measurements.vin_v > DAB_VIN_MAX_V)
                {
                    faults |= DAB_FAULT_VIN_OV;
                }
                if (s_measurements.vout_v >
                    dab_minf(DAB_VOUT_ABSOLUTE_MAX_V, s_vref_v + 4.0F))
                {
                    faults |= DAB_FAULT_VOUT_OV;
                }
                if ((s_measurements.temp_pri_c > DAB_TEMPERATURE_MAX_C) ||
                    (s_measurements.temp_sec_c > DAB_TEMPERATURE_MAX_C))
                {
                    faults |= DAB_FAULT_TEMP;
                }
            }
        }
        if (DAB_FAULT_NONE != faults)
        {
            dab_fault_latch(faults);
            return;
        }
    }

    switch (s_state)
    {
        case DAB_STATE_RESET_LOW:
            if (dab_state_elapsed_ms() >= DAB_LATCH_RESET_LOW_MS)
            {
                dab_board_protection_reset_set(true);
                dab_enter_state(DAB_STATE_LATCH_RELEASE);
            }
            break;

        case DAB_STATE_LATCH_RELEASE:
            if (dab_state_elapsed_ms() >= DAB_LATCH_RELEASE_MS)
            {
                dab_board_input_main_set(true);
                dab_board_output_protection_set(true);
                dab_board_input_precharge_set(false);
                dab_enter_state(DAB_STATE_MAIN_SETTLE);
                dab_log("[DAB][PATH] DIRECT four MOS ON; PRE=0 RUN=0 PWM=0; settle=%lums\r\n",
                        (unsigned long) DAB_DIRECT_PATH_SETTLE_MS);
            }
            break;

        case DAB_STATE_MAIN_SETTLE:
            if (dab_state_elapsed_ms() >= DAB_DIRECT_PATH_SETTLE_MS)
            {
                if (DAB_SOFTWARE_AUTO_TRIP_ENABLE &&
                    (s_measurements.vin_v < DAB_VIN_START_MIN_V))
                {
                    dab_log("[DAB][BLOCK] direct path Vin %.2fV below %.1fV; all power OFF\r\n",
                            (double) s_measurements.vin_v,
                            (double) DAB_VIN_START_MIN_V);
                    dab_fault_latch(DAB_FAULT_VIN_UV);
                }
                else
                {
                    dab_enter_state(DAB_STATE_OUTPUT_SETTLE);
                }
            }
            break;

        case DAB_STATE_OUTPUT_SETTLE:
            if (dab_state_elapsed_ms() >= DAB_OUTPUT_SETTLE_MS)
            {
                dab_control_reset();
                if (!dab_control_prearm_autonomous_charger())
                {
                    dab_force_all_safe();
                    dab_enter_state(DAB_STATE_READY);
                    dab_log("[DAB][BLOCK] autonomous charger pre-arm failed before MCU PWM; all outputs/path OFF\r\n");
                    break;
                }
                /* Force-output commissioning build: RUN_REQ readback and all
                 * fault/POEG states are observational only. */
                (void) dab_gate_driver_enable();
                if (!dab_pwm_hw_outputs_enable())
                {
                    if (DAB_SOFTWARE_AUTO_TRIP_ENABLE)
                    {
                        dab_fault_latch(DAB_FAULT_RAW_N | DAB_FAULT_POEG);
                    }
                    else
                    {
                        dab_force_all_safe();
                        dab_enter_state(DAB_STATE_READY);
                        dab_log("[DAB][BLOCK] on aborted before MCU PWM; all outputs/path OFF\r\n");
                    }
                }
                else
                {
                    dab_enter_state(DAB_STATE_SOFTSTART);
                    dab_log("[DAB][PWM] FORCE Q1..Q8 ON; GPT synchronized; POEG/RAW/OC ignored; send off to stop\r\n");
                    dab_print_pwm_hw_diag();
                }
            }
            break;

        case DAB_STATE_PATH_DIRECT:
            break;

        default:
            break;
    }
}

static void dab_print_pins(void)
{
    dab_log("[DAB][PINS] DEBUG/J-Link UART9 RX=P208 TX=P209 115200-8N1\r\n");
    dab_log("[DAB][PINS] PWM PRI_A H=P415/CN2.83 L=P414/.81; PRI_B H=P105/.87 L=P104/.85\r\n");
    dab_log("[DAB][PINS] PWM SEC_A H=P915/CN2.35 L=P914/.37; SEC_B H=P605/.39 L=P604/.41\r\n");
    dab_log("[DAB][PINS] ADC IPRI=P000/.71 VIN=P001/.101 IIN=P002/.65 VAFE=P003/.69 TEMP_SEC=P004/.33\r\n");
    dab_log("[DAB][PINS] ADC ISEC=P006/.73 VOUT=P007/.51 IOUT=P008/.67 TEMP_PRI=P009/.91\r\n");
    dab_log("[DAB][PINS] SAFE RAW_N=P401/.59+.61 IPRI_OC_N=P400/.79 ISEC_OC_N=P403/.55 RUN=P707/.57\r\n");
    dab_log("[DAB][PINS] HW PRI_OK=PB06/.95 SEC_OK=PB01/.49 AMB_OT_N=PB05/.115 I2C SDA=P514/.117 SCL=P515/.119\r\n");
    dab_log("[DAB][PINS] CANFD0 TX=P704/.109 RX=P705/.107; PHY_RESET_N=P513/.22; RMII1 uses P206/P306/P307/P310/P312/P805/P806/P905/P906/P907\r\n");
}

static void dab_print_drivers(void)
{
    dab_can_transport_status_t can_status = {0};
    dab_can_transport_status_get(&can_status);
    dab_log("[DRV] ADC_B+DMAC0: AMC3330 VIN/VOUT, ACS725 IIN/IOUT, TMCS1133 IPRI/ISEC, ISOTMP35 PRI/SEC, VAFE_HALF\r\n");
    dab_log("[DRV] IIC2 P514/P515: TMP1075@0x48 %s ID=0x%04X ambient=%.2fC\r\n",
            s_tmp1075_ok ? "PASS" : "NOT-FOUND",
            s_tmp1075.device_id,
            (double) s_tmp1075.temperature_c);
    dab_log("[DRV] TPS48111/TPS48110/UCC21520/ISO7741/protection latches: GPIO+fault driver\r\n");
    dab_log("[DRV] %s; controller=%s open_err=%ld classic=500k ID RX/TX=0x200/0x180..0x182\r\n",
            tcan3413_board_note(),
            can_status.initialized ? "OPEN" : "OFFLINE",
            (long) can_status.open_error);
    dab_log("[DRV] RMAC1/RMII1 + LAN8720A PHY@0 + FreeRTOS+TCP static-IP test enabled\r\n");
    dab_log("[ACCEL] Helium-F32=%s ITCM=hot ADC/PWM/protection DTCM=CPU state DMA=non-cache SRAM\r\n",
            dab_measurements_helium_enabled() ? "ON" : "OFF");
}

static void dab_print_pwm_hw_diag(void)
{
    dab_pwm_hw_diag_t diag;
    dab_pwm_hw_diag_get(&diag);
    dab_log("[DAB][PWMHW] REQ=%u POEG=%u GPT_RUN=0x%02X GPT_MOVE=0x%02X PIN_HI=0x%02X PIN_LO=0x%02X CNT=%lu/%lu/%lu/%lu\r\n",
            diag.outputs_requested ? 1U : 0U,
            diag.poeg_ok ? 1U : 0U,
            (unsigned int) diag.counter_running_mask,
            (unsigned int) diag.counter_moving_mask,
            (unsigned int) diag.pin_seen_high_mask,
            (unsigned int) diag.pin_seen_low_mask,
            (unsigned long) diag.counter[0],
            (unsigned long) diag.counter[1],
            (unsigned long) diag.counter[2],
            (unsigned long) diag.counter[3]);
    dab_log("[DAB][PWMREG] GTIOR=%08lX/%08lX/%08lX/%08lX expected RUN/MOVE=0F PIN_HI/LO=FF; POEG is observe-only\r\n",
            (unsigned long) diag.gtior[0],
            (unsigned long) diag.gtior[1],
            (unsigned long) diag.gtior[2],
            (unsigned long) diag.gtior[3]);
}

static void dab_print_status(void)
{
    dab_board_inputs_t inputs;
    dab_board_outputs_t outputs;
    dab_board_inputs_get(&inputs);
    dab_board_outputs_get(&outputs);
    dab_log("[DAB] %s CTRL=%s Vin=%.2fV Vout=%.2fV Iin=%.2fA Iout=%.2fA Ipri=%.2fA Isec=%.2fA Pin=%.1fW Pout=%.1fW ph=%lu/%luns Plim=%.0fW fault=%08lX\r\n",
            dab_state_name(s_state),
            s_control_enabled ? "ON" : "OFF",
            (double) s_measurements.vin_v,
            (double) s_measurements.vout_v,
            (double) s_measurements.iin_a,
            (double) s_measurements.iout_a,
            (double) s_measurements.ipri_a,
            (double) s_measurements.isec_a,
            (double) s_measurements.pin_w,
            (double) s_measurements.pout_w,
            (unsigned long) dab_pwm_hw_phase_get(),
            (unsigned long) (dab_pwm_hw_phase_get() * 4UL),
            (double) s_power_limit_w,
            (unsigned long) s_fault_mask);
    dab_log("[DAB][IO] RAW=%u IPRI_OC=%u ISEC_OC=%u PRI_OK=%u SEC_OK=%u AMB_OT_N=%u PWM=%u RUNstate=%s Vref=%.1f Kp=%.1f Ki=%.2f Kd=%.4f valid=%u Tpri=%.1fC Tsec=%.1fC Pcmd=%u unlock400=%u seq=%lu\r\n",
            inputs.raw_fault_n ? 1U : 0U,
            inputs.ipri_oc_n ? 1U : 0U,
            inputs.isec_oc_n ? 1U : 0U,
            inputs.pri_hw_ok ? 1U : 0U,
            inputs.sec_hw_ok ? 1U : 0U,
            inputs.temp_amb_ot_n ? 1U : 0U,
            dab_pwm_hw_outputs_are_enabled() ? 1U : 0U,
            dab_state_name(s_state),
            (double) s_vref_v,
            (double) s_kp_ns_per_v,
            (double) s_ki_us_per_vs,
            (double) s_kd_ns_s_per_v,
            s_measurements.valid ? 1U : 0U,
            (double) s_measurements.temp_pri_c,
            (double) s_measurements.temp_sec_c,
            s_power_command_received ? 1U : 0U,
            s_unlock_high_power ? 1U : 0U,
            (unsigned long) s_measurements.sequence);
    dab_log("[DAB][PATH] PRE=%u MAIN=%u OUT=%u RESET_N=%u RUN_REQ=%u DIS_BUS_EXPECT=%s manual_off=1\r\n",
            outputs.input_precharge_en ? 1U : 0U,
            outputs.input_main_en ? 1U : 0U,
            outputs.output_protection_en ? 1U : 0U,
            outputs.protection_reset_n ? 1U : 0U,
            outputs.run_request ? 1U : 0U,
            outputs.run_request ? "LOW/ENABLE" : "HIGH/DISABLE");
    dab_print_pwm_hw_diag();
    float const m = (s_measurements.vin_v > 1.0F) ?
                    (s_measurements.vout_v /
                     (2.0F * s_measurements.vin_v)) : 0.0F;
    uint32_t const raw_phase = dab_pwm_hw_phase_get();
    uint32_t const primary_inner = dab_pwm_hw_primary_inner_get();
    uint32_t const secondary_inner = dab_pwm_hw_secondary_inner_get();
    int32_t const effective_phase_ns =
        dab_effective_phase_ns(raw_phase, primary_inner, secondary_inner);
    dab_log("[DAB][MOD] requested=%s effective=%s M=%.3f raw=%luns "
            "effective_phase=%ldns pri_inner=%luns sec_inner=%luns "
            "startup_limit=%luns phase_mode=%s set=%luns\r\n",
            dab_modulation_name(s_modulation_requested),
            dab_modulation_name(s_modulation_effective), (double) m,
            (unsigned long) (raw_phase * DAB_PWM_COUNT_NS),
            (long) effective_phase_ns,
            (unsigned long) (primary_inner * DAB_PWM_COUNT_NS),
            (unsigned long) (secondary_inner * DAB_PWM_COUNT_NS),
            (unsigned long) (dab_startup_phase_limit_counts() *
                             DAB_PWM_COUNT_NS),
            s_manual_phase_override ? "MANUAL" :
                ((s_kd_ns_s_per_v > 0.0F) ? "PID" : "PI"),
            (unsigned long) (s_manual_phase_counts * DAB_PWM_COUNT_NS));
    dab_print_parameter_bundle();
#if DAB_SOFTWARE_AUTO_TRIP_ENABLE
    dab_log("[DAB][GUARD] profile=%s MCU_AUTO=ON hard_absolute=RAW/OC/OK/ADC-stale/55V/26A-in/18A-out/temperature; bench disables cold/power/soft-current/no-rise interference\r\n",
            dab_guard_profile_name(s_guard_profile));
#else
    dab_log("[DAB][GUARD] profile=%s MCU_AUTO=OFF MANUAL_STOP; RAW/OC/OK/ADC/current/voltage/temperature remain observable only\r\n",
            dab_guard_profile_name(s_guard_profile));
#endif
    dab_print_energy_status();
}

static void dab_print_adc_diag(void)
{
    dab_adc_dma_stats_t stats;
    dab_adc_frame_t frame;

    dab_adc_dma_service();
    dab_adc_dma_stats_get(&stats);
    bool const latest = dab_adc_dma_latest(&frame);
    dab_log("[ADC][DMA] run=%u frames=%lu rem=%u reload=%lu late=%lu latest=%u stage=%s\r\n",
            stats.running ? 1U : 0U,
            (unsigned long) stats.frame_count,
            (unsigned int) stats.dma_frames_remaining,
            (unsigned long) stats.batch_reload_count,
            (unsigned long) stats.late_reload_count,
            latest ? 1U : 0U,
            dab_adc_dma_last_error_stage());

    if (!latest)
    {
        dab_log("[ADC][V] no complete frame; PWM outputs remain OFF\r\n");
        return;
    }

    uint32_t mv[DAB_ADC_CHANNEL_COUNT];
    for (uint32_t channel = 0U; channel < DAB_ADC_CHANNEL_COUNT; channel++)
    {
        mv[channel] = dab_adc_raw_to_mv(frame.raw[channel]);
    }
    dab_log("[ADC][V] P000=%lu.%03luV P001=%lu.%03luV P002=%lu.%03luV "
            "P003=%lu.%03luV P004=%lu.%03luV\r\n",
            (unsigned long) (mv[DAB_ADC_IPRI] / 1000UL),
            (unsigned long) (mv[DAB_ADC_IPRI] % 1000UL),
            (unsigned long) (mv[DAB_ADC_VIN] / 1000UL),
            (unsigned long) (mv[DAB_ADC_VIN] % 1000UL),
            (unsigned long) (mv[DAB_ADC_IIN] / 1000UL),
            (unsigned long) (mv[DAB_ADC_IIN] % 1000UL),
            (unsigned long) (mv[DAB_ADC_VAFE_HALF] / 1000UL),
            (unsigned long) (mv[DAB_ADC_VAFE_HALF] % 1000UL),
            (unsigned long) (mv[DAB_ADC_TEMP_SEC] / 1000UL),
            (unsigned long) (mv[DAB_ADC_TEMP_SEC] % 1000UL));
    dab_log("[ADC][V] P006=%lu.%03luV P007=%lu.%03luV P008=%lu.%03luV "
            "P009=%lu.%03luV invalid=0x%04X seq=%lu\r\n",
            (unsigned long) (mv[DAB_ADC_ISEC] / 1000UL),
            (unsigned long) (mv[DAB_ADC_ISEC] % 1000UL),
            (unsigned long) (mv[DAB_ADC_VOUT] / 1000UL),
            (unsigned long) (mv[DAB_ADC_VOUT] % 1000UL),
            (unsigned long) (mv[DAB_ADC_IOUT] / 1000UL),
            (unsigned long) (mv[DAB_ADC_IOUT] % 1000UL),
            (unsigned long) (mv[DAB_ADC_TEMP_PRI] / 1000UL),
            (unsigned long) (mv[DAB_ADC_TEMP_PRI] % 1000UL),
            (unsigned int) frame.invalid_mask,
            (unsigned long) frame.sequence);
}

static void dab_print_adc_crosscheck(void)
{
    static const dab_adc_channel_t channels[] =
    {
        DAB_ADC_VOUT,
        DAB_ADC_IOUT,
        DAB_ADC_TEMP_PRI,
    };
    static const char * const names[] =
    {
        "P007/VOUT",
        "P008/IOUT",
        "P009/TEMP_PRI",
    };
    uint32_t dma_sum[3] = {0U};
    uint32_t direct_sum[3] = {0U};
    uint32_t dma_invalid[3] = {0U};
    uint32_t direct_invalid[3] = {0U};
    uint32_t captured = 0U;

    dab_force_all_safe();
    dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY :
                                            DAB_STATE_FAULT);

    dab_board_outputs_t outputs;
    dab_board_outputs_get(&outputs);
    if (outputs.input_main_en || outputs.output_protection_en ||
        outputs.input_precharge_en || outputs.run_request ||
        dab_pwm_hw_outputs_are_enabled())
    {
        dab_log("[ADC][XCHK][BLOCK] requires PWM/RUN_REQ/power paths OFF\r\n");
        return;
    }

    for (uint32_t sample = 0U;
         sample < DAB_ADC_CROSSCHECK_SAMPLES;
         sample++)
    {
        dab_adc_frame_t frame;
        dab_adc_dma_service();
        if (!dab_adc_dma_latest(&frame))
        {
            R_BSP_SoftwareDelay(DAB_ADC_CROSSCHECK_INTERVAL_US,
                                BSP_DELAY_UNITS_MICROSECONDS);
            continue;
        }

        bool direct_ok = true;
        uint16_t direct_raw[3] = {0U};
        bool direct_bad[3] = {false};
        for (uint32_t index = 0U; index < 3U; index++)
        {
            if (!dab_adc_dma_direct_read(channels[index],
                                         &direct_raw[index],
                                         &direct_bad[index]))
            {
                direct_ok = false;
                break;
            }
        }
        if (!direct_ok)
        {
            break;
        }

        for (uint32_t index = 0U; index < 3U; index++)
        {
            dma_sum[index] += frame.raw[channels[index]];
            direct_sum[index] += direct_raw[index];
            if (0U != (frame.invalid_mask & (1U << channels[index])))
            {
                dma_invalid[index]++;
            }
            if (direct_bad[index])
            {
                direct_invalid[index]++;
            }
        }
        captured++;
        R_BSP_SoftwareDelay(DAB_ADC_CROSSCHECK_INTERVAL_US,
                            BSP_DELAY_UNITS_MICROSECONDS);
    }

    if (0U == captured)
    {
        dab_log("[ADC][XCHK] FAIL no paired DMA/direct samples; outputs remain OFF\r\n");
        return;
    }

    dab_log("[ADC][XCHK] paired=%lu/%lu interval=%luus PWM=0 RUN_REQ=0 paths=OFF\r\n",
            (unsigned long) captured,
            (unsigned long) DAB_ADC_CROSSCHECK_SAMPLES,
            (unsigned long) DAB_ADC_CROSSCHECK_INTERVAL_US);
    for (uint32_t index = 0U; index < 3U; index++)
    {
        uint32_t const dma_raw =
            (dma_sum[index] + (captured / 2U)) / captured;
        uint32_t const direct_raw =
            (direct_sum[index] + (captured / 2U)) / captured;
        uint32_t const dma_mv = dab_adc_raw_to_mv((uint16_t) dma_raw);
        uint32_t const direct_mv = dab_adc_raw_to_mv((uint16_t) direct_raw);
        int32_t const delta_mv = (int32_t) dma_mv - (int32_t) direct_mv;
        dab_log("[ADC][XCHK] %s DMA=%lu.%03luV/raw%lu DIRECT=%lu.%03luV/raw%lu delta=%ldmV invalid=%lu/%lu\r\n",
                names[index],
                (unsigned long) (dma_mv / 1000UL),
                (unsigned long) (dma_mv % 1000UL),
                (unsigned long) dma_raw,
                (unsigned long) (direct_mv / 1000UL),
                (unsigned long) (direct_mv % 1000UL),
                (unsigned long) direct_raw,
                (long) delta_mv,
                (unsigned long) dma_invalid[index],
                (unsigned long) direct_invalid[index]);
    }
}

static void dab_print_vofa(void)
{
    uint32_t const raw_phase = dab_pwm_hw_phase_get();
    uint32_t const primary_inner = dab_pwm_hw_primary_inner_get();
    uint32_t const secondary_inner = dab_pwm_hw_secondary_inner_get();
    int32_t const effective_phase_ns =
        dab_effective_phase_ns(raw_phase, primary_inner, secondary_inner);
    uint32_t const command_phase_ns =
        (uint32_t) ((s_phase_command_counts * (float) DAB_PWM_COUNT_NS) +
                    0.5F);

    dab_log("DAB:%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,"
            "%.2f,%.2f,%.3f,%lu,%.1f,%.1f,%u,%u,%u,%lu,%u,%u,%u,"
            "%ld,%lu,%lu,%lu,%u,%.2f,%.3f,%.4f,%.4f,%lu,%.2f,"
            "%.2f,%.1f,%u,%u,%lu,%u,%lu,%u,%.1f\r\n",
            (unsigned long) s_measurements.sequence,
            (double) s_measurements.vin_v,
            (double) s_measurements.vout_v,
            (double) s_measurements.iin_a,
            (double) s_measurements.iout_a,
            (double) s_measurements.ipri_a,
            (double) s_measurements.isec_a,
            (double) s_measurements.temp_pri_c,
            (double) s_measurements.temp_sec_c,
            (double) s_measurements.pin_w,
            (double) s_measurements.pout_w,
            (double) s_measurements.vafe_half_v,
            (unsigned long) (raw_phase * DAB_PWM_COUNT_NS),
            (double) s_power_limit_w,
            (double) s_vref_v,
            (unsigned int) s_state,
            s_control_enabled ? 1U : 0U,
            dab_pwm_hw_outputs_are_enabled() ? 1U : 0U,
            (unsigned long) s_fault_mask,
            s_measurements.valid ? 1U : 0U,
            (unsigned int) s_modulation_requested,
            (unsigned int) s_modulation_effective,
            (long) effective_phase_ns,
            (unsigned long) command_phase_ns,
            (unsigned long) (primary_inner * DAB_PWM_COUNT_NS),
            (unsigned long) (secondary_inner * DAB_PWM_COUNT_NS),
            s_inner_manual_override ? 1U : 0U,
            (double) s_kp_ns_per_v,
            (double) s_ki_us_per_vs,
            (double) s_kd_ns_s_per_v,
            (double) DAB_DERIVATIVE_FILTER_ALPHA,
            (unsigned long) (s_closed_loop_phase_cap_counts *
                             DAB_PWM_COUNT_NS),
            (double) s_bench_vin_set_v,
            (double) s_load_nominal_ohm,
            (double) s_requested_power_layer_w,
            (unsigned int) s_control_architecture,
            s_schedule_enabled ? 1U : 0U,
            (unsigned long) s_schedule_entry_id,
            s_outer_saturated ? 1U : 0U,
            (unsigned long) s_outer_saturation_updates,
            s_feedforward_enabled ? 1U : 0U,
            (double) s_feedforward_effective_phase_ns);

    if (!s_algorithm_logging_enabled)
    {
        return;
    }

    uint32_t energy_age_ms = UINT32_MAX;
    if (g_dab_cpu0_dual_core_status.energy_reference_updates > 0U)
    {
        uint32_t const now_ms = (uint32_t) xTaskGetTickCount() *
                                (uint32_t) portTICK_PERIOD_MS;
        energy_age_ms = now_ms -
            g_dab_cpu0_dual_core_status.energy_reference_last_ms;
    }
    dab_log("ALG:%lu,%u,%u,%.3f,%.3f,%.3f,%.3f,%.4f,%.3f,%u,%u,%lu,%.3f,%.1f,%.2f,%u,%u,%u,%.2f,%u,%.2f,%.3f,%u,%u,%.3f,%u,%u,%u,%lu,%lu,%d\r\n",
            (unsigned long) s_measurements.sequence,
            (unsigned int) s_control_architecture,
            (unsigned int) s_guard_profile,
            (double) s_current_reference_command_a,
            (double) s_current_reference_active_a,
            (double) s_current_error_a,
            (double) s_current_kp_ns_per_a,
            (double) s_current_ki_us_per_as,
            (double) s_current_reference_slew_a_per_s,
            (unsigned int) g_dab_cpu0_dual_core_status.energy_mode,
            g_dab_cpu0_dual_core_status.energy_reference_valid ? 1U : 0U,
            (unsigned long) energy_age_ms,
            (double) g_dab_cpu0_dual_core_status.energy_current_reference_a,
            (double) g_dab_cpu0_dual_core_status.mppt_power_target_w,
            (double) g_dab_cpu0_dual_core_status.mppt_voltage_target_v,
            g_dab_cpu0_dual_core_status.mppt_limited ? 1U : 0U,
            (unsigned int) g_dab_cpu0_dual_core_status.battery_profile,
            (unsigned int) g_dab_cpu0_dual_core_status.charge_stage,
            (double) g_dab_cpu0_dual_core_status.charge_voltage_target_v,
            g_dab_cpu0_dual_core_status.charger_limited ? 1U : 0U,
            (double) g_dab_cpu0_dual_core_status.ai_hint_current_a,
            (double) g_dab_cpu0_dual_core_status.ai_confidence,
            g_dab_cpu0_dual_core_status.ai_applied ? 1U : 0U,
            (unsigned int) g_dab_cpu0_dual_core_status.ai_modulation_hint,
            (double) g_dab_cpu0_dual_core_status.ai_modulation_confidence,
            g_dab_cpu0_dual_core_status.ai_modulation_fresh ? 1U : 0U,
            g_dab_cpu0_dual_core_status.ipc.peer_seen ? 1U : 0U,
            g_dab_cpu0_dual_core_status.ipc.peer_stale ? 1U : 0U,
            (unsigned long) g_dab_cpu0_dual_core_status.energy_config_queued,
            (unsigned long) g_dab_cpu0_dual_core_status.energy_config_dropped,
            (int) s_current_control_direction);
    if (s_vin_adaptation.config.enabled)
    {
        dab_log("VAD:%lu,%u,%u,%.3f,%u,%.4f,%.3f,%.4f,%.3f,%.4f,%.3f,%.2f,%u,%u,%.0f\r\n",
                (unsigned long) s_measurements.sequence,
                s_vin_adaptation.config.enabled ? 1U : 0U,
                s_vin_adaptation.output.active ? 1U : 0U,
                (double) s_vin_adaptation.output.filtered_vin_v,
                (unsigned int) s_vin_adaptation.output.band,
                (double) s_vin_adaptation.output.gain_scale,
                (double) s_vin_adaptation.output.effective_kp_ns_per_a,
                (double) s_vin_adaptation.output.effective_ki_us_per_as,
                (double) s_vin_adaptation.config.feedforward_strength,
                (double) (s_vin_adaptation.output.feedforward_delta_counts *
                          (float) DAB_PWM_COUNT_NS),
                (double) s_vin_adaptation.output.phase_headroom_percent,
                (double) s_vin_adaptation.output.estimated_max_output_power_w,
                s_vin_adaptation.output.estimate_valid ? 1U : 0U,
                s_vin_adaptation.output.actuator_limited ? 1U : 0U,
                (double) (1000.0F *
                          s_vin_adaptation.output.actuator_limit_time_s));
    }
    if (DAB_AUTOTUNE_IDLE != s_autotune.stage)
    {
        dab_log("ATI:%lu,%u,%u,%.1f,%.1f,%.3f,%.3f,%.6f,%d,%.3f,%.4f,%.3f,%.1f,%u,%u,%u\r\n",
                (unsigned long) s_measurements.sequence,
                (unsigned int) s_autotune.stage,
                s_autotune.running ? 1U : 0U,
                (double) s_autotune.config.perturbation_ns,
                (double) s_autotune.injection_ns,
                (double) s_autotune.result.positive_current_a,
                (double) s_autotune.result.negative_current_a,
                (double) s_autotune.result.local_gain_a_per_ns,
                (int) s_autotune.result.recommended_direction,
                (double) s_autotune.result.recommended_kp_ns_per_a,
                (double) s_autotune.result.recommended_ki_us_per_as,
                (double) s_autotune.result.recommended_slew_a_per_s,
                (double) s_autotune.result.recommended_phase_cap_ns,
                s_autotune.result.valid ? 1U : 0U,
                s_autotune.result.applied ? 1U : 0U,
                (unsigned int) s_autotune.result.failure_reason);
    }
    if (DAB_MODSWITCH_IDLE != s_modswitch.stage)
    {
        dab_log("MSW:%lu,%u,%u,%u,%.1f,%.1f,%.3f,%.2f,%.1f,%d,%d,%u\r\n",
                (unsigned long) s_measurements.sequence,
                (unsigned int) s_modswitch.stage,
                (unsigned int) s_modswitch.source_mode,
                (unsigned int) s_modswitch.target_mode,
                (double) s_modswitch.target_primary_inner_ns,
                (double) s_modswitch.target_secondary_inner_ns,
                (double) s_modswitch.baseline_metric,
                (double) s_modswitch.metric_error_percent,
                (double) (1000.0F * s_modswitch.elapsed_s),
                (int) s_modswitch.source_direction,
                (int) s_modswitch.target_direction,
                (unsigned int) s_modswitch.failure_reason);
    }
    if (DAB_CONTROL_ARCH_CP_DAMPED_CASCADE == s_control_architecture)
    {
        dab_log("CPD:%lu,%u,%.1f,%.1f,%.2f,%.5f,%.5f,%.4f,%.2f,%.3f,%.3f,%.3f,%.3f,%u\r\n",
                (unsigned long) s_measurements.sequence,
                s_cp_damping.config.enabled ? 1U : 0U,
                (double) s_cp_damping.config.target_power_w,
                (double) s_cp_damping.active_power_target_w,
                (double) s_cp_damping.power_error_w,
                (double) s_cp_damping.config.power_kp_a_per_w,
                (double) s_cp_damping.config.power_ki_a_per_ws,
                (double) s_cp_damping.config.virtual_conductance_a_per_v,
                (double) s_cp_damping.config.nominal_output_v,
                (double) s_cp_damping.feedforward_current_a,
                (double) s_cp_damping.damping_current_a,
                (double) s_cp_damping.integral_current_a,
                (double) s_cp_damping.requested_current_a,
                s_cp_damping.saturated ? 1U : 0U);
    }
}

static void dab_print_observe(void)
{
    float const maximum_temperature = dab_maxf(s_measurements.temp_pri_c,
                                                s_measurements.temp_sec_c);
    dab_log("[OBS] %s Vin=%.2fV Vout=%.2fV Iin=%.2fA Iout=%.2fA "
            "Pin=%.1fW Pout=%.1fW Tmax=%.1fC ph=%luns fault=%08lX\r\n",
            dab_state_name(s_state),
            (double) s_measurements.vin_v,
            (double) s_measurements.vout_v,
            (double) s_measurements.iin_a,
            (double) s_measurements.iout_a,
            (double) s_measurements.pin_w,
            (double) s_measurements.pout_w,
            (double) maximum_temperature,
            (unsigned long) (dab_pwm_hw_phase_get() * DAB_PWM_COUNT_NS),
            (unsigned long) s_fault_mask);
}

static bool dab_modulation_set(dab_modulation_mode_t mode)
{
    if (mode > DAB_MODULATION_AUTO)
    {
        return false;
    }
    if ((DAB_STATE_READY != s_state) || s_control_enabled ||
        dab_pwm_hw_outputs_are_enabled())
    {
        dab_log("[DAB][BLOCK] change modulation only in READY with PWM/power OFF\r\n");
        return false;
    }
    s_modulation_requested = mode;
    s_inner_manual_override = false;
    s_primary_inner_manual_target_counts = 0.0F;
    s_secondary_inner_manual_target_counts = 0.0F;
    dab_schedule_mark_manual();
    dab_control_reset();
    dab_log("[DAB][MOD] requested=%s; inner=AUTO reset; soft-start remains SPS; partition applies in RUN\r\n",
            dab_modulation_name(mode));
    return true;
}

static bool dab_parse_float(char const * p_text, float * p_value)
{
    if ((NULL == p_text) || (NULL == p_value))
    {
        return false;
    }
    char * p_end = NULL;
    float const value = strtof(p_text, &p_end);
    if ((p_end == p_text) || ('\0' != *p_end))
    {
        return false;
    }
    *p_value = value;
    return true;
}

static const char * dab_energy_mode_name(dab_energy_mode_t mode)
{
    static const char * const names[] =
    {
        "OFF", "MANUAL_CURRENT", "MPPT_ONLY", "CHARGER_ONLY", "SOLAR_CHARGER"
    };
    uint32_t const index = (uint32_t) mode;
    return (index < (sizeof(names) / sizeof(names[0]))) ? names[index] : "?";
}

static const char * dab_mppt_mode_name(dab_mppt_mode_t mode)
{
    static const char * const names[] = {"OFF", "P_AND_O", "INC_COND", "FIXED_POWER"};
    uint32_t const index = (uint32_t) mode;
    return (index < (sizeof(names) / sizeof(names[0]))) ? names[index] : "?";
}

static const char * dab_battery_profile_name(dab_battery_profile_t profile)
{
    static const char * const names[] =
    {
        "CUSTOM", "LI_ION_10S_42V", "LIFEPO4_12S_43P8V", "LEAD_ACID_18S_36V"
    };
    uint32_t const index = (uint32_t) profile;
    return (index < (sizeof(names) / sizeof(names[0]))) ? names[index] : "?";
}

static const char * dab_charge_stage_name(dab_charge_stage_t stage)
{
    static const char * const names[] =
    {
        "OFF", "IDLE", "PRECHARGE", "CC", "CV", "FLOAT", "DONE", "FAULT"
    };
    uint32_t const index = (uint32_t) stage;
    return (index < (sizeof(names) / sizeof(names[0]))) ? names[index] : "?";
}

static bool dab_energy_parameter_set(dab_energy_config_key_t key, float value)
{
    dab_energy_state_t const previous = s_energy_shadow;
    uint32_t const now_ms = (uint32_t) xTaskGetTickCount() *
                            (uint32_t) portTICK_PERIOD_MS;
    if (!dab_energy_apply_config(&s_energy_shadow, key, value, now_ms))
    {
        return false;
    }
    if (!dab_dual_core_cpu0_energy_config_set(key, value))
    {
        s_energy_shadow = previous;
        return false;
    }
    s_algorithm_logging_enabled = true;
    return true;
}

static void dab_print_energy_status(void)
{
    dab_energy_config_t const * const c = &s_energy_shadow.config;
    uint32_t age_ms = UINT32_MAX;
    if (g_dab_cpu0_dual_core_status.energy_reference_updates > 0U)
    {
        uint32_t const now_ms = (uint32_t) xTaskGetTickCount() *
                                (uint32_t) portTICK_PERIOD_MS;
        age_ms = now_ms - g_dab_cpu0_dual_core_status.energy_reference_last_ms;
    }
    dab_log("[ENERGY] mode=%s arch=%s Icmd=%.2fA Iactive=%.2fA Icpu1=%.2fA valid=%u age=%lums Kp=%.2fns/A Ki=%.3fus/(A*s) slew=%.2fA/s idir=%s\r\n",
            dab_energy_mode_name(c->mode),
            dab_control_architecture_name(s_control_architecture),
            (double) s_current_reference_command_a,
            (double) s_current_reference_active_a,
            (double) g_dab_cpu0_dual_core_status.energy_current_reference_a,
            g_dab_cpu0_dual_core_status.energy_reference_valid ? 1U : 0U,
            (unsigned long) age_ms,
            (double) s_current_kp_ns_per_a,
            (double) s_current_ki_us_per_as,
            (double) s_current_reference_slew_a_per_s,
            (s_current_control_direction > 0) ? "forward" : "reverse");
    dab_log("[MPPT] mode=%s step=%.2fV interval=%lums range=%.1f..%.1fV PinMeasured=%.0fW Vknee=%.1fV limited=%u\r\n",
            dab_mppt_mode_name(c->mppt_mode), (double) c->mppt_step_v,
            (unsigned long) c->mppt_interval_ms,
            (double) c->mppt_min_v, (double) c->mppt_max_v,
            (double) g_dab_cpu0_dual_core_status.mppt_power_target_w,
            (double) g_dab_cpu0_dual_core_status.mppt_voltage_target_v,
            g_dab_cpu0_dual_core_status.mppt_limited ? 1U : 0U);
    dab_log("[BAT] profile=%s stage=%s start=%u capacity=%.1fAh Imax=%.2fA Ipre=%.2fA Iterm=%.2fA Vpre=%.2fV Vcv=%.2fV Vfloat=%.2fV Vtarget=%.2fV limited=%u\r\n",
            dab_battery_profile_name(c->battery_profile),
            dab_charge_stage_name(g_dab_cpu0_dual_core_status.charge_stage),
            c->battery_start ? 1U : 0U,
            (double) c->battery_capacity_ah,
            (double) c->battery_max_current_a,
            (double) c->battery_precharge_current_a,
            (double) c->battery_terminate_current_a,
            (double) c->battery_precharge_v,
            (double) c->battery_cv_v,
            (double) c->battery_float_v,
            (double) g_dab_cpu0_dual_core_status.charge_voltage_target_v,
            g_dab_cpu0_dual_core_status.charger_limited ? 1U : 0U);
    dab_log("[AIHOOK] mode=%u current_hint=%.2fA confidence=%.3f ttl=%lums applied=%u; deterministic fallback=LOCAL\r\n",
            (unsigned int) c->ai_mode,
            (double) c->ai_hint_current_a,
            (double) c->ai_confidence,
            (unsigned long) c->ai_ttl_ms,
            g_dab_cpu0_dual_core_status.ai_applied ? 1U : 0U);
    dab_log("[AIHOOK] modulation_hint=%s confidence=%.3f ttl=%lums fresh=%u; record-only, no live switch\r\n",
            dab_modulation_name((dab_modulation_mode_t)
                                g_dab_cpu0_dual_core_status.ai_modulation_hint),
            (double) g_dab_cpu0_dual_core_status.ai_modulation_confidence,
            (unsigned long) g_dab_cpu0_dual_core_status.ai_modulation_ttl_ms,
            g_dab_cpu0_dual_core_status.ai_modulation_fresh ? 1U : 0U);
}

static void dab_print_vin_adaptation_status(void)
{
    dab_vin_adaptation_config_t const * const c = &s_vin_adaptation.config;
    dab_vin_adaptation_output_t const * const o = &s_vin_adaptation.output;
    dab_log("[VAD] version=%s enabled=%u active=%u range=%.1f..%.1fV nominal=%.1fV band=%s VinRaw=%.3fV VinFilt=%.3fV\r\n",
            DAB_VIN_ADAPTATION_VERSION,
            c->enabled ? 1U : 0U,
            o->active ? 1U : 0U,
            (double) c->minimum_vin_v,
            (double) c->maximum_vin_v,
            (double) c->nominal_vin_v,
            dab_vin_adaptation_band_name(o->band),
            (double) s_measurements.vin_v,
            (double) o->filtered_vin_v);
    dab_log("[VAD] gain=%.4f KpBase/Eff=%.3f/%.3f KiBase/Eff=%.4f/%.4f kff=%.3f dPhase=%.3fns headroom=%.2f%%\r\n",
            (double) o->gain_scale,
            (double) s_current_kp_ns_per_a,
            (double) o->effective_kp_ns_per_a,
            (double) s_current_ki_us_per_as,
            (double) o->effective_ki_us_per_as,
            (double) c->feedforward_strength,
            (double) (o->feedforward_delta_counts *
                      (float) DAB_PWM_COUNT_NS),
            (double) o->phase_headroom_percent);
    dab_log("[VAD] PmaxEstimate=%.1fW estimateValid=%u actuatorLimited=%u limitTime=%.0fms; target current is never changed\r\n",
            (double) o->estimated_max_output_power_w,
            o->estimate_valid ? 1U : 0U,
            o->actuator_limited ? 1U : 0U,
            (double) (1000.0F * o->actuator_limit_time_s));
}

static void dab_print_autotune_status(void)
{
    dab_autotune_result_t const * const r = &s_autotune.result;
    dab_log("[AUTOTUNE] version=%s stage=%s running=%u step=%.1fns settle=%.2fs sample=%.2fs autoApply=%u injection=%.1fns\r\n",
            DAB_ADVANCED_CONTROL_VERSION,
            dab_autotune_stage_name(s_autotune.stage),
            s_autotune.running ? 1U : 0U,
            (double) s_autotune.config.perturbation_ns,
            (double) s_autotune.config.settle_time_s,
            (double) s_autotune.config.sample_time_s,
            s_autotune.config.auto_apply ? 1U : 0U,
            (double) s_autotune.injection_ns);
    dab_log("[AUTOTUNE] valid=%u applied=%u failure=%u gain=%.6fA/ns Iplus/Iminus=%.3f/%.3fA rangeEst=%.2f..%.2fA\r\n",
            r->valid ? 1U : 0U,
            r->applied ? 1U : 0U,
            (unsigned int) r->failure_reason,
            (double) r->local_gain_a_per_ns,
            (double) r->positive_current_a,
            (double) r->negative_current_a,
            (double) r->estimated_min_current_a,
            (double) r->estimated_max_current_a);
    dab_log("[AUTOTUNE] recommendation idir=%s Kp=%.3fns/A Ki=%.4fus/(A*s) slew=%.3fA/s cap=%.1fns; RAM+UART record, promote to table only after physical validation\r\n",
            (r->recommended_direction < 0) ? "reverse" : "forward",
            (double) r->recommended_kp_ns_per_a,
            (double) r->recommended_ki_us_per_as,
            (double) r->recommended_slew_a_per_s,
            (double) r->recommended_phase_cap_ns);
}

static void dab_print_modswitch_status(void)
{
    dab_log("[MODSW] version=%s stage=%s source=%s target=%s metricBase=%.3f error=%.2f%% elapsed=%.1fms failure=%u\r\n",
            DAB_ADVANCED_CONTROL_VERSION,
            dab_modswitch_stage_name(s_modswitch.stage),
            dab_modulation_name((dab_modulation_mode_t) s_modswitch.source_mode),
            dab_modulation_name((dab_modulation_mode_t) s_modswitch.target_mode),
            (double) s_modswitch.baseline_metric,
            (double) s_modswitch.metric_error_percent,
            (double) (1000.0F * s_modswitch.elapsed_s),
            (unsigned int) s_modswitch.failure_reason);
    dab_log("[MODSW] targetInner=%.1f/%.1fns slew=%.2fns/ms pass=%.1f%% abort=%.1f%% verify=%.0fms timeout=%.0fms dwell=%.0fms direction=%s\r\n",
            (double) s_modswitch.target_primary_inner_ns,
            (double) s_modswitch.target_secondary_inner_ns,
            (double) s_modswitch.config.inner_slew_ns_per_ms,
            (double) s_modswitch.config.allowed_metric_error_percent,
            (double) s_modswitch.config.abort_metric_error_percent,
            (double) (1000.0F * s_modswitch.config.verify_time_s),
            (double) (1000.0F * s_modswitch.config.timeout_s),
            (double) (1000.0F * s_modswitch.dwell_elapsed_s),
            (s_current_control_direction < 0) ? "reverse" : "forward");
}

static void dab_print_cp_damping_status(void)
{
    dab_cp_damping_config_t const * const c = &s_cp_damping.config;
    dab_log("[CPD] version=%s enabled=%u arch=%s Pcmd/Pactive/Pout=%.1f/%.1f/%.1fW error=%.2fW\r\n",
            DAB_ADVANCED_CONTROL_VERSION,
            c->enabled ? 1U : 0U,
            dab_control_architecture_name(s_control_architecture),
            (double) c->target_power_w,
            (double) s_cp_damping.active_power_target_w,
            (double) s_measurements.pout_w,
            (double) s_cp_damping.power_error_w);
    dab_log("[CPD] Kp=%.5fA/W Ki=%.5fA/(W*s) Gvirt=%.4fA/V Vnom/Vmin=%.2f/%.2fV slew=%.1fW/s Iff/Idamp/Iint/Iref/Ilim=%.3f/%.3f/%.3f/%.3f/%.3fA sat=%u\r\n",
            (double) c->power_kp_a_per_w,
            (double) c->power_ki_a_per_ws,
            (double) c->virtual_conductance_a_per_v,
            (double) c->nominal_output_v,
            (double) c->minimum_output_v,
            (double) c->target_slew_w_per_s,
            (double) s_cp_damping.feedforward_current_a,
            (double) s_cp_damping.damping_current_a,
            (double) s_cp_damping.integral_current_a,
            (double) s_cp_damping.requested_current_a,
            (double) s_cp_damping.effective_current_limit_a,
            s_cp_damping.saturated ? 1U : 0U);
}

static bool dab_power_limit_set(float requested)
{
    bool const autonomous_cp =
        DAB_CONTROL_ARCH_CP_DAMPED_CASCADE == s_control_architecture;
    float const maximum = (s_unlock_high_power || autonomous_cp) ?
                          DAB_POWER_ABSOLUTE_MAX_W :
                          DAB_POWER_NORMAL_MAX_W;
    if ((requested < DAB_POWER_DEFAULT_W) || (requested > maximum))
    {
        dab_log("[DAB][BLOCK] power must be 20..%.0fW; CP product targets are accepted only after arch cp\r\n",
                (double) maximum);
        return false;
    }

    s_power_limit_w = requested;
    s_power_command_received = true;
    dab_schedule_mark_manual();
    dab_log("[DAB][POWER] output ceiling=%.0fW normal-input-soft=%.0fW guard=%s; independent closed-loop cap=%luns\r\n",
            (double) s_power_limit_w,
            (double) (s_power_limit_w * DAB_INPUT_POWER_SOFT_MULTIPLIER),
            dab_guard_profile_name(s_guard_profile),
            (unsigned long) (s_closed_loop_phase_cap_counts *
                             DAB_PWM_COUNT_NS));
    return true;
}

static bool dab_phase_cap_set(float requested_ns)
{
    if ((DAB_STATE_READY != s_state) || s_control_enabled ||
        dab_pwm_hw_outputs_are_enabled())
    {
        dab_log("[DAB][BLOCK] cap changes require READY with PWM/power OFF\r\n");
        return false;
    }

    uint32_t const maximum_counts = s_unlock_high_power ?
        DAB_PHASE_ABSOLUTE_MAX_COUNTS : DAB_PHASE_NORMAL_MAX_COUNTS;
    float const maximum_ns = (float) (maximum_counts * DAB_PWM_COUNT_NS);
    if ((requested_ns < 0.0F) || (requested_ns > maximum_ns))
    {
        dab_log("[DAB][BLOCK] cap must be 0..%.0fns; unlock 400 permits 480ns\r\n",
                (double) maximum_ns);
        return false;
    }

    s_closed_loop_phase_cap_counts =
        (uint32_t) ((requested_ns / (float) DAB_PWM_COUNT_NS) + 0.5F);
    if (s_closed_loop_phase_cap_counts > maximum_counts)
    {
        s_closed_loop_phase_cap_counts = maximum_counts;
    }
    dab_schedule_mark_manual();
    dab_control_reset();
    dab_log("[DAB][CAP] closed-loop configured=%luns; cold gates remain 8/80/160ns below 5/15/30V\r\n",
            (unsigned long) (s_closed_loop_phase_cap_counts *
                             DAB_PWM_COUNT_NS));
    return true;
}

#if DAB_ALLOW_POWER_TRANSFER
static void dab_control_start_request(void)
{
    if (s_configuration_start_blocked)
    {
        dab_force_all_safe();
        dab_log("[DAB][START][BLOCK] the last automatic configuration had no exact powered seed; apply a valid target or use sched off for deliberate manual setup\r\n");
        return;
    }

    if (!DAB_SOFTWARE_AUTO_TRIP_ENABLE && !s_control_enabled &&
        (DAB_STATE_READY != s_state) && (DAB_STATE_PATH_DIRECT != s_state))
    {
        dab_force_all_safe();
        s_fault_mask = DAB_FAULT_NONE;
        dab_enter_state(DAB_STATE_READY);
    }
    bool const direct_path_ready = (DAB_STATE_PATH_DIRECT == s_state);
    if (s_control_enabled)
    {
        dab_log("[DAB][ON] already running; send off to stop\r\n");
    }
    else if (!s_power_command_received)
    {
        dab_log("[DAB][BLOCK] power limit not set\r\n");
    }
    else if (DAB_SOFTWARE_AUTO_TRIP_ENABLE &&
             (((DAB_STATE_READY != s_state) && !direct_path_ready) ||
              (0U != s_fault_mask)))
    {
        dab_log("[DAB][BLOCK] on requires READY/PATH and fault=0; send show\r\n");
    }
    else if (dab_start_preconditions_ok())
    {
        if (direct_path_ready)
        {
            dab_board_outputs_t outputs;
            dab_board_outputs_get(&outputs);
            if (!outputs.input_main_en || !outputs.output_protection_en ||
                !outputs.protection_reset_n ||
                (DAB_SOFTWARE_AUTO_TRIP_ENABLE &&
                 (s_measurements.vin_v < DAB_VIN_START_MIN_V)))
            {
                dab_log("[DAB][BLOCK] four-MOS path/Vin not ready; MAIN=%u OUT=%u RESET=%u Vin=%.2fV\r\n",
                        outputs.input_main_en ? 1U : 0U,
                        outputs.output_protection_en ? 1U : 0U,
                        outputs.protection_reset_n ? 1U : 0U,
                        (double) s_measurements.vin_v);
                return;
            }

            s_control_enabled = true;
            dab_board_fan_set(true);
            dab_control_reset();
            if (!dab_control_prearm_autonomous_charger())
            {
                dab_force_all_safe();
                dab_enter_state(DAB_STATE_READY);
                dab_log("[DAB][BLOCK] autonomous charger pre-arm failed before direct-path PWM; all outputs/path OFF\r\n");
                return;
            }
            (void) dab_gate_driver_enable();
            if (!dab_pwm_hw_outputs_enable())
            {
                if (DAB_SOFTWARE_AUTO_TRIP_ENABLE)
                {
                    dab_fault_latch(DAB_FAULT_RAW_N | DAB_FAULT_POEG);
                }
                else
                {
                    dab_force_all_safe();
                    dab_enter_state(DAB_STATE_READY);
                    dab_log("[DAB][BLOCK] on aborted before MCU PWM; all outputs/path OFF\r\n");
                }
                return;
            }
            dab_enter_state(DAB_STATE_SOFTSTART);
            dab_log("[DAB][ON] direct path accepted; Pout ceiling=%.0fW configured cap=%luns Vref=%.1fV SPS; protection=%s\r\n",
                    (double) s_power_limit_w,
                    (unsigned long) (s_closed_loop_phase_cap_counts *
                                     DAB_PWM_COUNT_NS),
                    (double) s_vref_v,
                    DAB_SOFTWARE_AUTO_TRIP_ENABLE ? "MCU_AUTO" : "MANUAL_STOP");
            dab_print_pwm_hw_diag();
        }
        else
        {
            dab_force_all_safe();
            s_control_enabled = true;
            dab_board_fan_set(true);
            dab_enter_state(DAB_STATE_RESET_LOW);
            dab_log("[DAB][ON] path sequence; Pout ceiling=%.0fW configured cap=%luns Vref=%.1fV; protection=%s\r\n",
                    (double) s_power_limit_w,
                    (unsigned long) (s_closed_loop_phase_cap_counts *
                                     DAB_PWM_COUNT_NS),
                    (double) s_vref_v,
                    DAB_SOFTWARE_AUTO_TRIP_ENABLE ? "MCU_AUTO" : "MANUAL_STOP");
        }
    }
}
#endif

static void dab_remote_text_set(char * p_destination,
                                size_t destination_size,
                                char const * p_source)
{
    if ((NULL == p_destination) || (0U == destination_size))
    {
        return;
    }

    (void) strncpy(p_destination,
                   (NULL != p_source) ? p_source : "",
                   destination_size - 1U);
    p_destination[destination_size - 1U] = '\0';
}

static void dab_remote_snapshot_publish_now(void)
{
    dab_remote_snapshot_t snapshot = {0};
    uint32_t const raw_phase = dab_pwm_hw_phase_get();
    uint32_t const primary_inner = dab_pwm_hw_primary_inner_get();
    uint32_t const secondary_inner = dab_pwm_hw_secondary_inner_get();
    snapshot.uptime_ms = (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS);
    snapshot.sequence = s_measurements.sequence;
    snapshot.fault_mask = s_fault_mask;
    snapshot.phase_ns = raw_phase * DAB_PWM_COUNT_NS;
    snapshot.raw_phase_ns = snapshot.phase_ns;
    snapshot.effective_phase_ns =
        dab_effective_phase_ns(raw_phase, primary_inner, secondary_inner);
    snapshot.command_phase_ns = (uint32_t)
        ((s_phase_command_counts * (float) DAB_PWM_COUNT_NS) + 0.5F);
    snapshot.primary_inner_ns = primary_inner * DAB_PWM_COUNT_NS;
    snapshot.secondary_inner_ns = secondary_inner * DAB_PWM_COUNT_NS;
    snapshot.phase_cap_ns =
        s_closed_loop_phase_cap_counts * DAB_PWM_COUNT_NS;
    snapshot.table_entry_id = s_schedule_entry_id;
    snapshot.outer_saturation_updates = s_outer_saturation_updates;
    snapshot.control_enabled = s_control_enabled;
    snapshot.pwm_enabled = dab_pwm_hw_outputs_are_enabled();
    snapshot.measurements_valid = s_measurements.valid;
    snapshot.load_mode_control_active = false;
    snapshot.inner_manual = s_inner_manual_override;
    snapshot.outer_saturated = s_outer_saturated;
    snapshot.schedule_enabled = s_schedule_enabled;
    snapshot.feedforward_enabled = s_feedforward_enabled;
    snapshot.vin_v = s_measurements.vin_v;
    snapshot.vout_v = s_measurements.vout_v;
    snapshot.iin_a = s_measurements.iin_a;
    snapshot.iout_a = s_measurements.iout_a;
    snapshot.ipri_a = s_measurements.ipri_a;
    snapshot.isec_a = s_measurements.isec_a;
    snapshot.pin_w = s_measurements.pin_w;
    snapshot.pout_w = s_measurements.pout_w;
    snapshot.temp_pri_c = s_measurements.temp_pri_c;
    snapshot.temp_sec_c = s_measurements.temp_sec_c;
    snapshot.power_limit_w = s_power_limit_w;
    snapshot.voltage_ref_v = s_vref_v;
    snapshot.kp_ns_per_v = s_kp_ns_per_v;
    snapshot.ki_us_per_vs = s_ki_us_per_vs;
    snapshot.kd_ns_s_per_v = s_kd_ns_s_per_v;
    snapshot.derivative_filter_alpha = DAB_DERIVATIVE_FILTER_ALPHA;
    snapshot.bench_vin_set_v = s_bench_vin_set_v;
    snapshot.load_nominal_ohm = s_load_nominal_ohm;
    snapshot.requested_power_layer_w = s_requested_power_layer_w;
    snapshot.feedforward_effective_phase_ns =
        s_feedforward_effective_phase_ns;
    snapshot.control_architecture_id = (uint32_t) s_control_architecture;
    dab_remote_text_set(snapshot.state,
                        sizeof(snapshot.state),
                        dab_state_name(s_state));
    dab_remote_text_set(snapshot.modulation_requested,
                        sizeof(snapshot.modulation_requested),
                        dab_modulation_name(s_modulation_requested));
    dab_remote_text_set(snapshot.modulation_effective,
                        sizeof(snapshot.modulation_effective),
                        dab_modulation_name(s_modulation_effective));
    dab_remote_text_set(snapshot.load_mode,
                        sizeof(snapshot.load_mode),
                        s_load_mode);
    dab_remote_text_set(snapshot.control_architecture,
                        sizeof(snapshot.control_architecture),
                        dab_control_architecture_name(s_control_architecture));
    dab_remote_text_set(snapshot.schedule_table,
                        sizeof(snapshot.schedule_table),
                        DAB_TUNED_PARAMETER_TABLE_VERSION);
    dab_remote_text_set(snapshot.firmware,
                        sizeof(snapshot.firmware),
                        DAB_FW_VERSION);
    dab_remote_snapshot_publish(&snapshot);
}

static bool dab_remote_modulation_parse(char const * p_text,
                                        dab_modulation_mode_t * p_mode)
{
    if ((NULL == p_text) || (NULL == p_mode))
    {
        return false;
    }
    if ((0 == strcmp(p_text, "sps")) || (0 == strcmp(p_text, "SPS")))
    {
        *p_mode = DAB_MODULATION_SPS;
    }
    else if ((0 == strcmp(p_text, "eps")) || (0 == strcmp(p_text, "EPS")))
    {
        *p_mode = DAB_MODULATION_EPS;
    }
    else if ((0 == strcmp(p_text, "tps")) || (0 == strcmp(p_text, "TPS")))
    {
        *p_mode = DAB_MODULATION_TPS;
    }
    else if ((0 == strcmp(p_text, "auto")) || (0 == strcmp(p_text, "AUTO")))
    {
        *p_mode = DAB_MODULATION_AUTO;
    }
    else
    {
        return false;
    }
    return true;
}

static void dab_schedule_mark_manual(void)
{
    s_schedule_enabled = false;
    s_schedule_entry_id = 0U;
    s_charger_low_input_runtime = false;
    s_charger_runtime_candidate_id = 0U;
    s_charger_runtime_candidate_low_input = false;
    s_feedforward_enabled = false;
    s_feedforward_effective_phase_ns = 0.0F;
}

static void dab_print_parameter_bundle(void)
{
    dab_log("[DAB][BUNDLE] arch=%s mod=%s schedule=%u auto=%u entry=%lu table=%s\r\n",
            dab_control_architecture_name(s_control_architecture),
            dab_modulation_name(s_modulation_requested),
            s_schedule_enabled ? 1U : 0U,
            s_schedule_autoload_enabled ? 1U : 0U,
            (unsigned long) s_schedule_entry_id,
            DAB_TUNED_PARAMETER_TABLE_VERSION);
    dab_log("[DAB][BUNDLE] VinSet=%.2fV Vref=%.1fV Rload=%.2fohm player=%.1fW Kp=%.2f Ki=%.3f Kd=%.4f Dalpha=%.4f\r\n",
            (double) s_bench_vin_set_v,
            (double) s_vref_v,
            (double) s_load_nominal_ohm,
            (double) s_requested_power_layer_w,
            (double) s_kp_ns_per_v,
            (double) s_ki_us_per_vs,
            (double) s_kd_ns_s_per_v,
            (double) DAB_DERIVATIVE_FILTER_ALPHA);
    dab_log("[DAB][BUNDLE] Plabel=%.1f phase_cap=%luns ff=%u/%.1fns inner=%lu/%luns sat=%u/%lu\r\n",
            (double) s_power_limit_w,
            (unsigned long) (s_closed_loop_phase_cap_counts *
                             DAB_PWM_COUNT_NS),
            s_feedforward_enabled ? 1U : 0U,
            (double) s_feedforward_effective_phase_ns,
            (unsigned long) (s_primary_inner_manual_target_counts *
                             (float) DAB_PWM_COUNT_NS + 0.5F),
            (unsigned long) (s_secondary_inner_manual_target_counts *
                             (float) DAB_PWM_COUNT_NS + 0.5F),
            s_outer_saturated ? 1U : 0U,
            (unsigned long) s_outer_saturation_updates);
    dab_log("[DAB][BUNDLE] current Iref=%.3fA Kp=%.3fns/A Ki=%.4fus/(A*s) slew=%.3fA/s dir=%s measured_Vin=%.3fV\r\n",
            (double) s_current_reference_command_a,
            (double) s_current_kp_ns_per_a,
            (double) s_current_ki_us_per_as,
            (double) s_current_reference_slew_a_per_s,
            (s_current_control_direction > 0) ? "forward" : "reverse",
            (double) s_measurements.vin_v);
}

static void dab_print_validated_charger_table(void)
{
    dab_log("[DAB][CHGVAL] table=%s revision=%lu entries=%lu record-only=1\r\n",
            DAB_VALIDATED_CHARGER_TABLE_VERSION,
            (unsigned long) DAB_VALIDATED_CHARGER_TABLE_REVISION,
            (unsigned long) DAB_VALIDATED_CHARGER_TABLE_ENTRY_COUNT);
    for (size_t i = 0U; i < DAB_VALIDATED_CHARGER_TABLE_ENTRY_COUNT; i++)
    {
        dab_validated_charger_entry_t const * const p =
            &g_dab_validated_charger_table[i];
        dab_log("[DAB][CHGVAL] id=%lu profile=%u stage=%u mod=%u arch=%u Vin=%.1f CV=%.1f inner=%.0f/%.0f cap=%.0f Ipi=%.2f/%.3f slew=%.2f Imax=%.2f Iterm=%.2f\r\n",
                (unsigned long) p->entry_id,
                (unsigned int) p->battery_profile_id,
                (unsigned int) p->charge_stage_id,
                (unsigned int) p->modulation_id,
                (unsigned int) p->control_architecture_id,
                (double) p->nominal_input_v,
                (double) p->external_cv_set_v,
                (double) p->primary_inner_ns,
                (double) p->secondary_inner_ns,
                (double) p->phase_cap_ns,
                (double) p->current_kp_ns_per_a,
                (double) p->current_ki_us_per_as,
                (double) p->current_slew_a_per_s,
                (double) p->configured_imax_a,
                (double) p->configured_iterm_a);
        dab_log("[DAB][CHGVAL] id=%lu board=%.3fV/%.3fA %.2f->%.2fW external=%.3fV/%.3fA/%.2fW eta_ext=%.3f%%\r\n",
                (unsigned long) p->entry_id,
                (double) p->board_vout_v,
                (double) p->board_iout_a,
                (double) p->board_pin_w,
                (double) p->board_pout_w,
                (double) p->external_voltage_v,
                (double) p->external_current_a,
                (double) p->external_power_w,
                (double) p->external_to_board_input_eff_pct);
    }
}

static bool dab_charger_bundle_select(float measured_vin_v,
                                      float operating_voltage_v,
                                      uint8_t raw_stage,
                                      float requested_current_a,
                                      dab_validated_charger_entry_t * p_charger,
                                      float * p_score,
                                      bool * p_low_input_runtime)
{
    if ((NULL == p_charger) || (NULL == p_low_input_runtime))
    {
        return false;
    }

    *p_low_input_runtime = false;
    float selection_current_a = requested_current_a;
    if ((selection_current_a <= 0.0F) ||
        (selection_current_a > s_energy_shadow.config.battery_max_current_a))
    {
        selection_current_a = s_energy_shadow.config.battery_max_current_a;
    }
    uint8_t const preferred_stage =
        dab_validated_charger_normalize_stage(raw_stage);
    bool found = dab_validated_charger_lookup_nearest(
        (uint8_t) s_energy_shadow.config.battery_profile,
        preferred_stage,
        measured_vin_v,
        operating_voltage_v,
        selection_current_a,
        p_charger,
        p_score);

    bool const precharge_seed =
        dab_validated_charger_build_precharge_seed(
            (uint8_t) s_energy_shadow.config.battery_profile,
            raw_stage,
            measured_vin_v,
            s_energy_shadow.config.battery_precharge_current_a,
            s_energy_shadow.config.battery_terminate_current_a,
            p_charger);
    if (precharge_seed)
    {
        if (NULL != p_score)
        {
            *p_score = 0.0F;
        }
        return true;
    }

    /* Fused charger/MPPT CC inherits the commissioned low-duty TPS waveform.
     * The local current/phase service then opens transfer authority in
     * bounded steps and compares SPS/EPS/TPS around the tracked source knee. */
    bool const fused_mppt_phase_search =
        (DAB_ENERGY_MODE_SOLAR_CHARGER == s_energy_shadow.config.mode) &&
        ((DAB_MPPT_MODE_PERTURB_OBSERVE ==
          s_energy_shadow.config.mppt_mode) ||
         (DAB_MPPT_MODE_INCREMENTAL_CONDUCTANCE ==
          s_energy_shadow.config.mppt_mode)) &&
        ((uint8_t) DAB_CHARGE_STAGE_CONSTANT_CURRENT == preferred_stage) &&
        (((uint8_t) DAB_BATTERY_PROFILE_LI_ION_10S_42V ==
          (uint8_t) s_energy_shadow.config.battery_profile) ||
         ((uint8_t) DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V ==
          (uint8_t) s_energy_shadow.config.battery_profile));
    if (fused_mppt_phase_search)
    {
        *p_charger = (dab_validated_charger_entry_t)
        {
            DAB_MPPT_PHASE_SEARCH_ENTRY_ID,
            (uint8_t) s_energy_shadow.config.battery_profile,
            (uint8_t) DAB_CHARGE_STAGE_CONSTANT_CURRENT,
            (uint8_t) DAB_MODULATION_TPS,
            (uint8_t) DAB_CONTROL_ARCH_ENERGY_SUPERVISOR,
            measured_vin_v,
            operating_voltage_v,
            DAB_MPPT_PHASE_RECOVERY_HIGH_NS,
            DAB_MPPT_PHASE_RECOVERY_LOW_NS,
            DAB_MPPT_PHASE_SEARCH_PHASE_CAP_NS,
            DAB_MPPT_PHASE_SEARCH_CURRENT_KP,
            DAB_MPPT_PHASE_SEARCH_CURRENT_KI,
            DAB_MPPT_PHASE_SEARCH_CURRENT_SLEW,
            selection_current_a,
            s_energy_shadow.config.battery_terminate_current_a,
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
        if (NULL != p_score)
        {
            *p_score = 0.0F;
        }
        return true;
    }

    if (dab_validated_charger_build_lfp_low_voltage_cc_seed(
            (uint8_t) s_energy_shadow.config.battery_profile,
            preferred_stage,
            measured_vin_v,
            operating_voltage_v,
            selection_current_a,
            s_energy_shadow.config.battery_terminate_current_a,
            p_charger))
    {
        if (NULL != p_score)
        {
            *p_score = 0.0F;
        }
        return true;
    }

    if (dab_validated_charger_build_low_voltage_high_current_cc_seed(
            (uint8_t) s_energy_shadow.config.battery_profile,
            preferred_stage,
            measured_vin_v,
            operating_voltage_v,
            selection_current_a,
            s_energy_shadow.config.battery_terminate_current_a,
            p_charger))
    {
        if (NULL != p_score)
        {
            *p_score = 0.0F;
        }
        return true;
    }

    bool const low_input_lfp_candidate =
        ((uint8_t) DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V ==
         (uint8_t) s_energy_shadow.config.battery_profile) &&
        ((uint8_t) DAB_CHARGE_STAGE_CONSTANT_CURRENT == preferred_stage) &&
        (measured_vin_v <= DAB_CHARGER_LOW_INPUT_MAX_V) &&
        (((operating_voltage_v >= DAB_CHARGER_LOW_OUTPUT_MIN_V) &&
          (operating_voltage_v <= DAB_CHARGER_LOW_OUTPUT_MAX_V)) ||
         s_charger_low_input_runtime) &&
        (selection_current_a >=
         DAB_CHARGER_LOW_CURRENT_MIN_A) &&
        (selection_current_a <=
         DAB_CHARGER_LOW_CURRENT_MAX_A);
    if (low_input_lfp_candidate)
    {
        p_charger->entry_id = DAB_CHARGER_LOW_ENTRY_ID;
        p_charger->battery_profile_id =
            (uint8_t) DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V;
        p_charger->charge_stage_id =
            (uint8_t) DAB_CHARGE_STAGE_CONSTANT_CURRENT;
        p_charger->modulation_id = (uint8_t) DAB_MODULATION_TPS;
        p_charger->control_architecture_id =
            (uint8_t) DAB_CONTROL_ARCH_ENERGY_SUPERVISOR;
        p_charger->nominal_input_v = 18.0F;
        p_charger->external_cv_set_v = 43.0F;
        p_charger->primary_inner_ns = DAB_CHARGER_LOW_START_PRIMARY_NS;
        p_charger->secondary_inner_ns = DAB_CHARGER_LOW_START_SECONDARY_NS;
        p_charger->phase_cap_ns = DAB_CHARGER_LOW_PHASE_CAP_NS;
        p_charger->current_kp_ns_per_a = 2.0F;
        p_charger->current_ki_us_per_as = 0.2F;
        p_charger->current_slew_a_per_s = 0.05F;
        p_charger->configured_imax_a =
            selection_current_a;
        p_charger->configured_iterm_a =
            s_energy_shadow.config.battery_terminate_current_a;
        p_charger->board_vin_v = measured_vin_v;
        p_charger->board_vout_v = operating_voltage_v;
        p_charger->board_iout_a = 0.0F;
        p_charger->board_pin_w = 0.0F;
        p_charger->board_pout_w = 0.0F;
        p_charger->external_voltage_v = operating_voltage_v;
        p_charger->external_current_a = 0.0F;
        p_charger->external_power_w = 0.0F;
        p_charger->external_to_board_input_eff_pct = 0.0F;
        if (NULL != p_score)
        {
            *p_score = 0.0F;
        }
        *p_low_input_runtime = true;
        found = true;
    }

    bool const mid_input_liion_candidate =
        ((uint8_t) DAB_BATTERY_PROFILE_LI_ION_10S_42V ==
         (uint8_t) s_energy_shadow.config.battery_profile) &&
        ((uint8_t) DAB_CHARGE_STAGE_CONSTANT_CURRENT == preferred_stage) &&
        (measured_vin_v <= DAB_CHARGER_LIION_MID_INPUT_MAX_V) &&
        /* Select from the high-level charge target, not transient Vout.
         * Otherwise a low-power PRECHARGE seed can hold Vout below the
         * candidate window forever and prevent the CC bundle from loading. */
        (s_energy_shadow.config.battery_cv_v >=
         DAB_CHARGER_LIION_MID_OUTPUT_MIN_V) &&
        (s_energy_shadow.config.battery_cv_v <=
         DAB_CHARGER_LIION_MID_OUTPUT_MAX_V);
    if (mid_input_liion_candidate)
    {
        bool const low_input_partition =
            measured_vin_v <= DAB_CHARGER_LIION_LOW_INPUT_MAX_V;
        p_charger->entry_id = low_input_partition ?
            DAB_CHARGER_LIION_LOW_ENTRY_ID :
            DAB_CHARGER_LIION_MID_ENTRY_ID;
        p_charger->battery_profile_id =
            (uint8_t) DAB_BATTERY_PROFILE_LI_ION_10S_42V;
        p_charger->charge_stage_id =
            (uint8_t) DAB_CHARGE_STAGE_CONSTANT_CURRENT;
        p_charger->modulation_id = (uint8_t) (low_input_partition ?
            DAB_MODULATION_TPS : DAB_MODULATION_EPS);
        p_charger->control_architecture_id =
            (uint8_t) DAB_CONTROL_ARCH_ENERGY_SUPERVISOR;
        p_charger->nominal_input_v = low_input_partition ? 20.0F : 22.0F;
        p_charger->external_cv_set_v = 40.0F;
        p_charger->primary_inner_ns = low_input_partition ?
            DAB_CHARGER_LIION_LOW_PRIMARY_NS :
            DAB_CHARGER_LIION_MID_PRIMARY_NS;
        p_charger->secondary_inner_ns = low_input_partition ?
            DAB_CHARGER_LIION_LOW_SECONDARY_NS :
            DAB_CHARGER_LIION_MID_SECONDARY_NS;
        p_charger->phase_cap_ns = low_input_partition ?
            DAB_CHARGER_LIION_LOW_PHASE_CAP_NS :
            DAB_CHARGER_LIION_MID_PHASE_CAP_NS;
        p_charger->current_kp_ns_per_a = 0.5F;
        p_charger->current_ki_us_per_as = 0.05F;
        p_charger->current_slew_a_per_s = 0.05F;
        p_charger->configured_imax_a =
            selection_current_a;
        p_charger->configured_iterm_a =
            s_energy_shadow.config.battery_terminate_current_a;
        p_charger->board_vin_v = measured_vin_v;
        p_charger->board_vout_v = operating_voltage_v;
        p_charger->board_iout_a = 0.0F;
        p_charger->board_pin_w = 0.0F;
        p_charger->board_pout_w = 0.0F;
        p_charger->external_voltage_v = operating_voltage_v;
        p_charger->external_current_a = 0.0F;
        p_charger->external_power_w = 0.0F;
        p_charger->external_to_board_input_eff_pct = 0.0F;
        if (NULL != p_score)
        {
            *p_score = 0.0F;
        }
        /* Do not set the legacy sub-18 V LFP runtime partition flag. */
        *p_low_input_runtime = false;
        found = true;
    }
    return found;
}

static bool dab_schedule_apply_current(void)
{
    /* A failed high-level configuration must not fall through to a later ON
     * command with stale manual gains.  Only a complete successful apply, or
     * an explicit `sched off`, releases this start interlock. */
    s_configuration_start_blocked = true;

    if ((DAB_STATE_READY != s_state) || s_control_enabled ||
        dab_pwm_hw_outputs_are_enabled())
    {
        dab_log("[DAB][SCHED][BLOCK] apply only in READY with PWM/power OFF\r\n");
        return false;
    }

    bool const cp_architecture =
        DAB_CONTROL_ARCH_CP_DAMPED_CASCADE == s_control_architecture;
    if ((DAB_CONTROL_ARCH_OUTPUT_CURRENT_PI == s_control_architecture) ||
        cp_architecture)
    {
        dab_tuned_current_bundle_t current_bundle = {0};
        float match_score = 0.0F;
        float const measured_vin = (s_measurements.vin_v >= 1.0F) ?
                                   s_measurements.vin_v : s_bench_vin_set_v;
        float const seed_voltage_v = cp_architecture ?
            s_cp_damping.config.nominal_output_v : s_vref_v;
        float const seed_current_a = cp_architecture ?
            dab_clampf(s_cp_damping.config.target_power_w /
                       dab_maxf(seed_voltage_v, 1.0F),
                       0.0F, DAB_CURRENT_REFERENCE_MAX_A) :
            s_current_reference_command_a;
        if (cp_architecture &&
            ((s_cp_damping.config.target_power_w > s_power_limit_w) ||
             (s_cp_damping.config.target_power_w >
              (s_cp_damping.config.current_limit_a * seed_voltage_v))))
        {
            dab_schedule_mark_manual();
            dab_log("[DAB][CPSCHED][BLOCK] target=%.1fW exceeds Pceiling=%.1fW or Imax*Vnom=%.1fW\r\n",
                    (double) s_cp_damping.config.target_power_w,
                    (double) s_power_limit_w,
                    (double) (s_cp_damping.config.current_limit_a *
                              seed_voltage_v));
            return false;
        }
        bool const current_found = cp_architecture ?
            dab_tuned_current_parameter_lookup_cp_nearest(
                measured_vin,
                seed_voltage_v,
                seed_current_a,
                &current_bundle,
                &match_score) :
            dab_tuned_current_parameter_lookup_exact(
                measured_vin,
                seed_voltage_v,
                seed_current_a,
                (uint8_t) s_modulation_requested,
                (uint8_t) DAB_CONTROL_ARCH_OUTPUT_CURRENT_PI,
                &current_bundle,
                &match_score);
        if (!current_found)
        {
            dab_schedule_mark_manual();
            dab_log("[DAB][%sSCHED][NO_MATCH] no powered current seed for Vin=%.2f Vseed=%.1f Iseed=%.2f mod=%s\r\n",
                    cp_architecture ? "CP" : "",
                    (double) measured_vin,
                    (double) seed_voltage_v,
                    (double) seed_current_a,
                    dab_modulation_name(s_modulation_requested));
            return false;
        }

        float const phase_maximum_ns = (float)
            ((s_unlock_high_power ? DAB_PHASE_ABSOLUTE_MAX_COUNTS :
                                    DAB_PHASE_NORMAL_MAX_COUNTS) *
             DAB_PWM_COUNT_NS);
        float const inner_maximum_ns = (float)
            ((s_unlock_high_power ? DAB_INNER_SHIFT_MAX_COUNTS :
                                    DAB_INNER_NORMAL_MAX_COUNTS) *
             DAB_PWM_COUNT_NS);
        bool const current_valid =
            (current_bundle.current_direction != 0) &&
            (current_bundle.current_kp_ns_per_a >= DAB_CURRENT_KP_MIN_NS_PER_A) &&
            (current_bundle.current_kp_ns_per_a <= DAB_CURRENT_KP_MAX_NS_PER_A) &&
            (current_bundle.current_ki_us_per_as >= DAB_CURRENT_KI_MIN_US_PER_AS) &&
            (current_bundle.current_ki_us_per_as <= DAB_CURRENT_KI_MAX_US_PER_AS) &&
            (current_bundle.current_slew_a_per_s >= 0.01F) &&
            (current_bundle.current_slew_a_per_s <= DAB_CURRENT_REFERENCE_SLEW_MAX_A_PER_S) &&
            (current_bundle.phase_cap_ns >= 0.0F) &&
            (current_bundle.phase_cap_ns <= phase_maximum_ns) &&
            (current_bundle.primary_inner_ns >= 0.0F) &&
            (current_bundle.primary_inner_ns <= inner_maximum_ns) &&
            (current_bundle.secondary_inner_ns >= 0.0F) &&
            (current_bundle.secondary_inner_ns <= inner_maximum_ns);
        if (!current_valid)
        {
            dab_schedule_mark_manual();
            dab_log("[DAB][SCHED][BLOCK] current seed %lu failed firmware bounds\r\n",
                    (unsigned long) current_bundle.entry_id);
            return false;
        }

        /* A powered record is an exact operating seed, not an extrapolation
         * model.  Never enlarge its phase ceiling for a different target. */
        float const phase_cap_ns = current_bundle.phase_cap_ns;

        float const requested_output_w = cp_architecture ?
            s_cp_damping.config.target_power_w :
            s_vref_v * s_current_reference_command_a;
        float const power_maximum = s_unlock_high_power ?
            DAB_POWER_ABSOLUTE_MAX_W : DAB_POWER_NORMAL_MAX_W;
        if (!cp_architecture)
        {
            s_power_limit_w = dab_clampf(
                (1.20F * requested_output_w) + 25.0F,
                DAB_POWER_DEFAULT_W,
                power_maximum);
        }
        s_power_command_received = true;
        if (cp_architecture)
        {
            s_modulation_requested =
                (dab_modulation_mode_t) current_bundle.modulation_id;
        }
        s_current_control_direction = current_bundle.current_direction;
        s_current_kp_ns_per_a = current_bundle.current_kp_ns_per_a;
        s_current_ki_us_per_as = current_bundle.current_ki_us_per_as;
        s_current_reference_slew_a_per_s = cp_architecture ?
            DAB_CP_CURRENT_REFERENCE_SLEW_A_PER_S :
            current_bundle.current_slew_a_per_s;
        s_closed_loop_phase_cap_counts =
            (uint32_t) ((phase_cap_ns / (float) DAB_PWM_COUNT_NS) + 0.5F);
        s_primary_inner_manual_target_counts =
            current_bundle.primary_inner_ns / (float) DAB_PWM_COUNT_NS;
        s_secondary_inner_manual_target_counts =
            current_bundle.secondary_inner_ns / (float) DAB_PWM_COUNT_NS;
        s_inner_manual_override =
            (DAB_MODULATION_EPS == s_modulation_requested) ||
            (DAB_MODULATION_TPS == s_modulation_requested);
        s_feedforward_enabled = false;
        s_feedforward_effective_phase_ns = 0.0F;
        s_vin_adaptation.config.enabled = true;
        s_schedule_enabled = true;
        s_schedule_entry_id = current_bundle.entry_id;
        s_configuration_start_blocked = false;
        dab_control_reset();
        dab_log("[DAB][%sSCHED] current seed=%lu score=%.3f source=%.2fV/%.1fV/%.2fA request=%.1fV/%.2fA/%.1fW eta_rel=%.3f%% cap=%.0fns Plim=%.1fW slew_src/run=%.3f/%.3fA/s table=%s\r\n",
                cp_architecture ? "CP" : "",
                (unsigned long) s_schedule_entry_id,
                (double) match_score,
                (double) current_bundle.measured_vin_v,
                (double) current_bundle.voltage_ref_v,
                (double) current_bundle.current_ref_a,
                (double) seed_voltage_v,
                (double) seed_current_a,
                (double) requested_output_w,
                (double) current_bundle.proven_relative_efficiency_pct,
                (double) phase_cap_ns,
                (double) s_power_limit_w,
                (double) current_bundle.current_slew_a_per_s,
                (double) s_current_reference_slew_a_per_s,
                DAB_TUNED_PARAMETER_TABLE_VERSION);
        dab_print_parameter_bundle();
        return true;
    }

    if (DAB_CONTROL_ARCH_ENERGY_SUPERVISOR == s_control_architecture)
    {
        dab_validated_charger_entry_t charger = {0};
        float score = 0.0F;
        float const measured_vin = (s_measurements.vin_v >= 1.0F) ?
                                   s_measurements.vin_v : s_bench_vin_set_v;
        uint8_t const raw_stage =
            (uint8_t) s_energy_shadow.output.charge_stage;
        float const operating_voltage_v =
            (s_measurements.valid && (s_measurements.vout_v >= 5.0F)) ?
            s_measurements.vout_v : s_energy_shadow.config.battery_cv_v;
        bool low_input_runtime = false;
        bool const found = dab_charger_bundle_select(
            measured_vin,
            operating_voltage_v,
            raw_stage,
            g_dab_cpu0_dual_core_status.energy_current_reference_a,
            &charger,
            &score,
            &low_input_runtime);
        float const phase_maximum_ns = (float)
            ((s_unlock_high_power ? DAB_PHASE_ABSOLUTE_MAX_COUNTS :
                                    DAB_PHASE_NORMAL_MAX_COUNTS) *
             DAB_PWM_COUNT_NS);
        float const ordinary_inner_maximum_ns = (float)
            ((s_unlock_high_power ? DAB_INNER_SHIFT_MAX_COUNTS :
                                    DAB_INNER_NORMAL_MAX_COUNTS) *
             DAB_PWM_COUNT_NS);
        float const primary_inner_maximum_ns =
            (found &&
             ((DAB_VALIDATED_LIION_PRECHARGE_ENTRY_ID == charger.entry_id) ||
              (DAB_VALIDATED_LFP_PRECHARGE_ENTRY_ID == charger.entry_id) ||
              (DAB_VALIDATED_LFP_MPPT_ULTRALOW_ENTRY_ID == charger.entry_id)) &&
             s_unlock_high_power) ?
            (float) (DAB_PRECHARGE_PRIMARY_INNER_MAX_COUNTS *
                     DAB_PWM_COUNT_NS) : ordinary_inner_maximum_ns;
        bool const valid = found &&
            (charger.modulation_id <= (uint8_t) DAB_MODULATION_TPS) &&
            (charger.current_kp_ns_per_a >= DAB_CURRENT_KP_MIN_NS_PER_A) &&
            (charger.current_kp_ns_per_a <= DAB_CURRENT_KP_MAX_NS_PER_A) &&
            (charger.current_ki_us_per_as >= DAB_CURRENT_KI_MIN_US_PER_AS) &&
            (charger.current_ki_us_per_as <= DAB_CURRENT_KI_MAX_US_PER_AS) &&
            (charger.current_slew_a_per_s >= 0.01F) &&
            (charger.current_slew_a_per_s <= DAB_CURRENT_REFERENCE_SLEW_MAX_A_PER_S) &&
            (charger.phase_cap_ns >= 0.0F) &&
            (charger.phase_cap_ns <= phase_maximum_ns) &&
            (charger.primary_inner_ns >= 0.0F) &&
            (charger.primary_inner_ns <= primary_inner_maximum_ns) &&
            (charger.secondary_inner_ns >= 0.0F) &&
            (charger.secondary_inner_ns <= ordinary_inner_maximum_ns);
        if (!valid)
        {
            dab_schedule_mark_manual();
            dab_log("[DAB][CHGSCHED][NO_MATCH] profile=%s Vin=%.2f Vcv=%.2f Imax=%.2f has no bounded powered seed\r\n",
                    dab_battery_profile_name(s_energy_shadow.config.battery_profile),
                    (double) measured_vin,
                    (double) s_energy_shadow.config.battery_cv_v,
                    (double) s_energy_shadow.config.battery_max_current_a);
            return false;
        }

        s_modulation_requested =
            (dab_modulation_mode_t) charger.modulation_id;
        s_current_control_direction =
            dab_validated_charger_current_direction(charger.entry_id);
        s_current_kp_ns_per_a = charger.current_kp_ns_per_a;
        s_current_ki_us_per_as = charger.current_ki_us_per_as;
        s_current_reference_slew_a_per_s = charger.current_slew_a_per_s;
        /* A parameter bundle tunes the plant controller.  Charge targets
         * remain owned by the selected firmware battery profile; a powered
         * record's historic 1 A test label must never overwrite them. */
        s_closed_loop_phase_cap_counts = (uint32_t)
            ((charger.phase_cap_ns / (float) DAB_PWM_COUNT_NS) + 0.5F);
        s_primary_inner_manual_target_counts =
            charger.primary_inner_ns / (float) DAB_PWM_COUNT_NS;
        s_secondary_inner_manual_target_counts =
            charger.secondary_inner_ns / (float) DAB_PWM_COUNT_NS;
        s_inner_manual_override =
            (DAB_MODULATION_EPS == s_modulation_requested) ||
            (DAB_MODULATION_TPS == s_modulation_requested);
        s_vin_adaptation.config.enabled = true;
        s_feedforward_enabled = false;
        s_feedforward_effective_phase_ns = 0.0F;
        s_power_limit_w = dab_clampf(
            (1.20F * s_energy_shadow.config.battery_cv_v *
             s_energy_shadow.config.battery_max_current_a) + 25.0F,
            DAB_POWER_DEFAULT_W,
            s_unlock_high_power ? DAB_POWER_ABSOLUTE_MAX_W :
                                  DAB_POWER_NORMAL_MAX_W);
        s_power_command_received = true;
        s_schedule_enabled = true;
        s_schedule_entry_id = charger.entry_id;
        s_charger_low_input_runtime = low_input_runtime;
        s_charger_runtime_candidate_id = 0U;
        s_charger_runtime_candidate_low_input = false;
        s_configuration_start_blocked = false;
        dab_control_reset();
        dab_log("[DAB][CHGSCHED] entry=%lu score=%.3f profile=%s stage_seed=%s mod=%s source=%.2fV/%.2fV/%.2fA eta_ext=%.3f%% table=%s\r\n",
                (unsigned long) charger.entry_id,
                (double) score,
                dab_battery_profile_name(s_energy_shadow.config.battery_profile),
                dab_charge_stage_name((dab_charge_stage_t) charger.charge_stage_id),
                dab_modulation_name(s_modulation_requested),
                (double) charger.board_vin_v,
                (double) charger.external_cv_set_v,
                (double) charger.configured_imax_a,
                (double) charger.external_to_board_input_eff_pct,
                DAB_VALIDATED_CHARGER_TABLE_VERSION);
        dab_print_parameter_bundle();
        return true;
    }

    dab_tuned_parameter_bundle_t bundle = {0};
    bool const found = dab_tuned_parameter_lookup_exact(
        s_bench_vin_set_v,
        s_vref_v,
        s_load_nominal_ohm,
        s_requested_power_layer_w,
        (uint8_t) s_modulation_requested,
        (uint8_t) s_control_architecture,
        &bundle);
    if (!found)
    {
        dab_schedule_mark_manual();
        dab_log("[DAB][SCHED][NO_MATCH] no promoted entry for VinSet=%.2f Vref=%.1f R=%.2f P=%.1f mod=%s arch=%s; manual bundle retained\r\n",
                (double) s_bench_vin_set_v,
                (double) s_vref_v,
                (double) s_load_nominal_ohm,
                (double) s_requested_power_layer_w,
                dab_modulation_name(s_modulation_requested),
                dab_control_architecture_name(s_control_architecture));
        return false;
    }

    float const power_maximum = s_unlock_high_power ?
        DAB_POWER_ABSOLUTE_MAX_W : DAB_POWER_NORMAL_MAX_W;
    float const phase_maximum_ns = (float)
        ((s_unlock_high_power ? DAB_PHASE_ABSOLUTE_MAX_COUNTS :
                                DAB_PHASE_NORMAL_MAX_COUNTS) *
         DAB_PWM_COUNT_NS);
    float const inner_maximum_ns = (float)
        ((s_unlock_high_power ? DAB_INNER_SHIFT_MAX_COUNTS :
                                DAB_INNER_NORMAL_MAX_COUNTS) *
         DAB_PWM_COUNT_NS);
    bool const valid =
        (bundle.control_architecture_id ==
         (uint8_t) DAB_CONTROL_ARCH_VOLTAGE_PI_SINGLE) &&
        (bundle.outer_kp_ns_per_v >= DAB_KP_MIN_NS_PER_V) &&
        (bundle.outer_kp_ns_per_v <= DAB_KP_MAX_NS_PER_V) &&
        (bundle.outer_ki_us_per_vs >= DAB_KI_MIN_US_PER_VS) &&
        (bundle.outer_ki_us_per_vs <= DAB_KI_MAX_US_PER_VS) &&
        (bundle.outer_kd_ns_s_per_v >= DAB_KD_MIN_NS_S_PER_V) &&
        (bundle.outer_kd_ns_s_per_v <= DAB_KD_MAX_NS_S_PER_V) &&
        (dab_measurements_abs(bundle.derivative_filter_alpha -
                              DAB_DERIVATIVE_FILTER_ALPHA) <= 0.0001F) &&
        (bundle.power_command_label_w >= DAB_POWER_DEFAULT_W) &&
        (bundle.power_command_label_w <= power_maximum) &&
        (bundle.phase_cap_ns >= 0.0F) &&
        (bundle.phase_cap_ns <= phase_maximum_ns) &&
        (bundle.feedforward_effective_phase_ns >= 0.0F) &&
        (bundle.feedforward_effective_phase_ns <= bundle.phase_cap_ns) &&
        (bundle.primary_inner_ns >= 0.0F) &&
        (bundle.primary_inner_ns <= inner_maximum_ns) &&
        (bundle.secondary_inner_ns >= 0.0F) &&
        (bundle.secondary_inner_ns <= inner_maximum_ns);
    if (!valid)
    {
        dab_schedule_mark_manual();
        dab_log("[DAB][SCHED][BLOCK] promoted entry %lu failed firmware bounds; manual bundle retained\r\n",
                (unsigned long) bundle.entry_id);
        return false;
    }

    s_kp_ns_per_v = bundle.outer_kp_ns_per_v;
    s_ki_us_per_vs = bundle.outer_ki_us_per_vs;
    s_kd_ns_s_per_v = bundle.outer_kd_ns_s_per_v;
    s_power_limit_w = bundle.power_command_label_w;
    s_power_command_received = true;
    s_closed_loop_phase_cap_counts =
        (uint32_t) ((bundle.phase_cap_ns / (float) DAB_PWM_COUNT_NS) + 0.5F);
    s_primary_inner_manual_target_counts =
        bundle.primary_inner_ns / (float) DAB_PWM_COUNT_NS;
    s_secondary_inner_manual_target_counts =
        bundle.secondary_inner_ns / (float) DAB_PWM_COUNT_NS;
    s_inner_manual_override =
        (DAB_MODULATION_EPS == s_modulation_requested) ||
        (DAB_MODULATION_TPS == s_modulation_requested);
    s_feedforward_effective_phase_ns =
        bundle.feedforward_effective_phase_ns;
    s_feedforward_enabled = s_feedforward_effective_phase_ns > 0.0F;
    s_schedule_enabled = true;
    s_schedule_entry_id = bundle.entry_id;
    s_configuration_start_blocked = false;
    dab_control_reset();
    dab_log("[DAB][SCHED] applied validated entry=%lu table=%s\r\n",
            (unsigned long) s_schedule_entry_id,
            DAB_TUNED_PARAMETER_TABLE_VERSION);
    dab_print_parameter_bundle();
    return true;
}

static void dab_charger_runtime_bundle_apply(
    dab_validated_charger_entry_t const * p_charger,
    bool low_input_runtime)
{
    s_modulation_requested =
        (dab_modulation_mode_t) p_charger->modulation_id;
    s_current_control_direction =
        dab_validated_charger_current_direction(p_charger->entry_id);
    s_current_kp_ns_per_a = p_charger->current_kp_ns_per_a;
    s_current_ki_us_per_as = p_charger->current_ki_us_per_as;
    s_current_reference_slew_a_per_s =
        dab_validated_charger_runtime_current_slew(
            p_charger->charge_stage_id,
            s_energy_shadow.config.battery_max_current_a,
            p_charger->current_slew_a_per_s);
    s_closed_loop_phase_cap_counts = (uint32_t)
        ((p_charger->phase_cap_ns / (float) DAB_PWM_COUNT_NS) + 0.5F);
    s_primary_inner_manual_target_counts =
        p_charger->primary_inner_ns / (float) DAB_PWM_COUNT_NS;
    s_secondary_inner_manual_target_counts =
        p_charger->secondary_inner_ns / (float) DAB_PWM_COUNT_NS;
    s_inner_manual_override =
        (DAB_MODULATION_EPS == s_modulation_requested) ||
        (DAB_MODULATION_TPS == s_modulation_requested);
    s_feedforward_enabled = false;
    s_feedforward_effective_phase_ns = 0.0F;
    s_schedule_entry_id = p_charger->entry_id;
    s_charger_low_input_runtime = low_input_runtime;
    s_charger_runtime_candidate_id = 0U;
    s_charger_runtime_candidate_low_input = false;
    s_configuration_start_blocked = false;
}

static void dab_charger_runtime_schedule_service(void)
{
    TickType_t const now = xTaskGetTickCount();
    if ((now - s_charger_runtime_last_select_tick) <
        pdMS_TO_TICKS(DAB_CHARGER_RUNTIME_RESELECT_MS))
    {
        return;
    }
    s_charger_runtime_last_select_tick = now;

    bool const charger_running = s_schedule_autoload_enabled &&
        s_schedule_enabled && s_control_enabled &&
        (DAB_CONTROL_ARCH_ENERGY_SUPERVISOR == s_control_architecture) &&
        s_energy_shadow.config.battery_start && s_measurements.valid &&
        ((DAB_STATE_SOFTSTART == s_state) || (DAB_STATE_RUN == s_state));
    if (!charger_running)
    {
        s_charger_runtime_candidate_id = 0U;
        s_charger_runtime_candidate_low_input = false;
        return;
    }

    uint8_t const raw_stage =
        (uint8_t) g_dab_cpu0_dual_core_status.charge_stage;

    dab_validated_charger_entry_t charger = {0};
    float score = 0.0F;
    bool low_input_runtime = false;
    if (!dab_charger_bundle_select(s_measurements.vin_v,
                                   s_measurements.vout_v,
                                   raw_stage,
                                   g_dab_cpu0_dual_core_status.energy_current_reference_a,
                                   &charger,
                                   &score,
                                   &low_input_runtime))
    {
        return;
    }

    /* A downward battery-voltage transition into PRECHARGE is a topology
     * change, not an ordinary tuning interpolation.  Keeping PWM active while
     * the 0.04 ns/ms inner slew walks from a CC/CV shape to the precharge
     * shape left the prior high-power bridge command active for many seconds
     * in the live 44 V -> 29 V sweep.  Remove all gate/path drive first, load
     * the precharge bundle while PWM is OFF, then autonomously traverse the
     * normal reset/settle/soft-start sequence.  This decision is driven only
     * by the battery state machine; it is not an Iin/Iout trip threshold. */
    bool const selected_precharge =
        dab_validated_charger_is_precharge_entry(charger.entry_id);
    bool const active_precharge =
        dab_validated_charger_is_precharge_entry(s_schedule_entry_id);
    if (selected_precharge && !active_precharge)
    {
        uint32_t const previous_entry = s_schedule_entry_id;
        dab_log("[DAB][CHGAUTO][PRECHARGE_RESTART] stage=%u entry=%lu->%lu; PWM and four paths OFF before offline pre-arm\r\n",
                (unsigned int) raw_stage,
                (unsigned long) previous_entry,
                (unsigned long) charger.entry_id);
        dab_force_all_safe();
        dab_charger_runtime_bundle_apply(&charger, low_input_runtime);
        dab_control_reset();
        s_control_enabled = true;
        dab_board_fan_set(true);
        dab_enter_state(DAB_STATE_RESET_LOW);
        s_force_telemetry = true;
        return;
    }
    if ((charger.entry_id == s_schedule_entry_id) &&
        (low_input_runtime == s_charger_low_input_runtime))
    {
        s_charger_runtime_candidate_id = 0U;
        s_charger_runtime_candidate_low_input = false;
        return;
    }
    if ((charger.entry_id != s_charger_runtime_candidate_id) ||
        (low_input_runtime != s_charger_runtime_candidate_low_input))
    {
        s_charger_runtime_candidate_id = charger.entry_id;
        s_charger_runtime_candidate_low_input = low_input_runtime;
        s_charger_runtime_candidate_tick = now;
        dab_log("[DAB][CHGAUTO] candidate=%lu Vin=%.2fV Vout=%.2fV mod=%s confirm=%lums\r\n",
                (unsigned long) charger.entry_id,
                (double) s_measurements.vin_v,
                (double) s_measurements.vout_v,
                dab_modulation_name((dab_modulation_mode_t)
                                    charger.modulation_id),
                (unsigned long) DAB_CHARGER_RUNTIME_CONFIRM_MS);
        return;
    }
    if ((now - s_charger_runtime_candidate_tick) <
        pdMS_TO_TICKS(DAB_CHARGER_RUNTIME_CONFIRM_MS))
    {
        return;
    }

    dab_charger_runtime_bundle_apply(&charger, low_input_runtime);
    s_force_telemetry = true;
    dab_log("[DAB][CHGAUTO] applied=%lu score=%.3f Vin=%.2fV mod=%s inner=%.0f/%.0fns cap=%.0fns source=LOCAL_TABLE AI=OPTIONAL\r\n",
            (unsigned long) charger.entry_id,
            (double) score,
            (double) s_measurements.vin_v,
            dab_modulation_name(s_modulation_requested),
            (double) charger.primary_inner_ns,
            (double) charger.secondary_inner_ns,
            (double) charger.phase_cap_ns);
}

static void dab_mppt_phase_search_service(void)
{
    bool const enabled = s_schedule_autoload_enabled &&
        s_schedule_enabled && s_control_enabled &&
        (DAB_CONTROL_ARCH_ENERGY_SUPERVISOR == s_control_architecture) &&
        (DAB_ENERGY_MODE_SOLAR_CHARGER == s_energy_shadow.config.mode) &&
        ((DAB_MPPT_MODE_PERTURB_OBSERVE ==
          s_energy_shadow.config.mppt_mode) ||
         (DAB_MPPT_MODE_INCREMENTAL_CONDUCTANCE ==
          s_energy_shadow.config.mppt_mode)) &&
        (DAB_CHARGE_STAGE_CONSTANT_CURRENT ==
         g_dab_cpu0_dual_core_status.charge_stage) &&
        (DAB_STATE_RUN == s_state) && s_measurements.valid;
    dab_mppt_phase_input_t const input =
    {
        .enabled = enabled,
        .valid = s_measurements.valid,
        .now_ms = (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS),
        .vin_v = s_measurements.vin_v,
        .vout_v = s_measurements.vout_v,
        .pin_w = s_measurements.pin_w,
        .pout_w = s_measurements.pout_w,
        .current_reference_a =
            g_dab_cpu0_dual_core_status.energy_current_reference_a,
        .current_active_a = s_current_reference_active_a,
        .measured_output_current_a = s_measurements.iout_a,
        .outer_saturated = s_outer_saturated,
        .applied_mode = (dab_mppt_phase_mode_t) s_modulation_effective,
        .applied_outer_phase_ns =
            (float) dab_pwm_hw_phase_get() * (float) DAB_PWM_COUNT_NS,
        .applied_primary_inner_ns =
            s_primary_inner_command_counts * (float) DAB_PWM_COUNT_NS,
        .applied_secondary_inner_ns =
            s_secondary_inner_command_counts * (float) DAB_PWM_COUNT_NS
    };
    dab_mppt_phase_update(&s_mppt_phase_search, &input);
    if (!s_mppt_phase_search.output.active)
    {
        return;
    }

    dab_mppt_phase_output_t const * const p =
        &s_mppt_phase_search.output;
    s_modulation_requested = (dab_modulation_mode_t) p->mode;
    if (DAB_MPPT_PHASE_MODE_SPS == p->mode)
    {
        s_inner_manual_override = false;
        s_primary_inner_manual_target_counts = 0.0F;
        s_secondary_inner_manual_target_counts = 0.0F;
    }
    else
    {
        s_inner_manual_override = true;
        s_primary_inner_manual_target_counts =
            p->primary_inner_ns / (float) DAB_PWM_COUNT_NS;
        s_secondary_inner_manual_target_counts =
            p->secondary_inner_ns / (float) DAB_PWM_COUNT_NS;
    }
    /* CPU1 owns the reference and it remains limited in both directions.
     * Inner shifts use the existing 1 ns/ms slew; the raw-phase conversion
     * compensates each PWM update to keep effective phase/power continuous. */
    s_current_reference_slew_a_per_s =
        dab_minf(s_current_reference_slew_a_per_s,
                 DAB_MPPT_PHASE_SEARCH_CURRENT_SLEW);
    /* The winning outer phase is retained as the initial HOLD cap plus a
     * small margin. It is then released gradually whenever the live current
     * loop saturates below the P&O request; it is never a permanent lock. */
    float const replay_phase_cap_ns = dab_clampf(
        p->phase_cap_ns, 0.0F, DAB_MPPT_PHASE_SEARCH_PHASE_CAP_NS);
    s_closed_loop_phase_cap_counts = (uint32_t)
        ((replay_phase_cap_ns /
          (float) DAB_PWM_COUNT_NS) + 0.5F);

    if (p->changed)
    {
        dab_log("[MPPT][PHASE] stage=%u mode=%s outer=%.0fns cap=%.0fns inner=%.0f/%.0fns PinBest=%.2fW etaBest=%.3f%% complete=%u research=%s AI=optional slewI<=0.5A/s slewInner<=0.2ns/ms\r\n",
                (unsigned int) p->stage,
                dab_modulation_name(s_modulation_requested),
                (double) p->selected_outer_phase_ns,
                (double) p->phase_cap_ns,
                (double) p->primary_inner_ns,
                (double) p->secondary_inner_ns,
                (double) p->selected_pin_w,
                (double) p->selected_efficiency_percent,
                p->selection_complete ? 1U : 0U,
                s_mppt_phase_search.local_research ? "LOCAL" : "INITIAL");
        s_force_telemetry = true;
    }
}

static bool dab_charger_completion_service(void)
{
    bool const completed = s_schedule_autoload_enabled &&
        s_schedule_enabled && s_control_enabled &&
        (DAB_CONTROL_ARCH_ENERGY_SUPERVISOR == s_control_architecture) &&
        s_energy_shadow.config.battery_start &&
        g_dab_cpu0_dual_core_status.energy_reference_valid &&
        (DAB_CHARGE_STAGE_DONE ==
         g_dab_cpu0_dual_core_status.charge_stage);
    if (!completed)
    {
        return false;
    }

    /* CPU1 has already completed the 30 s termination dwell.  DONE is a
     * functional terminal state: CPU0 must remove switching and all four
     * power paths without waiting for an upper-computer stop command. */
    dab_force_all_safe();
    s_schedule_autoload_enabled = false;
    s_schedule_enabled = false;
    s_charger_runtime_candidate_id = 0U;
    s_charger_runtime_candidate_low_input = false;
    dab_enter_state(DAB_STATE_READY);
    s_force_telemetry = true;
    dab_log("[BAT][AUTO][DONE] CPU1 termination complete; PWM=0 RUN=0 four path MOS=OFF; next charge requires a new high-level bat start\r\n");
    return true;
}

static void dab_can_snapshot_build(
    dab_can_application_status_t * const p_status,
    dab_can_measurements_t * const p_measurements)
{
    dab_can_transport_status_t transport = {0};
    dab_can_transport_status_get(&transport);

    if (NULL != p_status)
    {
        p_status->state = (uint8_t) s_state;
        p_status->architecture = (uint8_t) s_control_architecture;
        p_status->modulation = (uint8_t) s_modulation_effective;
        p_status->flags =
            (uint8_t) ((s_measurements.valid ? (1U << 0) : 0U) |
                       (s_control_enabled ? (1U << 1) : 0U) |
                       (dab_pwm_hw_outputs_are_enabled() ? (1U << 2) : 0U) |
                       (s_test_mode ? (1U << 3) : 0U) |
                       (transport.bus_off ? (1U << 4) : 0U) |
                       (s_cp_damping.config.enabled ? (1U << 5) : 0U) |
                       (s_schedule_enabled ? (1U << 6) : 0U) |
                       (DAB_SOFTWARE_AUTO_TRIP_ENABLE ? (1U << 7) : 0U));
        p_status->fault_mask = s_fault_mask;
    }
    if (NULL != p_measurements)
    {
        p_measurements->vin_v = s_measurements.vin_v;
        p_measurements->vout_v = s_measurements.vout_v;
        p_measurements->iout_a = s_measurements.iout_a;
        p_measurements->pout_w = s_measurements.pout_w;
    }
}

static void dab_can_command_service(void)
{
    /* Bound work per control-loop pass even if a host floods command ID 0x200. */
    for (uint32_t processed = 0U; processed < 4U; processed++)
    {
        dab_can_command_t command;
        if (!dab_can_transport_command_take(&command))
        {
            break;
        }

        dab_can_result_t result = command.decode_result;
        bool publish_status = false;
        if (DAB_CAN_RESULT_OK == result)
        {
            switch (command.opcode)
            {
                case DAB_CAN_OPCODE_PING:
                    break;

                case DAB_CAN_OPCODE_GET_STATUS:
                    publish_status = true;
                    break;

                case DAB_CAN_OPCODE_STOP:
                    dab_force_all_safe();
                    dab_enter_state((0U == s_fault_mask) ?
                                    DAB_STATE_READY : DAB_STATE_FAULT);
                    dab_log("[DAB][CAN] STOP PWM=0 RUN=0 four path MOS=OFF\r\n");
                    publish_status = true;
                    break;

                case DAB_CAN_OPCODE_SET_CP_TARGET:
                {
                    bool const configuration_safe =
                        (DAB_STATE_READY == s_state) &&
                        !s_control_enabled &&
                        !s_test_mode &&
                        !dab_pwm_hw_outputs_are_enabled();
                    float const allowed_power_limit_w = s_unlock_high_power ?
                        DAB_POWER_ABSOLUTE_MAX_W : DAB_POWER_NORMAL_MAX_W;
                    float const current_power_limit_w =
                        s_cp_damping.config.current_limit_a *
                        s_cp_damping.config.nominal_output_v;
                    if (!configuration_safe)
                    {
                        result = DAB_CAN_RESULT_BAD_STATE;
                    }
                    else if ((command.cp_target_w < DAB_POWER_DEFAULT_W) ||
                             (command.cp_target_w > allowed_power_limit_w) ||
                             (command.cp_target_w > current_power_limit_w))
                    {
                        result = DAB_CAN_RESULT_OUT_OF_RANGE;
                    }
                    else
                    {
                        dab_control_architecture_t const previous_architecture =
                            s_control_architecture;
                        dab_cp_damping_config_t const previous_cp_config =
                            s_cp_damping.config;
                        float const previous_power_limit_w = s_power_limit_w;
                        bool const previous_power_command_received =
                            s_power_command_received;
                        bool const previous_algorithm_logging_enabled =
                            s_algorithm_logging_enabled;
                        bool const previous_schedule_enabled = s_schedule_enabled;
                        uint32_t const previous_schedule_entry_id =
                            s_schedule_entry_id;
                        bool const previous_feedforward_enabled =
                            s_feedforward_enabled;
                        float const previous_feedforward_effective_phase_ns =
                            s_feedforward_effective_phase_ns;

                        s_control_architecture =
                            DAB_CONTROL_ARCH_CP_DAMPED_CASCADE;
                        s_cp_damping.config.enabled = true;
                        s_cp_damping.config.target_power_w = command.cp_target_w;
                        s_power_limit_w = command.cp_target_w;
                        s_power_command_received = true;
                        dab_cp_damping_runtime_reset(&s_cp_damping);
                        s_algorithm_logging_enabled = true;
                        if (!dab_schedule_apply_current())
                        {
                            s_control_architecture = previous_architecture;
                            s_cp_damping.config = previous_cp_config;
                            dab_cp_damping_runtime_reset(&s_cp_damping);
                            s_power_limit_w = previous_power_limit_w;
                            s_power_command_received =
                                previous_power_command_received;
                            s_algorithm_logging_enabled =
                                previous_algorithm_logging_enabled;
                            s_schedule_enabled = previous_schedule_enabled;
                            s_schedule_entry_id = previous_schedule_entry_id;
                            s_feedforward_enabled =
                                previous_feedforward_enabled;
                            s_feedforward_effective_phase_ns =
                                previous_feedforward_effective_phase_ns;
                            result = DAB_CAN_RESULT_NO_VALIDATED_SEED;
                        }
                        else
                        {
                            dab_log("[DAB][CAN] CP target %.1fW armed from powered current seed; CAN START is intentionally unsupported\r\n",
                                    (double) command.cp_target_w);
                        }
                    }
                    publish_status = true;
                    break;
                }

                default:
                    result = DAB_CAN_RESULT_BAD_OPCODE;
                    break;
            }
        }

        dab_can_application_status_t status;
        dab_can_measurements_t measurements;
        dab_can_snapshot_build(&status, &measurements);
        (void) dab_can_transport_ack_queue(command.opcode,
                                           command.sequence,
                                           result,
                                           &status);
        if (publish_status)
        {
            (void) dab_can_transport_status_queue(&status, &measurements);
        }
    }
}

static void dab_remote_command_service(void)
{
    dab_remote_command_t command;
    if (!dab_remote_command_take(&command))
    {
        return;
    }

    bool ok = false;
    char const * p_error = "unsupported_command";
    switch (command.type)
    {
        case DAB_REMOTE_COMMAND_START:
#if DAB_ALLOW_POWER_TRANSFER
            if (s_test_mode)
            {
                p_error = "diagnostic_test_mode_active";
                break;
            }
            if (!s_power_command_received)
            {
                s_power_limit_w = DAB_POWER_DEFAULT_W;
                s_power_command_received = true;
            }
            if (s_schedule_autoload_enabled &&
                !s_schedule_enabled &&
                !dab_schedule_apply_current())
            {
                p_error = "automatic_schedule_no_valid_seed";
                break;
            }
            dab_control_start_request();
            ok = s_control_enabled;
            p_error = ok ? "" : "start_rejected";
#else
            p_error = "power_transfer_compile_locked";
#endif
            break;

        case DAB_REMOTE_COMMAND_STOP:
            dab_force_all_safe();
            dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY : DAB_STATE_FAULT);
            dab_log("[DAB][ETH] STOP PWM=0 RUN=0 four path MOS=OFF\r\n");
            ok = true;
            p_error = "";
            break;

        case DAB_REMOTE_COMMAND_SET_PARAMETER:
            switch (command.parameter)
            {
                case DAB_REMOTE_PARAMETER_POWER_LIMIT_W:
                    ok = dab_power_limit_set(command.value);
                    p_error = ok ? "" : "power_limit_out_of_range";
                    break;

                case DAB_REMOTE_PARAMETER_VOLTAGE_REF_V:
                    ok = (command.value >= DAB_VREF_MIN_V) &&
                         (command.value <= DAB_VREF_MAX_V);
                    if (ok)
                    {
                        s_vref_v = command.value;
                        dab_schedule_mark_manual();
                        s_pi_integral_counts = 0.0F;
                    }
                    p_error = ok ? "" : "voltage_ref_out_of_range";
                    break;

                case DAB_REMOTE_PARAMETER_KP_NS_PER_V:
                    ok = (command.value >= DAB_KP_MIN_NS_PER_V) &&
                         (command.value <= DAB_KP_MAX_NS_PER_V);
                    if (ok)
                    {
                        s_kp_ns_per_v = command.value;
                        dab_schedule_mark_manual();
                    }
                    p_error = ok ? "" : "kp_out_of_range";
                    break;

                case DAB_REMOTE_PARAMETER_KI_US_PER_VS:
                    ok = (command.value >= DAB_KI_MIN_US_PER_VS) &&
                         (command.value <= DAB_KI_MAX_US_PER_VS);
                    if (ok)
                    {
                        s_ki_us_per_vs = command.value;
                        dab_schedule_mark_manual();
                    }
                    p_error = ok ? "" : "ki_out_of_range";
                    break;

                case DAB_REMOTE_PARAMETER_KD_NS_S_PER_V:
                    ok = (command.value >= DAB_KD_MIN_NS_S_PER_V) &&
                         (command.value <= DAB_KD_MAX_NS_S_PER_V);
                    if (ok)
                    {
                        s_kd_ns_s_per_v = command.value;
                        s_derivative_initialized = false;
                        dab_schedule_mark_manual();
                    }
                    p_error = ok ? "" : "kd_out_of_range";
                    break;

                case DAB_REMOTE_PARAMETER_PHASE_NS:
                {
                    float const maximum_ns = (float) (DAB_PHASE_ABSOLUTE_MAX_COUNTS *
                                                       DAB_PWM_COUNT_NS);
                    ok = (DAB_STATE_RUN == s_state) &&
                         (DAB_MODULATION_SPS == s_modulation_requested) &&
                         (command.value >= 0.0F) && (command.value <= maximum_ns);
                    if (ok)
                    {
                        s_manual_phase_counts =
                            (uint32_t) ((command.value / (float) DAB_PWM_COUNT_NS) + 0.5F);
                        s_manual_phase_override = true;
                        dab_schedule_mark_manual();
                    }
                    p_error = ok ? "" : "phase_requires_running_sps_or_value_out_of_range";
                    break;
                }

                case DAB_REMOTE_PARAMETER_MODULATION:
                {
                    dab_modulation_mode_t mode = DAB_MODULATION_SPS;
                    ok = dab_remote_modulation_parse(command.text, &mode) &&
                         dab_modulation_set(mode);
                    p_error = ok ? "" : "modulation_requires_ready_or_value_invalid";
                    break;
                }

                case DAB_REMOTE_PARAMETER_LOAD_MODE:
                    if ((0 == strcmp(command.text, "resistive_load")) ||
                        (0 == strcmp(command.text, "resistive")))
                    {
                        dab_remote_text_set(s_load_mode,
                                            sizeof(s_load_mode),
                                            "resistive_load");
                        ok = true;
                    }
                    else if ((0 == strcmp(command.text, "battery_load")) ||
                             (0 == strcmp(command.text, "battery")))
                    {
                        dab_remote_text_set(s_load_mode,
                                            sizeof(s_load_mode),
                                            "battery_load");
                        ok = true;
                    }
                    p_error = ok ? "" : "load_mode_value_invalid";
                    break;

                case DAB_REMOTE_PARAMETER_BENCH_VIN_SET_V:
                    ok = (DAB_STATE_READY == s_state) &&
                         !dab_pwm_hw_outputs_are_enabled() &&
                         (command.value >= DAB_BENCH_VIN_SET_MIN_V) &&
                         (command.value <= DAB_BENCH_VIN_SET_MAX_V);
                    if (ok)
                    {
                        s_bench_vin_set_v = command.value;
                        dab_schedule_mark_manual();
                    }
                    p_error = ok ? "" : "vinset_requires_ready_or_out_of_range";
                    break;

                case DAB_REMOTE_PARAMETER_LOAD_NOMINAL_OHM:
                    ok = (DAB_STATE_READY == s_state) &&
                         !dab_pwm_hw_outputs_are_enabled() &&
                         (command.value >= DAB_LOAD_NOMINAL_MIN_OHM) &&
                         (command.value <= DAB_LOAD_NOMINAL_MAX_OHM);
                    if (ok)
                    {
                        s_load_nominal_ohm = command.value;
                        dab_schedule_mark_manual();
                    }
                    p_error = ok ? "" : "rload_requires_ready_or_out_of_range";
                    break;

                case DAB_REMOTE_PARAMETER_REQUESTED_POWER_LAYER_W:
                    ok = (DAB_STATE_READY == s_state) &&
                         !dab_pwm_hw_outputs_are_enabled() &&
                         (command.value >= DAB_POWER_LAYER_MIN_W) &&
                         (command.value <= DAB_POWER_LAYER_MAX_W);
                    if (ok)
                    {
                        s_requested_power_layer_w = command.value;
                        dab_schedule_mark_manual();
                    }
                    p_error = ok ? "" : "power_layer_requires_ready_or_out_of_range";
                    break;

                case DAB_REMOTE_PARAMETER_CONTROL_ARCHITECTURE:
                    ok = (DAB_STATE_READY == s_state) &&
                         !dab_pwm_hw_outputs_are_enabled() &&
                         ((0 == strcmp(command.text, "single")) ||
                          (0 == strcmp(command.text,
                                      "VOLTAGE_PI_SINGLE_LOOP")));
                    if (ok)
                    {
                        s_control_architecture =
                            DAB_CONTROL_ARCH_VOLTAGE_PI_SINGLE;
                        dab_schedule_mark_manual();
                    }
                    p_error = ok ? "" :
                        "cascade_blocked_pending_current_sensor_validation";
                    break;

                case DAB_REMOTE_PARAMETER_SCHEDULE:
                    if (0 == strcmp(command.text, "off"))
                    {
                        dab_schedule_mark_manual();
                        ok = true;
                    }
                    else if (0 == strcmp(command.text, "on"))
                    {
                        ok = dab_schedule_apply_current();
                    }
                    p_error = ok ? "" : "schedule_no_validated_exact_match";
                    break;

                default:
                    p_error = "unknown_parameter";
                    break;
            }
            break;

        default:
            break;
    }

    dab_remote_snapshot_publish_now();
    dab_remote_command_complete(command.ticket, ok, p_error);
}

static void dab_nopower_diag_arm(const char * argument)
{
    if ((NULL != argument) && (0 == strcmp(argument, "off")))
    {
        s_nopower_diag_arm_available = false;
        s_nopower_diag_arm_deadline = 0U;
        dab_log("[DAB][DIAG-ARM] OFF\r\n");
        return;
    }

    if ((NULL == argument) || (0 != strcmp(argument, "isolated")))
    {
        dab_log("[DAB][DIAG-ARM] use diagarm isolated only when PRE/MAIN/OUT/RUN/PWM are OFF\r\n");
        dab_log("[DAB][DIAG-ARM] one VIN-sensor bypass, expires in %lums; VOUT/IIN/IPRI/ISEC/ADC/path checks remain mandatory; IOUT is external-reference only\r\n",
                (unsigned long) DAB_NOPOWER_DIAG_ARM_MS);
        return;
    }

    dab_force_all_safe();
    dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY :
                                            DAB_STATE_FAULT);

    dab_board_outputs_t outputs;
    dab_board_outputs_get(&outputs);
    if (outputs.input_main_en || outputs.output_protection_en ||
        outputs.input_precharge_en || outputs.run_request ||
        dab_pwm_hw_outputs_are_enabled())
    {
        dab_log("[DAB][DIAG-ARM][BLOCK] PWM/RUN_REQ/power paths are not all OFF\r\n");
        return;
    }

    if (!dab_measurements_refresh_from_adc() || !s_measurements_ready ||
        !s_measurements.valid)
    {
        dab_log("[DAB][DIAG-ARM][BLOCK] fresh valid ADC frame required\r\n");
        return;
    }

    if ((dab_measurements_abs(s_measurements.vout_v) >
         DAB_PWM8_DIAG_MAX_VOUT_V) ||
        (dab_measurements_abs(s_measurements.iin_a) > 0.5F) ||
        (dab_measurements_abs(s_measurements.ipri_a) > 1.0F) ||
        (dab_measurements_abs(s_measurements.isec_a) > 1.0F))
    {
        dab_log("[DAB][DIAG-ARM][BLOCK] VOUT/current not idle VOUT=%.2fV IIN=%.2fA IOUT=%.2fA IPRI=%.2fA ISEC=%.2fA\r\n",
                (double) s_measurements.vout_v,
                (double) s_measurements.iin_a,
                (double) s_measurements.iout_a,
                (double) s_measurements.ipri_a,
                (double) s_measurements.isec_a);
        return;
    }

    s_nopower_diag_arm_deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(DAB_NOPOWER_DIAG_ARM_MS);
    s_nopower_diag_arm_available = true;
    dab_log("[DAB][DIAG-ARM] ARMED ISOLATED one timed command/%lums; VIN_ADC=%.2fV bypassed once\r\n",
            (unsigned long) DAB_NOPOWER_DIAG_ARM_MS,
            (double) s_measurements.vin_v);
}

static bool dab_nopower_diag_vin_accept(float vin_abs,
                                        bool timed_only,
                                        const char * command_name)
{
    if (vin_abs <= DAB_PWM8_DIAG_MAX_VIN_V)
    {
        return true;
    }

    TickType_t const now = xTaskGetTickCount();
    bool const arm_fresh = s_nopower_diag_arm_available &&
        ((int32_t) (s_nopower_diag_arm_deadline - now) > 0);
    if (!timed_only || !arm_fresh)
    {
        if (s_nopower_diag_arm_available && !arm_fresh)
        {
            s_nopower_diag_arm_available = false;
            s_nopower_diag_arm_deadline = 0U;
            dab_log("[DAB][DIAG-ARM] EXPIRED\r\n");
        }
        dab_log("[DAB][BLOCK] %s VIN_ADC=%.2fV above %.1fV; use diagarm isolated after path isolation checks\r\n",
                command_name,
                (double) vin_abs,
                (double) DAB_PWM8_DIAG_MAX_VIN_V);
        return false;
    }

    s_nopower_diag_arm_available = false;
    s_nopower_diag_arm_deadline = 0U;
    dab_log("[DAB][DIAG-ARM] CONSUMED by %s; isolated-path confirmation overrides VIN_ADC=%.2fV once\r\n",
            command_name, (double) vin_abs);
    return true;
}

static void dab_single_gate_diag(const char * argument)
{
    static const char * const gate_names[] =
    {
        "Q1 PRI_A_H P415", "Q2 PRI_A_L P414",
        "Q3 PRI_B_H P105", "Q4 PRI_B_L P104",
        "Q5 SEC_A_H P915", "Q6 SEC_A_L P914",
        "Q7 SEC_B_H P605", "Q8 SEC_B_L P604"
    };

    if ((NULL == argument) || (0 == strcmp(argument, "help")))
    {
        dab_log("[DAB][GATE-DIAG] use gate q1..q8; one %lums pulse, never a diagonal pair\r\n",
                (unsigned long) DAB_GATE_DIAG_PULSE_MS);
        dab_log("[DAB][GATE-DIAG] disconnect main input and discharge both buses first\r\n");
        return;
    }

    if ((2U != strlen(argument)) ||
        (('q' != argument[0]) && ('Q' != argument[0])) ||
        (argument[1] < '1') || (argument[1] > '8'))
    {
        dab_log("[DAB][CMD] use gate q1..q8\r\n");
        return;
    }

    uint32_t const gate_index = (uint32_t) (argument[1] - '1');
    dab_force_all_safe();
    dab_enter_state(DAB_STATE_READY);

    if (!dab_measurements_refresh_from_adc() || !s_measurements_ready ||
        !s_measurements.valid)
    {
        dab_log("[DAB][BLOCK] gate diagnosis requires a fresh valid ADC frame\r\n");
        return;
    }

    float const vin_abs = dab_measurements_abs(s_measurements.vin_v);
    float const vout_abs = dab_measurements_abs(s_measurements.vout_v);
    if (vout_abs > DAB_GATE_DIAG_MAX_VOUT_V)
    {
        dab_log("[DAB][BLOCK] gate diagnosis VOUT not discharged VOUT=%.2fV limit=%.1fV\r\n",
                (double) vout_abs,
                (double) DAB_GATE_DIAG_MAX_VOUT_V);
        return;
    }
    if (!dab_nopower_diag_vin_accept(vin_abs, true, "gate"))
    {
        return;
    }

    if (!dab_board_fast_faults_clear() || !dab_pwm_hw_poeg_clear())
    {
        dab_log("[DAB][BLOCK] gate diagnosis requires RAW/IPRI/ISEC fault inputs and POEG healthy\r\n");
        return;
    }

    R_BSP_SoftwareDelay(DAB_LATCH_RESET_LOW_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);
    dab_board_protection_reset_set(true);
    R_BSP_SoftwareDelay(DAB_LATCH_RELEASE_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);

    if (!dab_gate_driver_enable() ||
        !dab_pwm_hw_single_gate_set(gate_index))
    {
        dab_force_all_safe();
        dab_log("[DAB][BLOCK] gate diagnosis could not enable %s\r\n",
                gate_names[gate_index]);
        return;
    }

    dab_log("[DAB][GATE-DIAG] %s HIGH; other seven gates LOW; RUN_REQ=1 paths=OFF pulse=%lums\r\n",
            gate_names[gate_index],
            (unsigned long) DAB_GATE_DIAG_PULSE_MS);
    R_BSP_SoftwareDelay(DAB_GATE_DIAG_PULSE_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);
    dab_force_all_safe();
    dab_enter_state(DAB_STATE_READY);
    dab_log("[DAB][GATE-DIAG] DONE all gates LOW RUN_REQ=0 paths=OFF\r\n");
}

static void dab_primary_bridge_diag(const char * argument,
                                    dab_primary_diag_target_t target)
{
    char const * command_name = "pri";
    char const * active_gates = "Q1..Q4";
    char const * low_gates = "Q5..Q8";
    uint32_t half_bridge_index = 0U;
    bool full_bridge = true;

    if (DAB_PRIMARY_DIAG_A == target)
    {
        command_name = "pria";
        active_gates = "Q1/Q2";
        low_gates = "Q3..Q8";
        full_bridge = false;
    }
    else if (DAB_PRIMARY_DIAG_B == target)
    {
        command_name = "prib";
        active_gates = "Q3/Q4";
        low_gates = "Q1/Q2,Q5..Q8";
        half_bridge_index = 1U;
        full_bridge = false;
    }

    if ((NULL != argument) && (0 == strcmp(argument, "off")))
    {
        dab_force_all_safe();
        dab_enter_state(DAB_STATE_READY);
        dab_log("[DAB][PRI-DIAG] %s OFF; all gates LOW RUN_REQ=0 paths=OFF\r\n",
                command_name);
        return;
    }

    bool const continuous = (NULL != argument) &&
                            (0 == strcmp(argument, "on"));
    unsigned long pulse_ms = DAB_PRIMARY_TEST_DEFAULT_MS;
    if ((NULL != argument) && !continuous &&
        (0 != strcmp(argument, "help")))
    {
        char * p_end = NULL;
        pulse_ms = strtoul(argument, &p_end, 10);
        if ((p_end == argument) || ('\0' != *p_end) ||
            (pulse_ms < DAB_PRIMARY_TEST_MIN_MS) ||
            (pulse_ms > DAB_PRIMARY_TEST_MAX_MS))
        {
            dab_log("[DAB][CMD] use %s on/off or %s %lu..%lums; default=%lums\r\n",
                    command_name, command_name,
                    (unsigned long) DAB_PRIMARY_TEST_MIN_MS,
                    (unsigned long) DAB_PRIMARY_TEST_MAX_MS,
                    (unsigned long) DAB_PRIMARY_TEST_DEFAULT_MS);
            return;
        }
    }
    else if ((NULL != argument) && (0 == strcmp(argument, "help")))
    {
        dab_log("[DAB][PRI-DIAG] %s on=continuous; off or %s off=stop; %s [10..500]=timed\r\n",
                command_name, command_name, command_name);
        dab_log("[DAB][PRI-DIAG] %s production PWM; %s GPIO LOW\r\n",
                active_gates, low_gates);
        dab_log("[DAB][PRI-DIAG] keep U_IPRI open for unloaded primary bridge isolation\r\n");
        return;
    }

    dab_force_all_safe();
    dab_enter_state(DAB_STATE_READY);

    R_BSP_SoftwareDelay(DAB_LATCH_RESET_LOW_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);
    dab_board_protection_reset_set(true);
    R_BSP_SoftwareDelay(DAB_LATCH_RELEASE_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);
    (void) dab_pwm_hw_poeg_clear();

    bool pwm_started = false;
    if (dab_gate_driver_enable())
    {
        pwm_started = full_bridge ?
                      dab_pwm_hw_primary_outputs_enable() :
                      dab_pwm_hw_primary_half_bridge_outputs_enable(
                          half_bridge_index);
    }
    if (!pwm_started)
    {
        dab_force_all_safe();
        dab_enter_state(DAB_STATE_READY);
        dab_log("[DAB][BLOCK] %s PWM could not start\r\n", command_name);
        return;
    }

    dab_enter_state(DAB_STATE_TEST_MODE);
    if (continuous)
    {
        s_primary_diag_running = true;
        dab_log("[DAB][PRI-DIAG] START CONTINUOUS %s=300kHz deadtime=%luns %s=LOW paths=OFF; send off to stop\r\n",
                active_gates, (unsigned long) DAB_DEADTIME_NS, low_gates);
        return;
    }

    dab_log("[DAB][PRI-DIAG] START %s=300kHz deadtime=%luns %s=LOW paths=OFF pulse=%lums\r\n",
            active_gates, (unsigned long) DAB_DEADTIME_NS, low_gates,
            pulse_ms);

    R_BSP_SoftwareDelay(pulse_ms, BSP_DELAY_UNITS_MILLISECONDS);

    dab_force_all_safe();
    dab_enter_state(DAB_STATE_READY);
    bool const have_post_adc = dab_measurements_refresh_from_adc() &&
                               s_measurements_ready && s_measurements.valid;
    dab_log("[DAB][PRI-DIAG] DONE %lums; all gates LOW RUN_REQ=0 paths=OFF\r\n",
            pulse_ms);
    if (have_post_adc)
    {
        dab_log("[DAB][PRI-DIAG] POST VIN=%.2fV VOUT=%.2fV IIN=%.3fA IPRI=%.3fA\r\n",
                (double) s_measurements.vin_v,
                (double) s_measurements.vout_v,
                (double) s_measurements.iin_a,
                (double) s_measurements.ipri_a);
    }
}

static void dab_secondary_bridge_diag(const char * argument,
                                      dab_secondary_diag_target_t target)
{
    char const * command_name = "sec";
    char const * active_gates = "Q5..Q8";
    char const * low_gates = "Q1..Q4";
    uint32_t half_bridge_index = 0U;
    bool full_bridge = true;
    uint8_t expected_high_mask = 0xF0U;

    if (DAB_SECONDARY_DIAG_A == target)
    {
        command_name = "seca";
        active_gates = "Q5/Q6";
        low_gates = "Q1..Q4,Q7/Q8";
        full_bridge = false;
        expected_high_mask = 0x30U;
    }
    else if (DAB_SECONDARY_DIAG_B == target)
    {
        command_name = "secb";
        active_gates = "Q7/Q8";
        low_gates = "Q1..Q6";
        half_bridge_index = 1U;
        full_bridge = false;
        expected_high_mask = 0xC0U;
    }

    if ((NULL != argument) && (0 == strcmp(argument, "off")))
    {
        dab_force_all_safe();
        dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY :
                                                DAB_STATE_FAULT);
        dab_log("[DAB][SEC-DIAG] %s OFF; Q1..Q8 LOW RUN_REQ=0 paths=OFF\r\n",
                command_name);
        return;
    }

    if ((NULL == argument) || (0 == strcmp(argument, "help")))
    {
        dab_log("[DAB][SEC-DIAG] use %s %lu..%lums; timed-only, no continuous mode\r\n",
                command_name,
                (unsigned long) DAB_SECONDARY_TEST_MIN_MS,
                (unsigned long) DAB_SECONDARY_TEST_MAX_MS);
        dab_log("[DAB][SEC-DIAG] %s production 300kHz complementary PWM; %s forced LOW\r\n",
                active_gates, low_gates);
        dab_log("[DAB][SEC-DIAG] disconnect input and electronic load, discharge VIN/VOUT below %.1f/%.1fV\r\n",
                (double) DAB_PWM8_DIAG_MAX_VIN_V,
                (double) DAB_PWM8_DIAG_MAX_VOUT_V);
        return;
    }

    char * p_end = NULL;
    unsigned long const pulse_ms = strtoul(argument, &p_end, 10);
    if ((p_end == argument) || ('\0' != *p_end) ||
        (pulse_ms < DAB_SECONDARY_TEST_MIN_MS) ||
        (pulse_ms > DAB_SECONDARY_TEST_MAX_MS))
    {
        dab_log("[DAB][CMD] use %s %lu..%lums or %s off\r\n",
                command_name,
                (unsigned long) DAB_SECONDARY_TEST_MIN_MS,
                (unsigned long) DAB_SECONDARY_TEST_MAX_MS,
                command_name);
        return;
    }

    dab_force_all_safe();
    dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY :
                                            DAB_STATE_FAULT);

    dab_board_outputs_t outputs;
    dab_board_outputs_get(&outputs);
    if (outputs.input_main_en || outputs.output_protection_en ||
        outputs.input_precharge_en || outputs.run_request)
    {
        dab_log("[DAB][BLOCK] %s could not force all power paths/RUN_REQ OFF\r\n",
                command_name);
        return;
    }

    if (!dab_measurements_refresh_from_adc() || !s_measurements_ready ||
        !s_measurements.valid)
    {
        dab_log("[DAB][BLOCK] %s requires a fresh valid ADC frame\r\n",
                command_name);
        return;
    }

    float const vin_abs = dab_measurements_abs(s_measurements.vin_v);
    float const vout_abs = dab_measurements_abs(s_measurements.vout_v);
    if (vout_abs > DAB_PWM8_DIAG_MAX_VOUT_V)
    {
        dab_log("[DAB][BLOCK] %s VOUT not discharged VOUT=%.2fV limit=%.1fV\r\n",
                command_name,
                (double) vout_abs,
                (double) DAB_PWM8_DIAG_MAX_VOUT_V);
        return;
    }
    if (!dab_nopower_diag_vin_accept(vin_abs, true, command_name))
    {
        return;
    }

    if (!dab_board_fast_faults_clear() || !dab_pwm_hw_poeg_clear())
    {
        dab_log("[DAB][BLOCK] %s requires RAW/IPRI/ISEC fault inputs healthy\r\n",
                command_name);
        return;
    }

    R_BSP_SoftwareDelay(DAB_LATCH_RESET_LOW_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);
    dab_board_protection_reset_set(true);
    R_BSP_SoftwareDelay(DAB_LATCH_RELEASE_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);

    bool pwm_started = false;
    if (dab_gate_driver_enable())
    {
        pwm_started = full_bridge ?
                      dab_pwm_hw_secondary_outputs_enable() :
                      dab_pwm_hw_secondary_half_bridge_outputs_enable(
                          half_bridge_index);
    }
    if (!pwm_started)
    {
        dab_force_all_safe();
        dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY :
                                                DAB_STATE_FAULT);
        dab_log("[DAB][BLOCK] %s PWM could not start\r\n", command_name);
        return;
    }

    dab_enter_state(DAB_STATE_TEST_MODE);
    dab_log("[DAB][SEC-DIAG] START %s=300kHz deadtime=%luns %s=LOW paths=OFF pulse=%lums\r\n",
            active_gates, (unsigned long) DAB_DEADTIME_NS, low_gates,
            pulse_ms);

    R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
    dab_pwm_hw_diag_t diag;
    dab_pwm_hw_diag_get(&diag);
    bool const mcu_waveform_ok =
        diag.outputs_requested &&
        (0x0FU == diag.counter_running_mask) &&
        (0x0FU == diag.counter_moving_mask) &&
        (expected_high_mask == diag.pin_seen_high_mask) &&
        (0xFFU == diag.pin_seen_low_mask);
    dab_log("[DAB][SEC-DIAG][MCU] %s expected_HI=0x%02X actual_HI=0x%02X LO=0x%02X RUN/MOVE=0x%02X/0x%02X\r\n",
            mcu_waveform_ok ? "PASS" : "FAIL",
            (unsigned int) expected_high_mask,
            (unsigned int) diag.pin_seen_high_mask,
            (unsigned int) diag.pin_seen_low_mask,
            (unsigned int) diag.counter_running_mask,
            (unsigned int) diag.counter_moving_mask);
    dab_print_pwm_hw_diag();
    if (pulse_ms > 1UL)
    {
        R_BSP_SoftwareDelay(pulse_ms - 1UL,
                            BSP_DELAY_UNITS_MILLISECONDS);
    }

    dab_force_all_safe();
    dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY :
                                            DAB_STATE_FAULT);
    bool const have_post_adc = dab_measurements_refresh_from_adc() &&
                               s_measurements_ready && s_measurements.valid;
    dab_log("[DAB][SEC-DIAG] DONE %lums; Q1..Q8 LOW RUN_REQ=0 paths=OFF\r\n",
            pulse_ms);
    if (have_post_adc)
    {
        dab_log("[DAB][SEC-DIAG] POST VIN=%.2fV VOUT=%.2fV IOUT=%.3fA ISEC=%.3fA\r\n",
                (double) s_measurements.vin_v,
                (double) s_measurements.vout_v,
                (double) s_measurements.iout_a,
                (double) s_measurements.isec_a);
    }
}

static void dab_all_gates_pwm_diag(const char * argument)
{
    if ((NULL != argument) && (0 == strcmp(argument, "off")))
    {
        dab_force_all_safe();
        dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY :
                                                DAB_STATE_FAULT);
        dab_log("[DAB][PWM8-DIAG] OFF; Q1..Q8 LOW RUN_REQ=0 paths=OFF\r\n");
        return;
    }

    if ((NULL != argument) && (0 == strcmp(argument, "show")))
    {
        dab_board_outputs_t outputs;
        dab_board_outputs_get(&outputs);
        dab_log("[DAB][PWM8-DIAG] state=%s PWM=%u RUN_REQ=%u MAIN=%u OUT=%u PRE=%u phase=%luns inner=0/0ns\r\n",
                dab_state_name(s_state),
                dab_pwm_hw_outputs_are_enabled() ? 1U : 0U,
                outputs.run_request ? 1U : 0U,
                outputs.input_main_en ? 1U : 0U,
                outputs.output_protection_en ? 1U : 0U,
                outputs.input_precharge_en ? 1U : 0U,
                (unsigned long) (dab_pwm_hw_phase_get() *
                                 DAB_PWM_COUNT_NS));
        dab_print_pwm_hw_diag();
        return;
    }

    if ((NULL == argument) || (0 == strcmp(argument, "help")))
    {
        dab_log("[DAB][PWM8-DIAG] pwmtest %lu..%lums=timed Q1..Q8; on=continuous; off=stop; show=status\r\n",
                (unsigned long) DAB_SECONDARY_TEST_MIN_MS,
                (unsigned long) DAB_SECONDARY_TEST_MAX_MS);
        dab_log("[DAB][PWM8-DIAG] SPS outer=0ns inner=0/0ns, 300kHz, deadtime=%luns, paths remain OFF\r\n",
                (unsigned long) DAB_DEADTIME_NS);
        dab_log("[DAB][PWM8-DIAG] disconnect main input/output power and discharge both buses first\r\n");
        return;
    }

    bool const continuous = (0 == strcmp(argument, "on"));
    unsigned long pulse_ms = 0UL;
    if (!continuous)
    {
        char * p_end = NULL;
        pulse_ms = strtoul(argument, &p_end, 10);
        if ((p_end == argument) || ('\0' != *p_end) ||
            (pulse_ms < DAB_SECONDARY_TEST_MIN_MS) ||
            (pulse_ms > DAB_SECONDARY_TEST_MAX_MS))
        {
            dab_log("[DAB][CMD] use pwmtest %lu..%lums|on|off|show\r\n",
                    (unsigned long) DAB_SECONDARY_TEST_MIN_MS,
                    (unsigned long) DAB_SECONDARY_TEST_MAX_MS);
            return;
        }
    }

    dab_force_all_safe();
    dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY :
                                            DAB_STATE_FAULT);

    dab_board_outputs_t outputs;
    dab_board_outputs_get(&outputs);
    if (outputs.input_main_en || outputs.output_protection_en ||
        outputs.input_precharge_en)
    {
        dab_log("[DAB][BLOCK] pwmtest could not force paths OFF MAIN=%u OUT=%u PRE=%u\r\n",
                outputs.input_main_en ? 1U : 0U,
                outputs.output_protection_en ? 1U : 0U,
                outputs.input_precharge_en ? 1U : 0U);
        return;
    }

    bool const adc_qualified = dab_measurements_refresh_from_adc() &&
                               s_measurements_ready &&
                               s_measurements.valid;
    if (adc_qualified)
    {
        float const vin_abs = dab_measurements_abs(s_measurements.vin_v);
        float const vout_abs = dab_measurements_abs(s_measurements.vout_v);
        if (vout_abs > DAB_PWM8_DIAG_MAX_VOUT_V)
        {
            dab_log("[DAB][BLOCK] pwmtest VOUT not discharged VOUT=%.2fV limit=%.1fV\r\n",
                    (double) vout_abs,
                    (double) DAB_PWM8_DIAG_MAX_VOUT_V);
            return;
        }
        if (!dab_nopower_diag_vin_accept(vin_abs, !continuous, "pwmtest"))
        {
            return;
        }
    }
    else
    {
        if (!continuous)
        {
            dab_log("[DAB][PWM8-DIAG][BLOCK] timed test requires valid ADC bus qualification\r\n");
            return;
        }
        dab_log("[DAB][PWM8-DIAG][WARN] ADC INVALID; continuous mode is NO-POWER TEST ONLY\r\n");
    }

    if (!dab_board_fast_faults_clear() || !dab_pwm_hw_poeg_clear())
    {
        dab_log("[DAB][BLOCK] pwmtest requires RAW/IPRI/ISEC fault inputs and POEG healthy\r\n");
        return;
    }

    R_BSP_SoftwareDelay(DAB_LATCH_RESET_LOW_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);
    dab_board_protection_reset_set(true);
    R_BSP_SoftwareDelay(DAB_LATCH_RELEASE_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);
    (void) dab_pwm_hw_poeg_clear();

    if (!dab_gate_driver_enable() || !dab_pwm_hw_outputs_enable())
    {
        dab_force_all_safe();
        dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY :
                                                DAB_STATE_FAULT);
        dab_log("[DAB][BLOCK] pwmtest could not start all eight PWM outputs\r\n");
        return;
    }

    dab_enter_state(DAB_STATE_TEST_MODE);
    if (continuous)
    {
        s_primary_diag_running = true;
        dab_log("[DAB][PWM8-DIAG] START CONTINUOUS Q1..Q8=300kHz deadtime=%luns SPS phase=0ns inner=0/0ns paths=OFF ADC=%s; send pwmtest off or off to stop\r\n",
                (unsigned long) DAB_DEADTIME_NS,
                adc_qualified ? "QUALIFIED" : "INVALID/NO-POWER-ONLY");
        dab_print_pwm_hw_diag();
        return;
    }

    dab_log("[DAB][PWM8-DIAG] START TIMED Q1..Q8=300kHz deadtime=%luns SPS phase=0ns paths=OFF pulse=%lums\r\n",
            (unsigned long) DAB_DEADTIME_NS, pulse_ms);
    R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
    dab_print_pwm_hw_diag();
    if (pulse_ms > 1UL)
    {
        R_BSP_SoftwareDelay(pulse_ms - 1UL,
                            BSP_DELAY_UNITS_MILLISECONDS);
    }
    dab_force_all_safe();
    dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY :
                                            DAB_STATE_FAULT);
    dab_log("[DAB][PWM8-DIAG] DONE %lums; Q1..Q8 LOW RUN_REQ=0 paths=OFF\r\n",
            pulse_ms);
}

static void dab_low_voltage_live_burst(const char * argument)
{
    if (NULL == argument)
    {
        dab_log("[DAB][CMD] use liveburst PHASE_NS,DURATION_MS or liveburst pri,DURATION_MS\r\n");
        return;
    }

    bool const primary_only = (0 == strncmp(argument, "pri,", 4U));
    char * p_end = NULL;
    unsigned long phase_ns = 0UL;
    char * p_duration = NULL;
    if (primary_only)
    {
        p_duration = (char *) argument + 4;
    }
    else
    {
        phase_ns = strtoul(argument, &p_end, 10);
        if ((p_end == argument) || (',' != *p_end))
        {
            dab_log("[DAB][CMD] use liveburst PHASE_NS,DURATION_MS; example liveburst 0,2 or liveburst pri,20\r\n");
            return;
        }
        p_duration = p_end + 1;
    }

    if ('\0' == *p_duration)
    {
        dab_log("[DAB][CMD] liveburst duration is missing\r\n");
        return;
    }

    unsigned long const duration_ms = strtoul(p_duration, &p_end, 10);
    if ((p_end == p_duration) || ('\0' != *p_end) ||
        (phase_ns > DAB_LIVE_BURST_MAX_PHASE_NS) ||
        (0UL != (phase_ns % DAB_PWM_COUNT_NS)) ||
        (duration_ms < DAB_LIVE_BURST_MIN_MS) ||
        (duration_ms > DAB_LIVE_BURST_MAX_MS) ||
        ((duration_ms > DAB_LIVE_BURST_SHORT_MAX_MS) &&
         (phase_ns > DAB_LIVE_BURST_LONG_PHASE_MAX_NS)))
    {
        dab_log("[DAB][BLOCK] liveburst permits 0..80ns/1..50ms; duration 51..15000ms requires phase 0..32ns\r\n");
        return;
    }

    dab_force_all_safe();
    dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY : DAB_STATE_FAULT);

    dab_board_outputs_t outputs;
    dab_board_outputs_get(&outputs);
    if (outputs.input_main_en || outputs.output_protection_en ||
        outputs.input_precharge_en || outputs.run_request ||
        dab_pwm_hw_outputs_are_enabled())
    {
        dab_log("[DAB][LIVE-BURST][BLOCK] could not force initial PWM/RUN/path state OFF\r\n");
        return;
    }

    if (!dab_measurements_refresh_from_adc() || !s_measurements_ready ||
        !s_measurements.valid ||
        (dab_measurements_abs(s_measurements.vout_v) > DAB_PWM8_DIAG_MAX_VOUT_V) ||
        (dab_measurements_abs(s_measurements.iin_a) > DAB_LIVE_BURST_IDLE_IIN_MAX_A) ||
        (dab_measurements_abs(s_measurements.ipri_a) > DAB_LIVE_BURST_IDLE_BRIDGE_MAX_A) ||
        (dab_measurements_abs(s_measurements.isec_a) > DAB_LIVE_BURST_IDLE_BRIDGE_MAX_A))
    {
        dab_log("[DAB][LIVE-BURST][BLOCK] initial ADC/VOUT/current not idle\r\n");
        return;
    }

    if (!dab_nopower_diag_vin_accept(
            dab_measurements_abs(s_measurements.vin_v), true, "liveburst"))
    {
        return;
    }
    if (!dab_board_fast_faults_clear() || !dab_pwm_hw_poeg_clear())
    {
        dab_log("[DAB][LIVE-BURST][BLOCK] RAW/IPRI/ISEC inputs not healthy before pulse\r\n");
        return;
    }

    R_BSP_SoftwareDelay(DAB_LATCH_RESET_LOW_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);
    dab_board_protection_reset_set(true);
    R_BSP_SoftwareDelay(DAB_LATCH_RELEASE_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);
    dab_board_input_precharge_set(false);
    dab_board_input_main_set(true);
    dab_board_output_protection_set(true);
    R_BSP_SoftwareDelay(DAB_DIRECT_PATH_SETTLE_MS,
                        BSP_DELAY_UNITS_MILLISECONDS);

    if (!dab_measurements_refresh_from_adc() || !s_measurements.valid ||
        (dab_measurements_abs(s_measurements.vout_v) > DAB_GATE_DIAG_MAX_VOUT_V) ||
        (dab_measurements_abs(s_measurements.iin_a) > DAB_LIVE_BURST_IDLE_IIN_MAX_A) ||
        (dab_measurements_abs(s_measurements.ipri_a) > DAB_LIVE_BURST_IDLE_BRIDGE_MAX_A) ||
        (dab_measurements_abs(s_measurements.isec_a) > DAB_LIVE_BURST_IDLE_BRIDGE_MAX_A))
    {
        dab_log("[DAB][LIVE-BURST][BLOCK] path-on idle check failed Vout=%.2fV Iin=%.2fA Iout=%.2fA Ipri=%.2fA Isec=%.2fA\r\n",
                (double) s_measurements.vout_v,
                (double) s_measurements.iin_a,
                (double) s_measurements.iout_a,
                (double) s_measurements.ipri_a,
                (double) s_measurements.isec_a);
        dab_force_all_safe();
        dab_enter_state(DAB_STATE_READY);
        return;
    }

    uint32_t const phase_counts = (uint32_t) (phase_ns / DAB_PWM_COUNT_NS);
    bool pwm_started = dab_pwm_hw_modulation_set(phase_counts, 0U, 0U) &&
                       dab_gate_driver_enable();
    if (pwm_started)
    {
        pwm_started = primary_only ? dab_pwm_hw_primary_outputs_enable() :
                                     dab_pwm_hw_outputs_enable();
    }
    if (!pwm_started)
    {
        dab_force_all_safe();
        dab_enter_state(DAB_STATE_READY);
        dab_log("[DAB][LIVE-BURST][BLOCK] PWM could not start; all outputs/path OFF\r\n");
        return;
    }

    dab_enter_state(DAB_STATE_TEST_MODE);
    dab_log("[DAB][LIVE-BURST] START mode=%s phase=%luns duration=%lums paths=ON external-current-limit-required\r\n",
            primary_only ? "PRI_ONLY" : "FULL",
            phase_ns, duration_ms);

    dab_pwm_hw_diag_t live_diag = {0};
    uint32_t active_phase_counts = 0U;
    float peak_iin = 0.0F;
    float peak_iout = 0.0F;
    float peak_ipri = 0.0F;
    float peak_isec = 0.0F;
    float peak_vout = 0.0F;
    for (unsigned long sample = 0UL; sample < duration_ms; sample++)
    {
        R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
        if (0UL == sample)
        {
            active_phase_counts = dab_pwm_hw_phase_get();
            dab_pwm_hw_diag_get(&live_diag);
        }
        if (dab_measurements_refresh_from_adc() && s_measurements.valid)
        {
            peak_iin = dab_maxf(peak_iin,
                               dab_measurements_abs(s_measurements.iin_a));
            peak_iout = dab_maxf(peak_iout,
                                dab_measurements_abs(s_measurements.iout_a));
            peak_ipri = dab_maxf(peak_ipri,
                                dab_measurements_abs(s_measurements.ipri_a));
            peak_isec = dab_maxf(peak_isec,
                                dab_measurements_abs(s_measurements.isec_a));
            peak_vout = dab_maxf(peak_vout,
                                dab_measurements_abs(s_measurements.vout_v));
        }
    }

    dab_force_all_safe();
    dab_enter_state(DAB_STATE_READY);
    dab_log("[DAB][LIVE-BURST] DONE mode=%s requested=%luns active=%luns duration=%lums PWM/RUN/paths=OFF peak Vout=%.2fV Iin=%.2fA Iout=%.2fA Ipri=%.2fA Isec=%.2fA\r\n",
            primary_only ? "PRI_ONLY" : "FULL",
            phase_ns,
            (unsigned long) (active_phase_counts * DAB_PWM_COUNT_NS),
            duration_ms,
            (double) peak_vout,
            (double) peak_iin,
            (double) peak_iout,
            (double) peak_ipri,
            (double) peak_isec);
    dab_log("[DAB][LIVE-BURST][MCU] REQ=%u RUN/MOVE=0x%02X/0x%02X PIN_HI/LO=0x%02X/0x%02X expected_HI=0x%02X LO=0xFF\r\n",
            live_diag.outputs_requested ? 1U : 0U,
            (unsigned int) live_diag.counter_running_mask,
            (unsigned int) live_diag.counter_moving_mask,
            (unsigned int) live_diag.pin_seen_high_mask,
            (unsigned int) live_diag.pin_seen_low_mask,
            primary_only ? 0x0FU : 0xFFU);
}

static void dab_command_execute(char * p_line)
{
    char * command = strtok(p_line, " ");
    char * argument = strtok(NULL, " ");
    if (NULL == command)
    {
        return;
    }

    if ((0 == strcmp(command, "help")) || (0 == strcmp(command, "h")) ||
        (0 == strcmp(command, "?")))
    {
        dab_log("[CMD] on=START direct four MOS + PWM; default 20W/44V/SPS\r\n");
        dab_log("[CMD] off=STOP PWM + RUN + all four path MOS immediately\r\n");
        dab_log("[CMD] show=status | save=VOFA data | view=compact output\r\n");
        dab_log("[CMD] p 20..400=output safety ceiling | cap 0..480=closed-loop phase cap | v 36..48 | clear\r\n");
        if ((NULL != argument) && (0 == strcmp(argument, "all")))
        {
            dab_log("[ADV] measure adcdiag adccheck zero mos on/off diagarm isolated/off gate q1..q8\r\n");
            dab_log("[ADV] pri/pria/prib on/off/[10..500]\r\n");
            dab_log("[ADV] seca/secb/sec 20..200=timed no-power secondary PWM | pwmtest 20..200/on/off/show\r\n");
            dab_log("[ADV] liveburst PHASE_NS,DURATION_MS=full bridge; liveburst pri,DURATION_MS=primary-only body-diode test\r\n");
            dab_log("[ADV] mode save/observe test pause/resume\r\n");
            dab_log("[ADV] mod sps/eps/tps/auto | inner PRI_NS SEC_NS/auto/show | kp 0..40 | ki 0..10 | kd 0..0.1 | unlock 400 | cap 0..480\r\n");
            dab_log("[ADV] ph 0..480=manual effective outer phase in RUN for SPS/EPS/TPS | ph auto=return to PI\r\n");
            dab_log("[ADV] arch voltage/current/energy/cp/show | iref/ikp/iki/islew VALUE | idir forward/reverse/show | alglog on/off/show | protect bench/normal/show\r\n");
            dab_log("[ADAPT] vinadapt on/off/show/range/nominal/kff/alpha | autotune start/stop/show/apply/auto/step/settle/sample/min_delta | modswitch MODE/config | cpdamp on/off/show/target/kp/ki/gv/vnom/slew/imax\r\n");
            dab_log("[ENERGY] energy off/manual/mppt/charge/solar/show | mppt po/inc/off/fixed/step/interval/vmin/vmax/pmax/show\r\n");
            dab_log("[CHARGE] bat profile liion10/lfp12/lead36/custom | capacity/imax/ipre/iterm/vpre/vcv/vfloat/absorb/timeout/temp/kp/ki | start/stop/show\r\n");
            dab_log("[AIHOOK] ai off/assist/show | ai hint CURRENT_A CONFIDENCE [TTL_MS] | ai mod sps|eps|tps CONFIDENCE [TTL_MS]; modulation hint is record-only\r\n");
            dab_log("[DATA] version | target V I | vinset 18..24 | rload 1..100 | layer 20..400 | sched on/auto/off/show | bundle show\r\n");
        }
    }
    else if (0 == strcmp(command, "target"))
    {
        float target_v = 0.0F;
        float target_i = 0.0F;
        char * current_argument = strtok(NULL, " ");
        bool const valid = dab_parse_float(argument, &target_v) &&
                           dab_parse_float(current_argument, &target_i) &&
                           (DAB_STATE_READY == s_state) &&
                           !s_control_enabled &&
                           !dab_pwm_hw_outputs_are_enabled() &&
                           (DAB_MODULATION_AUTO != s_modulation_requested) &&
                           (target_v >= DAB_VREF_MIN_V) &&
                           (target_v <= DAB_VREF_MAX_V) &&
                           (target_i >= 0.0F) &&
                           (target_i <= DAB_CURRENT_REFERENCE_MAX_A);
        if (!valid)
        {
            dab_log("[DAB][TARGET][BLOCK] use READY: target 36..48V 0..18A after mod sps|eps|tps\r\n");
        }
        else
        {
            s_control_architecture = DAB_CONTROL_ARCH_OUTPUT_CURRENT_PI;
            s_vref_v = target_v;
            s_current_reference_command_a = target_i;
            (void) dab_energy_parameter_set(DAB_ENERGY_CONFIG_MANUAL_CURRENT_A,
                                            target_i);
            s_vin_adaptation.config.enabled = true;
            s_schedule_autoload_enabled = true;
            dab_control_reset();
            if (dab_schedule_apply_current())
            {
                dab_log("[DAB][TARGET] armed %.1fV %.3fA mod=%s; selected seed is retained through PATH and START\r\n",
                        (double) target_v,
                        (double) target_i,
                        dab_modulation_name(s_modulation_requested));
            }
            else
            {
                s_schedule_autoload_enabled = false;
                dab_log("[DAB][TARGET][BLOCK] no compatible powered seed; manual values retained\r\n");
            }
        }
    }
    else if (0 == strcmp(command, "on"))
    {
#if DAB_ALLOW_POWER_TRANSFER
        if (!s_power_command_received)
        {
            s_power_limit_w = DAB_POWER_DEFAULT_W;
            s_power_command_received = true;
        }
        if (s_schedule_autoload_enabled &&
            !s_schedule_enabled &&
            !dab_schedule_apply_current())
        {
            dab_log("[DAB][START][BLOCK] automatic schedule has no compatible powered seed\r\n");
        }
        else
        {
            dab_control_start_request();
        }
#else
        dab_log("[DAB][BLOCK] power transfer is compile-locked\r\n");
#endif
    }
    else if (0 == strcmp(command, "off"))
    {
        s_nopower_diag_arm_available = false;
        s_nopower_diag_arm_deadline = 0U;
        dab_force_all_safe();
        dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY : DAB_STATE_FAULT);
        dab_log("[DAB][OFF] STOP PWM=0 RUN=0 four path MOS=OFF\r\n");
    }
    else if ((0 == strcmp(command, "version")) ||
             (0 == strcmp(command, "ver")))
    {
        dab_log("[DAB][VERSION] %s\r\n", DAB_FW_VERSION);
    }
    else if ((0 == strcmp(command, "status")) ||
             (0 == strcmp(command, "show")) ||
             (0 == strcmp(command, "s")))
    {
        s_force_telemetry = true;
    }
    else if ((0 == strcmp(command, "measure")) || (0 == strcmp(command, "m")))
    {
        bool refreshed = false;
        TickType_t const deadline = xTaskGetTickCount() + pdMS_TO_TICKS(5UL);
        do
        {
            dab_adc_dma_service();
            refreshed = dab_measurements_refresh_from_adc();
            if (!refreshed)
            {
                taskYIELD();
            }
        }
        while (!refreshed &&
               ((int32_t) (deadline - xTaskGetTickCount()) > 0));

        if (refreshed)
        {
            dab_measurements_print(&s_measurements);
        }
        else
        {
            dab_log("[MEAS] unavailable: no coherent ADC frame; previous sample not printed\r\n");
        }
    }
    else if ((0 == strcmp(command, "adcdiag")) ||
             (0 == strcmp(command, "adc")))
    {
        dab_print_adc_diag();
    }
    else if ((0 == strcmp(command, "adccheck")) ||
             (0 == strcmp(command, "adcx")))
    {
        dab_print_adc_crosscheck();
    }
    else if ((0 == strcmp(command, "stream")) && (NULL != argument) &&
             (0 == strcmp(argument, "on")))
    {
        s_stream = true;
        s_vofa_stream = false;
        dab_log("[CMD] measurement stream ON (%lums idle, %lums running)\r\n",
                (unsigned long) DAB_IDLE_STREAM_PERIOD_MS,
                (unsigned long) DAB_TELEMETRY_PERIOD_MS);
    }
    else if ((0 == strcmp(command, "stream")) && (NULL != argument) &&
             (0 == strcmp(argument, "off")))
    {
        s_stream = false;
        dab_log("[CMD] measurement stream OFF\r\n");
    }
    else if (0 == strcmp(command, "alglog"))
    {
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_log("[VOFA] ALG extension %s; default OFF preserves legacy DAB-only save stream\r\n",
                    s_algorithm_logging_enabled ? "ON" : "OFF");
        }
        else if (0 == strcmp(argument, "on"))
        {
            s_algorithm_logging_enabled = true;
            dab_log("[VOFA] ALG extension ON; each DAB row is followed by an ALG row with the same seq\r\n");
        }
        else if (0 == strcmp(argument, "off"))
        {
            s_algorithm_logging_enabled = false;
            dab_log("[VOFA] ALG extension OFF; legacy DAB-only save stream restored\r\n");
        }
        else
        {
            dab_log("[VOFA][CMD] use alglog on|off|show\r\n");
        }
    }
    else if ((0 == strcmp(command, "save")) ||
             (((0 == strcmp(command, "mode")) && (NULL != argument) &&
              ((0 == strcmp(argument, "save")) ||
               (0 == strcmp(argument, "data")))) ||
             ((0 == strcmp(command, "vofa")) && (NULL != argument) &&
              (0 == strcmp(argument, "on")))))
    {
        s_vofa_stream = true;
        s_stream = false;
        dab_log("[MODE] SAVE; VOFA+ FireWater numeric data every %lums; ALG extension=%s\r\n",
                (unsigned long) DAB_VOFA_PERIOD_MS,
                s_algorithm_logging_enabled ? "ON" : "OFF");
        dab_log("[VOFA] schema=v40 DAB fields unchanged: seq,VIN,VOUT,IIN,IOUT,IPRI,ISEC,TPRI,TSEC,Pin,Pout,VAFE,raw_phase_ns,Plimit,Vref,state,control,pwm,fault,valid,mod_req,mod_eff,effective_phase_ns,command_phase_ns,pri_inner_ns,sec_inner_ns,inner_manual\r\n");
        dab_log("[VOFA] fields2=kp_ns_per_v,ki_us_per_vs,kd_ns_s_per_v,d_filter_alpha,phase_cap_ns,bench_vin_set_v,load_nominal_ohm,requested_power_layer_w,control_arch,schedule_enabled,table_entry_id,outer_saturated,outer_saturation_updates,feedforward_enabled,feedforward_effective_phase_ns\r\n");
        if (s_algorithm_logging_enabled)
        {
            dab_log("[VOFA] ALG join_key=seq fields=seq,arch,guard,Iref_cmd,Iref_active,Ierr,Ikp,Iki,Islew,energy_mode,energy_valid,energy_age_ms,energy_Iref,mppt_P,mppt_V,mppt_limited,bat_profile,charge_stage,charge_V,charge_limited,ai_hint,ai_conf,ai_applied,ai_mod_hint,ai_mod_conf,ai_mod_fresh,ipc_peer,ipc_stale,config_queued,config_dropped,current_control_direction\r\n");
            dab_log("[VOFA] optional join_key=seq VAD=input adaptation, ATI=auto identify/tune, MSW=bumpless modulation switch, CPD=constant-power damping; DAB/ALG schemas unchanged\r\n");
        }
    }
    else if ((0 == strcmp(command, "view")) ||
             (((0 == strcmp(command, "mode")) && (NULL != argument) &&
              ((0 == strcmp(argument, "observe")) ||
               (0 == strcmp(argument, "view")))) ||
             ((0 == strcmp(command, "vofa")) && (NULL != argument) &&
              (0 == strcmp(argument, "off")))))
    {
        s_vofa_stream = false;
        s_stream = false;
        dab_log("[MODE] OBSERVE; one compact line/s while running plus immediate events\r\n");
    }
    else if (0 == strcmp(command, "pins"))
    {
        dab_print_pins();
    }
    else if (0 == strcmp(command, "drivers"))
    {
        dab_print_drivers();
    }
    else if (0 == strcmp(command, "temp"))
    {
        float ambient = 0.0F;
        fsp_err_t const err = tmp1075_temperature_read(&ambient);
        if (FSP_SUCCESS == err)
        {
            s_tmp1075.temperature_c = ambient;
            s_tmp1075_ok = true;
        }
        dab_log("[TMP1075] %s ambient=%.3fC err=%ld\r\n",
                (FSP_SUCCESS == err) ? "PASS" : "FAIL",
                (double) ambient, (long) err);
    }
    else if ((0 == strcmp(command, "test")) || (0 == strcmp(command, "t")))
    {
        dab_force_all_safe();
        s_test_mode = true;
        dab_enter_state(DAB_STATE_TEST_MODE);
        dab_ra8p1_scope_test_enter();
    }
    else if (0 == strcmp(command, "gate"))
    {
        dab_single_gate_diag(argument);
    }
    else if (0 == strcmp(command, "diagarm"))
    {
        dab_nopower_diag_arm(argument);
    }
    else if (0 == strcmp(command, "liveburst"))
    {
        dab_low_voltage_live_burst(argument);
    }
    else if (0 == strcmp(command, "pri"))
    {
        dab_primary_bridge_diag(argument, DAB_PRIMARY_DIAG_FULL);
    }
    else if (0 == strcmp(command, "pria"))
    {
        dab_primary_bridge_diag(argument, DAB_PRIMARY_DIAG_A);
    }
    else if (0 == strcmp(command, "prib"))
    {
        dab_primary_bridge_diag(argument, DAB_PRIMARY_DIAG_B);
    }
    else if (0 == strcmp(command, "sec"))
    {
        dab_secondary_bridge_diag(argument, DAB_SECONDARY_DIAG_FULL);
    }
    else if (0 == strcmp(command, "seca"))
    {
        dab_secondary_bridge_diag(argument, DAB_SECONDARY_DIAG_A);
    }
    else if (0 == strcmp(command, "secb"))
    {
        dab_secondary_bridge_diag(argument, DAB_SECONDARY_DIAG_B);
    }
    else if ((0 == strcmp(command, "pwmtest")) ||
             (0 == strcmp(command, "pwm8")))
    {
        dab_all_gates_pwm_diag(argument);
    }
    else if (0 == strcmp(command, "precharge"))
    {
        dab_log("[DAB][CMD] precharge removed; use on to start or off to stop\r\n");
    }
    else if ((0 == strcmp(command, "mos")) ||
             (0 == strcmp(command, "path")))
    {
        if ((NULL != argument) && (0 == strcmp(argument, "on")))
        {
#if DAB_ALLOW_POWER_TRANSFER
            dab_path_direct_start();
#else
            dab_log("[DAB][BLOCK] mos on is compile-locked; hardware unchanged\r\n");
#endif
        }
        else if ((NULL != argument) && (0 == strcmp(argument, "off")))
        {
            dab_force_all_safe();
            dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY : DAB_STATE_FAULT);
            dab_log("[DAB][PATH] OFF; MAIN/OUT/PRE/RESET/RUN and all PWM outputs are OFF\r\n");
        }
        else
        {
            dab_log("[DAB][CMD] use mos on or mos off\r\n");
        }
    }
    else if ((0 == strcmp(command, "power")) ||
             (0 == strcmp(command, "p")) ||
             (0 == strcmp(command, "limit")))
    {
        float requested = 0.0F;
        if (!dab_parse_float(argument, &requested))
        {
            dab_log("[DAB][CMD] use power 20..400; unlock 400 is required above 100W\r\n");
        }
        else
        {
            (void) dab_power_limit_set(requested);
        }
    }
    else if ((0 == strcmp(command, "cap")) ||
             (0 == strcmp(command, "phasecap")))
    {
        float requested_ns = 0.0F;
        if (!dab_parse_float(argument, &requested_ns))
        {
            dab_log("[DAB][CMD] use cap 0..480 ns; unlock 400 is required above 320ns\r\n");
        }
        else
        {
            (void) dab_phase_cap_set(requested_ns);
        }
    }
    else if ((0 == strcmp(command, "phase")) ||
             (0 == strcmp(command, "ph")))
    {
        if (DAB_STATE_RUN != s_state)
        {
            dab_log("[DAB][BLOCK] ph is live tuning only; start and wait for RUN\r\n");
        }
        else if ((NULL != argument) && (0 == strcmp(argument, "auto")))
        {
            s_manual_phase_override = false;
            s_pi_integral_counts = s_phase_command_counts;
            dab_log("[DAB][PHASE] closed-loop PI/PID control restored at %luns\r\n",
                    (unsigned long) (dab_pwm_hw_phase_get() * DAB_PWM_COUNT_NS));
        }
        else
        {
            float requested_ns = 0.0F;
            float const maximum_ns = (float) (DAB_PHASE_ABSOLUTE_MAX_COUNTS *
                                               DAB_PWM_COUNT_NS);
            if (!dab_parse_float(argument, &requested_ns) ||
                !((requested_ns >= 0.0F) && (requested_ns <= maximum_ns)))
            {
                dab_log("[DAB][CMD] use ph 0..%.0f ns or ph auto\r\n",
                        (double) maximum_ns);
            }
            else
            {
                uint32_t const requested_counts =
                    (uint32_t) ((requested_ns / (float) DAB_PWM_COUNT_NS) + 0.5F);
                s_manual_phase_counts = requested_counts;
                s_manual_phase_override = true;
                dab_schedule_mark_manual();
                dab_log("[DAB][PHASE] MANUAL effective target=%luns; slew=4ns/100ms; ph auto restores PI/PID\r\n",
                        (unsigned long) (requested_counts * DAB_PWM_COUNT_NS));
            }
        }
    }
    else if (0 == strcmp(command, "control"))
    {
        if ((NULL != argument) && (0 == strcmp(argument, "on")))
        {
#if DAB_ALLOW_POWER_TRANSFER
            dab_control_start_request();
#else
            dab_log("[DAB][BLOCK] control is compile-locked; hardware unchanged\r\n");
#endif
        }
        else if ((NULL != argument) && (0 == strcmp(argument, "off")))
        {
            dab_force_all_safe();
            dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY : DAB_STATE_FAULT);
            dab_log("[DAB][CONTROL] OFF; power paths and all PWM outputs are OFF\r\n");
        }
        else
        {
            dab_log("[DAB][CMD] use control on or control off\r\n");
        }
    }
    else if (0 == strcmp(command, "start"))
    {
#if !DAB_ALLOW_POWER_TRANSFER
        dab_log("[DAB][BLOCK] start is compile-locked; hardware unchanged\r\n");
#else
        float requested = s_power_limit_w;
        if ((NULL != argument) && !dab_parse_float(argument, &requested))
        {
            dab_log("[DAB][CMD] use start or start 20..200\r\n");
        }
        else if (((NULL == argument) || dab_power_limit_set(requested)))
        {
            dab_control_start_request();
        }
#endif
    }
    else if (0 == strcmp(command, "pause"))
    {
        if ((DAB_STATE_SOFTSTART == s_state) || (DAB_STATE_RUN == s_state))
        {
            dab_board_run_request_set(false);
            dab_pwm_hw_outputs_disable();
            dab_control_reset();
            dab_enter_state(DAB_STATE_PAUSED);
        }
        else
        {
            dab_log("[DAB][CMD] pause only applies while running\r\n");
        }
    }
    else if (0 == strcmp(command, "resume"))
    {
        if (!s_control_enabled || (DAB_STATE_PAUSED != s_state) ||
            (0U != s_fault_mask))
        {
            dab_log("[DAB][BLOCK] resume requires control ON, PAUSED, and fault=0\r\n");
        }
        else
        {
            (void) dab_gate_driver_enable();
            if (!dab_pwm_hw_outputs_enable())
            {
                if (DAB_SOFTWARE_AUTO_TRIP_ENABLE)
                {
                    dab_fault_latch(DAB_FAULT_RAW_N | DAB_FAULT_POEG);
                }
                else
                {
                    dab_force_all_safe();
                    dab_enter_state(DAB_STATE_READY);
                    dab_log("[DAB][BLOCK] resume failed to initialize GPT/PFS; all outputs/path OFF\r\n");
                }
            }
            else
            {
                dab_control_reset();
                dab_enter_state(DAB_STATE_SOFTSTART);
                dab_log("[DAB][PWM] FORCE: GPT synchronized and Q1..Q8 muxed; POEG ignored; resume slow-start\r\n");
                dab_print_pwm_hw_diag();
            }
        }
    }
    else if (0 == strcmp(command, "stop"))
    {
        dab_force_all_safe();
        dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY : DAB_STATE_FAULT);
        dab_log("[DAB][STOP] control OFF; power paths and all PWM outputs are OFF\r\n");
    }
    else if (0 == strcmp(command, "clear"))
    {
        dab_board_inputs_t inputs;
        dab_board_inputs_get(&inputs);
        if (DAB_SOFTWARE_AUTO_TRIP_ENABLE &&
            (!dab_required_digital_faults_clear() ||
             !inputs.pri_hw_ok || !inputs.sec_hw_ok || !inputs.temp_amb_ot_n))
        {
            dab_log("[DAB][BLOCK] clear requires every hardware fault/OK input healthy\r\n");
        }
        else
        {
            dab_force_all_safe();
            R_BSP_SoftwareDelay(DAB_LATCH_RESET_LOW_MS,
                                BSP_DELAY_UNITS_MILLISECONDS);
            if (DAB_SOFTWARE_AUTO_TRIP_ENABLE && !dab_pwm_hw_poeg_clear())
            {
                dab_log("[DAB][BLOCK] POEG still latched\r\n");
            }
            else
            {
                if (!DAB_SOFTWARE_AUTO_TRIP_ENABLE)
                {
                    (void) dab_pwm_hw_poeg_clear();
                }
                s_fault_mask = 0U;
                dab_enter_state(DAB_STATE_READY);
                dab_log("[DAB][CLEAR] reset complete; outputs remain OFF\r\n");
            }
        }
    }
    else if (0 == strcmp(command, "zero"))
    {
        if (DAB_STATE_READY != s_state)
        {
            dab_log("[DAB][BLOCK] zero only in READY with all power OFF\r\n");
        }
        else if (!dab_current_zero_calibrate())
        {
            dab_fault_latch(DAB_FAULT_ADC);
        }
    }
    else if (0 == strcmp(command, "unlock"))
    {
        if ((DAB_STATE_READY == s_state) && (NULL != argument) &&
            (0 == strcmp(argument, "400")))
        {
            s_unlock_high_power = true;
            dab_log("[DAB][UNLOCK] legacy token accepted; 360W output ceiling and cap up to 480ns enabled; use p and cap independently\r\n");
        }
        else
        {
            dab_log("[DAB][BLOCK] use unlock 400 only in READY\r\n");
        }
    }
    else if (0 == strcmp(command, "lock"))
    {
        if ((DAB_STATE_READY == s_state) || (DAB_STATE_PAUSED == s_state))
        {
            s_unlock_high_power = false;
            s_power_limit_w = dab_minf(s_power_limit_w, DAB_POWER_NORMAL_MAX_W);
            if (s_closed_loop_phase_cap_counts > DAB_PHASE_NORMAL_MAX_COUNTS)
            {
                s_closed_loop_phase_cap_counts = DAB_PHASE_NORMAL_MAX_COUNTS;
            }
            dab_log("[DAB][CMD] normal 100W ceiling and 320ns maximum cap restored\r\n");
        }
    }
    else if (0 == strcmp(command, "inner"))
    {
        char * secondary_argument = strtok(NULL, " ");
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_log("[DAB][INNER] source=%s target=%lu/%luns applied=%lu/%luns limit=%luns\r\n",
                    s_inner_manual_override ? "MANUAL" : "AUTO",
                    (unsigned long) ((s_primary_inner_manual_target_counts *
                                      (float) DAB_PWM_COUNT_NS) + 0.5F),
                    (unsigned long) ((s_secondary_inner_manual_target_counts *
                                      (float) DAB_PWM_COUNT_NS) + 0.5F),
                    (unsigned long) (dab_pwm_hw_primary_inner_get() *
                                     DAB_PWM_COUNT_NS),
                    (unsigned long) (dab_pwm_hw_secondary_inner_get() *
                                     DAB_PWM_COUNT_NS),
                    (unsigned long) ((s_unlock_high_power ?
                                      DAB_INNER_SHIFT_MAX_COUNTS :
                                      DAB_INNER_NORMAL_MAX_COUNTS) *
                                     DAB_PWM_COUNT_NS));
        }
        else if (0 == strcmp(argument, "auto"))
        {
            s_inner_manual_override = false;
            s_primary_inner_manual_target_counts = 0.0F;
            s_secondary_inner_manual_target_counts = 0.0F;
            dab_schedule_mark_manual();
            dab_log("[DAB][INNER] AUTO ratio-based targets restored; slew=%.2fns/ms\r\n",
                    (double) DAB_INNER_SLEW_NS_PER_MS);
        }
        else if ((DAB_STATE_READY != s_state) && (DAB_STATE_RUN != s_state))
        {
            dab_log("[DAB][BLOCK] inner tuning requires READY or RUN\r\n");
        }
        else if ((DAB_MODULATION_EPS != s_modulation_requested) &&
                 (DAB_MODULATION_TPS != s_modulation_requested))
        {
            dab_log("[DAB][BLOCK] select mod eps or mod tps before manual inner tuning\r\n");
        }
        else
        {
            float primary_ns = 0.0F;
            float secondary_ns = 0.0F;
            float const maximum_ns =
                (float) ((s_unlock_high_power ? DAB_INNER_SHIFT_MAX_COUNTS :
                          DAB_INNER_NORMAL_MAX_COUNTS) * DAB_PWM_COUNT_NS);
            if (!dab_parse_float(argument, &primary_ns) ||
                !dab_parse_float(secondary_argument, &secondary_ns) ||
                (primary_ns < 0.0F) || (secondary_ns < 0.0F) ||
                (primary_ns > maximum_ns) || (secondary_ns > maximum_ns))
            {
                dab_log("[DAB][CMD] use inner PRI_NS SEC_NS, each 0..%.0f; or inner auto\r\n",
                        (double) maximum_ns);
            }
            else
            {
                s_primary_inner_manual_target_counts =
                    primary_ns / (float) DAB_PWM_COUNT_NS;
                s_secondary_inner_manual_target_counts =
                    secondary_ns / (float) DAB_PWM_COUNT_NS;
                s_inner_manual_override = true;
                dab_schedule_mark_manual();
                dab_log("[DAB][INNER] MANUAL target=%.0f/%.0fns slew=%.2fns/ms; applied values remain in VOFA\r\n",
                        (double) primary_ns, (double) secondary_ns,
                        (double) DAB_INNER_SLEW_NS_PER_MS);
            }
        }
    }
    else if (0 == strcmp(command, "mod"))
    {
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            float const m = (s_measurements.vin_v > 1.0F) ?
                            (s_measurements.vout_v /
                             (2.0F * s_measurements.vin_v)) : 0.0F;
            dab_log("[DAB][MOD] requested=%s effective=%s M=%.3f "
                    "outer=%luns pri_inner=%luns sec_inner=%luns\r\n",
                    dab_modulation_name(s_modulation_requested),
                    dab_modulation_name(s_modulation_effective), (double) m,
                    (unsigned long) (dab_pwm_hw_phase_get() *
                                     DAB_PWM_COUNT_NS),
                    (unsigned long) (dab_pwm_hw_primary_inner_get() *
                                     DAB_PWM_COUNT_NS),
                    (unsigned long) (dab_pwm_hw_secondary_inner_get() *
                                     DAB_PWM_COUNT_NS));
        }
        else if (0 == strcmp(argument, "sps"))
        {
            (void) dab_modulation_set(DAB_MODULATION_SPS);
        }
        else if (0 == strcmp(argument, "eps"))
        {
            (void) dab_modulation_set(DAB_MODULATION_EPS);
        }
        else if (0 == strcmp(argument, "tps"))
        {
            (void) dab_modulation_set(DAB_MODULATION_TPS);
        }
        else if (0 == strcmp(argument, "auto"))
        {
            (void) dab_modulation_set(DAB_MODULATION_AUTO);
        }
        else
        {
            dab_log("[DAB][CMD] use mod sps|eps|tps|auto|show\r\n");
        }
    }
    else if (0 == strcmp(command, "idir"))
    {
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_log("[CURRENT][DIR] %s (%d); forward is default for SPS/EPS, reverse supports negative-gain TPS regions\r\n",
                    (s_current_control_direction > 0) ? "forward" : "reverse",
                    (int) s_current_control_direction);
        }
        else if ((DAB_STATE_READY != s_state) || s_control_enabled ||
                 dab_pwm_hw_outputs_are_enabled())
        {
            dab_log("[CURRENT][BLOCK] idir changes require READY with PWM/power OFF\r\n");
        }
        else if ((0 == strcmp(argument, "forward")) ||
                 (0 == strcmp(argument, "normal")))
        {
            s_current_control_direction = 1;
            dab_control_reset();
            dab_log("[CURRENT][DIR] forward (+1)\r\n");
        }
        else if ((0 == strcmp(argument, "reverse")) ||
                 (0 == strcmp(argument, "negative")))
        {
            s_current_control_direction = -1;
            dab_control_reset();
            dab_log("[CURRENT][DIR] reverse (-1)\r\n");
        }
        else
        {
            dab_log("[CURRENT][CMD] use idir forward|reverse|show\r\n");
        }
    }
    else if ((0 == strcmp(command, "iref")) ||
             (0 == strcmp(command, "ikp")) ||
             (0 == strcmp(command, "iki")) ||
             (0 == strcmp(command, "islew")))
    {
        float value = 0.0F;
        if (!dab_parse_float(argument, &value))
        {
            dab_log("[CURRENT][CMD] use iref 0..18 | ikp 0..80 | iki 0..20 | islew 0.01..100\r\n");
        }
        else if (0 == strcmp(command, "iref"))
        {
            if ((value < 0.0F) || (value > DAB_CURRENT_REFERENCE_MAX_A))
            {
                dab_log("[CURRENT][BLOCK] iref must be 0..18A\r\n");
            }
            else
            {
                float const previous_current_a =
                    s_current_reference_command_a;
                s_algorithm_logging_enabled = true;
                s_current_reference_command_a = value;
                (void) dab_energy_parameter_set(DAB_ENERGY_CONFIG_MANUAL_CURRENT_A,
                                                value);
                if (s_schedule_autoload_enabled)
                {
                    float const requested_output_w = s_vref_v * value;
                    float const power_maximum = s_unlock_high_power ?
                        DAB_POWER_ABSOLUTE_MAX_W : DAB_POWER_NORMAL_MAX_W;
                    float const required_power_limit_w = dab_clampf(
                        (1.20F * requested_output_w) + 25.0F,
                        DAB_POWER_DEFAULT_W,
                        power_maximum);
                    s_power_limit_w = dab_maxf(s_power_limit_w,
                                               required_power_limit_w);
                    s_power_command_received = true;

                    if (value > previous_current_a)
                    {
                        float const extra_phase_ns =
                            24.0F * (value - previous_current_a);
                        uint32_t extra_phase_counts = (uint32_t)
                            ((extra_phase_ns / (float) DAB_PWM_COUNT_NS) +
                             0.5F);
                        uint32_t const phase_maximum_counts =
                            s_unlock_high_power ?
                            DAB_PHASE_ABSOLUTE_MAX_COUNTS :
                            DAB_PHASE_NORMAL_MAX_COUNTS;
                        if (s_closed_loop_phase_cap_counts <
                            phase_maximum_counts)
                        {
                            uint32_t const available_counts =
                                phase_maximum_counts -
                                s_closed_loop_phase_cap_counts;
                            extra_phase_counts = (extra_phase_counts <
                                                  available_counts) ?
                                extra_phase_counts : available_counts;
                            s_closed_loop_phase_cap_counts +=
                                extra_phase_counts;
                        }
                    }
                }
                dab_log("[CURRENT][SET] manual Iref=%.3fA auto=%u Plim=%.1fW cap=%luns\r\n",
                        (double) value,
                        s_schedule_autoload_enabled ? 1U : 0U,
                        (double) s_power_limit_w,
                        (unsigned long) (s_closed_loop_phase_cap_counts *
                                         DAB_PWM_COUNT_NS));
            }
        }
        else if (0 == strcmp(command, "ikp"))
        {
            if ((value < DAB_CURRENT_KP_MIN_NS_PER_A) ||
                (value > DAB_CURRENT_KP_MAX_NS_PER_A))
            {
                dab_log("[CURRENT][BLOCK] ikp must be 0..80 ns/A\r\n");
            }
            else
            {
                s_algorithm_logging_enabled = true;
                s_current_kp_ns_per_a = value;
                dab_control_reset();
                dab_log("[CURRENT][SET] Kp=%.3fns/A\r\n", (double) value);
            }
        }
        else if (0 == strcmp(command, "iki"))
        {
            if ((value < DAB_CURRENT_KI_MIN_US_PER_AS) ||
                (value > DAB_CURRENT_KI_MAX_US_PER_AS))
            {
                dab_log("[CURRENT][BLOCK] iki must be 0..20 us/(A*s)\r\n");
            }
            else
            {
                s_algorithm_logging_enabled = true;
                s_current_ki_us_per_as = value;
                dab_control_reset();
                dab_log("[CURRENT][SET] Ki=%.4fus/(A*s)\r\n", (double) value);
            }
        }
        else if ((value < 0.01F) ||
                 (value > DAB_CURRENT_REFERENCE_SLEW_MAX_A_PER_S))
        {
            dab_log("[CURRENT][BLOCK] islew must be 0.01..100 A/s\r\n");
        }
        else
        {
            s_algorithm_logging_enabled = true;
            s_current_reference_slew_a_per_s = value;
            dab_log("[CURRENT][SET] reference slew=%.3fA/s\r\n", (double) value);
        }
    }
    else if (0 == strcmp(command, "energy"))
    {
        dab_energy_mode_t mode = s_energy_shadow.config.mode;
        bool set = true;
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            set = false;
            dab_print_energy_status();
        }
        else if (0 == strcmp(argument, "off")) mode = DAB_ENERGY_MODE_OFF;
        else if (0 == strcmp(argument, "manual")) mode = DAB_ENERGY_MODE_MANUAL_CURRENT;
        else if (0 == strcmp(argument, "mppt")) mode = DAB_ENERGY_MODE_MPPT_ONLY;
        else if (0 == strcmp(argument, "charge")) mode = DAB_ENERGY_MODE_CHARGER_ONLY;
        else if (0 == strcmp(argument, "solar")) mode = DAB_ENERGY_MODE_SOLAR_CHARGER;
        else
        {
            set = false;
            dab_log("[ENERGY][CMD] use energy off|manual|mppt|charge|solar|show\r\n");
        }
        if (set)
        {
            if (dab_energy_parameter_set(DAB_ENERGY_CONFIG_MODE, (float) mode))
            {
                dab_log("[ENERGY][SET] mode=%s; use arch energy to close CPU0 current PI around CPU1 reference\r\n",
                        dab_energy_mode_name(mode));
            }
            else
            {
                dab_log("[ENERGY][BLOCK] invalid setting or CPU1 config queue full\r\n");
            }
        }
    }
    else if (0 == strcmp(command, "mppt"))
    {
        char * value_text = strtok(NULL, " ");
        float value = 0.0F;
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_print_energy_status();
        }
        else if ((0 == strcmp(argument, "off")) ||
                 (0 == strcmp(argument, "po")) ||
                 (0 == strcmp(argument, "inc")))
        {
            dab_mppt_mode_t const mode = (0 == strcmp(argument, "off")) ?
                DAB_MPPT_MODE_OFF : ((0 == strcmp(argument, "po")) ?
                DAB_MPPT_MODE_PERTURB_OBSERVE :
                DAB_MPPT_MODE_INCREMENTAL_CONDUCTANCE);
            if (!dab_energy_parameter_set(DAB_ENERGY_CONFIG_MPPT_MODE,
                                          (float) mode))
            {
                dab_log("[MPPT][BLOCK] CPU1 config queue full\r\n");
            }
            else
            {
                dab_log("[MPPT][SET] mode=%s\r\n", dab_mppt_mode_name(mode));
            }
        }
        else if (!dab_parse_float(value_text, &value))
        {
            dab_log("[MPPT][CMD] mppt po|inc|off|show | step V | interval MS | vmin V | vmax V | pmax W | fixed W\r\n");
        }
        else
        {
            dab_energy_config_key_t key = DAB_ENERGY_CONFIG_COUNT;
            if (0 == strcmp(argument, "step")) key = DAB_ENERGY_CONFIG_MPPT_STEP_V;
            else if (0 == strcmp(argument, "interval")) key = DAB_ENERGY_CONFIG_MPPT_INTERVAL_MS;
            else if (0 == strcmp(argument, "vmin")) key = DAB_ENERGY_CONFIG_MPPT_MIN_V;
            else if (0 == strcmp(argument, "vmax")) key = DAB_ENERGY_CONFIG_MPPT_MAX_V;
            else if (0 == strcmp(argument, "pmax")) key = DAB_ENERGY_CONFIG_MPPT_MAX_POWER_W;
            else if (0 == strcmp(argument, "fixed")) key = DAB_ENERGY_CONFIG_MPPT_FIXED_POWER_W;
            if ((DAB_ENERGY_CONFIG_COUNT == key) ||
                !dab_energy_parameter_set(key, value))
            {
                dab_log("[MPPT][BLOCK] invalid value/subcommand or CPU1 config queue full\r\n");
            }
            else
            {
                if (DAB_ENERGY_CONFIG_MPPT_FIXED_POWER_W == key)
                {
                    (void) dab_energy_parameter_set(DAB_ENERGY_CONFIG_MPPT_MODE,
                                                    (float) DAB_MPPT_MODE_FIXED_POWER);
                }
                dab_log("[MPPT][SET] %s=%.3f\r\n", argument, (double) value);
            }
        }
    }
    else if ((0 == strcmp(command, "bat")) ||
             (0 == strcmp(command, "battery")))
    {
        char * value_text = strtok(NULL, " ");
        float value = 0.0F;
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_print_energy_status();
        }
        else if ((0 == strcmp(argument, "start")) ||
                 (0 == strcmp(argument, "stop")))
        {
            float const start = (0 == strcmp(argument, "start")) ? 1.0F : 0.0F;
            bool configured = false;
            if (start > 0.5F)
            {
                /* Queue the derived lower-controller mode before START so
                 * CPU1 can never observe an armed charger in a stale mode. */
                dab_energy_mode_t const start_mode =
                    dab_energy_charge_start_mode(
                        s_energy_shadow.config.battery_profile,
                        s_energy_shadow.config.mppt_mode);
                configured = dab_energy_parameter_set(
                    DAB_ENERGY_CONFIG_MODE, (float) start_mode) &&
                    dab_energy_parameter_set(
                        DAB_ENERGY_CONFIG_BATTERY_START, start);
            }
            else
            {
                configured = dab_energy_parameter_set(
                    DAB_ENERGY_CONFIG_BATTERY_START, start);
            }
            if (configured)
            {
                if ((start > 0.5F) && (DAB_STATE_READY == s_state) &&
                    !s_control_enabled && !dab_pwm_hw_outputs_are_enabled())
                {
                    /* A high-level battery start is sufficient for standalone
                     * operation.  The lower firmware selects fused MPPT for
                     * the two qualified lithium profiles when P&O/INC is
                     * enabled; CPU1 still permits MPPT ownership only in CC.
                     * PRECHARGE/CV/DONE remain charger-owned. */
                    s_control_architecture = DAB_CONTROL_ARCH_ENERGY_SUPERVISOR;
                    s_schedule_autoload_enabled = true;
                    if (!dab_schedule_apply_current())
                    {
                        s_schedule_autoload_enabled = false;
                        dab_log("[BAT][WARN] no powered parameter seed for this profile; charger algorithm is configured but automatic power start remains unarmed\r\n");
                    }
                    else
                    {
#if DAB_ALLOW_POWER_TRANSFER
                        dab_control_start_request();
                        dab_log("[BAT][AUTO] high-level START owns schedule/power; energy=%s; MPPT is automatic only in CC; no host mode/modulation command required\r\n",
                                dab_energy_mode_name(
                                    s_energy_shadow.config.mode));
#endif
                    }
                }
                else if (start <= 0.5F)
                {
                    s_schedule_autoload_enabled = false;
                    dab_force_all_safe();
                    dab_enter_state((0U == s_fault_mask) ?
                                    DAB_STATE_READY : DAB_STATE_FAULT);
                    dab_log("[BAT][AUTO] high-level STOP PWM=0 RUN=0 four path MOS=OFF\r\n");
                }
                dab_log("[BAT][SET] charger=%s; firmware never bypasses pack BMS limits\r\n",
                        (start > 0.5F) ? "START" : "STOP");
            }
            else
            {
                dab_log("[BAT][BLOCK] invalid setting or CPU1 config queue full\r\n");
            }
        }
        else if (0 == strcmp(argument, "profile"))
        {
            dab_battery_profile_t profile = DAB_BATTERY_PROFILE_CUSTOM;
            bool valid = (NULL != value_text);
            if (valid && ((0 == strcmp(value_text, "liion10")) ||
                          (0 == strcmp(value_text, "nmc10"))))
                profile = DAB_BATTERY_PROFILE_LI_ION_10S_42V;
            else if (valid && ((0 == strcmp(value_text, "lfp12")) ||
                               (0 == strcmp(value_text, "lifepo4"))))
                profile = DAB_BATTERY_PROFILE_LIFEPO4_12S_43P8V;
            else if (valid && ((0 == strcmp(value_text, "lead36")) ||
                               (0 == strcmp(value_text, "vrla36"))))
                profile = DAB_BATTERY_PROFILE_LEAD_ACID_18S_36V;
            else if (!(valid && (0 == strcmp(value_text, "custom"))))
                valid = false;
            if (!valid || !dab_energy_parameter_set(DAB_ENERGY_CONFIG_BATTERY_PROFILE,
                                                     (float) profile))
            {
                dab_log("[BAT][CMD] bat profile liion10|lfp12|lead36|custom\r\n");
            }
            else
            {
                dab_log("[BAT][SET] profile=%s; confirm pack datasheet and BMS current/temperature limits before real battery\r\n",
                        dab_battery_profile_name(profile));
                dab_print_energy_status();
            }
        }
        else if ((0 == strcmp(argument, "temp")) &&
                 (NULL != value_text) && (0 == strcmp(value_text, "off")))
        {
            (void) dab_energy_parameter_set(DAB_ENERGY_CONFIG_BATTERY_TEMPERATURE_VALID,
                                            0.0F);
            dab_log("[BAT][SET] external battery temperature unavailable; BMS remains authoritative\r\n");
        }
        else if (!dab_parse_float(value_text, &value))
        {
            dab_log("[BAT][CMD] bat start|stop|show|profile TYPE | capacity Ah | imax/ipre/iterm A | vpre/vcv/vfloat V | absorb/timeout s | temp C | kp/ki VALUE\r\n");
        }
        else
        {
            dab_energy_config_key_t key = DAB_ENERGY_CONFIG_COUNT;
            if (0 == strcmp(argument, "capacity")) key = DAB_ENERGY_CONFIG_BATTERY_CAPACITY_AH;
            else if (0 == strcmp(argument, "imax")) key = DAB_ENERGY_CONFIG_BATTERY_MAX_CURRENT_A;
            else if (0 == strcmp(argument, "ipre")) key = DAB_ENERGY_CONFIG_BATTERY_PRECHARGE_CURRENT_A;
            else if (0 == strcmp(argument, "iterm")) key = DAB_ENERGY_CONFIG_BATTERY_TERMINATE_CURRENT_A;
            else if (0 == strcmp(argument, "vpre")) key = DAB_ENERGY_CONFIG_BATTERY_PRECHARGE_V;
            else if (0 == strcmp(argument, "vcv")) key = DAB_ENERGY_CONFIG_BATTERY_CV_V;
            else if (0 == strcmp(argument, "vfloat")) key = DAB_ENERGY_CONFIG_BATTERY_FLOAT_V;
            else if (0 == strcmp(argument, "absorb")) key = DAB_ENERGY_CONFIG_BATTERY_ABSORB_S;
            else if (0 == strcmp(argument, "timeout")) key = DAB_ENERGY_CONFIG_BATTERY_TIMEOUT_S;
            else if (0 == strcmp(argument, "temp")) key = DAB_ENERGY_CONFIG_BATTERY_TEMPERATURE_C;
            else if (0 == strcmp(argument, "kp")) key = DAB_ENERGY_CONFIG_CHARGE_KP_A_PER_V;
            else if (0 == strcmp(argument, "ki")) key = DAB_ENERGY_CONFIG_CHARGE_KI_A_PER_VS;
            bool ok = (DAB_ENERGY_CONFIG_COUNT != key) &&
                      dab_energy_parameter_set(key, value);
            if (ok && (DAB_ENERGY_CONFIG_BATTERY_TEMPERATURE_C == key))
            {
                ok = dab_energy_parameter_set(DAB_ENERGY_CONFIG_BATTERY_TEMPERATURE_VALID,
                                              1.0F);
            }
            if (!ok)
            {
                dab_log("[BAT][BLOCK] invalid value/subcommand or CPU1 config queue full\r\n");
            }
            else
            {
                dab_log("[BAT][SET] %s=%.3f\r\n", argument, (double) value);
            }
        }
    }
    else if (0 == strcmp(command, "ai"))
    {
        char * value2 = strtok(NULL, " ");
        char * value3 = strtok(NULL, " ");
        float hint = 0.0F;
        float confidence = 0.0F;
        float ttl = 1000.0F;
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_print_energy_status();
        }
        else if ((0 == strcmp(argument, "off")) ||
                 (0 == strcmp(argument, "assist")))
        {
            dab_ai_assist_mode_t const mode = (0 == strcmp(argument, "assist")) ?
                DAB_AI_ASSIST_BOUNDED : DAB_AI_ASSIST_OFF;
            if (dab_energy_parameter_set(DAB_ENERGY_CONFIG_AI_MODE, (float) mode))
            {
                dab_log("[AIHOOK][SET] mode=%s; AI may only reduce the deterministic local current ceiling\r\n",
                        (DAB_AI_ASSIST_BOUNDED == mode) ? "BOUNDED" : "OFF");
            }
        }
        else if (0 == strcmp(argument, "mod"))
        {
            dab_modulation_mode_t modulation = DAB_MODULATION_SPS;
            char * value4 = strtok(NULL, " ");
            if ((NULL != value4) && !dab_parse_float(value4, &ttl))
            {
                ttl = -1.0F;
            }
            bool const valid = dab_remote_modulation_parse(value2, &modulation) &&
                               (DAB_MODULATION_AUTO != modulation) &&
                               dab_parse_float(value3, &confidence);
            bool const ok = valid &&
                            dab_energy_parameter_set(DAB_ENERGY_CONFIG_AI_HINT_MODULATION,
                                                     (float) modulation) &&
                            dab_energy_parameter_set(DAB_ENERGY_CONFIG_AI_MODULATION_CONFIDENCE,
                                                     confidence) &&
                            dab_energy_parameter_set(DAB_ENERGY_CONFIG_AI_MODULATION_TTL_MS,
                                                     ttl);
            if (ok)
            {
                dab_log("[AIHOOK][SET] modulation_hint=%s confidence=%.3f ttl=%.0fms; recorded only, no live mode switch\r\n",
                        dab_modulation_name(modulation),
                        (double) confidence,
                        (double) ttl);
            }
            else
            {
                dab_log("[AIHOOK][BLOCK] mod requires sps|eps|tps, confidence 0..1, ttl 100..10000ms\r\n");
            }
        }
        else if ((0 == strcmp(argument, "hint")) &&
                 dab_parse_float(value2, &hint) &&
                 dab_parse_float(value3, &confidence))
        {
            char * value4 = strtok(NULL, " ");
            if ((NULL != value4) && !dab_parse_float(value4, &ttl))
            {
                ttl = -1.0F;
            }
            bool const ok = dab_energy_parameter_set(DAB_ENERGY_CONFIG_AI_HINT_CURRENT_A,
                                                     hint) &&
                            dab_energy_parameter_set(DAB_ENERGY_CONFIG_AI_CONFIDENCE,
                                                     confidence) &&
                            dab_energy_parameter_set(DAB_ENERGY_CONFIG_AI_TTL_MS,
                                                     ttl);
            if (ok)
            {
                dab_log("[AIHOOK][SET] hint=%.3fA confidence=%.3f ttl=%.0fms\r\n",
                        (double) hint, (double) confidence, (double) ttl);
            }
            else
            {
                dab_log("[AIHOOK][BLOCK] hint requires 0..18A, confidence 0..1, ttl 100..10000ms\r\n");
            }
        }
        else
        {
            dab_log("[AIHOOK][CMD] ai off|assist|show | ai hint CURRENT_A CONFIDENCE [TTL_MS] | ai mod sps|eps|tps CONFIDENCE [TTL_MS]\r\n");
        }
    }
    else if ((0 == strcmp(command, "protect")) ||
             (0 == strcmp(command, "guard")))
    {
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_log("[DAB][GUARD] %s\r\n", dab_guard_profile_name(s_guard_profile));
        }
        else if ((DAB_STATE_READY != s_state) ||
                 dab_pwm_hw_outputs_are_enabled())
        {
            dab_log("[DAB][BLOCK] guard change requires READY with PWM OFF\r\n");
        }
        else if (0 == strcmp(argument, "bench"))
        {
            s_guard_profile = DAB_GUARD_PROFILE_BENCH;
            s_unlock_high_power = true;
            dab_log("[DAB][GUARD] BENCH: soft power/current retreat, cold clamp and no-rise disabled; absolute/hardware trips remain\r\n");
        }
        else if (0 == strcmp(argument, "normal"))
        {
            s_guard_profile = DAB_GUARD_PROFILE_NORMAL;
            dab_log("[DAB][GUARD] NORMAL layered protection restored\r\n");
        }
        else
        {
            dab_log("[DAB][CMD] protect bench|normal|show\r\n");
        }
    }
    else if (0 == strcmp(command, "vinadapt"))
    {
        bool const config_ready = (DAB_STATE_READY == s_state) &&
                                  !s_control_enabled &&
                                  !dab_pwm_hw_outputs_are_enabled();
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_print_vin_adaptation_status();
        }
        else if (!config_ready)
        {
            dab_log("[VAD][BLOCK] configuration changes require READY with PWM/power OFF\r\n");
        }
        else if ((0 == strcmp(argument, "on")) ||
                 (0 == strcmp(argument, "off")))
        {
            s_vin_adaptation.config.enabled = (0 == strcmp(argument, "on"));
            dab_vin_adaptation_runtime_reset(&s_vin_adaptation);
            s_algorithm_logging_enabled = true;
            dab_log("[VAD] %s; current target, base gains, phase cap and modulation remain unchanged\r\n",
                    s_vin_adaptation.config.enabled ? "ON" : "OFF");
            dab_print_vin_adaptation_status();
        }
        else if (0 == strcmp(argument, "range"))
        {
            char * minimum_text = strtok(NULL, " ");
            char * maximum_text = strtok(NULL, " ");
            float minimum_v = 0.0F;
            float maximum_v = 0.0F;
            if (!dab_parse_float(minimum_text, &minimum_v) ||
                !dab_parse_float(maximum_text, &maximum_v) ||
                (minimum_v < 16.0F) || (maximum_v > 24.0F) ||
                (minimum_v >= maximum_v))
            {
                dab_log("[VAD][BLOCK] use vinadapt range MIN MAX, 16<=MIN<MAX<=24V\r\n");
            }
            else
            {
                s_vin_adaptation.config.minimum_vin_v = minimum_v;
                s_vin_adaptation.config.maximum_vin_v = maximum_v;
                dab_vin_adaptation_runtime_reset(&s_vin_adaptation);
                dab_print_vin_adaptation_status();
            }
        }
        else
        {
            char * value_text = strtok(NULL, " ");
            float value = 0.0F;
            if (!dab_parse_float(value_text, &value))
            {
                dab_log("[VAD][CMD] vinadapt on|off|show|range MIN MAX|nominal V|kff 0..1|alpha 0.0001..1\r\n");
            }
            else if ((0 == strcmp(argument, "nominal")) &&
                     (value >= 18.0F) && (value <= 24.0F))
            {
                s_vin_adaptation.config.nominal_vin_v = value;
                dab_vin_adaptation_runtime_reset(&s_vin_adaptation);
                dab_print_vin_adaptation_status();
            }
            else if ((0 == strcmp(argument, "kff")) &&
                     (value >= 0.0F) && (value <= 1.0F))
            {
                s_vin_adaptation.config.feedforward_strength = value;
                dab_print_vin_adaptation_status();
            }
            else if ((0 == strcmp(argument, "alpha")) &&
                     (value >= 0.0001F) && (value <= 1.0F))
            {
                s_vin_adaptation.config.filter_alpha = value;
                dab_vin_adaptation_runtime_reset(&s_vin_adaptation);
                dab_print_vin_adaptation_status();
            }
            else
            {
                dab_log("[VAD][BLOCK] nominal=18..24V, kff=0..1, alpha=0.0001..1\r\n");
            }
        }
    }
    else if (0 == strcmp(command, "autotune"))
    {
        bool const config_ready = (DAB_STATE_READY == s_state) &&
                                  !s_control_enabled &&
                                  !dab_pwm_hw_outputs_are_enabled();
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_print_autotune_status();
        }
        else if (0 == strcmp(argument, "start"))
        {
            char * step_text = strtok(NULL, " ");
            float step_ns = s_autotune.config.perturbation_ns;
            bool const step_valid = (NULL == step_text) ||
                (dab_parse_float(step_text, &step_ns) &&
                 (step_ns >= 4.0F) && (step_ns <= 40.0F));
            bool const current_arch =
                DAB_CONTROL_ARCH_VOLTAGE_PI_SINGLE != s_control_architecture;
            if (!step_valid || (DAB_STATE_RUN != s_state) || !current_arch ||
                s_manual_phase_override || !s_measurements.valid ||
                (s_current_reference_active_a < 0.20F) ||
                (DAB_MODSWITCH_TRANSITION == s_modswitch.stage) ||
                (DAB_MODSWITCH_VERIFY == s_modswitch.stage))
            {
                dab_log("[AUTOTUNE][BLOCK] start requires stable RUN current/energy/cp arch, Iref>=0.2A, no manual phase/switch; optional step=4..40ns\r\n");
            }
            else
            {
                s_autotune.config.perturbation_ns = step_ns;
                if (dab_autotune_start(
                        &s_autotune,
                        s_phase_command_counts * (float) DAB_PWM_COUNT_NS,
                        (float) s_closed_loop_phase_cap_counts *
                            (float) DAB_PWM_COUNT_NS,
                        s_measurements.iout_a))
                {
                    s_algorithm_logging_enabled = true;
                    dab_log("[AUTOTUNE] START step=%.1fns; current target is held and only bounded local phase perturbations are injected\r\n",
                            (double) step_ns);
                }
                else
                {
                    dab_log("[AUTOTUNE][BLOCK] insufficient phase headroom for +/-%.1fns or tuning already active\r\n",
                            (double) step_ns);
                }
            }
        }
        else if (0 == strcmp(argument, "stop"))
        {
            s_autotune.running = false;
            s_autotune.injection_ns = 0.0F;
            s_autotune.stage = DAB_AUTOTUNE_FAILED;
            s_autotune.result.failure_reason =
                DAB_AUTOTUNE_FAILURE_USER_STOP;
            dab_log("[AUTOTUNE] STOP; original controller remains active\r\n");
        }
        else if ((0 == strcmp(argument, "apply")) && config_ready &&
                 s_autotune.result.valid)
        {
            s_current_control_direction =
                s_autotune.result.recommended_direction;
            s_current_kp_ns_per_a =
                s_autotune.result.recommended_kp_ns_per_a;
            s_current_ki_us_per_as =
                s_autotune.result.recommended_ki_us_per_as;
            s_current_reference_slew_a_per_s =
                s_autotune.result.recommended_slew_a_per_s;
            s_closed_loop_phase_cap_counts = (uint32_t)
                ((s_autotune.result.recommended_phase_cap_ns /
                  (float) DAB_PWM_COUNT_NS) + 0.5F);
            s_autotune.result.applied = true;
            dab_schedule_mark_manual();
            dab_control_reset();
            dab_print_autotune_status();
        }
        else if (!config_ready)
        {
            dab_log("[AUTOTUNE][BLOCK] tuning configuration/apply requires READY with PWM/power OFF\r\n");
        }
        else if (0 == strcmp(argument, "auto"))
        {
            char * value_text = strtok(NULL, " ");
            if ((NULL != value_text) &&
                ((0 == strcmp(value_text, "on")) ||
                 (0 == strcmp(value_text, "off"))))
            {
                s_autotune.config.auto_apply = (0 == strcmp(value_text, "on"));
                dab_print_autotune_status();
            }
            else
            {
                dab_log("[AUTOTUNE][CMD] use autotune auto on|off\r\n");
            }
        }
        else
        {
            char * value_text = strtok(NULL, " ");
            float value = 0.0F;
            if (!dab_parse_float(value_text, &value))
            {
                dab_log("[AUTOTUNE][CMD] start [4..40ns]|stop|show|apply|auto on/off|step ns|settle s|sample s|min_delta A\r\n");
            }
            else if ((0 == strcmp(argument, "step")) &&
                     (value >= 4.0F) && (value <= 40.0F))
            {
                s_autotune.config.perturbation_ns = value;
                dab_print_autotune_status();
            }
            else if ((0 == strcmp(argument, "settle")) &&
                     (value >= 0.10F) && (value <= 5.0F))
            {
                s_autotune.config.settle_time_s = value;
                dab_print_autotune_status();
            }
            else if ((0 == strcmp(argument, "sample")) &&
                     (value >= 0.05F) && (value <= 2.0F))
            {
                s_autotune.config.sample_time_s = value;
                dab_print_autotune_status();
            }
            else if ((0 == strcmp(argument, "min_delta")) &&
                     (value >= 0.005F) && (value <= 1.0F))
            {
                s_autotune.config.minimum_current_delta_a = value;
                dab_print_autotune_status();
            }
            else
            {
                dab_log("[AUTOTUNE][BLOCK] step 4..40ns, settle 0.1..5s, sample 0.05..2s, min_delta 0.005..1A\r\n");
            }
        }
    }
    else if (0 == strcmp(command, "modswitch"))
    {
        bool const config_ready = (DAB_STATE_READY == s_state) &&
                                  !s_control_enabled &&
                                  !dab_pwm_hw_outputs_are_enabled();
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_print_modswitch_status();
        }
        else if ((0 == strcmp(argument, "slew")) ||
                 (0 == strcmp(argument, "pass")) ||
                 (0 == strcmp(argument, "abort")) ||
                 (0 == strcmp(argument, "verify")) ||
                 (0 == strcmp(argument, "timeout")) ||
                 (0 == strcmp(argument, "dwell")))
        {
            char * value_text = strtok(NULL, " ");
            float value = 0.0F;
            if (!config_ready || !dab_parse_float(value_text, &value))
            {
                dab_log("[MODSW][BLOCK] configuration requires READY; provide a numeric value\r\n");
            }
            else if ((0 == strcmp(argument, "slew")) &&
                     (value >= 0.1F) && (value <= 20.0F))
            {
                s_modswitch.config.inner_slew_ns_per_ms = value;
                dab_print_modswitch_status();
            }
            else if ((0 == strcmp(argument, "pass")) &&
                     (value >= 1.0F) && (value <= 10.0F))
            {
                s_modswitch.config.allowed_metric_error_percent = value;
                dab_print_modswitch_status();
            }
            else if ((0 == strcmp(argument, "abort")) &&
                     (value >= 5.0F) && (value <= 50.0F))
            {
                s_modswitch.config.abort_metric_error_percent = value;
                dab_print_modswitch_status();
            }
            else if ((0 == strcmp(argument, "verify")) &&
                     (value >= 20.0F) && (value <= 1000.0F))
            {
                s_modswitch.config.verify_time_s = value / 1000.0F;
                dab_print_modswitch_status();
            }
            else if ((0 == strcmp(argument, "timeout")) &&
                     (value >= 100.0F) && (value <= 5000.0F))
            {
                s_modswitch.config.timeout_s = value / 1000.0F;
                dab_print_modswitch_status();
            }
            else if ((0 == strcmp(argument, "dwell")) &&
                     (value >= 100.0F) && (value <= 10000.0F))
            {
                s_modswitch.config.minimum_dwell_s = value / 1000.0F;
                dab_print_modswitch_status();
            }
            else
            {
                dab_log("[MODSW][BLOCK] slew 0.1..20ns/ms, pass 1..10%%, abort 5..50%%, verify 20..1000ms, timeout 100..5000ms, dwell 100..10000ms\r\n");
            }
        }
        else
        {
            dab_modulation_mode_t target_mode = DAB_MODULATION_AUTO;
            if (0 == strcmp(argument, "sps")) target_mode = DAB_MODULATION_SPS;
            else if (0 == strcmp(argument, "eps")) target_mode = DAB_MODULATION_EPS;
            else if (0 == strcmp(argument, "tps")) target_mode = DAB_MODULATION_TPS;

            char * primary_text = strtok(NULL, " ");
            char * secondary_text = strtok(NULL, " ");
            char * direction_text = strtok(NULL, " ");
            float primary_ns = 0.0F;
            float secondary_ns = 0.0F;
            int8_t target_direction = s_current_control_direction;
            bool values_valid = DAB_MODULATION_SPS == target_mode;
            if (DAB_MODULATION_SPS == target_mode)
            {
                direction_text = primary_text;
            }
            else
            {
                values_valid = dab_parse_float(primary_text, &primary_ns) &&
                               dab_parse_float(secondary_text, &secondary_ns);
            }
            if (NULL != direction_text)
            {
                if (0 == strcmp(direction_text, "forward")) target_direction = 1;
                else if (0 == strcmp(direction_text, "reverse")) target_direction = -1;
                else values_valid = false;
            }
            float const inner_maximum_ns =
                (float) DAB_INNER_SHIFT_MAX_COUNTS *
                (float) DAB_PWM_COUNT_NS;
            float const baseline_metric =
                (DAB_CONTROL_ARCH_VOLTAGE_PI_SINGLE == s_control_architecture) ?
                s_measurements.pout_w : s_measurements.iout_a;
            bool const can_start = (DAB_STATE_RUN == s_state) &&
                s_measurements.valid && !s_manual_phase_override &&
                !s_autotune.running &&
                (DAB_MODULATION_AUTO != target_mode) &&
                (DAB_MODULATION_AUTO != s_modulation_requested) &&
                (target_mode != s_modulation_effective) &&
                values_valid && (primary_ns >= 0.0F) &&
                (primary_ns <= inner_maximum_ns) &&
                (secondary_ns >= 0.0F) &&
                (secondary_ns <= inner_maximum_ns);
            if (!can_start)
            {
                dab_log("[MODSW][BLOCK] use RUN: modswitch sps [dir] | eps PRI_NS SEC_NS [dir] | tps PRI_NS SEC_NS [dir]; AUTO/manual-phase/tuning not allowed\r\n");
            }
            else
            {
                s_modswitch_source_primary_inner_counts =
                    s_primary_inner_command_counts;
                s_modswitch_source_secondary_inner_counts =
                    s_secondary_inner_command_counts;
                if (dab_modswitch_start(
                        &s_modswitch,
                        (uint8_t) s_modulation_effective,
                        (uint8_t) target_mode,
                        s_current_control_direction,
                        target_direction,
                        primary_ns,
                        secondary_ns,
                        baseline_metric))
                {
                    s_modulation_requested = target_mode;
                    s_current_control_direction = target_direction;
                    s_inner_manual_override =
                        DAB_MODULATION_SPS != target_mode;
                    s_primary_inner_manual_target_counts =
                        primary_ns / (float) DAB_PWM_COUNT_NS;
                    s_secondary_inner_manual_target_counts =
                        secondary_ns / (float) DAB_PWM_COUNT_NS;
                    s_modswitch_completion_reported = false;
                    s_algorithm_logging_enabled = true;
                    dab_schedule_mark_manual();
                    dab_log("[MODSW] START %s->%s effectivePhase=%.1fns innerTarget=%.1f/%.1fns idir=%s; PI integral preserved\r\n",
                            dab_modulation_name(
                                (dab_modulation_mode_t) s_modswitch.source_mode),
                            dab_modulation_name(target_mode),
                            (double) (s_phase_command_counts *
                                      (float) DAB_PWM_COUNT_NS),
                            (double) primary_ns,
                            (double) secondary_ns,
                            (target_direction < 0) ? "reverse" : "forward");
                }
                else
                {
                    dab_log("[MODSW][BLOCK] transition active or minimum dwell not met\r\n");
                }
            }
        }
    }
    else if (0 == strcmp(command, "cpdamp"))
    {
        bool const config_ready = (DAB_STATE_READY == s_state) &&
                                  !s_control_enabled &&
                                  !dab_pwm_hw_outputs_are_enabled();
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_print_cp_damping_status();
        }
        else if ((0 == strcmp(argument, "on")) ||
                 (0 == strcmp(argument, "off")))
        {
            if (!config_ready)
            {
                dab_log("[CPD][BLOCK] damping A/B selection requires READY with PWM/power OFF\r\n");
            }
            else
            {
                s_cp_damping.config.enabled = (0 == strcmp(argument, "on"));
                dab_cp_damping_runtime_reset(&s_cp_damping);
                s_algorithm_logging_enabled = true;
                if ((DAB_CONTROL_ARCH_CP_DAMPED_CASCADE ==
                     s_control_architecture) && s_schedule_autoload_enabled)
                {
                    (void) dab_schedule_apply_current();
                }
                dab_print_cp_damping_status();
            }
        }
        else
        {
            char * value_text = strtok(NULL, " ");
            float value = 0.0F;
            bool const parsed = dab_parse_float(value_text, &value);
            if ((0 == strcmp(argument, "target")) && parsed &&
                ((DAB_STATE_READY == s_state) ||
                 ((DAB_STATE_RUN == s_state) &&
                  (DAB_CONTROL_ARCH_CP_DAMPED_CASCADE ==
                   s_control_architecture))) &&
                (value >= DAB_POWER_DEFAULT_W) &&
                (value <= s_power_limit_w))
            {
                s_cp_damping.config.target_power_w = value;
                s_algorithm_logging_enabled = true;
                dab_log("[CPD] target=%.1fW; active reference slews at %.1fW/s\r\n",
                        (double) value,
                        (double) s_cp_damping.config.target_slew_w_per_s);
            }
            else if (!config_ready || !parsed)
            {
                dab_log("[CPD][BLOCK] gains require READY; live target is allowed only in RUN arch cp and must not exceed p ceiling\r\n");
            }
            else if ((0 == strcmp(argument, "kp")) &&
                     (value >= 0.0F) && (value <= 0.10F))
            {
                s_cp_damping.config.power_kp_a_per_w = value;
                dab_print_cp_damping_status();
            }
            else if ((0 == strcmp(argument, "ki")) &&
                     (value >= 0.0F) && (value <= 1.0F))
            {
                s_cp_damping.config.power_ki_a_per_ws = value;
                dab_print_cp_damping_status();
            }
            else if ((0 == strcmp(argument, "gv")) &&
                     (value >= 0.0F) && (value <= 1.0F))
            {
                s_cp_damping.config.virtual_conductance_a_per_v = value;
                dab_print_cp_damping_status();
            }
            else if ((0 == strcmp(argument, "vnom")) &&
                     (value >= 36.0F) && (value <= 48.0F))
            {
                s_cp_damping.config.nominal_output_v = value;
                dab_print_cp_damping_status();
            }
            else if ((0 == strcmp(argument, "vmin")) &&
                     (value >= 5.0F) && (value <= 36.0F))
            {
                s_cp_damping.config.minimum_output_v = value;
                dab_print_cp_damping_status();
            }
            else if ((0 == strcmp(argument, "slew")) &&
                     (value >= 1.0F) && (value <= 1000.0F))
            {
                s_cp_damping.config.target_slew_w_per_s = value;
                dab_print_cp_damping_status();
            }
            else if ((0 == strcmp(argument, "imax")) &&
                     (value >= 0.1F) &&
                     (value <= DAB_CURRENT_REFERENCE_MAX_A))
            {
                s_cp_damping.config.current_limit_a = value;
                dab_print_cp_damping_status();
            }
            else
            {
                dab_log("[CPD][CMD] on|off|show|target W|kp 0..0.1|ki 0..1|gv 0..1|vnom 36..48|vmin 5..36|slew 1..1000|imax 0.1..18\r\n");
            }
        }
    }
    else if (0 == strcmp(command, "bundle"))
    {
        dab_print_parameter_bundle();
    }
    else if ((0 == strcmp(command, "chgtable")) ||
             (0 == strcmp(command, "validated")))
    {
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_print_validated_charger_table();
        }
        else
        {
            dab_log("[DAB][CMD] use chgtable show\r\n");
        }
    }
    else if ((0 == strcmp(command, "sched")) ||
             (0 == strcmp(command, "schedule")))
    {
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_log("[DAB][SCHED] enabled=%u auto=%u start_block=%u entry=%lu voltage_entries=%lu current_entries=%lu table=%s\r\n",
                    s_schedule_enabled ? 1U : 0U,
                    s_schedule_autoload_enabled ? 1U : 0U,
                    s_configuration_start_blocked ? 1U : 0U,
                    (unsigned long) s_schedule_entry_id,
                    (unsigned long) DAB_TUNED_PARAMETER_TABLE_ENTRY_COUNT,
                    (unsigned long) DAB_TUNED_CURRENT_ENTRY_COUNT,
                    DAB_TUNED_PARAMETER_TABLE_VERSION);
        }
        else if (0 == strcmp(argument, "off"))
        {
            s_schedule_autoload_enabled = false;
            dab_schedule_mark_manual();
            s_configuration_start_blocked = false;
            dab_log("[DAB][SCHED] OFF; current values retained as deliberate manual bundle and automatic-start interlock cleared\r\n");
        }
        else if (0 == strcmp(argument, "on"))
        {
            (void) dab_schedule_apply_current();
        }
        else if (0 == strcmp(argument, "auto"))
        {
            s_schedule_autoload_enabled = true;
            if (!dab_schedule_apply_current())
            {
                s_schedule_autoload_enabled = false;
            }
            else
            {
                dab_log("[DAB][SCHED] AUTO armed; target/Vin seed refresh occurs on every start\r\n");
            }
        }
        else
        {
            dab_log("[DAB][CMD] use sched on|auto|off|show\r\n");
        }
    }
    else if ((0 == strcmp(command, "arch")) ||
             (0 == strcmp(command, "architecture")))
    {
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_log("[DAB][ARCH] %s id=%u; current PI runs on CPU0, energy supervisor runs on CPU1\r\n",
                    dab_control_architecture_name(s_control_architecture),
                    (unsigned int) s_control_architecture);
        }
        else if (((0 == strcmp(argument, "single")) ||
                  (0 == strcmp(argument, "voltage"))) &&
                 (DAB_STATE_READY == s_state) &&
                 !dab_pwm_hw_outputs_are_enabled())
        {
            s_control_architecture = DAB_CONTROL_ARCH_VOLTAGE_PI_SINGLE;
            dab_schedule_mark_manual();
            dab_control_reset();
            dab_log("[DAB][ARCH] VOLTAGE_PI_SINGLE_LOOP selected\r\n");
        }
        else if ((0 == strcmp(argument, "current")) &&
                 (DAB_STATE_READY == s_state) &&
                 !dab_pwm_hw_outputs_are_enabled())
        {
            s_algorithm_logging_enabled = true;
            s_control_architecture = DAB_CONTROL_ARCH_OUTPUT_CURRENT_PI;
            dab_schedule_mark_manual();
            dab_control_reset();
            dab_log("[DAB][ARCH] OUTPUT_CURRENT_PI selected; set iref/ikp/iki/islew before start\r\n");
        }
        else if ((0 == strcmp(argument, "energy")) &&
                 (DAB_STATE_READY == s_state) &&
                 !dab_pwm_hw_outputs_are_enabled())
        {
            s_algorithm_logging_enabled = true;
            s_control_architecture = DAB_CONTROL_ARCH_ENERGY_SUPERVISOR;
            dab_schedule_mark_manual();
            dab_control_reset();
            dab_log("[DAB][ARCH] CPU1_ENERGY_SUPERVISOR -> CPU0_OUTPUT_CURRENT_PI selected\r\n");
        }
        else if (((0 == strcmp(argument, "cp")) ||
                  (0 == strcmp(argument, "power"))) &&
                 (DAB_STATE_READY == s_state) &&
                 !dab_pwm_hw_outputs_are_enabled())
        {
            s_algorithm_logging_enabled = true;
            s_control_architecture = DAB_CONTROL_ARCH_CP_DAMPED_CASCADE;
            dab_schedule_mark_manual();
            s_schedule_autoload_enabled = true;
            dab_control_reset();
            dab_log("[DAB][ARCH] CP power-PI -> output-current-PI selected; lower-controller AUTO seed selection armed; configure high-level target and p ceiling before start\r\n");
        }
        else
        {
            dab_log("[DAB][CMD] use arch voltage|current|energy|cp|show in READY with PWM OFF\r\n");
        }
    }
    else if ((0 == strcmp(command, "vinset")) ||
             (0 == strcmp(command, "rload")) ||
             (0 == strcmp(command, "layer")))
    {
        float value = 0.0F;
        if ((DAB_STATE_READY != s_state) || s_control_enabled ||
            dab_pwm_hw_outputs_are_enabled())
        {
            dab_log("[DAB][BLOCK] condition metadata changes require READY with PWM/power OFF\r\n");
        }
        else if (!dab_parse_float(argument, &value))
        {
            dab_log("[DAB][CMD] condition metadata numeric value invalid\r\n");
        }
        else if (0 == strcmp(command, "vinset"))
        {
            if ((value < DAB_BENCH_VIN_SET_MIN_V) ||
                (value > DAB_BENCH_VIN_SET_MAX_V))
            {
                dab_log("[DAB][BLOCK] vinset must be 18..24V\r\n");
            }
            else
            {
                s_bench_vin_set_v = value;
                dab_schedule_mark_manual();
                dab_print_parameter_bundle();
            }
        }
        else if (0 == strcmp(command, "rload"))
        {
            if ((value < DAB_LOAD_NOMINAL_MIN_OHM) ||
                (value > DAB_LOAD_NOMINAL_MAX_OHM))
            {
                dab_log("[DAB][BLOCK] rload must be 1..100 ohm\r\n");
            }
            else
            {
                s_load_nominal_ohm = value;
                dab_schedule_mark_manual();
                dab_print_parameter_bundle();
            }
        }
        else if ((value < DAB_POWER_LAYER_MIN_W) ||
                 (value > DAB_POWER_LAYER_MAX_W))
        {
            dab_log("[DAB][BLOCK] layer must be 20..360W\r\n");
        }
        else
        {
            s_requested_power_layer_w = value;
            dab_schedule_mark_manual();
            dab_print_parameter_bundle();
        }
    }
    else if ((0 == strcmp(command, "vref")) ||
             (0 == strcmp(command, "v")) ||
             (0 == strcmp(command, "kp")) ||
             (0 == strcmp(command, "ki")) ||
             (0 == strcmp(command, "kd")))
    {
        float value = 0.0F;
        if (!dab_parse_float(argument, &value))
        {
            dab_log("[DAB][CMD] numeric value invalid\r\n");
            return;
        }
        if ((0 == strcmp(command, "vref")) || (0 == strcmp(command, "v")))
        {
            if ((value < DAB_VREF_MIN_V) || (value > DAB_VREF_MAX_V))
            {
                dab_log("[DAB][BLOCK] vref must be 36..48V\r\n");
            }
            else
            {
                s_vref_v = value;
                dab_schedule_mark_manual();
                s_pi_integral_counts = 0.0F;
            }
        }
        else if (0 == strcmp(command, "kp"))
        {
            if ((value < DAB_KP_MIN_NS_PER_V) || (value > DAB_KP_MAX_NS_PER_V))
            {
                dab_log("[DAB][BLOCK] kp must be 0..40 ns/V\r\n");
            }
            else
            {
                s_kp_ns_per_v = value;
                dab_schedule_mark_manual();
            }
        }
        else if (0 == strcmp(command, "ki"))
        {
            if ((value < DAB_KI_MIN_US_PER_VS) ||
                (value > DAB_KI_MAX_US_PER_VS))
            {
                dab_log("[DAB][BLOCK] ki must be 0..10 us/(V*s)\r\n");
            }
            else
            {
                s_ki_us_per_vs = value;
                dab_schedule_mark_manual();
            }
        }
        else
        {
            if ((value < DAB_KD_MIN_NS_S_PER_V) ||
                (value > DAB_KD_MAX_NS_S_PER_V))
            {
                dab_log("[DAB][BLOCK] kd must be 0..0.1 ns*s/V\r\n");
            }
            else
            {
                s_kd_ns_s_per_v = value;
                s_derivative_initialized = false;
                dab_schedule_mark_manual();
            }
        }
        dab_log("[DAB][SET] Vref=%.1fV limit=%.0fW Kp=%.1f Ki=%.3f Kd=%.4f\r\n",
                (double) s_vref_v, (double) s_power_limit_w,
                (double) s_kp_ns_per_v, (double) s_ki_us_per_vs,
                (double) s_kd_ns_s_per_v);
    }
    else
    {
        dab_log("[DAB][CMD] unknown; send help\r\n");
    }
}

static void dab_process_uart(void)
{
    static char command[UART_COMMAND_LENGTH];
    static uint32_t length;

    while (s_rx_tail != s_rx_head)
    {
        char character = (char) s_rx_ring[s_rx_tail];
        s_rx_tail = (uint8_t) ((s_rx_tail + 1U) & (UART_RX_RING_LENGTH - 1U));

        if (s_test_mode)
        {
            if (!dab_ra8p1_scope_test_process_char((uint8_t) character))
            {
                s_test_mode = false;
                dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY :
                                                       DAB_STATE_FAULT);
                dab_log("[APP] measurement shell resumed; send help\r\n");
            }
            continue;
        }

        if (('\r' == character) || ('\n' == character))
        {
            if (length > 0U)
            {
                command[length] = '\0';
                for (uint32_t i = 0U; i < length; i++)
                {
                    if ((command[i] >= 'A') && (command[i] <= 'Z'))
                    {
                        command[i] = (char) (command[i] - 'A' + 'a');
                    }
                }
                dab_command_execute(command);
                length = 0U;
            }
        }
        else if ((character == '\b') || (character == 0x7F))
        {
            if (length > 0U)
            {
                length--;
            }
        }
        else if ((character >= ' ') && (length < (UART_COMMAND_LENGTH - 1U)))
        {
            command[length++] = character;
        }
    }
}

void User_Init_Task_entry(void * pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    dab_board_initialize_safe();
    dab_energy_initialize(&s_energy_shadow);
    dab_vin_adaptation_initialize(&s_vin_adaptation);
    dab_autotune_initialize(&s_autotune);
    dab_modswitch_initialize(&s_modswitch);
    dab_mppt_phase_initialize(&s_mppt_phase_search);
    dab_cp_damping_initialize(&s_cp_damping);
    dab_dual_core_cpu0_start();
    if (FSP_SUCCESS != R_SCI_B_UART_Open(&g_uart9_ctrl, &g_uart9_cfg))
    {
        while (true)
        {
            dab_board_user_led_set(true);
            vTaskDelay(pdMS_TO_TICKS(100U));
            dab_board_user_led_set(false);
            vTaskDelay(pdMS_TO_TICKS(100U));
        }
    }

    dab_log("\r\n[DAB][BOOT] %s; DEBUG/J-Link UART9 P208/P209 115200-8N1\r\n",
            DAB_FW_VERSION);
#if DAB_ALLOW_POWER_TRANSFER
    dab_log("[DAB][SAFE] DEBUG-UART control switch defaults OFF; 300kHz actual=%luHz deadtime=%luns; outputs/path OFF\r\n",
#else
    dab_log("[DAB][SAFE] start compile-locked; 300kHz actual=%luHz deadtime=%luns; outputs/path OFF\r\n",
#endif
            (unsigned long) DAB_PWM_ACTUAL_HZ,
            (unsigned long) DAB_DEADTIME_NS);
    dab_log("[DAB][MODEL] default SPS+voltage PI preserved; CPU0 current/advanced control and CPU1 MPPT/charger supervisor are UART opt-in\r\n");
    dab_log("[DAB][TUNE] p=output safety ceiling; cap=independent closed-loop 0..480ns; ph=RUN-only manual override\r\n");
    dab_log("[DAB][DATA] VinSet=%.1fV Vref=%.1fV Rload=%.1fohm player=%.0fW arch=%s table=%s entries=%lu\r\n",
            (double) s_bench_vin_set_v,
            (double) s_vref_v,
            (double) s_load_nominal_ohm,
            (double) s_requested_power_layer_w,
            dab_control_architecture_name(s_control_architecture),
            DAB_TUNED_PARAMETER_TABLE_VERSION,
            (unsigned long) DAB_TUNED_PARAMETER_TABLE_ENTRY_COUNT);

#if DAB_SOFTWARE_AUTO_TRIP_ENABLE
    dab_log("[DAB][PROTECT] default=%s MCU_AUTO=ON; RAW/OC/OK, ADC stale, 55V, 26A/18A and temperature shutdown active\r\n",
            dab_guard_profile_name(s_guard_profile));
    dab_log("[DAB][PROTECT] BENCH removes soft power/current retreat, cold clamp and no-rise from test path; configured cap=%luns; POEG GPT clamp remains FSP-disabled\r\n",
            (unsigned long) (s_closed_loop_phase_cap_counts *
                             DAB_PWM_COUNT_NS));
#else
    dab_log("[DAB][PROTECT] MCU_AUTO=OFF MANUAL_STOP; RAW/OC/OK, ADC/current/voltage/temperature are telemetry only; operator off owns stop\r\n");
    dab_log("[DAB][PROTECT] configured cap=%luns; POEG GPT clamp remains FSP-disabled; external board/supply/load protection unchanged\r\n",
            (unsigned long) (s_closed_loop_phase_cap_counts *
                             DAB_PWM_COUNT_NS));
#endif

    if (!dab_adc_and_pwm_initialize())
    {
        s_fault_mask = DAB_FAULT_INIT | DAB_FAULT_ADC;
        dab_force_all_safe();
        dab_enter_state(DAB_STATE_FAULT);
    }
    else
    {
        dab_enter_state(DAB_STATE_READY);
        dab_log("[DAB][READY] ADC0+ADC1 DMA=%lu frame/s; send test or mode save; in test use 0/x to stop PWM\r\n",
                (unsigned long) DAB_ADC_FRAME_RATE_HZ);
    }

    fsp_err_t const tmp_err = tmp1075_open(&s_tmp1075);
    s_tmp1075_ok = (FSP_SUCCESS == tmp_err);
    dab_log("[DAB][INIT] TMP1075=%s err=%ld\r\n",
            s_tmp1075_ok ? "PASS" : "NOT-FOUND", (long) tmp_err);
    bool const can_ok = dab_can_transport_initialize();
    dab_can_transport_status_t can_status = {0};
    dab_can_transport_status_get(&can_status);
    dab_log("[DAB][INIT] CANFD0=%s err=%ld classic=500k TX=P704 RX=P705; failure is nonfatal\r\n",
            can_ok ? "PASS" : "NOT-OPEN",
            (long) can_status.open_error);
    dab_print_pins();
    dab_print_drivers();
    dab_log("[CMD] on/off | p/cap/ph | arch voltage/current/energy/cp | energy/mppt/bat/ai | vinadapt/autotune/modswitch/cpdamp | show/save/help all\r\n");

    dab_remote_snapshot_publish_now();
    if (dab_ethernet_start())
    {
        dab_log("[ETH] non-blocking init queued; static %s/24 TCP=%u; ICMP echo enabled\r\n",
                DAB_ETHERNET_IP_ADDRESS,
                DAB_ETHERNET_TCP_PORT);
    }
    else
    {
        dab_log("[ETH][WARN] task create failed; DAB UART/control continues unchanged\r\n");
    }

    TickType_t last_telemetry = xTaskGetTickCount();
    TickType_t last_remote_snapshot = last_telemetry;
    TickType_t last_ethernet_diag = last_telemetry;
    bool heartbeat = false;

    while (true)
    {
        dab_uart_service();
        dab_process_uart();
        dab_remote_command_service();
        dab_can_command_service();
        dab_can_transport_service();

        dab_adc_dma_service();

        dab_adc_frame_t frame;
        bool running = !s_test_mode &&
                       ((DAB_STATE_SOFTSTART == s_state) ||
                        (DAB_STATE_RUN == s_state));
        uint32_t const minimum_frame_delta = running ? DAB_CONTROL_FRAME_DIVIDER : 1UL;
        if (dab_adc_dma_latest(&frame) &&
            (!s_measurements_ready ||
             ((uint32_t) (frame.sequence - s_measurements.sequence) >=
              minimum_frame_delta)))
        {
            if (dab_measurements_update(&frame, &s_measurements))
            {
                s_measurements_ready = true;
                s_last_adc_tick = xTaskGetTickCount();
                if (s_measurements.valid)
                {
                    s_last_valid_adc_tick = s_last_adc_tick;
                }
            }
            else if (s_measurements_ready &&
                     ((xTaskGetTickCount() - s_last_adc_tick) >
                      pdMS_TO_TICKS(DAB_ADC_STALE_TIMEOUT_MS)))
            {
                /* One ADC ADDR_ERR frame is discarded.  Only a genuinely
                 * stale stream invalidates measurements; the running safety
                 * guard below then removes PWM and the power path. */
                s_measurements.valid = false;
            }
        }

        if (!s_test_mode)
        {
            dab_state_service();
        }

        if (dab_charger_completion_service())
        {
            running = false;
        }

        /*
         * This guard is independent of a new ADC frame.  If the ADC trigger or
         * DMA ever stops, the normal frame-synchronous controller cannot run
         * and therefore cannot detect its own stale input.
         */
        if (DAB_SOFTWARE_AUTO_TRIP_ENABLE && running &&
            (((xTaskGetTickCount() - s_last_adc_tick) >
              pdMS_TO_TICKS(DAB_ADC_STALE_TIMEOUT_MS)) ||
             !dab_required_digital_faults_clear() ||
             !dab_pwm_hw_poeg_ok()))
        {
            uint32_t immediate_fault = DAB_FAULT_NONE;
            if ((xTaskGetTickCount() - s_last_adc_tick) >
                pdMS_TO_TICKS(DAB_ADC_STALE_TIMEOUT_MS))
            {
                immediate_fault |= DAB_FAULT_ADC_STALE;
            }
            if (!dab_required_digital_faults_clear())
            {
                dab_board_inputs_t inputs;
                dab_board_inputs_get(&inputs);
                if (!inputs.raw_fault_n)
                {
                    immediate_fault |= DAB_FAULT_RAW_N;
                }
#if DAB_GPIO_OC_TRIP_ENABLE
                if (!inputs.ipri_oc_n)
                {
                    immediate_fault |= DAB_FAULT_IPRI_OC_N;
                }
                if (!inputs.isec_oc_n)
                {
                    immediate_fault |= DAB_FAULT_ISEC_OC_N;
                }
#endif
            }
            if (!dab_pwm_hw_poeg_ok())
            {
                immediate_fault |= DAB_FAULT_POEG;
            }
            dab_fault_latch(immediate_fault);
            running = false;
        }

        dab_charger_runtime_schedule_service();
        dab_mppt_phase_search_service();

        if (((DAB_STATE_SOFTSTART == s_state) || (DAB_STATE_RUN == s_state)) &&
            s_measurements_ready &&
            ((uint32_t) (s_measurements.sequence - s_last_control_sequence) >=
             DAB_CONTROL_FRAME_DIVIDER))
        {
            s_last_control_sequence = s_measurements.sequence;
            dab_control_update();
        }

        TickType_t const now = xTaskGetTickCount();
        if ((now - last_ethernet_diag) >= pdMS_TO_TICKS(2000U))
        {
            dab_ethernet_diag_t eth_diag;
            last_ethernet_diag = now;
            dab_ethernet_diag_get(&eth_diag);
            dab_log("[ETHDIAG] task=%u reset_n=%u ipinit=%u/%u ok=%u "
                    "tcp=%08lX rmac=%08lX phy=%08lX phy_if=%lu link=%lu net=%u evt=%lu\r\n",
                    eth_diag.task_started ? 1U : 0U,
                    eth_diag.reset_released ? 1U : 0U,
                    eth_diag.ip_init_called ? 1U : 0U,
                    eth_diag.ip_init_returned ? 1U : 0U,
                    eth_diag.ip_init_ok ? 1U : 0U,
                    (unsigned long) eth_diag.tcp_open,
                    (unsigned long) eth_diag.rmac_open,
                    (unsigned long) eth_diag.phy_open,
                    (unsigned long) eth_diag.phy_interface_status,
                    (unsigned long) eth_diag.link_establish_status,
                    eth_diag.network_up ? 1U : 0U,
                    (unsigned long) eth_diag.network_event_count);
        }
        if ((now - last_remote_snapshot) >= pdMS_TO_TICKS(50U))
        {
            last_remote_snapshot = now;
            dab_dual_core_cpu0_measurements_publish(
                s_measurements.vin_v, s_measurements.iin_a,
                s_measurements.vout_v, s_measurements.iout_a,
                s_measurements.temp_pri_c, s_measurements.temp_sec_c,
                s_measurements.valid);
            dab_remote_snapshot_publish_now();
        }
        uint32_t const telemetry_period_ms = s_vofa_stream ?
            DAB_VOFA_PERIOD_MS :
            (s_stream ? (running ? DAB_TELEMETRY_PERIOD_MS :
                                   DAB_IDLE_STREAM_PERIOD_MS) :
                        DAB_OBSERVE_PERIOD_MS);
        if (s_force_telemetry ||
            ((running || s_stream || s_vofa_stream) &&
             ((now - last_telemetry) >= pdMS_TO_TICKS(telemetry_period_ms))))
        {
            last_telemetry = now;
            if (s_force_telemetry)
            {
                s_force_telemetry = false;
                dab_print_status();
            }
            else if (s_vofa_stream)
            {
                dab_print_vofa();
            }
            else if (running && s_stream)
            {
                dab_print_status();
            }
            else if (running)
            {
                dab_print_observe();
            }
            else
            {
                dab_measurements_print(&s_measurements);
            }
            heartbeat = !heartbeat;
            dab_board_user_led_set(heartbeat);
            dab_board_status_led_set((DAB_STATE_RUN == s_state) ||
                                     (DAB_STATE_SOFTSTART == s_state));
        }

        /*
         * During power transfer, poll the 300 ksample/s DMA ring so one control
         * update occurs every third coherent frame.  Outside RUN, yield because
         * millisecond state timing is sufficient and outputs are physically off.
         */
        if ((DAB_STATE_SOFTSTART != s_state) && (DAB_STATE_RUN != s_state))
        {
            vTaskDelay(pdMS_TO_TICKS(1U));
        }
    }
}

#endif /* !RA8P1_PERIPHERAL_TEST_ENABLE */
