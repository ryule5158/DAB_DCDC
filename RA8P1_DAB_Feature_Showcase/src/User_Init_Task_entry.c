#include "ra8p1_peripheral_test_config.h"

#if !RA8P1_PERIPHERAL_TEST_ENABLE

/*
 * Production DAB application for the final 2026-07-22 hardware.
 *
 * The power application is deliberately separate from
 * ra8p1_dab_peripheral_test.c.  The tested PWM/ADC commissioning image can be
 * restored by changing one switch in ra8p1_peripheral_test_config.h.
 *
 * Safety policy:
 * - no automatic power transfer after reset;
 * - RUN_REQ and every path control start LOW;
 * - power transfer requires compile permission, an explicit USB-CDC power command, and control on;
 * - phase starts at zero and rises slowly;
 * - RAW_FAULT_N also reaches POEG0 in hardware;
 * - any ADC, overcurrent, overvoltage, stale-data, or POEG fault drops
 *   RUN_REQ first, removes PWM pin muxing, and opens every power path.
 */

#include "User_Init_Task.h"
#include "dab_accel.h"
#include "dab_adc_dma.h"
#include "dab_ai_supervisor.h"
#include "dab_board.h"
#include "dab_power_dsp.h"
#include "dab_production_config.h"
#include "dab_measurements.h"
#include "dab_pwm_hw.h"
#include "dab_production.h"
#include "dab_ra8p1_scope_test.h"
#include "dab_usb_console.h"
#include "drv_tcan3413.h"
#include "drv_tmp1075.h"
#include "drv_ra8p1_crc.h"
#include "drv_ra8p1_ipc.h"
#include "drv_ra8p1_sdram.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UART_TX_QUEUE_DEPTH       (16U)
#define UART_TX_MESSAGE_LENGTH    (512U)
#define UART_RX_RING_LENGTH       (128U)
#define UART_COMMAND_LENGTH       (96U)
#define DAB_USB_BOOT_RESET_DELAY_MS (5000U)

typedef enum e_dab_state
{
    DAB_STATE_BOOT = 0,
    DAB_STATE_READY,
    DAB_STATE_RESET_LOW,
    DAB_STATE_LATCH_RELEASE,
    DAB_STATE_PRECHARGE,
    DAB_STATE_MAIN_SETTLE,
    DAB_STATE_OUTPUT_SETTLE,
    DAB_STATE_SOFTSTART,
    DAB_STATE_RUN,
    DAB_STATE_PAUSED,
    DAB_STATE_TEST_MODE,
    DAB_STATE_FAULT
} dab_state_t;

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
static uint32_t           s_fault_mask;
static uint32_t           s_last_control_sequence;
static dab_measurements_t s_measurements DAB_DTCM_ZERO;
static bool               s_measurements_ready;
static bool               s_control_enabled;
static bool               s_power_command_received;
static TickType_t         s_power_command_tick;
static bool               s_zero_calibrated;
static TickType_t         s_zero_tick;
static bool               s_unlock_200w;
static bool               s_force_telemetry;
static bool               s_stream;
static bool               s_test_mode;
static bool               s_usb_boot_pending;
static TickType_t         s_usb_boot_reset_tick;
static bool               s_tmp1075_ok;
static tmp1075_info_t      s_tmp1075;
static bool               s_fan_on;
static float              s_vref_v       = DAB_VREF_DEFAULT_V;
static float              s_power_limit_w = DAB_POWER_DEFAULT_W;
static float              s_kp_ns_per_v  = DAB_KP_DEFAULT_NS_PER_V;
static float              s_ki_us_per_vs = DAB_KI_DEFAULT_US_PER_VS;
static dab_production_controller_t s_controller DAB_DTCM_ZERO;
static uint32_t           s_pwm_update_miss_count;
static bool               s_dsp_self_test_ok;

static void dab_log(const char * fmt, ...);
static void dab_uart_service(void);
static void dab_process_uart(void);
static void dab_print_status(void);
static void dab_print_ai_status(void);
static void dab_print_pins(void);
static void dab_print_drivers(void);
static void dab_print_features(void);
static void dab_force_all_safe(void);
static void dab_fault_latch(uint32_t fault_mask);
static bool dab_adc_and_pwm_initialize(void);
static bool dab_current_zero_calibrate(void);
static void dab_state_service(void);
static void dab_control_update(void);
static uint32_t dab_detect_run_faults(void);
static void dab_control_reset(void);
static void dab_disarm(void);
static dab_production_request_t dab_request_get(void);
static void dab_safety_snapshot_get(dab_production_safety_snapshot_t * p_snapshot);
#if DAB_ALLOW_POWER_TRANSFER
static bool dab_start_preconditions_ok(void);
#endif
int _write(int fd, char * p_buffer, int size);

static float dab_minf(float a, float b)
{
    return (a < b) ? a : b;
}

static dab_production_request_t dab_request_get(void)
{
    dab_production_request_t request =
    {
        .power_limit_w =
            dab_ai_supervisor_power_cap(s_power_limit_w, &s_measurements),
        .vref_v = s_vref_v,
        .kp_ns_per_v = s_kp_ns_per_v,
        .ki_us_per_vs = s_ki_us_per_vs,
        .high_power_unlocked = s_unlock_200w,
    };
    return request;
}

static void dab_safety_snapshot_get(dab_production_safety_snapshot_t * p_snapshot)
{
    if (NULL == p_snapshot)
    {
        return;
    }
    dab_board_inputs_get(&p_snapshot->inputs);
    p_snapshot->measurements = s_measurements;
    p_snapshot->poeg_ok = dab_pwm_hw_poeg_ok();
    p_snapshot->adc_fresh = s_measurements_ready &&
        ((xTaskGetTickCount() - s_last_adc_tick) <=
         pdMS_TO_TICKS(DAB_ADC_STALE_TIMEOUT_MS));
}

