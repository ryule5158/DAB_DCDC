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
#include "dab_accel.h"
#include "dab_adc_dma.h"
#include "dab_board.h"
#include "dab_dual_core_cpu0.h"
#include "dab_ethernet.h"
#include "dab_first_power_config.h"
#include "dab_measurements.h"
#include "dab_pwm_hw.h"
#include "dab_ra8p1_scope_test.h"
#include "dab_remote_control.h"
#include "dab_tuned_parameter_table.h"
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
    DAB_CONTROL_ARCH_ENERGY_SUPERVISOR
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
static uint32_t s_schedule_entry_id;
static bool  s_feedforward_enabled;
static float s_feedforward_effective_phase_ns;

static void dab_log(const char * fmt, ...);
static void dab_uart_service(void);
static void dab_process_uart(void);
static void dab_print_status(void);
static void dab_print_observe(void);
static void dab_print_vofa(void);
static void dab_print_pins(void);
static void dab_print_drivers(void);
static void dab_print_adc_diag(void);
static void dab_print_pwm_hw_diag(void);
static void dab_single_gate_diag(const char * argument);
static void dab_primary_bridge_diag(const char * argument,
                                    dab_primary_diag_target_t target);
static void dab_force_all_safe(void);
static void dab_fault_latch(uint32_t fault_mask);
static bool dab_adc_and_pwm_initialize(void);
static bool dab_measurements_refresh_from_adc(void);
static bool dab_current_zero_calibrate(void);
static void dab_state_service(void);
static void dab_control_update(void);
static void dab_remote_command_service(void);
static void dab_remote_snapshot_publish_now(void);
static uint32_t dab_detect_run_faults(void);
static void dab_control_reset(void);
static bool dab_schedule_apply_current(void);
static void dab_schedule_mark_manual(void);
static void dab_print_parameter_bundle(void);
static void dab_print_energy_status(void);
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
        "CPU1_ENERGY_SUPERVISOR_CURRENT_PI"
    };
    uint32_t const index = (uint32_t) architecture;
    return (index < (sizeof(names) / sizeof(names[0]))) ? names[index] : "?";
}

