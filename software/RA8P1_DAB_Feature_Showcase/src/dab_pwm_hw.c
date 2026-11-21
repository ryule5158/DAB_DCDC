#include "dab_pwm_hw.h"

#include "dab_accel.h"
#include "dab_production_config.h"

#include <stddef.h>

#define DAB_GPT_CHANNEL_MASK   ((1UL << 0) | (1UL << 1) | (1UL << 5) | (1UL << 8))
#define DAB_PHASE_UPDATE_LATEST_COUNT  (600UL)

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

static void dab_pwm_hw_pins_gpt(void)
{
    uint32_t const cfg = IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_GPT1;
    for (uint32_t i = 0U; i < (sizeof(s_pwm_pins) / sizeof(s_pwm_pins[0])); i++)
    {
        (void) R_IOPORT_PinCfg(&g_ioport_ctrl, s_pwm_pins[i].pin, cfg);
    }
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

static bool dab_pwm_hw_all_edges_set(uint32_t phase_counts, bool force_push)
{
    return dab_pwm_hw_half_bridge_set(&g_pwm_pri_a_ctrl, 0U, true, force_push) &&
           dab_pwm_hw_half_bridge_set(&g_pwm_pri_b_ctrl, 0U, false, force_push) &&
           dab_pwm_hw_half_bridge_set(&g_pwm_sec_a_ctrl, phase_counts, true, force_push) &&
           dab_pwm_hw_half_bridge_set(&g_pwm_sec_b_ctrl, phase_counts, false, force_push);
}

fsp_err_t dab_pwm_hw_open(void)
{
    if (s_open)
    {
        return FSP_ERR_ALREADY_OPEN;
    }

    dab_pwm_hw_pins_gpio_low();
    fsp_err_t err = R_POEG_Open(&g_poeg0_ctrl, &g_poeg0_cfg);
    if (FSP_SUCCESS != err)
    {
        return err;
    }
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
    if (!dab_pwm_hw_all_edges_set(0U, true))
    {
        return FSP_ERR_INVALID_STATE;
    }
    s_phase_counts = 0U;
    return FSP_SUCCESS;
}

fsp_err_t dab_pwm_hw_clock_start(void)
{
    if (!s_open)
    {
        return FSP_ERR_NOT_OPEN;
    }
    if (!dab_pwm_hw_poeg_clear())
    {
        return FSP_ERR_INVALID_STATE;
    }
    dab_pwm_hw_pins_gpio_low();
    g_pwm_pri_a_ctrl.p_reg->GTCLR = DAB_GPT_CHANNEL_MASK;
    g_pwm_pri_a_ctrl.p_reg->GTSTR = DAB_GPT_CHANNEL_MASK;
    s_clock_running = true;
    return FSP_SUCCESS;
}

bool dab_pwm_hw_outputs_enable(void)
{
    if ((!s_open) || (!s_clock_running) || (!dab_pwm_hw_poeg_ok()))
    {
        return false;
    }

    /* Preserve the phase prepared while pins were safely GPIO LOW.  The
     * production sequencer always prepares zero; scope mode prepares its
     * selected test phase before muxing any pin to GPT. */
    if (!dab_pwm_hw_phase_set(s_phase_counts))
    {
        return false;
    }
    dab_pwm_hw_pins_gpt();
    s_outputs_enabled = true;
    return true;
}

DAB_ITCM_CODE void dab_pwm_hw_outputs_disable(void)
{
    dab_pwm_hw_pins_gpio_low();
    s_outputs_enabled = false;
    s_phase_counts    = 0U;
    if (s_open)
    {
        (void) dab_pwm_hw_all_edges_set(0U, true);
    }
}

DAB_ITCM_CODE bool dab_pwm_hw_phase_set(uint32_t phase_counts)
{
    if ((!s_open) || (phase_counts > DAB_PHASE_ABSOLUTE_MAX_COUNTS))
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
    bool const ok =
        dab_pwm_hw_half_bridge_set(&g_pwm_sec_a_ctrl, phase_counts, true,
                                   !running_update) &&
        dab_pwm_hw_half_bridge_set(&g_pwm_sec_b_ctrl, phase_counts, false,
                                   !running_update);
    FSP_CRITICAL_SECTION_EXIT;
    if (ok)
    {
        s_phase_counts = phase_counts;
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
    if (FSP_SUCCESS != R_POEG_Reset(&g_poeg0_ctrl))
    {
        return false;
    }
    return dab_pwm_hw_poeg_ok();
}

bool dab_pwm_hw_poeg_ok(void)
{
    if (!s_open)
    {
        return false;
    }
    poeg_status_t status = {.state = POEG_STATE_NO_DISABLE_REQUEST};
    return (FSP_SUCCESS == R_POEG_StatusGet(&g_poeg0_ctrl, &status)) &&
           (POEG_STATE_NO_DISABLE_REQUEST == status.state);
}

bool dab_pwm_hw_is_open(void)
{
    return s_open;
}

bool dab_pwm_hw_outputs_are_enabled(void)
{
    return s_outputs_enabled;
}

uint32_t dab_pwm_hw_phase_get(void)
{
    return s_phase_counts;
}
