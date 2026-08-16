/*
 * Isolated RA8P1/DAB peripheral commissioning firmware.
 *
 * Production control is compiled out on the temporary test branch. This file
 * owns USBFS CDC, ADC_B, IIC2/TMP1075, GPIO, POEG0 and GPT0/1/5/8 only while
 * RA8P1_PERIPHERAL_TEST_ENABLE is set. RUN_REQ and all power-path enables stay
 * LOW during PWM observation, so the eight MCU waveforms can be checked before
 * the gate drivers or the DAB power stage are allowed to operate.
 */

#include "ra8p1_peripheral_test.h"

#if RA8P1_PERIPHERAL_TEST_ENABLE

#include "User_Init_Task.h"
#include "dab_adc_dma.h"
#include "dab_usb_console.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------- Board/CN2 pin contract --------------------- */
#define PIN_IN_PRECHARGE_EN    BSP_IO_PORT_11_PIN_00 /* PB00 -> CN2.97 */
#define PIN_IN_MAIN_EN         BSP_IO_PORT_11_PIN_02 /* PB02 -> CN2.99 */
#define PIN_OUT_PROT_EN        BSP_IO_PORT_07_PIN_06 /* P706 -> CN2.45 */
#define PIN_PROT_RESET_N       BSP_IO_PORT_11_PIN_03 /* PB03 -> CN2.47 */
#define PIN_RUN_REQ            BSP_IO_PORT_07_PIN_07 /* P707 -> CN2.57 */
#define PIN_DAB_STATUS_LED     BSP_IO_PORT_04_PIN_02 /* P402 -> CN2.75 */
#define PIN_RAW_FAULT_N        BSP_IO_PORT_04_PIN_01 /* P401 -> CN2.59/.61/GTETRGA */
#define PIN_IPRI_OC_N          BSP_IO_PORT_04_PIN_00 /* P400 -> CN2.79 */
#define PIN_ISEC_OC_N          BSP_IO_PORT_04_PIN_03 /* P403 -> CN2.55 */
#define PIN_FAN_EN             BSP_IO_PORT_11_PIN_04 /* PB04 -> CN2.113 */
#define PIN_USER_LED           BSP_IO_PORT_01_PIN_10

#define DAB_GPT_CHANNEL_MASK   ((1UL << 0) | (1UL << 1) | (1UL << 5) | (1UL << 8))
#define DAB_GPT_ADC_TRIGGER_CHANNEL_MASK (1UL << 0)

typedef struct st_pwm_pin_mux
{
    bsp_io_port_pin_t pin;
    const char      * name;
} pwm_pin_mux_t;

static const pwm_pin_mux_t s_pwm_pins[] =
{
    {BSP_IO_PORT_04_PIN_15, "PWM_PRI_A_H/P415/CN2.83"},
    {BSP_IO_PORT_04_PIN_14, "PWM_PRI_A_L/P414/CN2.81"},
    {BSP_IO_PORT_01_PIN_05, "PWM_PRI_B_H/P105/CN2.87"},
    {BSP_IO_PORT_01_PIN_04, "PWM_PRI_B_L/P104/CN2.85"},
    {BSP_IO_PORT_09_PIN_15, "PWM_SEC_A_H/P915/CN2.35"},
    {BSP_IO_PORT_09_PIN_14, "PWM_SEC_A_L/P914/CN2.37"},
    {BSP_IO_PORT_06_PIN_05, "PWM_SEC_B_H/P605/CN2.39"},
    {BSP_IO_PORT_06_PIN_04, "PWM_SEC_B_L/P604/CN2.41"},
};

#define UART_TX_QUEUE_DEPTH     (8U)
#define UART_TX_MESSAGE_LENGTH  (640U)

static volatile bool               s_uart_tx_busy;
static volatile uint8_t            s_uart_tx_head;
static volatile uint8_t            s_uart_tx_tail;
static char                        s_uart_tx_queue[UART_TX_QUEUE_DEPTH][UART_TX_MESSAGE_LENGTH];
static volatile uint8_t            s_rx_head;
static volatile uint8_t            s_rx_tail;
static uint8_t                     s_rx_ring[128];
static volatile i2c_master_event_t s_i2c_event;
static volatile bool               s_i2c_done;

static dab_adc_frame_t s_adc;
static bool            s_adc_open;
static bool            s_i2c_open;
static bool            s_pwm_open;
static bool            s_pwm_running;
static bool            s_pwm_ready;
static bool            s_adc_trigger_enabled;
static bool            s_adc_stream_enabled = true;
static bool            s_status_led_on;
static bool            s_fan_on;
static uint32_t        s_phase_counts = RA8P1_TEST_PHASE_COUNTS_DEFAULT;

static bool              s_gpio_pulse_active;
static bsp_io_port_pin_t s_gpio_pulse_pin;
static const char      * s_gpio_pulse_name;
static TickType_t        s_gpio_pulse_deadline;

static void test_log(const char * fmt, ...);
static void test_uart_service(void);
static void test_force_safe_outputs(void);
static bool test_pwm_configure_phase(uint32_t phase_counts);
static void test_pwm_stop(void);
static bool test_pwm_start(void);
static bool test_adc_initialize(void);
static void test_process_uart(void);
static void test_print_pin_map(void);
int _write(int fd, char * p_buffer, int size);

