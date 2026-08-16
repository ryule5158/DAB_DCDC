#include "dab_pwm_hw.h"

#include "dab_accel.h"
#include "dab_first_power_config.h"

#include <stddef.h>

#define DAB_GPT_CHANNEL_MASK   ((1UL << 0) | (1UL << 1) | (1UL << 5) | (1UL << 8))
#define DAB_PHASE_UPDATE_LATEST_COUNT  (600UL)
#define DAB_PRIMARY_GATE_COUNT          (4UL)
#define DAB_PRIMARY_HALF_GATE_COUNT     (2UL)
#define DAB_PRIMARY_HALF_BRIDGE_COUNT   (2UL)

typedef struct st_dab_pwm_pin
{
    bsp_io_port_pin_t pin;
} dab_pwm_pin_t;

/*
 * Fixed, scope-tested, developer-board-exported pins:
 * PRI A H/L P415/P414 CN2.83/.81, PRI B H/L P105/P104 CN2.87/.85,
 * SEC A H/L P915/P914 CN2.35/.37, SEC B H/L P605/P604 CN2.39/.41.
 */
static const dab_pwm_pin_t s_pwm_pins[] =
{
    {BSP_IO_PORT_04_PIN_15},
    {BSP_IO_PORT_04_PIN_14},
    {BSP_IO_PORT_01_PIN_05},
    {BSP_IO_PORT_01_PIN_04},
    {BSP_IO_PORT_09_PIN_15},
    {BSP_IO_PORT_09_PIN_14},
    {BSP_IO_PORT_06_PIN_05},
    {BSP_IO_PORT_06_PIN_04},
};

static bool     s_open;
static bool     s_clock_running;
static bool     s_outputs_enabled;
static uint32_t s_phase_counts;
static uint32_t s_primary_inner_counts;
static uint32_t s_secondary_inner_counts;

static void dab_pwm_hw_pins_gpio_low(void)
{
    uint32_t const cfg = IOPORT_CFG_DRIVE_HS_HIGH |
                         IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                         IOPORT_CFG_PORT_OUTPUT_LOW;
    for (uint32_t i = 0U; i < (sizeof(s_pwm_pins) / sizeof(s_pwm_pins[0])); i++)
    {
        (void) R_IOPORT_PinCfg(&g_ioport_ctrl, s_pwm_pins[i].pin, cfg);
        (void) R_IOPORT_PinWrite(&g_ioport_ctrl, s_pwm_pins[i].pin, BSP_IO_LEVEL_LOW);
    }
}