static void dab_disarm(void)
{
    s_power_command_received = false;
    s_unlock_200w = false;
    s_power_limit_w = DAB_POWER_DEFAULT_W;
}

static const char * dab_state_name(dab_state_t state)
{
    static const char * const names[] =
    {
        "BOOT", "READY", "RESET", "RELEASE", "PRECHARGE", "MAIN",
        "OUTPUT", "SOFTSTART", "RUN", "PAUSED", "TEST", "FAULT"
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
    dab_usb_console_service();

    uint8_t received;
    while (dab_usb_console_read_byte(&received))
    {
        uint8_t const next = (uint8_t) ((s_rx_head + 1U) &
                                        (UART_RX_RING_LENGTH - 1U));
        if (next != s_rx_tail)
        {
            s_rx_ring[s_rx_head] = received;
            s_rx_head = next;
        }
    }

    if (s_uart_tx_head == s_uart_tx_tail)
    {
        return;
    }

    char * p_message = s_uart_tx_queue[s_uart_tx_tail];
    size_t const length = strlen(p_message);
    if (length == dab_usb_console_write(p_message, length))
    {
        s_uart_tx_tail = (uint8_t) ((s_uart_tx_tail + 1U) &
                                    (UART_TX_QUEUE_DEPTH - 1U));
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
    dab_production_controller_reset(&s_controller);
    s_pwm_update_miss_count = 0U;
    (void) dab_pwm_hw_phase_set(0U);
}

static DAB_ITCM_CODE void dab_force_all_safe(void)
{
    /* The first write disables the gate-driver chain. */
    dab_board_run_request_set(false);
    dab_pwm_hw_outputs_disable();
    dab_board_force_path_safe();
    dab_board_fan_set(false);
    s_fan_on = false;
    s_control_enabled = false;
    dab_control_reset();
}

static DAB_ITCM_CODE void dab_fault_latch(uint32_t fault_mask)
{
    if (0U == fault_mask)
    {
        return;
    }
    s_fault_mask |= fault_mask;
    s_zero_calibrated = false;
    dab_ai_supervisor_enable(false);
    dab_disarm();
    dab_force_all_safe();
    dab_enter_state(DAB_STATE_FAULT);
    (void) drv_ra8p1_ipc_fault_report(s_fault_mask);
    dab_log("[DAB][TRIP] primary=%s mask=0x%08lX; power OFF and command disarmed; use status, fix cause, then clear\r\n",
            dab_production_fault_name(s_fault_mask),
            (unsigned long) s_fault_mask);
    s_force_telemetry = true;
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
        s_zero_calibrated = false;
        return false;
    }
    float const divisor = (float) samples;
    sum.iin_v  /= divisor;
    sum.iout_v /= divisor;
    sum.ipri_v /= divisor;
    sum.isec_v /= divisor;
    if (!dab_measurements_zero_set(&sum))
    {
        s_zero_calibrated = false;
        dab_log("[DAB][ZERO] FAIL IIN=%.3fV IOUT=%.3fV IPRI=%.3fV ISEC=%.3fV\r\n",
                (double) sum.iin_v, (double) sum.iout_v,
                (double) sum.ipri_v, (double) sum.isec_v);
        return false;
    }

    dab_measurements_initialize();
    s_zero_calibrated = true;
    s_zero_tick = xTaskGetTickCount();
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

    if (!dab_current_zero_calibrate())
    {
        return false;
    }

    /*
     * The boot calibration establishes usable engineering values for idle
     * telemetry, but it is not operator authorisation to energise the DAB.
     * A deliberate "zero" command with the power path known OFF is required
     * before every first start and after every fault.
     */
    s_zero_calibrated = false;
    dab_log("[DAB][ZERO] boot baseline only; verify all current is zero, then send zero before control on\r\n");

    dab_adc_frame_t frame;
    if (!dab_adc_dma_latest(&frame) ||
        !dab_measurements_update(&frame, &s_measurements))
    {
        return false;
    }
    s_measurements_ready       = true;
    s_last_adc_tick            = xTaskGetTickCount();
    s_last_control_sequence    = frame.sequence;
    return true;
}

static DAB_ITCM_CODE uint32_t dab_detect_run_faults(void)
{
    dab_production_safety_snapshot_t snapshot;
    dab_safety_snapshot_get(&snapshot);
    dab_production_request_t const request = dab_request_get();
    return dab_production_faults_evaluate(&snapshot, &request, true);
}

static DAB_ITCM_CODE void dab_control_update(void)
{
    uint32_t const faults = dab_detect_run_faults();
    if (DAB_FAULT_NONE != faults)
    {
        dab_fault_latch(faults);
        return;
    }

    dab_production_request_t const request = dab_request_get();
    uint32_t const phase_limit =
        dab_production_phase_limit(&request, &s_measurements);
    dab_production_control_result_t result = {0};
    dab_production_control_step(&s_controller, &request, &s_measurements,
                                phase_limit, &result);

    if (result.phase_counts != dab_pwm_hw_phase_get())
    {
        if (!dab_pwm_hw_phase_set(result.phase_counts))
        {
            s_pwm_update_miss_count++;
            if (s_pwm_update_miss_count > DAB_PWM_UPDATE_MISS_LIMIT)
            {
                dab_fault_latch(DAB_FAULT_PWM_UPDATE);
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
    if ((DAB_STATE_SOFTSTART == s_state) &&
        (dab_state_elapsed_ms() > DAB_SOFTSTART_NO_RISE_TIMEOUT_MS) &&
        (s_measurements.vout_v < DAB_COLD_VOUT_1_V))
    {
        dab_fault_latch(DAB_FAULT_NO_RISE);
    }
}

#if DAB_ALLOW_POWER_TRANSFER
static bool dab_start_preconditions_ok(void)
{
    if (!s_zero_calibrated ||
        ((xTaskGetTickCount() - s_zero_tick) >
         pdMS_TO_TICKS(DAB_CURRENT_ZERO_VALID_MS)))
    {
        dab_log("[DAB][BLOCK] current zero is missing/stale; power OFF, then send zero\r\n");
        return false;
    }

    dab_production_safety_snapshot_t snapshot;
    dab_safety_snapshot_get(&snapshot);
    dab_start_block_t block = DAB_START_OK;
    if (!dab_production_start_ready(&snapshot, &block))
    {
        dab_log("[DAB][BLOCK] start check=%s Vin=%.2fV Iin=%.2fA Iout=%.2fA Ipri=%.2fA Isec=%.2fA Tpri=%.1fC Tsec=%.1fC\r\n",
                dab_production_start_block_name(block),
                (double) snapshot.measurements.vin_v,
                (double) snapshot.measurements.iin_a,
                (double) snapshot.measurements.iout_a,
                (double) snapshot.measurements.ipri_a,
                (double) snapshot.measurements.isec_a,
                (double) snapshot.measurements.temp_pri_c,
                (double) snapshot.measurements.temp_sec_c);
        return false;
    }
    return true;
}
#endif

static void dab_state_service(void)
{
    bool const control_path =
        (DAB_STATE_RESET_LOW == s_state) ||
        (DAB_STATE_LATCH_RELEASE == s_state) ||
        (DAB_STATE_PRECHARGE == s_state) ||
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
        (DAB_STATE_PRECHARGE == s_state) ||
        (DAB_STATE_MAIN_SETTLE == s_state) ||
        (DAB_STATE_OUTPUT_SETTLE == s_state) ||
        (DAB_STATE_PAUSED == s_state);
    if (energized_path)
    {
        dab_production_safety_snapshot_t snapshot;
        dab_safety_snapshot_get(&snapshot);
        dab_production_request_t const request = dab_request_get();
        uint32_t const faults =
            dab_production_faults_evaluate(&snapshot, &request, false);
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
                if (!dab_board_fast_faults_clear() || !dab_pwm_hw_poeg_clear())
                {
                    dab_fault_latch(DAB_FAULT_RAW_N | DAB_FAULT_POEG);
                }
                else
                {
                    dab_board_input_precharge_set(true);
                    dab_enter_state(DAB_STATE_PRECHARGE);
                }
            }
            break;

        case DAB_STATE_PRECHARGE:
            if (dab_state_elapsed_ms() >= DAB_INPUT_PRECHARGE_MS)
            {
                dab_board_input_main_set(true);
                dab_enter_state(DAB_STATE_MAIN_SETTLE);
            }
            break;

        case DAB_STATE_MAIN_SETTLE:
            if (dab_state_elapsed_ms() >= DAB_MAIN_SETTLE_MS)
            {
                dab_board_input_precharge_set(false);
                dab_board_output_protection_set(true);
                dab_enter_state(DAB_STATE_OUTPUT_SETTLE);
            }
            break;

        case DAB_STATE_OUTPUT_SETTLE:
            if (dab_state_elapsed_ms() >= DAB_OUTPUT_SETTLE_MS)
            {
                if (!dab_board_fast_faults_clear() ||
                    !dab_pwm_hw_poeg_ok() ||
                    !dab_pwm_hw_outputs_enable())
                {
                    dab_fault_latch(DAB_FAULT_RAW_N | DAB_FAULT_POEG);
                }
                else
                {
                    dab_control_reset();
                    dab_board_run_request_set(true);
                    dab_enter_state(DAB_STATE_SOFTSTART);
                }
            }
            break;

        default:
            break;
    }
}

static void dab_print_pins(void)
{
    dab_log("[DAB][PINS] USB CDC VBUS=P407 DP=P814/USB_P DM=P815/USB_N; UART9 P208/P209 inactive\r\n");
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
    dab_log("[DRV] LAN8720A MDIO library compiled; ESWM/RMII1 transport remains power-test disabled\r\n");
    dab_log("[ACCEL] Helium-F32=%s ITCM=hot ADC/PWM/protection DTCM=CPU state DMA=non-cache SRAM\r\n",
            dab_measurements_helium_enabled() ? "ON" : "OFF");
    dab_log("[ACCEL] Ethos-U55=%s async advisory only; AI default=OFF and cannot raise the operator power limit\r\n",
            dab_ai_supervisor_enabled() ? "READY+ON" : "OFF/see ai status");
}

static void dab_print_features(void)
{
    ra8p1_sdram_status_t sdram;
    ra8p1_ipc_status_t ipc;
    ra8p1_crc_status_t crc;
    drv_ra8p1_sdram_status_get(&sdram);
    drv_ra8p1_ipc_status_get(&ipc);
    drv_ra8p1_crc_status_get(&crc);
    dab_log("[RA8P1][DSP] Cortex-M85 Helium-F32=%s reusable affine/window driver selftest=%s\r\n",
            dab_power_dsp_helium_enabled() ? "ON" : "SCALAR",
            s_dsp_self_test_ok ? "PASS" : "FAIL");
    dab_log("[RA8P1][SDRAM] state=%s base=0x%08lX capacity=%luB allocated=%luB last-test=%lu/%lu; explicit OFF-state test only\r\n",
            drv_ra8p1_sdram_state_name(sdram.state),
            (unsigned long) sdram.base,
            (unsigned long) sdram.capacity_bytes,
            (unsigned long) sdram.allocated_bytes,
            (unsigned long) sdram.last_test_words,
            (unsigned long) sdram.last_test_failures);
    dab_log("[RA8P1][IPC] core%u channel=%u enabled=%u peer=%u stale=%u tx=%lu/full=%lu rx=%lu last=%u:%u/0x%05lX\r\n",
            ipc.local_core,
            ipc.channel,
            ipc.enabled ? 1U : 0U,
            ipc.peer_seen ? 1U : 0U,
            ipc.peer_stale ? 1U : 0U,
            (unsigned long) ipc.tx_count,
            (unsigned long) ipc.tx_full_count,
            (unsigned long) ipc.rx_count,
            (unsigned int) ipc.last_rx.type,
            ipc.last_rx_sequence,
            (unsigned long) ipc.last_rx.payload);
    dab_log("[RA8P1][CRC] hardware CRC-32 ready=%u selftest=%s calculations=%lu last=0x%08lX\r\n",
            crc.initialized ? 1U : 0U,
            crc.self_test_passed ? "PASS" : "FAIL",
            (unsigned long) crc.calculation_count,
            (unsigned long) crc.last_result);
}

static void dab_print_ai_status(void)
{
    dab_ai_status_t ai;
    dab_ai_supervisor_status_get(&ai);
    float const cap_w =
        dab_ai_supervisor_power_cap(s_power_limit_w, &s_measurements);
    dab_log("[DAB][AI] NPU=%u enable=%u busy=%u model=%u cap=%.0f/%.0fW infer=%lu err=%lu last=%u | MPPT=%u/%lumV/%.2f PV=%u/%lu/%.2f TH=%u/%lu/%.2f\r\n",
            ai.npu_ready ? 1U : 0U,
            ai.enabled ? 1U : 0U,
            ai.busy ? 1U : 0U,
            (unsigned int) ai.active_model,
            (double) cap_w,
            (double) s_power_limit_w,
            (unsigned long) ai.inference_count,
            (unsigned long) ai.error_count,
            (unsigned int) ai.last_status,
            ai.mppt_valid ? 1U : 0U,
            (unsigned long) ai.mppt_vmpp_mv,
            (double) ai.mppt_confidence,
            ai.pv_fault_valid ? 1U : 0U,
            (unsigned long) ai.pv_fault_class,
            (double) ai.pv_fault_confidence,
            ai.thermal_valid ? 1U : 0U,
            (unsigned long) ai.thermal_class,
            (double) ai.thermal_confidence);
}

static void dab_print_status(void)
{
    dab_board_inputs_t inputs;
    dab_board_inputs_get(&inputs);
    TickType_t const now = xTaskGetTickCount();
    uint32_t const zero_age_ms = s_zero_calibrated ?
        (uint32_t) ((now - s_zero_tick) * portTICK_PERIOD_MS) : UINT32_MAX;
    uint32_t const command_age_ms = s_power_command_received ?
        (uint32_t) ((now - s_power_command_tick) * portTICK_PERIOD_MS) : UINT32_MAX;
    dab_log("[DAB] %s CTRL=%s Vin=%.2fV Vout=%.2fV Iin=%.2fA Iout=%.2fA Ipri=%.2fA Isec=%.2fA Pin=%.1fW Pout=%.1fW ph=%lu/%luns Plim=%.0fW fault=%08lX/%s\r\n",
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
            (unsigned long) s_fault_mask,
            dab_production_fault_name(s_fault_mask));
    dab_log("[DAB][IO] RAW=%u IPRI_OC=%u ISEC_OC=%u PRI_OK=%u SEC_OK=%u AMB_OT_N=%u PWM=%u RUNstate=%s Vref=%.1f Kp=%.1fns/V Ki=%.2fus/Vs valid=%u Tpri=%.1fC Tsec=%.1fC Pcmd=%u unlock200=%u bootusb=%u seq=%lu\r\n",
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
            s_measurements.valid ? 1U : 0U,
            (double) s_measurements.temp_pri_c,
            (double) s_measurements.temp_sec_c,
            s_power_command_received ? 1U : 0U,
            s_unlock_200w ? 1U : 0U,
            s_usb_boot_pending ? 1U : 0U,
            (unsigned long) s_measurements.sequence);
    dab_log("[DAB][GUARD] zero=%u age=%lums arm=%u age=%lums Ilim=%u Plim=%u\r\n",
            s_zero_calibrated ? 1U : 0U,
            (unsigned long) zero_age_ms,
            s_power_command_received ? 1U : 0U,
            (unsigned long) command_age_ms,
            s_controller.current_limited ? 1U : 0U,
            s_controller.power_limited ? 1U : 0U);
    dab_print_ai_status();
}

static bool dab_parse_float(char const * p_text, float * p_value)
{
    if ((NULL == p_text) || (NULL == p_value))
    {
        return false;
    }
    char * p_end = NULL;
    float const value = strtof(p_text, &p_end);
    if ((p_end == p_text) || ('\0' != *p_end) || !isfinite(value))
    {
        return false;
    }
    *p_value = value;
    return true;
}

static bool dab_parse_mppt_scan(char const * p_text,
                                float power_scan_w[9])
{
    if ((NULL == p_text) || (NULL == power_scan_w))
    {
        return false;
    }

    char const * p = p_text;
    for (uint32_t i = 0U; i < 9U; ++i)
    {
        while ((' ' == *p) || (',' == *p))
        {
            ++p;
        }
        if ('\0' == *p)
        {
            return false;
        }
        char * p_end = NULL;
        float const value = strtof(p, &p_end);
        if ((p_end == p) || !isfinite(value))
        {
            return false;
        }
        power_scan_w[i] = value;
        p = p_end;
    }
    while ((' ' == *p) || (',' == *p))
    {
        ++p;
    }
    return '\0' == *p;
}

static bool dab_power_limit_set(float requested)
{
    float const maximum = s_unlock_200w ? DAB_POWER_ABSOLUTE_MAX_W :
                                           DAB_POWER_NORMAL_MAX_W;
    if ((requested < DAB_POWER_DEFAULT_W) || (requested > maximum))
    {
        dab_log("[DAB][BLOCK] power must be 20..%.0fW; use unlock 200 after lower-power validation\r\n",
                (double) maximum);
        return false;
    }

    bool const active = (DAB_STATE_SOFTSTART == s_state) ||
                        (DAB_STATE_RUN == s_state);
    bool const allowed_state = (DAB_STATE_READY == s_state) ||
                               (DAB_STATE_PAUSED == s_state) || active;
    if (!allowed_state)
    {
        dab_log("[DAB][BLOCK] power may change only in READY, PAUSED, SOFTSTART, or RUN\r\n");
        return false;
    }
    if (active && (requested > s_power_limit_w))
    {
        dab_log("[DAB][BLOCK] pause before increasing power above %.0fW\r\n",
                (double) s_power_limit_w);
        return false;
    }

    s_power_limit_w = requested;
    s_power_command_received = true;
    s_power_command_tick = xTaskGetTickCount();
    dab_log("[DAB][POWER] limit=%.0fW (safety ceiling; actual power follows Vref and load)\r\n",
            (double) s_power_limit_w);
    return true;
}

#if DAB_ALLOW_POWER_TRANSFER
static void dab_control_start_request(void)
{
    if (s_usb_boot_pending)
    {
        dab_log("[DAB][BLOCK] USB Boot reset is armed; use boot cancel first\r\n");
    }
    else if (s_control_enabled)
    {
        dab_log("[DAB][CONTROL] already ON; use power N, pause, or control off\r\n");
    }
    else if (!s_power_command_received)
    {
        dab_log("[DAB][BLOCK] send power 20..200 before control on\r\n");
    }
    else if ((xTaskGetTickCount() - s_power_command_tick) >
             pdMS_TO_TICKS(DAB_POWER_COMMAND_VALID_MS))
    {
        dab_disarm();
        dab_log("[DAB][BLOCK] power command expired; send power N again\r\n");
    }
    else if ((DAB_STATE_READY != s_state) || (0U != s_fault_mask))
    {
        dab_log("[DAB][BLOCK] control on requires READY and fault=0\r\n");
    }
    else if (dab_start_preconditions_ok())
    {
        dab_force_all_safe();
        s_control_enabled = true;
        s_stream = true;
        dab_board_fan_set(true);
        s_fan_on = true;
        dab_enter_state(DAB_STATE_RESET_LOW);
        dab_log("[DAB][CONTROL] ON limit=%.0fW Vref=%.1fV; soft-start from phase=0; control off always wins\r\n",
                (double) s_power_limit_w, (double) s_vref_v);
    }
}
#endif

static void dab_command_execute(char * p_line)
{
    char * command = p_line;
    while (' ' == *command)
    {
        ++command;
    }
    if ('\0' == *command)
    {
        return;
    }
    char * argument = strchr(command, ' ');
    if (NULL != argument)
    {
        *argument++ = '\0';
        while (' ' == *argument)
        {
            ++argument;
        }
        if ('\0' == *argument)
        {
            argument = NULL;
        }
    }

    if (s_usb_boot_pending &&
        (0 != strcmp(command, "status")) &&
        (0 != strcmp(command, "s")) &&
        (0 != strcmp(command, "help")) &&
        (0 != strcmp(command, "h")) &&
        (0 != strcmp(command, "?")) &&
        (0 != strcmp(command, "boot")))
    {
        dab_log("[BOOT][BLOCK] reset pending; use boot cancel or wait for reset\r\n");
        return;
    }

    if ((0 == strcmp(command, "help")) || (0 == strcmp(command, "h")) ||
        (0 == strcmp(command, "?")))
    {
        dab_log("[CMD] status | measure | stream on/off | test | zero | temp | pins | drivers\r\n");
        dab_log("[DAB][CMD] zero | power 20..100 | unlock 200 then power 101..200 | lock\r\n");
        dab_log("[DAB][CMD] control on/off | start [W] | pause/resume | stop | clear\r\n");
        dab_log("[AI][CMD] ai on/off/status | ai mppt P19,P20,...,P31 (9 power samples in W)\r\n");
        dab_log("[RA8P1][CMD] features | features dsp-test | features crc-test | features sdram-test | features ipc-ping\r\n");
        dab_log("[BOOT][CMD] boot usb (5s reset; hold SW202/BOOT) | boot cancel\r\n");
        dab_log("[TUNE] vref 36..48 | kp 0..40 | ki 0..10 | help\r\n");
#if DAB_ALLOW_POWER_TRANSFER
        dab_log("[SAFE] Power-transfer build permission=ON; runtime faults remain mandatory\r\n");
#else
        dab_log("[SAFE] Power-transfer build permission=LOCKED; start cannot energize hardware\r\n");
#endif
    }
    else if ((0 == strcmp(command, "status")) || (0 == strcmp(command, "s")))
    {
        s_force_telemetry = true;
    }
    else if ((0 == strcmp(command, "measure")) || (0 == strcmp(command, "m")))
    {
        dab_measurements_print(&s_measurements);
    }
    else if ((0 == strcmp(command, "stream")) && (NULL != argument) &&
             (0 == strcmp(argument, "on")))
    {
        s_stream = true;
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
    else if (0 == strcmp(command, "pins"))
    {
        dab_print_pins();
    }
    else if (0 == strcmp(command, "drivers"))
    {
        dab_print_drivers();
    }
    else if ((0 == strcmp(command, "features")) ||
             (0 == strcmp(command, "feature")))
    {
        if ((NULL == argument) || (0 == strcmp(argument, "status")))
        {
            dab_print_features();
        }
        else if (0 == strcmp(argument, "dsp-test"))
        {
            s_dsp_self_test_ok = dab_power_dsp_self_test();
            dab_log("[RA8P1][DSP] %s path selftest=%s\r\n",
                    dab_power_dsp_helium_enabled() ? "Helium-F32" : "scalar",
                    s_dsp_self_test_ok ? "PASS" : "FAIL");
        }
        else if (0 == strcmp(argument, "crc-test"))
        {
            bool const passed = drv_ra8p1_crc_self_test();
            dab_log("[RA8P1][CRC] CRC-32(ABCDEFGHIJKLMNOP)=%s expected=0xE0E8FF4D\r\n",
                    passed ? "PASS" : "FAIL");
        }
        else if (0 == strcmp(argument, "sdram-test"))
        {
            if ((DAB_STATE_READY != s_state) || (0U != s_fault_mask) ||
                s_control_enabled)
            {
                dab_log("[RA8P1][SDRAM][BLOCK] requires READY, fault=0, control OFF and all power paths OFF\r\n");
            }
            else
            {
                dab_disarm();
                dab_force_all_safe();
                bool const passed = drv_ra8p1_sdram_quick_test();
                dab_log("[RA8P1][SDRAM] bounded save/write/read/restore test=%s\r\n",
                        passed ? "PASS" : "BLOCKED/FAIL");
                dab_print_features();
            }
        }
        else if (0 == strcmp(argument, "ipc-ping"))
        {
            bool const sent = drv_ra8p1_ipc_try_send(RA8P1_IPC_MESSAGE_STATUS,
                                                     s_fault_mask);
            dab_log("[RA8P1][IPC] status message=%s; companion M33 image is required\r\n",
                    sent ? "QUEUED" : "DISABLED/FIFO-FULL");
        }
        else
        {
            dab_log("[RA8P1][CMD] use features/status/dsp-test/crc-test/sdram-test/ipc-ping\r\n");
        }
    }
    else if (0 == strcmp(command, "ai"))
    {
        if ((NULL == argument) || (0 == strcmp(argument, "status")))
        {
            dab_print_ai_status();
        }
        else if (0 == strcmp(argument, "on"))
        {
            dab_ai_supervisor_enable(true);
            dab_log("[DAB][AI] %s; advisory derating only, deterministic control and hardware trips remain authoritative\r\n",
                    dab_ai_supervisor_enabled() ? "ON" : "BLOCKED: NPU not ready");
            dab_print_ai_status();
        }
        else if (0 == strcmp(argument, "off"))
        {
            dab_ai_supervisor_enable(false);
            dab_log("[DAB][AI] OFF; any accepted NPU job will be drained, no new inference starts\r\n");
        }
        else if ((0 == strncmp(argument, "mppt ", 5U)) ||
                 (0 == strncmp(argument, "mppt,", 5U)))
        {
            float scan[9];
            if (!dab_ai_supervisor_enabled())
            {
                dab_log("[DAB][AI] send ai on before submitting an MPPT scan\r\n");
            }
            else if (!dab_parse_mppt_scan(argument + 5U, scan) ||
                     !dab_ai_supervisor_mppt_submit(scan))
            {
                dab_log("[DAB][AI] invalid scan; provide 9 values, each 0..555.6W\r\n");
            }
            else
            {
                dab_log("[DAB][AI] MPPT scan queued for 19..31V classes\r\n");
            }
        }
        else
        {
            dab_log("[DAB][AI] use ai on/off/status or ai mppt with 9 power samples\r\n");
        }
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
        if ((DAB_STATE_READY != s_state) || (0U != s_fault_mask))
        {
            dab_log("[DAB][BLOCK] test requires READY, fault=0, and power paths OFF\r\n");
        }
        else
        {
            dab_disarm();
            dab_force_all_safe();
            dab_ai_supervisor_enable(false);
            s_test_mode = true;
            dab_enter_state(DAB_STATE_TEST_MODE);
            dab_ra8p1_scope_test_enter();
        }
    }
    else if ((0 == strcmp(command, "power")) ||
             (0 == strcmp(command, "limit")))
    {
        float requested = 0.0F;
        if (!dab_parse_float(argument, &requested))
        {
            dab_log("[DAB][CMD] use power 20..200\r\n");
        }
        else
        {
            (void) dab_power_limit_set(requested);
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
            dab_disarm();
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
        else if (!dab_board_fast_faults_clear() || !dab_pwm_hw_poeg_clear() ||
                 !dab_pwm_hw_outputs_enable())
        {
            dab_fault_latch(DAB_FAULT_RAW_N | DAB_FAULT_POEG);
        }
        else
        {
            dab_control_reset();
            dab_board_run_request_set(true);
            dab_enter_state(DAB_STATE_SOFTSTART);
        }
    }
    else if (0 == strcmp(command, "stop"))
    {
        dab_disarm();
        dab_force_all_safe();
        dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY : DAB_STATE_FAULT);
        dab_log("[DAB][STOP] control OFF; power paths and all PWM outputs are OFF\r\n");
    }
    else if (0 == strcmp(command, "boot"))
    {
        if ((NULL != argument) && (0 == strcmp(argument, "usb")))
        {
            dab_disarm();
            dab_force_all_safe();
            s_stream = false;
            s_usb_boot_pending = true;
            s_usb_boot_reset_tick =
                xTaskGetTickCount() + pdMS_TO_TICKS(DAB_USB_BOOT_RESET_DELAY_MS);
            dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY : DAB_STATE_FAULT);
            dab_log("[BOOT][USB] SAFE: control/PWM/power paths are OFF\r\n");
            dab_log("[BOOT][USB] HOLD SW202/BOOT now; automatic reset in %lu ms\r\n",
                    (unsigned long) DAB_USB_BOOT_RESET_DELAY_MS);
            dab_log("[BOOT][USB] COM will disappear; release SW202 after reset, then use RFP USB Direct\r\n");
        }
        else if ((NULL != argument) && (0 == strcmp(argument, "cancel")))
        {
            s_usb_boot_pending = false;
            dab_log("[BOOT][USB] reset cancelled; outputs remain OFF\r\n");
        }
        else
        {
            dab_log("[BOOT][CMD] use boot usb or boot cancel\r\n");
        }
    }
    else if (0 == strcmp(command, "clear"))
    {
        dab_board_inputs_t inputs;
        dab_board_inputs_get(&inputs);
        if (!inputs.raw_fault_n || !inputs.ipri_oc_n || !inputs.isec_oc_n ||
            !inputs.pri_hw_ok || !inputs.sec_hw_ok || !inputs.temp_amb_ot_n)
        {
            dab_log("[DAB][BLOCK] clear requires every hardware fault/OK input healthy\r\n");
        }
        else
        {
            dab_force_all_safe();
            R_BSP_SoftwareDelay(DAB_LATCH_RESET_LOW_MS,
                                BSP_DELAY_UNITS_MILLISECONDS);
            if (!dab_pwm_hw_poeg_clear())
            {
                dab_log("[DAB][BLOCK] POEG still latched\r\n");
            }
            else
            {
                s_fault_mask = 0U;
                s_zero_calibrated = false;
                dab_enter_state(DAB_STATE_READY);
                dab_log("[DAB][CLEAR] reset complete; outputs OFF; run zero before the next start\r\n");
            }
        }
    }
    else if (0 == strcmp(command, "zero"))
    {
        if (DAB_STATE_READY != s_state)
        {
            dab_log("[DAB][BLOCK] zero only in READY with all power OFF\r\n");
        }
        else
        {
            dab_disarm();
            dab_force_all_safe();
            if (!dab_current_zero_calibrate())
            {
                dab_fault_latch(DAB_FAULT_ADC);
            }
        }
    }
    else if (0 == strcmp(command, "unlock"))
    {
        if ((DAB_STATE_READY == s_state) && (NULL != argument) &&
            (0 == strcmp(argument, "200")))
        {
            s_unlock_200w = true;
            dab_log("[DAB][WARN] 200W ceiling unlocked; validate 20W and 100W first\r\n");
        }
        else
        {
            dab_log("[DAB][BLOCK] use unlock 200 only in READY\r\n");
        }
    }
    else if (0 == strcmp(command, "lock"))
    {
        if ((DAB_STATE_READY == s_state) || (DAB_STATE_PAUSED == s_state))
        {
            s_unlock_200w = false;
            s_power_limit_w = dab_minf(s_power_limit_w, DAB_POWER_NORMAL_MAX_W);
            dab_log("[DAB][CMD] normal 100W ceiling restored\r\n");
        }
    }
    else if ((0 == strcmp(command, "vref")) ||
             (0 == strcmp(command, "kp")) ||
             (0 == strcmp(command, "ki")))
    {
        if ((DAB_STATE_READY != s_state) && (DAB_STATE_PAUSED != s_state))
        {
            dab_log("[DAB][BLOCK] tune only in READY or PAUSED\r\n");
            return;
        }
        float value = 0.0F;
        if (!dab_parse_float(argument, &value))
        {
            dab_log("[DAB][CMD] numeric value invalid\r\n");
            return;
        }
        if (0 == strcmp(command, "vref"))
        {
            if ((value < DAB_VREF_MIN_V) || (value > DAB_VREF_MAX_V))
            {
                dab_log("[DAB][BLOCK] vref must be 36..48V\r\n");
            }
            else
            {
                s_vref_v = value;
                dab_control_reset();
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
            }
        }
        else
        {
            if ((value < DAB_KI_MIN_US_PER_VS) ||
                (value > DAB_KI_MAX_US_PER_VS))
            {
                dab_log("[DAB][BLOCK] ki must be 0..10 us/(V*s)\r\n");
            }
            else
            {
                s_ki_us_per_vs = value;
            }
        }
        dab_log("[DAB][SET] Vref=%.1fV limit=%.0fW Kp=%.1fns/V Ki=%.2fus/(V*s)\r\n",
                (double) s_vref_v, (double) s_power_limit_w,
                (double) s_kp_ns_per_v, (double) s_ki_us_per_vs);
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
    static bool overflow;

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
            if (overflow)
            {
                dab_log("[DAB][CMD] line too long; command rejected\r\n");
                overflow = false;
                length = 0U;
            }
            else if (length > 0U)
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
        else if (!overflow &&
                 ((character == '\b') || (character == 0x7F)))
        {
            if (length > 0U)
            {
                length--;
            }
        }
        else if ((character >= ' ') && !overflow)
        {
            if (length < (UART_COMMAND_LENGTH - 1U))
            {
                command[length++] = character;
            }
            else
            {
                overflow = true;
            }
        }
    }
}

void User_Init_Task_entry(void * pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    dab_board_initialize_safe();
    if (FSP_SUCCESS != dab_usb_console_init())
    {
        while (true)
        {
            dab_board_user_led_set(true);
            vTaskDelay(pdMS_TO_TICKS(100U));
            dab_board_user_led_set(false);
            vTaskDelay(pdMS_TO_TICKS(100U));
        }
    }

    dab_log("\r\n[DAB][BOOT] %s; USB CDC P814/USB_P P815/USB_N (virtual COM)\r\n",
            DAB_FW_VERSION);
#if DAB_ALLOW_POWER_TRANSFER
    dab_log("[DAB][SAFE] USB control switch defaults OFF; 300kHz actual=%luHz deadtime=%luns; outputs/path OFF\r\n",
#else
    dab_log("[DAB][SAFE] start compile-locked; 300kHz actual=%luHz deadtime=%luns; outputs/path OFF\r\n",
#endif
            (unsigned long) DAB_PWM_ACTUAL_HZ,
            (unsigned long) DAB_DEADTIME_NS);
    dab_log("[DAB][MODEL] production SPS voltage loop, PI=20ns/V+5us/(V*s), phase rise=8ns/ms\r\n");

    if (!dab_adc_and_pwm_initialize())
    {
        s_fault_mask = DAB_FAULT_INIT | DAB_FAULT_ADC;
        dab_force_all_safe();
        dab_enter_state(DAB_STATE_FAULT);
    }
    else
    {
        dab_enter_state(DAB_STATE_READY);
        dab_log("[DAB][READY] ADC0+ADC1 DMA=%lu frame/s; send test or stream on; in test use 0/x to stop PWM\r\n",
                (unsigned long) DAB_PWM_ACTUAL_HZ);
    }

    fsp_err_t const tmp_err = tmp1075_open(&s_tmp1075);
    s_tmp1075_ok = (FSP_SUCCESS == tmp_err);
    dab_log("[DAB][INIT] TMP1075=%s err=%ld\r\n",
            s_tmp1075_ok ? "PASS" : "NOT-FOUND", (long) tmp_err);
    fsp_err_t const ai_err = dab_ai_supervisor_initialize();
    dab_log("[DAB][INIT] Ethos-U55=%s err=%ld; AI remains OFF until 'ai on'\r\n",
            (FSP_SUCCESS == ai_err) ? "READY" : "NOT-READY",
            (long) ai_err);
    s_dsp_self_test_ok = dab_power_dsp_self_test();
    (void) drv_ra8p1_crc_initialize();
    drv_ra8p1_sdram_initialize();
    drv_ra8p1_ipc_initialize();
    dab_print_pins();
    dab_print_drivers();
    dab_print_features();
    dab_log("[CMD] power N | control on/off | status | measure | stream on/off | test | boot usb | help\r\n");

    TickType_t last_telemetry = xTaskGetTickCount();
    bool heartbeat = false;

    while (true)
    {
        dab_uart_service();
        dab_process_uart();

        if (dab_usb_console_take_disconnect())
        {
            if (s_test_mode)
            {
                dab_ra8p1_scope_test_exit();
                s_test_mode = false;
                dab_enter_state((0U == s_fault_mask) ? DAB_STATE_READY :
                                                       DAB_STATE_FAULT);
                dab_log("[DAB][USB] link lost in test; all PWM pins forced LOW\r\n");
            }
            else if ((DAB_STATE_RESET_LOW == s_state) ||
                     (DAB_STATE_LATCH_RELEASE == s_state) ||
                     (DAB_STATE_PRECHARGE == s_state) ||
                     (DAB_STATE_MAIN_SETTLE == s_state) ||
                     (DAB_STATE_OUTPUT_SETTLE == s_state) ||
                     (DAB_STATE_SOFTSTART == s_state) ||
                     (DAB_STATE_RUN == s_state) ||
                     (DAB_STATE_PAUSED == s_state))
            {
                dab_fault_latch(DAB_FAULT_USB_LINK);
            }
        }
        if (dab_usb_console_take_reconnect())
        {
            dab_log("[DAB][USB] PC connected; commands ready; send help\r\n");
        }

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
            }
            else
            {
                s_measurements.valid = false;
            }
        }

        if (!s_test_mode)
        {
            dab_state_service();
        }

        running = !s_test_mode &&
                  ((DAB_STATE_SOFTSTART == s_state) ||
                   (DAB_STATE_RUN == s_state));
        dab_ai_supervisor_service(
            &s_measurements, running, s_tmp1075_ok,
            s_tmp1075.temperature_c, s_fan_on,
            (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS));
        drv_ra8p1_ipc_service(
            (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS));

        /*
         * This guard is independent of a new ADC frame.  If the ADC trigger or
         * DMA ever stops, the normal frame-synchronous controller cannot run
         * and therefore cannot detect its own stale input.
         */
        if (running &&
            (((xTaskGetTickCount() - s_last_adc_tick) >
              pdMS_TO_TICKS(DAB_ADC_STALE_TIMEOUT_MS)) ||
             !dab_board_fast_faults_clear() ||
             !dab_pwm_hw_poeg_ok()))
        {
            uint32_t immediate_fault = DAB_FAULT_NONE;
            if ((xTaskGetTickCount() - s_last_adc_tick) >
                pdMS_TO_TICKS(DAB_ADC_STALE_TIMEOUT_MS))
            {
                immediate_fault |= DAB_FAULT_ADC_STALE;
            }
            if (!dab_board_fast_faults_clear())
            {
                dab_board_inputs_t inputs;
                dab_board_inputs_get(&inputs);
                if (!inputs.raw_fault_n)
                {
                    immediate_fault |= DAB_FAULT_RAW_N;
                }
                if (!inputs.ipri_oc_n)
                {
                    immediate_fault |= DAB_FAULT_IPRI_OC_N;
                }
                if (!inputs.isec_oc_n)
                {
                    immediate_fault |= DAB_FAULT_ISEC_OC_N;
                }
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
        if (s_usb_boot_pending &&
            ((int32_t) (now - s_usb_boot_reset_tick) >= 0))
        {
            s_usb_boot_pending = false;
            dab_force_all_safe();
            NVIC_SystemReset();
        }

        uint32_t const telemetry_period_ms = running ? DAB_TELEMETRY_PERIOD_MS :
                                                       DAB_IDLE_STREAM_PERIOD_MS;
        if (s_force_telemetry ||
            ((running || s_stream) &&
             ((now - last_telemetry) >= pdMS_TO_TICKS(telemetry_period_ms))))
        {
            s_force_telemetry = false;
            last_telemetry = now;
            if (running || !s_stream)
            {
                dab_print_status();
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