static void test_pin_write(bsp_io_port_pin_t pin, bool high)
{
    (void) R_IOPORT_PinWrite(&g_ioport_ctrl, pin, high ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
}

static bool test_pin_read(bsp_io_port_pin_t pin)
{
    bsp_io_level_t level = BSP_IO_LEVEL_LOW;
    (void) R_IOPORT_PinRead(&g_ioport_ctrl, pin, &level);
    return BSP_IO_LEVEL_HIGH == level;
}

void uart_callback(uart_callback_args_t * p_args)
{
    if (NULL == p_args)
    {
        return;
    }
    if (UART_EVENT_TX_COMPLETE == p_args->event)
    {
        s_uart_tx_tail = (uint8_t) ((s_uart_tx_tail + 1U) & (UART_TX_QUEUE_DEPTH - 1U));
        s_uart_tx_busy = false;
    }
    else if (UART_EVENT_RX_CHAR == p_args->event)
    {
        uint8_t next = (uint8_t) ((s_rx_head + 1U) & (uint8_t) (sizeof(s_rx_ring) - 1U));
        if (next != s_rx_tail)
        {
            s_rx_ring[s_rx_head] = (uint8_t) p_args->data;
            s_rx_head = next;
        }
    }
}

void i2c_master_callback(i2c_master_callback_args_t * p_args)
{
    if (NULL != p_args)
    {
        s_i2c_event = p_args->event;
        if ((I2C_MASTER_EVENT_TX_COMPLETE == p_args->event) ||
            (I2C_MASTER_EVENT_RX_COMPLETE == p_args->event) ||
            (I2C_MASTER_EVENT_ABORTED == p_args->event))
        {
            s_i2c_done = true;
        }
    }
}

static void test_uart_service(void)
{
    dab_usb_console_service();

    uint8_t received;
    while (dab_usb_console_read_byte(&received))
    {
        uint8_t const next =
            (uint8_t) ((s_rx_head + 1U) & (uint8_t) (sizeof(s_rx_ring) - 1U));
        if (next != s_rx_tail)
        {
            s_rx_ring[s_rx_head] = received;
            s_rx_head = next;
        }
    }

    if (s_uart_tx_tail == s_uart_tx_head)
    {
        return;
    }
    char * p_message = s_uart_tx_queue[s_uart_tx_tail];
    size_t const length = strlen(p_message);
    if (length == dab_usb_console_write(p_message, length))
    {
        s_uart_tx_tail = (uint8_t) ((s_uart_tx_tail + 1U) & (UART_TX_QUEUE_DEPTH - 1U));
    }
}

static int test_uart_enqueue(const char * p_data, size_t length)
{
    uint8_t next = (uint8_t) ((s_uart_tx_head + 1U) & (UART_TX_QUEUE_DEPTH - 1U));
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
    test_uart_service();
    return (int) length;
}

int _write(int fd, char * p_buffer, int size)
{
    FSP_PARAMETER_NOT_USED(fd);
    if ((NULL == p_buffer) || (size <= 0))
    {
        return 0;
    }
    return test_uart_enqueue(p_buffer, (size_t) size);
}

static void test_log(const char * fmt, ...)
{
    uint8_t next = (uint8_t) ((s_uart_tx_head + 1U) & (UART_TX_QUEUE_DEPTH - 1U));
    if (next == s_uart_tx_tail)
    {
        return;
    }
    va_list args;
    va_start(args, fmt);
    int length = vsnprintf(s_uart_tx_queue[s_uart_tx_head], UART_TX_MESSAGE_LENGTH, fmt, args);
    va_end(args);
    if (length < 0)
    {
        return;
    }
    s_uart_tx_queue[s_uart_tx_head][UART_TX_MESSAGE_LENGTH - 1U] = '\0';
    s_uart_tx_head = next;
    test_uart_service();
}

/* ----------------------- Safe PWM pin ownership ----------------------- */
static void test_pwm_pins_to_gpio_low(void)
{
    uint32_t const cfg = IOPORT_CFG_DRIVE_HS_HIGH |
                         IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                         IOPORT_CFG_PORT_OUTPUT_LOW;
    for (uint32_t i = 0U; i < (sizeof(s_pwm_pins) / sizeof(s_pwm_pins[0])); i++)
    {
        (void) R_IOPORT_PinCfg(&g_ioport_ctrl, s_pwm_pins[i].pin, cfg);
        test_pin_write(s_pwm_pins[i].pin, false);
    }
}

static void test_pwm_pins_to_gpt(void)
{
    uint32_t const cfg = IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_GPT1;
    for (uint32_t i = 0U; i < (sizeof(s_pwm_pins) / sizeof(s_pwm_pins[0])); i++)
    {
        (void) R_IOPORT_PinCfg(&g_ioport_ctrl, s_pwm_pins[i].pin, cfg);
    }
}

static bool test_fault_inputs_clear(void)
{
    return test_pin_read(PIN_RAW_FAULT_N) && test_pin_read(PIN_IPRI_OC_N) && test_pin_read(PIN_ISEC_OC_N);
}

static void test_pwm_stop(void)
{
    /* GPIO ownership first guarantees a physical LOW even for wrap pulses.
     * GPT0 is restarted internally when ADC sampling is armed; its two output
     * pins remain GPIO LOW, so ADC can run without exposing any PWM waveform. */
    test_pwm_pins_to_gpio_low();
    if (s_pwm_open)
    {
        g_pwm_pri_a_ctrl.p_reg->GTSTP = DAB_GPT_CHANNEL_MASK;
        g_pwm_pri_a_ctrl.p_reg->GTCLR = DAB_GPT_CHANNEL_MASK;
        if (s_adc_trigger_enabled)
        {
            g_pwm_pri_a_ctrl.p_reg->GTSTR = DAB_GPT_ADC_TRIGGER_CHANNEL_MASK;
        }
    }
    s_pwm_running = false;
}

static void test_force_safe_outputs(void)
{
    /* RUN_REQ directly controls the UCC21520 disable chain: always drop it first. */
    test_pin_write(PIN_RUN_REQ, false);
    test_pwm_stop();
    test_pin_write(PIN_OUT_PROT_EN, false);
    test_pin_write(PIN_IN_MAIN_EN, false);
    test_pin_write(PIN_IN_PRECHARGE_EN, false);
    test_pin_write(PIN_PROT_RESET_N, false);
    test_pin_write(PIN_FAN_EN, false);
}

/*
 * A one-shot pin toggles at its leading and trailing compare values. The
 * generated stop level selects a positive pulse (LOW) or a wrap pulse (HIGH).
 */
static bool test_set_pin_edges(timer_ctrl_t * p_ctrl, gpt_io_pin_t pin,
                               uint32_t leading, uint32_t trailing)
{
    uint32_t lead_select = (uint32_t) pin | (uint32_t) GPT_IO_PIN_ONE_SHOT_LEADING_EDGE |
                           (uint32_t) GPT_BUFFER_FORCE_PUSH;
    uint32_t trail_select = (uint32_t) pin | (uint32_t) GPT_IO_PIN_ONE_SHOT_TRAILING_EDGE |
                            (uint32_t) GPT_BUFFER_FORCE_PUSH;
    return (FSP_SUCCESS == R_GPT_DutyCycleSet(p_ctrl, leading, lead_select)) &&
           (FSP_SUCCESS == R_GPT_DutyCycleSet(p_ctrl, trailing, trail_select));
}

static bool test_set_half_bridge(timer_ctrl_t * p_ctrl, uint32_t phase_counts,
                                 bool high_side_is_positive_pulse)
{
    uint32_t const base = RA8P1_TEST_EDGE_GUARD_COUNTS + phase_counts;
    uint32_t const positive_lead = base + RA8P1_TEST_DEADTIME_COUNTS;
    uint32_t const positive_trail = base + RA8P1_TEST_PWM_HALF_COUNTS;
    uint32_t const wrap_low_lead = base;
    uint32_t const wrap_low_trail = base + RA8P1_TEST_PWM_HALF_COUNTS +
                                    RA8P1_TEST_DEADTIME_COUNTS;
    if (high_side_is_positive_pulse)
    {
        return test_set_pin_edges(p_ctrl, GPT_IO_PIN_GTIOCA, positive_lead, positive_trail) &&
               test_set_pin_edges(p_ctrl, GPT_IO_PIN_GTIOCB, wrap_low_lead, wrap_low_trail);
    }
    return test_set_pin_edges(p_ctrl, GPT_IO_PIN_GTIOCA, wrap_low_lead, wrap_low_trail) &&
           test_set_pin_edges(p_ctrl, GPT_IO_PIN_GTIOCB, positive_lead, positive_trail);
}

static bool test_pwm_configure_phase(uint32_t phase_counts)
{
    if ((!s_pwm_open) || (phase_counts > RA8P1_TEST_PHASE_MAX_COUNTS))
    {
        return false;
    }
    bool was_running = s_pwm_running;
    if (was_running)
    {
        test_pwm_stop();
    }

    /* Leg B is 180 degrees from leg A; the secondary bridge lags by phase_counts. */
    bool ok = test_set_half_bridge(&g_pwm_pri_a_ctrl, 0U, true) &&
              test_set_half_bridge(&g_pwm_pri_b_ctrl, 0U, false) &&
              test_set_half_bridge(&g_pwm_sec_a_ctrl, phase_counts, true) &&
              test_set_half_bridge(&g_pwm_sec_b_ctrl, phase_counts, false);
    if (ok)
    {
        s_phase_counts = phase_counts;
    }
    if (ok && was_running)
    {
        ok = test_pwm_start();
    }
    return ok;
}

static bool test_pwm_start(void)
{
    if ((!s_pwm_ready) || (!s_pwm_open) || s_pwm_running)
    {
        return s_pwm_running;
    }
    if (!test_fault_inputs_clear())
    {
        test_log("[TEST][PWM] BLOCKED: RAW_FAULT_N/IPRI_OC_N/ISEC_OC_N must all be HIGH\r\n");
        return false;
    }

    test_pin_write(PIN_RUN_REQ, false);
    test_pin_write(PIN_IN_PRECHARGE_EN, false);
    test_pin_write(PIN_IN_MAIN_EN, false);
    test_pin_write(PIN_OUT_PROT_EN, false);
    (void) R_POEG_Reset(&g_poeg0_ctrl);
    g_pwm_pri_a_ctrl.p_reg->GTSTP = DAB_GPT_CHANNEL_MASK;
    g_pwm_pri_a_ctrl.p_reg->GTCLR = DAB_GPT_CHANNEL_MASK;
    g_pwm_pri_a_ctrl.p_reg->GTSTR = DAB_GPT_CHANNEL_MASK;
    s_pwm_running = true;
    test_pwm_pins_to_gpt();
    return true;
}

static bool test_pwm_initialize(void)
{
    test_pwm_pins_to_gpio_low();
    if ((FSP_SUCCESS != R_POEG_Open(&g_poeg0_ctrl, &g_poeg0_cfg)) ||
        (FSP_SUCCESS != R_GPT_Open(&g_pwm_pri_a_ctrl, &g_pwm_pri_a_cfg)) ||
        (FSP_SUCCESS != R_GPT_Open(&g_pwm_pri_b_ctrl, &g_pwm_pri_b_cfg)) ||
        (FSP_SUCCESS != R_GPT_Open(&g_pwm_sec_a_ctrl, &g_pwm_sec_a_cfg)) ||
        (FSP_SUCCESS != R_GPT_Open(&g_pwm_sec_b_ctrl, &g_pwm_sec_b_cfg)))
    {
        test_pwm_pins_to_gpio_low();
        return false;
    }
    s_pwm_open = true;
    test_pwm_stop();
    bool period_ok =
        (FSP_SUCCESS == R_GPT_PeriodSet(&g_pwm_pri_a_ctrl, RA8P1_TEST_PWM_PERIOD_COUNTS)) &&
        (FSP_SUCCESS == R_GPT_PeriodSet(&g_pwm_pri_b_ctrl, RA8P1_TEST_PWM_PERIOD_COUNTS)) &&
        (FSP_SUCCESS == R_GPT_PeriodSet(&g_pwm_sec_a_ctrl, RA8P1_TEST_PWM_PERIOD_COUNTS)) &&
        (FSP_SUCCESS == R_GPT_PeriodSet(&g_pwm_sec_b_ctrl, RA8P1_TEST_PWM_PERIOD_COUNTS));
    s_pwm_ready = period_ok && test_pwm_configure_phase(RA8P1_TEST_PHASE_COUNTS_DEFAULT);
    return s_pwm_ready;
}

/* ----------------------- ADC and temperature test --------------------- */
static bool test_adc_initialize(void)
{
    TickType_t const started = xTaskGetTickCount();
    fsp_err_t const err = dab_adc_dma_open();
    if (FSP_SUCCESS != err)
    {
        test_log("[TEST][ADC-DMA] FAIL stage=%s err=%ld adc_state=%u\r\n",
                 dab_adc_dma_last_error_stage(), (long) err,
                 (unsigned int) g_adc0_ctrl.adc_state);
        return false;
    }
    test_log("[TEST][ADC-DMA] CAL PASS clock=%luHz ADC0=5ch ADC1=4ch scan~%luns ready~%luns mode=dual-GPT/no-sync time=%lums\r\n",
             (unsigned long) DAB_ADC_CLOCK_HZ,
             (unsigned long) DAB_ADC_FRAME_TIME_NS,
             (unsigned long) DAB_ADC_TRIGGER_READY_TIME_NS,
             (unsigned long) ((xTaskGetTickCount() - started) * portTICK_PERIOD_MS));
    return true;
}

static bool test_wait_i2c(i2c_master_event_t expected, uint32_t timeout_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (!s_i2c_done && ((int32_t) (deadline - xTaskGetTickCount()) > 0))
    {
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
    return s_i2c_done && (expected == s_i2c_event);
}

static bool test_tmp1075_read(int32_t * p_milli_c)
{
    if (!s_i2c_open)
    {
        return false;
    }
    uint8_t register_address = 0x00U;
    uint8_t data[2] = {0U, 0U};
    s_i2c_done = false;
    s_i2c_event = I2C_MASTER_EVENT_ABORTED;
    if ((FSP_SUCCESS != R_IIC_MASTER_Write(&g_i2c2_ctrl, &register_address, 1U, true)) ||
        (!test_wait_i2c(I2C_MASTER_EVENT_TX_COMPLETE, 25U)))
    {
        return false;
    }
    s_i2c_done = false;
    s_i2c_event = I2C_MASTER_EVENT_ABORTED;
    if ((FSP_SUCCESS != R_IIC_MASTER_Read(&g_i2c2_ctrl, data, sizeof(data), false)) ||
        (!test_wait_i2c(I2C_MASTER_EVENT_RX_COMPLETE, 25U)))
    {
        return false;
    }
    int16_t raw = (int16_t) (((uint16_t) data[0] << 8U) | data[1]);
    raw >>= 4;
    *p_milli_c = (int32_t) raw * 625L / 10L;
    return (*p_milli_c > -40000L) && (*p_milli_c < 130000L);
}

static void test_adc_print(void)
{
    static bool wait_reported;

    if (!dab_adc_dma_latest(&s_adc))
    {
        if (!wait_reported)
        {
            test_log("[TEST][ADC] WAIT: no complete GPT0-triggered frame yet\r\n");
            wait_reported = true;
        }
        return;
    }
    wait_reported = false;

#if RA8P1_TEST_ADC_RATE_DIAGNOSTICS
    static uint32_t last_frame_count;
    static TickType_t last_tick;
    static bool have_rate_baseline;
    dab_adc_dma_stats_t stats;
    dab_adc_dma_stats_get(&stats);
    TickType_t const now = xTaskGetTickCount();
    uint32_t rate_hz = 0U;
    uint32_t const target_rate_hz = RA8P1_TEST_GPT_CLOCK_HZ / RA8P1_TEST_PWM_PERIOD_COUNTS;
    char const * acquisition_status = "WARMUP";
    if (have_rate_baseline && (now != last_tick))
    {
        rate_hz = ((stats.frame_count - last_frame_count) * (uint32_t) configTICK_RATE_HZ) /
                  (uint32_t) (now - last_tick);
        uint32_t const tolerance_hz = target_rate_hz / 20U; /* +/-5% allows RTOS/UART timing jitter. */
        acquisition_status = ((rate_hz >= (target_rate_hz - tolerance_hz)) &&
                              (rate_hz <= (target_rate_hz + tolerance_hz)) &&
                              (0U == stats.late_reload_count)) ? "PASS" : "FAIL";
    }
    last_frame_count = stats.frame_count;
    last_tick        = now;
    have_rate_baseline = true;
    test_log("[TEST][ADC-DMA] %s frame=%lu rate=%lu/%luHz dma0=%s ring=%u late=%lu data_err=0x%03X\r\n",
             acquisition_status,
             (unsigned long) stats.frame_count, (unsigned long) rate_hz,
             (unsigned long) target_rate_hz,
             stats.running ? "RUN" : "STOP",
             (unsigned int) DAB_ADC_DMA_RING_FRAMES,
             (unsigned long) stats.late_reload_count,
             (unsigned int) s_adc.invalid_mask);
#endif

    char line[UART_TX_MESSAGE_LENGTH];
    size_t used = (size_t) snprintf(line, sizeof(line), "[TEST][ADC] ");
    for (uint32_t vc = 0U; (vc < DAB_ADC_CHANNEL_COUNT) && (used < sizeof(line)); vc++)
    {
        uint32_t const mv = dab_adc_raw_to_mv(s_adc.raw[vc]);
        int n = snprintf(&line[used], sizeof(line) - used, "%s=%lu.%03luV%s",
                         dab_adc_channel_pin_name((dab_adc_channel_t) vc),
                         (unsigned long) (mv / 1000UL),
                         (unsigned long) (mv % 1000UL),
                         (vc + 1U < DAB_ADC_CHANNEL_COUNT) ? " " : "\r\n");
        if (n < 0)
        {
            break;
        }
        used += (size_t) n;
    }
    test_log("%s", line);
}

/* ------------------------- Commands and reporting --------------------- */
static uint32_t test_phase_hundredth_degrees(void)
{
    return ((s_phase_counts * 36000UL) + (RA8P1_TEST_PWM_PERIOD_COUNTS / 2UL)) /
           RA8P1_TEST_PWM_PERIOD_COUNTS;
}

static void test_print_pwm_contract(void)
{
    uint32_t const actual_hz = RA8P1_TEST_GPT_CLOCK_HZ / RA8P1_TEST_PWM_PERIOD_COUNTS;
    uint32_t const on_counts = RA8P1_TEST_PWM_HALF_COUNTS - RA8P1_TEST_DEADTIME_COUNTS;
    uint32_t const gate_duty_x100 = (on_counts * 10000UL) / RA8P1_TEST_PWM_PERIOD_COUNTS;
    uint32_t const angle_x100 = test_phase_hundredth_degrees();
    test_log("[TEST][PWM] target=%luHz actual=%luHz period=%lucnt/%luns logical_duty=50.00%% "
             "gate_high=%lucnt/%lu.%02lu%% dead=%lucnt/%luns phase=%lucnt/%luns/%lu.%02ludeg "
             "running=%u RUN_REQ=LOW\r\n",
             (unsigned long) RA8P1_TEST_PWM_TARGET_HZ, (unsigned long) actual_hz,
             (unsigned long) RA8P1_TEST_PWM_PERIOD_COUNTS,
             (unsigned long) (RA8P1_TEST_PWM_PERIOD_COUNTS * 4UL),
             (unsigned long) on_counts, (unsigned long) (gate_duty_x100 / 100UL),
             (unsigned long) (gate_duty_x100 % 100UL),
             (unsigned long) RA8P1_TEST_DEADTIME_COUNTS,
             (unsigned long) RA8P1_TEST_DEADTIME_NS,
             (unsigned long) s_phase_counts, (unsigned long) (s_phase_counts * 4UL),
             (unsigned long) (angle_x100 / 100UL), (unsigned long) (angle_x100 % 100UL),
             s_pwm_running ? 1U : 0U);
}

static void test_print_status(void)
{
    test_print_pwm_contract();
    test_log("[TEST][DIN] RAW_FAULT_N=%u IPRI_OC_N=%u ISEC_OC_N=%u | "
             "[DOUT] PRE=%u MAIN=%u OUT=%u RESET_N=%u RUN=%u FAN=%u LED=%u\r\n",
             test_pin_read(PIN_RAW_FAULT_N) ? 1U : 0U,
             test_pin_read(PIN_IPRI_OC_N) ? 1U : 0U,
             test_pin_read(PIN_ISEC_OC_N) ? 1U : 0U,
             test_pin_read(PIN_IN_PRECHARGE_EN) ? 1U : 0U,
             test_pin_read(PIN_IN_MAIN_EN) ? 1U : 0U,
             test_pin_read(PIN_OUT_PROT_EN) ? 1U : 0U,
             test_pin_read(PIN_PROT_RESET_N) ? 1U : 0U,
             test_pin_read(PIN_RUN_REQ) ? 1U : 0U,
             s_fan_on ? 1U : 0U, s_status_led_on ? 1U : 0U);
}

static void test_print_help(void)
{
    test_log("[TEST][HELP] status | pins | adc | stream on/off | tmp | pwm on | pwm off\r\n");
    test_log("[TEST][HELP] phase 0 | phase 1 | phase 2 | phase counts N (0..%lu)\r\n",
             (unsigned long) RA8P1_TEST_PHASE_MAX_COUNTS);
    test_log("[TEST][HELP] fan on/off | led on/off | gpio pre/main/out/reset/run\r\n");
    test_log("[TEST][HELP] GPIO pulses are %lums and force PWM OFF first; start/clear are disabled.\r\n",
             (unsigned long) RA8P1_TEST_GPIO_PULSE_MS);
}

static void test_gpio_pulse_start(bsp_io_port_pin_t pin, const char * name)
{
    test_pwm_stop();
    test_pin_write(PIN_RUN_REQ, false);
    test_pin_write(PIN_IN_PRECHARGE_EN, false);
    test_pin_write(PIN_IN_MAIN_EN, false);
    test_pin_write(PIN_OUT_PROT_EN, false);
    test_pin_write(PIN_PROT_RESET_N, false);
    test_pin_write(pin, true);
    s_gpio_pulse_pin = pin;
    s_gpio_pulse_name = name;
    s_gpio_pulse_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(RA8P1_TEST_GPIO_PULSE_MS);
    s_gpio_pulse_active = true;
    test_log("[TEST][GPIO] %s HIGH for %lums; PWM OFF\r\n", name,
             (unsigned long) RA8P1_TEST_GPIO_PULSE_MS);
}

static void test_gpio_pulse_update(void)
{
    if (s_gpio_pulse_active &&
        ((int32_t) (xTaskGetTickCount() - s_gpio_pulse_deadline) >= 0))
    {
        test_pin_write(s_gpio_pulse_pin, false);
        test_log("[TEST][GPIO] %s returned LOW\r\n", s_gpio_pulse_name);
        s_gpio_pulse_active = false;
    }
}

static void test_set_phase_and_report(uint32_t counts)
{
    if (test_pwm_configure_phase(counts))
    {
        test_log("[TEST][PHASE] applied; four GPT counters restarted synchronously if PWM was active\r\n");
        test_print_pwm_contract();
    }
    else
    {
        test_log("[TEST][PHASE] REJECTED counts=%lu allowed=0..%lu\r\n",
                 (unsigned long) counts, (unsigned long) RA8P1_TEST_PHASE_MAX_COUNTS);
    }
}

static void test_handle_command(char * command)
{
    while ((*command == ' ') || (*command == '\t'))
    {
        command++;
    }
    if ((0 == strcmp(command, "help")) || (0 == strcmp(command, "test")))
    {
        test_print_help();
    }
    else if (0 == strcmp(command, "status"))
    {
        test_print_status();
    }
    else if (0 == strcmp(command, "pins"))
    {
        test_print_pin_map();
    }
    else if (0 == strcmp(command, "adc"))
    {
        test_adc_print();
    }
    else if (0 == strcmp(command, "stream on"))
    {
        s_adc_stream_enabled = true;
        test_log("[TEST][ADC] stream ON (%lums)\r\n",
                 (unsigned long) RA8P1_TEST_TELEMETRY_PERIOD_MS);
    }
    else if (0 == strcmp(command, "stream off"))
    {
        s_adc_stream_enabled = false;
        test_log("[TEST][ADC] stream OFF; send adc for one frame\r\n");
    }
    else if (0 == strcmp(command, "tmp"))
    {
        int32_t milli_c = 0;
        if (test_tmp1075_read(&milli_c))
        {
            test_log("[TEST][TMP1075] %ld.%03ldC PASS\r\n", (long) (milli_c / 1000L),
                     (long) ((milli_c < 0 ? -milli_c : milli_c) % 1000L));
        }
        else
        {
            test_log("[TEST][TMP1075] FAIL/NOT-CONNECTED (test continues)\r\n");
        }
    }
    else if (0 == strcmp(command, "pwm on"))
    {
        test_log("[TEST][PWM] %s\r\n", test_pwm_start() ? "ON: scope eight CN2 pins" : "NOT STARTED");
        test_print_pwm_contract();
    }
    else if (0 == strcmp(command, "pwm off"))
    {
        test_pwm_stop();
        test_log("[TEST][PWM] OFF: all eight pins GPIO LOW\r\n");
    }
    else if (0 == strcmp(command, "phase 0"))
    {
        test_set_phase_and_report(RA8P1_TEST_PHASE_PRESET_0_COUNTS);
    }
    else if (0 == strcmp(command, "phase 1"))
    {
        test_set_phase_and_report(RA8P1_TEST_PHASE_PRESET_1_COUNTS);
    }
    else if (0 == strcmp(command, "phase 2"))
    {
        test_set_phase_and_report(RA8P1_TEST_PHASE_PRESET_2_COUNTS);
    }
    else if (0 == strncmp(command, "phase counts ", 13U))
    {
        unsigned long counts = 0UL;
        if (1 == sscanf(&command[13], "%lu", &counts))
        {
            test_set_phase_and_report((uint32_t) counts);
        }
        else
        {
            test_log("[TEST][PHASE] parse error\r\n");
        }
    }
    else if (0 == strcmp(command, "fan on"))
    {
        s_fan_on = true;
        test_pin_write(PIN_FAN_EN, true);
        test_log("[TEST][FAN] ON\r\n");
    }
    else if (0 == strcmp(command, "fan off"))
    {
        s_fan_on = false;
        test_pin_write(PIN_FAN_EN, false);
        test_log("[TEST][FAN] OFF\r\n");
    }
    else if (0 == strcmp(command, "led on"))
    {
        s_status_led_on = true;
        test_pin_write(PIN_DAB_STATUS_LED, true);
        test_log("[TEST][LED] P402/CN2.75 ON\r\n");
    }
    else if (0 == strcmp(command, "led off"))
    {
        s_status_led_on = false;
        test_pin_write(PIN_DAB_STATUS_LED, false);
        test_log("[TEST][LED] P402/CN2.75 OFF\r\n");
    }
    else if (0 == strcmp(command, "gpio pre"))
    {
        test_gpio_pulse_start(PIN_IN_PRECHARGE_EN, "IN_PRECHARGE_EN/PB00/CN2.97");
    }
    else if (0 == strcmp(command, "gpio main"))
    {
        test_gpio_pulse_start(PIN_IN_MAIN_EN, "IN_MAIN_EN/PB02/CN2.99");
    }
    else if (0 == strcmp(command, "gpio out"))
    {
        test_gpio_pulse_start(PIN_OUT_PROT_EN, "OUT_PROT_EN/P706/CN2.45");
    }
    else if (0 == strcmp(command, "gpio reset"))
    {
        test_gpio_pulse_start(PIN_PROT_RESET_N, "PROT_RESET_N/PB03/CN2.47");
    }
    else if (0 == strcmp(command, "gpio run"))
    {
        test_gpio_pulse_start(PIN_RUN_REQ, "RUN_REQ/P707/CN2.57");
    }
    else if ((0 == strcmp(command, "start")) || (0 == strcmp(command, "clear")))
    {
        test_log("[TEST][SAFE] production command '%s' disabled in peripheral-test mode\r\n", command);
    }
    else if ('\0' != command[0])
    {
        test_log("[TEST][CMD] unknown='%s'; send help\r\n", command);
    }
}

static void test_process_uart(void)
{
    static char line[96];
    static uint32_t length;
    while (s_rx_tail != s_rx_head)
    {
        char c = (char) s_rx_ring[s_rx_tail];
        s_rx_tail = (uint8_t) ((s_rx_tail + 1U) & (uint8_t) (sizeof(s_rx_ring) - 1U));
        if ((c == '\r') || (c == '\n'))
        {
            if (length > 0U)
            {
                line[length] = '\0';
                test_handle_command(line);
                length = 0U;
            }
        }
        else if (length < (sizeof(line) - 1U))
        {
            line[length++] = c;
        }
        else
        {
            length = 0U;
            test_log("[TEST][CMD] line too long\r\n");
        }
    }
}

static void test_print_pin_map(void)
{
    test_log("[TEST][PIN] USB CDC VBUS=P407 DP=P814/USB_P DM=P815/USB_N | UART P208/P209 inactive\r\n");
    test_log("[TEST][PIN] PWM CN2.83=P415 .81=P414 .87=P105 .85=P104 "
             ".35=P915 .37=P914 .39=P605 .41=P604\r\n");
    test_log("[TEST][PIN] DOUT PRE=PB00/CN2.97 MAIN=PB02/.99 OUT=P706/.45 RESET_N=PB03/.47 "
             "RUN=P707/.57 | DIN RAW=P401/.59+.61 IPRI_OC=P400/.79 ISEC_OC=P403/.55\r\n");
    test_log("[TEST][PIN] ADC P000/P5.3/.71 P001/P5.5/.101 P002/P5.7/.65 P003/P5.9/.69 "
             "P004/P5.11/.33 P006/P5.4/.73 P007/P5.6/.51 P008/P5.8/.67 P009/P5.10/.91 | "
             "LED=P402/.75 FAN=PB04/.113\r\n");
}

/* ----------------------------- Task entry ------------------------------ */
void User_Init_Task_entry(void * pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    test_force_safe_outputs();
    test_pin_write(PIN_DAB_STATUS_LED, false);
    test_pin_write(PIN_USER_LED, false);

    bool usb_ok = (FSP_SUCCESS == dab_usb_console_init());
    if (!usb_ok)
    {
        for (;;)
        {
            test_pin_write(PIN_USER_LED, true);
            vTaskDelay(pdMS_TO_TICKS(100U));
            test_pin_write(PIN_USER_LED, false);
            vTaskDelay(pdMS_TO_TICKS(100U));
        }
    }

    test_log("\r\n[TEST][BOOT] ISOLATED peripheral firmware; production state machine compiled out\r\n");
    test_log("[TEST][BOOT] USB CDC P814/USB_P P815/USB_N; DAB target=300kHz deadtime=160ns\r\n");
    test_log("[TEST][SAFE] PWM pins/RUN_REQ/power enables stay LOW; GPT0 runs internally for ADC only\r\n");

    s_adc_open = test_adc_initialize();
    s_i2c_open = (FSP_SUCCESS == R_IIC_MASTER_Open(&g_i2c2_ctrl, &g_i2c2_cfg));
    bool pwm_ok = test_pwm_initialize();
    bool adc_dma_started = false;
    if (s_adc_open && pwm_ok)
    {
        fsp_err_t adc_err = dab_adc_dma_bind_pwm(&g_pwm_pri_a_ctrl,
                                                 RA8P1_TEST_ADC_TRIGGER_COUNTS);
        if (FSP_SUCCESS == adc_err)
        {
            adc_err = dab_adc_dma_start();
        }
        adc_dma_started = (FSP_SUCCESS == adc_err);
        if (adc_dma_started)
        {
            s_adc_trigger_enabled = true;
            test_pwm_stop();
        }
        else
        {
            test_log("[TEST][ADC-DMA] START FAIL stage=%s err=%ld\r\n",
                     dab_adc_dma_last_error_stage(), (long) adc_err);
        }
    }
    bool pwm_started = false;
    if ((RA8P1_TEST_PWM_AUTOSTART != 0U) && pwm_ok)
    {
        pwm_started = test_pwm_start();
    }

    test_log("[TEST][INIT] ADC_B+DMAC0(9ch@PWM/%lu.%03luMHz)=%s IIC2=%s GPT0/1/5/8+POEG=%s\r\n",
             (unsigned long) (DAB_ADC_CLOCK_HZ / 1000000UL),
             (unsigned long) ((DAB_ADC_CLOCK_HZ % 1000000UL) / 1000UL),
             adc_dma_started ? "PASS" : "FAIL-CONTINUE",
             s_i2c_open ? "PASS(TMP:send tmp)" : "FAIL-CONTINUE",
             pwm_ok ? "PASS" : "FAIL-CONTINUE");
    test_print_pwm_contract();
    test_log("[TEST][READY] PWM pins=%s; ADC trigger=%s acquisition=%luHz hardware+DMA, display=%lums\r\n",
             pwm_started ? "ON" : "OFF(GPIO LOW)",
             s_adc_trigger_enabled ? "GPT0-RUNNING" : "OFF",
             (unsigned long) (RA8P1_TEST_GPT_CLOCK_HZ / RA8P1_TEST_PWM_PERIOD_COUNTS),
             (unsigned long) RA8P1_TEST_TELEMETRY_PERIOD_MS);

    TickType_t last_adc = xTaskGetTickCount();
    TickType_t last_status = last_adc;
    TickType_t last_heartbeat = last_adc;
    bool heartbeat = false;

    for (;;)
    {
        test_uart_service();
        test_process_uart();

        if (dab_usb_console_take_disconnect())
        {
            test_pwm_stop();
            test_force_safe_outputs();
            s_gpio_pulse_active = false;
            test_log("[TEST][USB] link lost; PWM and every controlled output forced LOW\r\n");
        }
        if (dab_usb_console_take_reconnect())
        {
            test_log("[TEST][USB] PC connected; send help\r\n");
        }

        test_gpio_pulse_update();
        dab_adc_dma_service();

        TickType_t now = xTaskGetTickCount();
        if (s_adc_stream_enabled &&
            ((now - last_adc) >= pdMS_TO_TICKS(RA8P1_TEST_TELEMETRY_PERIOD_MS)))
        {
            test_adc_print();
            last_adc = now;
        }
#if RA8P1_TEST_PERIODIC_STATUS_ENABLE
        if ((now - last_status) >= pdMS_TO_TICKS(RA8P1_TEST_STATUS_PERIOD_MS))
        {
            test_print_status();
            last_status = now;
        }
#else
        FSP_PARAMETER_NOT_USED(last_status);
#endif
        if ((now - last_heartbeat) >= pdMS_TO_TICKS(RA8P1_TEST_LED_HEARTBEAT_MS))
        {
            heartbeat = !heartbeat;
            test_pin_write(PIN_USER_LED, heartbeat);
            last_heartbeat = now;
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

#endif /* RA8P1_PERIPHERAL_TEST_ENABLE */