static bool dab_pwm_hw_pins_gpt(void)
{
    uint32_t const cfg = (uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                         (uint32_t) IOPORT_PERIPHERAL_GPT1;
    for (uint32_t i = 0U; i < (sizeof(s_pwm_pins) / sizeof(s_pwm_pins[0])); i++)
    {
        if (FSP_SUCCESS != R_IOPORT_PinCfg(&g_ioport_ctrl,
                                           s_pwm_pins[i].pin, cfg))
        {
            dab_pwm_hw_pins_gpio_low();
            return false;
        }
    }
    return true;
}

static bool dab_pwm_hw_primary_pin_range_gpt(uint32_t first_gate,
                                              uint32_t gate_count)
{
    if ((0U == gate_count) ||
        (first_gate >= DAB_PRIMARY_GATE_COUNT) ||
        ((first_gate + gate_count) > DAB_PRIMARY_GATE_COUNT))
    {
        return false;
    }

    uint32_t const cfg = (uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                         (uint32_t) IOPORT_PERIPHERAL_GPT1;
    for (uint32_t i = first_gate; i < (first_gate + gate_count); i++)
    {
        if (FSP_SUCCESS != R_IOPORT_PinCfg(&g_ioport_ctrl,
                                           s_pwm_pins[i].pin, cfg))
        {
            dab_pwm_hw_pins_gpio_low();
            return false;
        }
    }
    return true;
}

static bool dab_pwm_hw_edge_set(timer_ctrl_t * p_ctrl, gpt_io_pin_t pin,
                                uint32_t leading, uint32_t trailing,
                                bool force_push)
{
    uint32_t force = force_push ? (uint32_t) GPT_BUFFER_FORCE_PUSH : 0U;
    uint32_t leading_select = (uint32_t) pin |
                              (uint32_t) GPT_IO_PIN_ONE_SHOT_LEADING_EDGE |
                              force;
    uint32_t trailing_select = (uint32_t) pin |
                               (uint32_t) GPT_IO_PIN_ONE_SHOT_TRAILING_EDGE |
                               force;
    return (FSP_SUCCESS == R_GPT_DutyCycleSet(p_ctrl, leading, leading_select)) &&
           (FSP_SUCCESS == R_GPT_DutyCycleSet(p_ctrl, trailing, trailing_select));
}

static bool dab_pwm_hw_half_bridge_set(timer_ctrl_t * p_ctrl, uint32_t phase_counts,
                                       bool high_side_positive, bool force_push)
{
    uint32_t const base          = DAB_PWM_EDGE_GUARD_COUNTS + phase_counts;
    uint32_t const positive_lead = base + DAB_DEADTIME_COUNTS;
    uint32_t const positive_end  = base + DAB_PWM_HALF_COUNTS;
    uint32_t const wrap_lead     = base;
    uint32_t const wrap_end      = base + DAB_PWM_HALF_COUNTS +
                                   DAB_DEADTIME_COUNTS;

    if (high_side_positive)
    {
        return dab_pwm_hw_edge_set(p_ctrl, GPT_IO_PIN_GTIOCA,
                                   positive_lead, positive_end, force_push) &&
               dab_pwm_hw_edge_set(p_ctrl, GPT_IO_PIN_GTIOCB,
                                   wrap_lead, wrap_end, force_push);
    }

    return dab_pwm_hw_edge_set(p_ctrl, GPT_IO_PIN_GTIOCA,
                               wrap_lead, wrap_end, force_push) &&
           dab_pwm_hw_edge_set(p_ctrl, GPT_IO_PIN_GTIOCB,
                               positive_lead, positive_end, force_push);
}

static bool dab_pwm_hw_all_edges_set(uint32_t phase_counts,
                                     uint32_t primary_inner_counts,
                                     uint32_t secondary_inner_counts,
                                     bool force_push)
{
    return dab_pwm_hw_half_bridge_set(&g_pwm_pri_a_ctrl, 0U, true, force_push) &&
           dab_pwm_hw_half_bridge_set(&g_pwm_pri_b_ctrl,
                                      primary_inner_counts, false, force_push) &&
           dab_pwm_hw_half_bridge_set(&g_pwm_sec_a_ctrl, phase_counts, true, force_push) &&
           dab_pwm_hw_half_bridge_set(&g_pwm_sec_b_ctrl,
                                      phase_counts + secondary_inner_counts,
                                      false, force_push);
}

static bool dab_pwm_hw_restart_synchronized(uint32_t phase_counts,
                                             uint32_t primary_inner_counts,
                                             uint32_t secondary_inner_counts)
{
    timer_status_t pri_a = {0};
    timer_status_t pri_b = {0};
    timer_status_t sec_a = {0};
    timer_status_t sec_b = {0};

    /* Keep every physical gate command LOW while all four one-shot GPT
     * channels are re-armed from the same count and edge schedule. */
    dab_pwm_hw_pins_gpio_low();
    s_outputs_enabled = false;
    g_pwm_pri_a_ctrl.p_reg->GTSTP = DAB_GPT_CHANNEL_MASK;
    g_pwm_pri_a_ctrl.p_reg->GTCLR = DAB_GPT_CHANNEL_MASK;

    bool const edges_ok = dab_pwm_hw_all_edges_set(phase_counts,
                                                    primary_inner_counts,
                                                    secondary_inner_counts,
                                                    true);
    g_pwm_pri_a_ctrl.p_reg->GTSTR = DAB_GPT_CHANNEL_MASK;
    if (!edges_ok)
    {
        return false;
    }

    return (FSP_SUCCESS == R_GPT_StatusGet(&g_pwm_pri_a_ctrl, &pri_a)) &&
           (FSP_SUCCESS == R_GPT_StatusGet(&g_pwm_pri_b_ctrl, &pri_b)) &&
           (FSP_SUCCESS == R_GPT_StatusGet(&g_pwm_sec_a_ctrl, &sec_a)) &&
           (FSP_SUCCESS == R_GPT_StatusGet(&g_pwm_sec_b_ctrl, &sec_b)) &&
           (TIMER_STATE_COUNTING == pri_a.state) &&
           (TIMER_STATE_COUNTING == pri_b.state) &&
           (TIMER_STATE_COUNTING == sec_a.state) &&
           (TIMER_STATE_COUNTING == sec_b.state);
}

static bool dab_pwm_hw_primary_gate_range_enable(uint32_t first_gate,
                                                  uint32_t gate_count)
{
    if ((!s_open) || (!s_clock_running))
    {
        return false;
    }

    /* Start from eight explicit LOW GPIOs, then expose only the requested
     * primary gate pair/range to the zero-phase production edge schedule. */
    dab_pwm_hw_outputs_disable();
    if (!dab_pwm_hw_restart_synchronized(0U, 0U, 0U) ||
        !dab_pwm_hw_primary_pin_range_gpt(first_gate, gate_count))
    {
        dab_pwm_hw_outputs_disable();
        return false;
    }

    s_outputs_enabled = true;
    return true;
}

fsp_err_t dab_pwm_hw_open(void)
{
    if (s_open)
    {
        return FSP_ERR_ALREADY_OPEN;
    }

    dab_pwm_hw_pins_gpio_low();
#if DAB_POEG_PWM_SHUTDOWN_ENABLE
    fsp_err_t const poeg_err = R_POEG_Open(&g_poeg0_ctrl, &g_poeg0_cfg);
    if (FSP_SUCCESS != poeg_err)
    {
        return poeg_err;
    }
#endif
    if ((FSP_SUCCESS != R_GPT_Open(&g_pwm_pri_a_ctrl, &g_pwm_pri_a_cfg)) ||
        (FSP_SUCCESS != R_GPT_Open(&g_pwm_pri_b_ctrl, &g_pwm_pri_b_cfg)) ||
        (FSP_SUCCESS != R_GPT_Open(&g_pwm_sec_a_ctrl, &g_pwm_sec_a_cfg)) ||
        (FSP_SUCCESS != R_GPT_Open(&g_pwm_sec_b_ctrl, &g_pwm_sec_b_cfg)))
    {
        dab_pwm_hw_pins_gpio_low();
        return FSP_ERR_INVALID_STATE;
    }

    if ((FSP_SUCCESS != R_GPT_PeriodSet(&g_pwm_pri_a_ctrl, DAB_PWM_PERIOD_COUNTS)) ||
        (FSP_SUCCESS != R_GPT_PeriodSet(&g_pwm_pri_b_ctrl, DAB_PWM_PERIOD_COUNTS)) ||
        (FSP_SUCCESS != R_GPT_PeriodSet(&g_pwm_sec_a_ctrl, DAB_PWM_PERIOD_COUNTS)) ||
        (FSP_SUCCESS != R_GPT_PeriodSet(&g_pwm_sec_b_ctrl, DAB_PWM_PERIOD_COUNTS)))
    {
        dab_pwm_hw_pins_gpio_low();
        return FSP_ERR_INVALID_STATE;
    }

    s_open = true;
    g_pwm_pri_a_ctrl.p_reg->GTSTP = DAB_GPT_CHANNEL_MASK;
    g_pwm_pri_a_ctrl.p_reg->GTCLR = DAB_GPT_CHANNEL_MASK;
    if (!dab_pwm_hw_all_edges_set(0U, 0U, 0U, true))
    {
        return FSP_ERR_INVALID_STATE;
    }
    s_phase_counts = 0U;
    s_primary_inner_counts = 0U;
    s_secondary_inner_counts = 0U;
    return FSP_SUCCESS;
}

fsp_err_t dab_pwm_hw_clock_start(void)
{
    if (!s_open)
    {
        return FSP_ERR_NOT_OPEN;
    }

    /* GPT0 is also the ADC hardware trigger timebase.  Keep all bridge pins
     * forced GPIO LOW here, but allow the counters to run even while POEG is
     * latched.  The separate output-enable path still requires POEG healthy. */
    dab_pwm_hw_pins_gpio_low();
    g_pwm_pri_a_ctrl.p_reg->GTCLR = DAB_GPT_CHANNEL_MASK;
    g_pwm_pri_a_ctrl.p_reg->GTSTR = DAB_GPT_CHANNEL_MASK;
    s_clock_running = true;
    return FSP_SUCCESS;
}

bool dab_pwm_hw_outputs_enable(void)
{
    if ((!s_open) || (!s_clock_running))
    {
        return false;
    }

    /* Re-arm all four one-shot GPT channels from count zero on every enable.
     * This avoids reconnecting pins to stale/asynchronous output state after
     * OFF left the ADC timebase running in the background. */
    if (!dab_pwm_hw_restart_synchronized(s_phase_counts,
                                         s_primary_inner_counts,
                                         s_secondary_inner_counts) ||
        !dab_pwm_hw_pins_gpt())
    {
        return false;
    }
    s_outputs_enabled = true;
    return true;
}

bool dab_pwm_hw_primary_outputs_enable(void)
{
    return dab_pwm_hw_primary_gate_range_enable(0U, DAB_PRIMARY_GATE_COUNT);
}

bool dab_pwm_hw_primary_half_bridge_outputs_enable(uint32_t bridge_index)
{
    if (bridge_index >= DAB_PRIMARY_HALF_BRIDGE_COUNT)
    {
        return false;
    }

    return dab_pwm_hw_primary_gate_range_enable(
        bridge_index * DAB_PRIMARY_HALF_GATE_COUNT,
        DAB_PRIMARY_HALF_GATE_COUNT);
}

DAB_ITCM_CODE void dab_pwm_hw_outputs_disable(void)
{
    dab_pwm_hw_pins_gpio_low();
    s_outputs_enabled = false;
    s_phase_counts    = 0U;
    if (s_open)
    {
        (void) dab_pwm_hw_all_edges_set(0U, 0U, 0U, true);
    }
    s_primary_inner_counts = 0U;
    s_secondary_inner_counts = 0U;
}

bool dab_pwm_hw_single_gate_set(uint32_t gate_index)
{
    uint32_t const gate_count = (uint32_t) (sizeof(s_pwm_pins) /
                                             sizeof(s_pwm_pins[0]));
    if ((!s_open) || (gate_index >= gate_count))
    {
        return false;
    }

    /* Never inherit a GPT edge or another diagnostic gate. */
    dab_pwm_hw_outputs_disable();

    uint32_t const cfg = IOPORT_CFG_DRIVE_HS_HIGH |
                         IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                         IOPORT_CFG_PORT_OUTPUT_HIGH;
    if (FSP_SUCCESS != R_IOPORT_PinCfg(&g_ioport_ctrl,
                                       s_pwm_pins[gate_index].pin, cfg))
    {
        dab_pwm_hw_outputs_disable();
        return false;
    }
    if (FSP_SUCCESS != R_IOPORT_PinWrite(&g_ioport_ctrl,
                                         s_pwm_pins[gate_index].pin,
                                         BSP_IO_LEVEL_HIGH))
    {
        dab_pwm_hw_outputs_disable();
        return false;
    }
    return true;
}

DAB_ITCM_CODE bool dab_pwm_hw_phase_set(uint32_t phase_counts)
{
    return dab_pwm_hw_modulation_set(phase_counts,
                                     s_primary_inner_counts,
                                     s_secondary_inner_counts);
}

DAB_ITCM_CODE bool dab_pwm_hw_modulation_set(uint32_t phase_counts,
                                             uint32_t primary_inner_counts,
                                             uint32_t secondary_inner_counts)
{
    if ((!s_open) || (phase_counts > DAB_PHASE_ABSOLUTE_MAX_COUNTS) ||
        (primary_inner_counts > DAB_INNER_SHIFT_MAX_COUNTS) ||
        (secondary_inner_counts > DAB_INNER_SHIFT_MAX_COUNTS))
    {
        return false;
    }

    /*
     * One-shot mode uses fixed compare buffering.  At run time GTCCRC..F are
     * written without a force-push, so the complete secondary schedule is
     * taken at the next cycle boundary.  The ADC DMA completes near count 124;
     * refusing late-cycle writes prevents a partially updated boundary.
     */
    bool const running_update = s_clock_running && s_outputs_enabled;
    if (running_update &&
        (g_pwm_pri_a_ctrl.p_reg->GTCNT > DAB_PHASE_UPDATE_LATEST_COUNT))
    {
        return false;
    }

    FSP_CRITICAL_SECTION_DEFINE;
    FSP_CRITICAL_SECTION_ENTER;
    bool ok = true;
    if (primary_inner_counts != s_primary_inner_counts)
    {
        ok = dab_pwm_hw_half_bridge_set(&g_pwm_pri_b_ctrl,
                                        primary_inner_counts, false,
                                        !running_update);
    }
    if (ok && (phase_counts != s_phase_counts))
    {
        ok = dab_pwm_hw_half_bridge_set(&g_pwm_sec_a_ctrl,
                                        phase_counts, true,
                                        !running_update);
    }
    if (ok && ((phase_counts != s_phase_counts) ||
               (secondary_inner_counts != s_secondary_inner_counts)))
    {
        ok = dab_pwm_hw_half_bridge_set(&g_pwm_sec_b_ctrl,
                                        phase_counts + secondary_inner_counts,
                                        false, !running_update);
    }
    FSP_CRITICAL_SECTION_EXIT;
    if (ok)
    {
        s_phase_counts = phase_counts;
        s_primary_inner_counts = primary_inner_counts;
        s_secondary_inner_counts = secondary_inner_counts;
    }
    return ok;
}

void dab_pwm_hw_shutdown(void)
{
    dab_pwm_hw_outputs_disable();
    if (s_open)
    {
        g_pwm_pri_a_ctrl.p_reg->GTSTP = DAB_GPT_CHANNEL_MASK;
        g_pwm_pri_a_ctrl.p_reg->GTCLR = DAB_GPT_CHANNEL_MASK;
    }
    s_clock_running = false;
}

bool dab_pwm_hw_poeg_clear(void)
{
    if (!s_open)
    {
        return false;
    }
#if !DAB_POEG_PWM_SHUTDOWN_ENABLE
    return true;
#else
    if (FSP_SUCCESS != R_POEG_Reset(&g_poeg0_ctrl))
    {
        return false;
    }
    return dab_pwm_hw_poeg_ok();
#endif
}

bool dab_pwm_hw_poeg_ok(void)
{
    if (!s_open)
    {
        return false;
    }
#if !DAB_POEG_PWM_SHUTDOWN_ENABLE
    return true;
#else
    poeg_status_t status = {.state = POEG_STATE_NO_DISABLE_REQUEST};
    return (FSP_SUCCESS == R_POEG_StatusGet(&g_poeg0_ctrl, &status)) &&
           (POEG_STATE_NO_DISABLE_REQUEST == status.state);
#endif
}

bool dab_pwm_hw_is_open(void)
{
    return s_open;
}

bool dab_pwm_hw_outputs_are_enabled(void)
{
    return s_outputs_enabled;
}

void dab_pwm_hw_diag_get(dab_pwm_hw_diag_t * p_diag)
{
    if (NULL == p_diag)
    {
        return;
    }

    *p_diag = (dab_pwm_hw_diag_t) {0};
    p_diag->outputs_requested = s_outputs_enabled;
    p_diag->poeg_ok = dab_pwm_hw_poeg_ok();

    gpt_instance_ctrl_t * const controls[] =
    {
        &g_pwm_pri_a_ctrl,
        &g_pwm_pri_b_ctrl,
        &g_pwm_sec_a_ctrl,
        &g_pwm_sec_b_ctrl,
    };
    uint32_t before[4] = {0};

    for (uint32_t i = 0U; i < 4U; i++)
    {
        timer_status_t status = {0};
        if (FSP_SUCCESS == R_GPT_StatusGet((timer_ctrl_t *) controls[i],
                                           &status))
        {
            before[i] = status.counter;
            if (TIMER_STATE_COUNTING == status.state)
            {
                p_diag->counter_running_mask |= (uint8_t) (1U << i);
            }
        }
        p_diag->gtior[i] = controls[i]->p_reg->GTIOR;
    }

    /* PIDR remains readable while the pins are peripheral outputs.  Repeated
     * samples establish whether each physical MCU pin was observed at both
     * logic levels; this is stronger evidence than the software enable flag. */
    for (uint32_t sample = 0U; sample < 512U; sample++)
    {
        for (uint32_t pin = 0U;
             pin < (sizeof(s_pwm_pins) / sizeof(s_pwm_pins[0])); pin++)
        {
            bsp_io_level_t level = BSP_IO_LEVEL_LOW;
            if (FSP_SUCCESS == R_IOPORT_PinRead(&g_ioport_ctrl,
                                                s_pwm_pins[pin].pin,
                                                &level))
            {
                if (BSP_IO_LEVEL_HIGH == level)
                {
                    p_diag->pin_seen_high_mask |= (uint8_t) (1U << pin);
                }
                else
                {
                    p_diag->pin_seen_low_mask |= (uint8_t) (1U << pin);
                }
            }
        }
    }

    for (uint32_t i = 0U; i < 4U; i++)
    {
        timer_status_t status = {0};
        if (FSP_SUCCESS == R_GPT_StatusGet((timer_ctrl_t *) controls[i],
                                           &status))
        {
            p_diag->counter[i] = status.counter;
            if (status.counter != before[i])
            {
                p_diag->counter_moving_mask |= (uint8_t) (1U << i);
            }
        }
    }
}

uint32_t dab_pwm_hw_phase_get(void)
{
    return s_phase_counts;
}

uint32_t dab_pwm_hw_primary_inner_get(void)
{
    return s_primary_inner_counts;
}

uint32_t dab_pwm_hw_secondary_inner_get(void)
{
    return s_secondary_inner_counts;
}