static const char * dab_guard_profile_name(dab_guard_profile_t profile)
{
    return (DAB_GUARD_PROFILE_BENCH == profile) ? "BENCH_ABSOLUTE_ONLY" :
                                                  "NORMAL_LAYERED";
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
static uint32_t dab_raw_phase_from_effective(uint32_t effective_counts,
                                             uint32_t primary_inner_counts,
                                             uint32_t secondary_inner_counts)
{
    int32_t const raw_x2 = ((int32_t) effective_counts * 2) +
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

    if ((DAB_STATE_RUN != s_state) || !s_measurements.valid ||
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
        *p_primary_counts = s_primary_inner_manual_target_counts;
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
    s_primary_inner_command_counts = 0.0F;
    s_secondary_inner_command_counts = 0.0F;
    s_modulation_effective = DAB_MODULATION_SPS;
    s_pwm_update_miss_count = 0U;
    (void) dab_pwm_hw_modulation_set(0U, 0U, 0U);
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
    uint32_t const unlocked_maximum = s_unlock_high_power ?
        DAB_PHASE_ABSOLUTE_MAX_COUNTS : DAB_PHASE_NORMAL_MAX_COUNTS;
    uint32_t configured = s_closed_loop_phase_cap_counts;
    if (configured > unlocked_maximum)
    {
        configured = unlocked_maximum;
    }

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
    float const phase_limit = s_manual_phase_override ?
                              (float) DAB_PHASE_ABSOLUTE_MAX_COUNTS :
                              (float) phase_limit_u32;

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
            requested_current = dab_clampf(requested_current, 0.0F,
                                           DAB_CURRENT_REFERENCE_MAX_A);
            float const current_slew_step =
                s_current_reference_slew_a_per_s * DAB_CONTROL_DT_S;
            s_current_reference_active_a = dab_slew(
                s_current_reference_active_a, requested_current,
                current_slew_step);
            s_current_error_a = s_current_reference_active_a -
                                s_measurements.iout_a;
            float const current_kp_counts_per_a =
                s_current_kp_ns_per_a / (float) DAB_PWM_COUNT_NS;
            float const current_ki_counts_per_as =
                s_current_ki_us_per_as * 250.0F;
            integral_candidate += s_current_error_a *
                current_ki_counts_per_as * DAB_CONTROL_DT_S;
            integral_candidate = dab_clampf(integral_candidate, 0.0F,
                                            phase_limit);
            target = (current_kp_counts_per_a * s_current_error_a) +
                     integral_candidate;
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
    float const inner_step = (DAB_INNER_SLEW_NS_PER_MS *
                              DAB_CONTROL_DT_S * 1000.0F) /
                             (float) DAB_PWM_COUNT_NS;
    s_primary_inner_command_counts = dab_slew(
        s_primary_inner_command_counts, primary_target, inner_step);
    s_secondary_inner_command_counts = dab_slew(
        s_secondary_inner_command_counts, secondary_target, inner_step);

    uint32_t const requested_effective =
        (uint32_t) (s_phase_command_counts + 0.5F);
    uint32_t const primary_inner =
        (uint32_t) (s_primary_inner_command_counts + 0.5F);
    uint32_t const secondary_inner =
        (uint32_t) (s_secondary_inner_command_counts + 0.5F);
    uint32_t const requested_raw =
        dab_raw_phase_from_effective(requested_effective,
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

    if ((DAB_STATE_SOFTSTART == s_state) &&
        (s_measurements.vout_v >= (0.90F * s_vref_v)))
    {
        dab_enter_state(DAB_STATE_RUN);
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
                    dab_control_reset();
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
    dab_log("[DRV] ADC_B+DMAC0: AMC3330 VIN/VOUT, ACS725 IIN/IOUT, TMCS1133 IPRI/ISEC, ISOTMP35 PRI/SEC, VAFE_HALF\r\n");
    dab_log("[DRV] IIC2 P514/P515: TMP1075@0x48 %s ID=0x%04X ambient=%.2fC\r\n",
            s_tmp1075_ok ? "PASS" : "NOT-FOUND",
            s_tmp1075.device_id,
            (double) s_tmp1075.temperature_c);
    dab_log("[DRV] TPS48111/TPS48110/UCC21520/ISO7741/protection latches: GPIO+fault driver\r\n");
    dab_log("[DRV] %s\r\n", tcan3413_board_note());
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
    dab_log("[DAB][MOD] requested=%s effective=%s M=%.3f outer=%luns "
            "pri_inner=%luns sec_inner=%luns phase_mode=%s set=%luns\r\n",
            dab_modulation_name(s_modulation_requested),
            dab_modulation_name(s_modulation_effective), (double) m,
            (unsigned long) (dab_pwm_hw_phase_get() * DAB_PWM_COUNT_NS),
            (unsigned long) (dab_pwm_hw_primary_inner_get() *
                             DAB_PWM_COUNT_NS),
            (unsigned long) (dab_pwm_hw_secondary_inner_get() *
                             DAB_PWM_COUNT_NS),
            s_manual_phase_override ? "MANUAL" :
                ((s_kd_ns_s_per_v > 0.0F) ? "PID" : "PI"),
            (unsigned long) (s_manual_phase_counts * DAB_PWM_COUNT_NS));
    dab_print_parameter_bundle();
    dab_log("[DAB][GUARD] profile=%s hard_absolute=RAW/OC/OK/ADC-stale/55V/26A-in/18A-out/temperature; bench disables cold/power/soft-current/no-rise interference\r\n",
            dab_guard_profile_name(s_guard_profile));
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
    dab_log("ALG:%lu,%u,%u,%.3f,%.3f,%.3f,%.3f,%.4f,%.3f,%u,%u,%lu,%.3f,%.1f,%.2f,%u,%u,%u,%.2f,%u,%.2f,%.3f,%u,%u,%.3f,%u,%u,%u,%lu,%lu\r\n",
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
            (unsigned long) g_dab_cpu0_dual_core_status.energy_config_dropped);
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
    dab_log("[ENERGY] mode=%s arch=%s Icmd=%.2fA Iactive=%.2fA Icpu1=%.2fA valid=%u age=%lums Kp=%.2fns/A Ki=%.3fus/(A*s) slew=%.2fA/s\r\n",
            dab_energy_mode_name(c->mode),
            dab_control_architecture_name(s_control_architecture),
            (double) s_current_reference_command_a,
            (double) s_current_reference_active_a,
            (double) g_dab_cpu0_dual_core_status.energy_current_reference_a,
            g_dab_cpu0_dual_core_status.energy_reference_valid ? 1U : 0U,
            (unsigned long) age_ms,
            (double) s_current_kp_ns_per_a,
            (double) s_current_ki_us_per_as,
            (double) s_current_reference_slew_a_per_s);
    dab_log("[MPPT] mode=%s step=%.2fV interval=%lums range=%.1f..%.1fV Pmax=%.0fW Ptarget=%.0fW Vtarget=%.1fV limited=%u\r\n",
            dab_mppt_mode_name(c->mppt_mode), (double) c->mppt_step_v,
            (unsigned long) c->mppt_interval_ms,
            (double) c->mppt_min_v, (double) c->mppt_max_v,
            (double) c->mppt_max_power_w,
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

static bool dab_power_limit_set(float requested)
{
    float const maximum = s_unlock_high_power ? DAB_POWER_ABSOLUTE_MAX_W :
                                           DAB_POWER_NORMAL_MAX_W;
    if ((requested < DAB_POWER_DEFAULT_W) || (requested > maximum))
    {
        dab_log("[DAB][BLOCK] power must be 20..%.0fW; use unlock 400 after lower-power validation\r\n",
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
            dab_log("[DAB][ON] direct path accepted; Pout ceiling=%.0fW configured cap=%luns Vref=%.1fV SPS; automatic trips active\r\n",
                    (double) s_power_limit_w,
                    (unsigned long) (s_closed_loop_phase_cap_counts *
                                     DAB_PWM_COUNT_NS),
                    (double) s_vref_v);
            dab_print_pwm_hw_diag();
        }
        else
        {
            dab_force_all_safe();
            s_control_enabled = true;
            dab_board_fan_set(true);
            dab_enter_state(DAB_STATE_RESET_LOW);
            dab_log("[DAB][ON] protected path sequence; Pout ceiling=%.0fW configured cap=%luns Vref=%.1fV; cold gates and automatic trips active\r\n",
                    (double) s_power_limit_w,
                    (unsigned long) (s_closed_loop_phase_cap_counts *
                                     DAB_PWM_COUNT_NS),
                    (double) s_vref_v);
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
    s_feedforward_enabled = false;
    s_feedforward_effective_phase_ns = 0.0F;
}

static void dab_print_parameter_bundle(void)
{
    dab_log("[DAB][BUNDLE] arch=%s mod=%s schedule=%u entry=%lu table=%s\r\n",
            dab_control_architecture_name(s_control_architecture),
            dab_modulation_name(s_modulation_requested),
            s_schedule_enabled ? 1U : 0U,
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
}

static bool dab_schedule_apply_current(void)
{
    if ((DAB_STATE_READY != s_state) || s_control_enabled ||
        dab_pwm_hw_outputs_are_enabled())
    {
        dab_log("[DAB][SCHED][BLOCK] apply only in READY with PWM/power OFF\r\n");
        return false;
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
    dab_control_reset();
    dab_log("[DAB][SCHED] applied validated entry=%lu table=%s\r\n",
            (unsigned long) s_schedule_entry_id,
            DAB_TUNED_PARAMETER_TABLE_VERSION);
    dab_print_parameter_bundle();
    return true;
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
    if ((vin_abs > DAB_GATE_DIAG_MAX_VIN_V) ||
        (vout_abs > DAB_GATE_DIAG_MAX_VOUT_V))
    {
        dab_log("[DAB][BLOCK] gate diagnosis buses not discharged VIN=%.2fV VOUT=%.2fV limits=%.1f/%.1fV\r\n",
                (double) vin_abs, (double) vout_abs,
                (double) DAB_GATE_DIAG_MAX_VIN_V,
                (double) DAB_GATE_DIAG_MAX_VOUT_V);
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
            dab_log("[ADV] measure adcdiag zero mos on/off gate q1..q8 pri/pria/prib on/off/[10..500] mode save/observe test pause/resume\r\n");
            dab_log("[ADV] mod sps/eps/tps/auto | inner PRI_NS SEC_NS/auto/show | kp 0..40 | ki 0..10 | kd 0..0.1 | unlock 400 | cap 0..480\r\n");
            dab_log("[ADV] ph 0..480=manual effective outer phase in RUN for SPS/EPS/TPS | ph auto=return to PI\r\n");
            dab_log("[ADV] arch voltage/current/energy/show | iref/ikp/iki/islew VALUE | alglog on/off/show | protect bench/normal/show\r\n");
            dab_log("[ENERGY] energy off/manual/mppt/charge/solar/show | mppt po/inc/off/fixed/step/interval/vmin/vmax/pmax/show\r\n");
            dab_log("[CHARGE] bat profile liion10/lfp12/lead36/custom | capacity/imax/ipre/iterm/vpre/vcv/vfloat/absorb/timeout/temp/kp/ki | start/stop/show\r\n");
            dab_log("[AIHOOK] ai off/assist/show | ai hint CURRENT_A CONFIDENCE [TTL_MS] | ai mod sps|eps|tps CONFIDENCE [TTL_MS]; modulation hint is record-only\r\n");
            dab_log("[DATA] vinset 18..24 | rload 1..100 | layer 20..400 | sched on/off/show | bundle show\r\n");
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
        dab_control_start_request();
#else
        dab_log("[DAB][BLOCK] power transfer is compile-locked\r\n");
#endif
    }
    else if (0 == strcmp(command, "off"))
    {
        dab_force_all_safe();
        dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY : DAB_STATE_FAULT);
        dab_log("[DAB][OFF] STOP PWM=0 RUN=0 four path MOS=OFF\r\n");
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
        dab_log("[VOFA] schema=v39 DAB fields unchanged: seq,VIN,VOUT,IIN,IOUT,IPRI,ISEC,TPRI,TSEC,Pin,Pout,VAFE,raw_phase_ns,Plimit,Vref,state,control,pwm,fault,valid,mod_req,mod_eff,effective_phase_ns,command_phase_ns,pri_inner_ns,sec_inner_ns,inner_manual\r\n");
        dab_log("[VOFA] fields2=kp_ns_per_v,ki_us_per_vs,kd_ns_s_per_v,d_filter_alpha,phase_cap_ns,bench_vin_set_v,load_nominal_ohm,requested_power_layer_w,control_arch,schedule_enabled,table_entry_id,outer_saturated,outer_saturation_updates,feedforward_enabled,feedforward_effective_phase_ns\r\n");
        if (s_algorithm_logging_enabled)
        {
            dab_log("[VOFA] ALG join_key=seq fields=seq,arch,guard,Iref_cmd,Iref_active,Ierr,Ikp,Iki,Islew,energy_mode,energy_valid,energy_age_ms,energy_Iref,mppt_P,mppt_V,mppt_limited,bat_profile,charge_stage,charge_V,charge_limited,ai_hint,ai_conf,ai_applied,ai_mod_hint,ai_mod_conf,ai_mod_fresh,ipc_peer,ipc_stale,config_queued,config_dropped\r\n");
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
            dab_log("[DAB][UNLOCK] 400W output ceiling and cap up to 480ns enabled; use p and cap independently\r\n");
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
                s_algorithm_logging_enabled = true;
                s_current_reference_command_a = value;
                (void) dab_energy_parameter_set(DAB_ENERGY_CONFIG_MANUAL_CURRENT_A,
                                                value);
                dab_log("[CURRENT][SET] manual Iref=%.3fA\r\n", (double) value);
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
            if (dab_energy_parameter_set(DAB_ENERGY_CONFIG_BATTERY_START, start))
            {
                dab_log("[BAT][SET] charger=%s; firmware never bypasses pack BMS limits\r\n",
                        (start > 0.5F) ? "START" : "STOP");
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
    else if (0 == strcmp(command, "bundle"))
    {
        dab_print_parameter_bundle();
    }
    else if ((0 == strcmp(command, "sched")) ||
             (0 == strcmp(command, "schedule")))
    {
        if ((NULL == argument) || (0 == strcmp(argument, "show")))
        {
            dab_log("[DAB][SCHED] enabled=%u entry=%lu promoted_entries=%lu table=%s\r\n",
                    s_schedule_enabled ? 1U : 0U,
                    (unsigned long) s_schedule_entry_id,
                    (unsigned long) DAB_TUNED_PARAMETER_TABLE_ENTRY_COUNT,
                    DAB_TUNED_PARAMETER_TABLE_VERSION);
        }
        else if (0 == strcmp(argument, "off"))
        {
            dab_schedule_mark_manual();
            dab_log("[DAB][SCHED] OFF; current values retained as manual bundle\r\n");
        }
        else if (0 == strcmp(argument, "on"))
        {
            (void) dab_schedule_apply_current();
        }
        else
        {
            dab_log("[DAB][CMD] use sched on|off|show\r\n");
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
        else
        {
            dab_log("[DAB][CMD] use arch voltage|current|energy|show in READY with PWM OFF\r\n");
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
            dab_log("[DAB][BLOCK] layer must be 20..400W\r\n");
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
    dab_log("[DAB][MODEL] default SPS+voltage PI preserved; CPU0 output-current PI and CPU1 MPPT/charger supervisor available by UART\r\n");
    dab_log("[DAB][TUNE] p=output safety ceiling; cap=independent closed-loop 0..480ns; ph=RUN-only manual override\r\n");
    dab_log("[DAB][DATA] VinSet=%.1fV Vref=%.1fV Rload=%.1fohm player=%.0fW arch=%s table=%s entries=%lu\r\n",
            (double) s_bench_vin_set_v,
            (double) s_vref_v,
            (double) s_load_nominal_ohm,
            (double) s_requested_power_layer_w,
            dab_control_architecture_name(s_control_architecture),
            DAB_TUNED_PARAMETER_TABLE_VERSION,
            (unsigned long) DAB_TUNED_PARAMETER_TABLE_ENTRY_COUNT);

    dab_log("[DAB][PROTECT] default=%s; hardware/absolute RAW/OC/OK, ADC stale, 55V, 26A/18A and temperature shutdown remain active\r\n",
            dab_guard_profile_name(s_guard_profile));
    dab_log("[DAB][PROTECT] BENCH removes soft power/current retreat, cold clamp and no-rise from test path; configured cap=%luns; POEG GPT clamp remains FSP-disabled\r\n",
            (unsigned long) (s_closed_loop_phase_cap_counts *
                             DAB_PWM_COUNT_NS));

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
    dab_print_pins();
    dab_print_drivers();
    dab_log("[CMD] on/off | p/cap/ph | arch voltage/current/energy | energy/mppt/bat/ai | protect | show/save/help all\r\n");

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
    bool heartbeat = false;

    while (true)
    {
        dab_uart_service();
        dab_process_uart();
        dab_remote_command_service();

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

        if (((DAB_STATE_SOFTSTART == s_state) || (DAB_STATE_RUN == s_state)) &&
            s_measurements_ready &&
            ((uint32_t) (s_measurements.sequence - s_last_control_sequence) >=
             DAB_CONTROL_FRAME_DIVIDER))
        {
            s_last_control_sequence = s_measurements.sequence;
            dab_control_update();
        }

        TickType_t const now = xTaskGetTickCount();
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
