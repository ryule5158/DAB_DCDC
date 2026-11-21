#include "dab_ra8p1_scope_test.h"

#include "dab_board.h"
#include "dab_production_config.h"
#include "dab_pwm_hw.h"

#include <stdio.h>

static const uint32_t s_phase_presets[] =
{
    0U,   /* 0 ns */
    40U,  /* 160 ns */
    80U,  /* 320 ns */
};

static uint32_t s_phase_index = 1U;

static bool scope_phase_set(uint32_t phase_counts)
{
    /* A running GPT can be inside the deliberately blocked late-update
     * window.  A few bounded retries cross at most one 3.336 us period. */
    for (uint32_t attempt = 0U; attempt < 32U; attempt++)
    {
        if (dab_pwm_hw_phase_set(phase_counts))
        {
            return true;
        }
    }
    return false;
}

static void scope_print_pins(void)
{
    printf("[TEST][PIN] PRI_A_H GPT0A P415 -> CN2.83\r\n");
    printf("[TEST][PIN] PRI_A_L GPT0B P414 -> CN2.81\r\n");
    printf("[TEST][PIN] PRI_B_H GPT1A P105 -> CN2.87\r\n");
    printf("[TEST][PIN] PRI_B_L GPT1B P104 -> CN2.85\r\n");
    printf("[TEST][PIN] SEC_A_H GPT5A P915 -> CN2.35\r\n");
    printf("[TEST][PIN] SEC_A_L GPT5B P914 -> CN2.37\r\n");
    printf("[TEST][PIN] SEC_B_H GPT8A P605 -> CN2.39\r\n");
    printf("[TEST][PIN] SEC_B_L GPT8B P604 -> CN2.41\r\n");
    printf("[TEST][PIN] UART9 RX/TX P208/P209 -> 115200-8N1\r\n");
}

static void scope_print_status(void)
{
    uint32_t const phase_counts = dab_pwm_hw_phase_get();
    uint32_t const phase_ns = phase_counts * DAB_PWM_COUNT_NS;
    uint32_t const phase_mdeg =
        (phase_counts * 360000UL) / DAB_PWM_PERIOD_COUNTS;

    printf("[TEST][PWM] %s target=%luHz actual=%luHz period=%lucnt/%luns "
           "dead=%lucnt/%luns phase=%lucnt/%luns/%lu.%03ludeg RUN_REQ=LOW\r\n",
           dab_pwm_hw_outputs_are_enabled() ? "ON" : "OFF",
           (unsigned long) DAB_PWM_TARGET_HZ,
           (unsigned long) DAB_PWM_ACTUAL_HZ,
           (unsigned long) DAB_PWM_PERIOD_COUNTS,
           (unsigned long) (DAB_PWM_PERIOD_COUNTS * DAB_PWM_COUNT_NS),
           (unsigned long) DAB_DEADTIME_COUNTS,
           (unsigned long) DAB_DEADTIME_NS,
           (unsigned long) phase_counts,
           (unsigned long) phase_ns,
           (unsigned long) (phase_mdeg / 1000UL),
           (unsigned long) (phase_mdeg % 1000UL));
}

static void scope_print_help(void)
{
    printf("[TEST][HELP] 1=PWM ON, 0=PWM OFF, p=next phase, "
           "+/-=phase +/-1 count, s=status, i=pins, h=help\r\n");
    printf("[TEST][SAFE] RUN_REQ/PRE/MAIN/OUT stay LOW; x exits and forces all PWM pins LOW\r\n");
}

void dab_ra8p1_scope_test_enter(void)
{
    dab_board_force_path_safe();
    dab_pwm_hw_outputs_disable();
    s_phase_index = 1U;
    (void) scope_phase_set(s_phase_presets[s_phase_index]);

    printf("\r\n[TEST][ENTER] Isolated eight-PWM scope mode; ADC-DMA continues\r\n");
    scope_print_pins();
    scope_print_status();
    scope_print_help();
}

bool dab_ra8p1_scope_test_process_char(uint8_t ch)
{
    uint32_t phase = dab_pwm_hw_phase_get();

    switch (ch)
    {
        case 'x':
        case 'X':
            dab_ra8p1_scope_test_exit();
            return false;

        case '1':
            dab_board_force_path_safe();
            dab_pwm_hw_outputs_disable();
            if (scope_phase_set(phase) && dab_pwm_hw_poeg_clear() &&
                dab_pwm_hw_outputs_enable())
            {
                printf("[TEST][CMD] PWM ON; RUN_REQ remains LOW\r\n");
            }
            else
            {
                dab_pwm_hw_outputs_disable();
                printf("[TEST][CMD] PWM ON FAIL; check RAW_FAULT_N/POEG\r\n");
            }
            scope_print_status();
            break;

        case '0':
            dab_pwm_hw_outputs_disable();
            printf("[TEST][CMD] PWM OFF; all eight pins forced LOW\r\n");
            break;

        case 'p':
        case 'P':
            s_phase_index = (s_phase_index + 1U) %
                            (sizeof(s_phase_presets) / sizeof(s_phase_presets[0]));
            (void) scope_phase_set(s_phase_presets[s_phase_index]);
            scope_print_status();
            break;

        case '+':
            if (phase < DAB_PHASE_NORMAL_MAX_COUNTS)
            {
                (void) scope_phase_set(phase + 1U);
            }
            scope_print_status();
            break;

        case '-':
            if (phase > 0U)
            {
                (void) scope_phase_set(phase - 1U);
            }
            scope_print_status();
            break;

        case 's':
        case 'S':
            scope_print_status();
            break;

        case 'i':
        case 'I':
            scope_print_pins();
            break;

        case 'h':
        case 'H':
        case '?':
            scope_print_help();
            break;

        case '\r':
        case '\n':
        case ' ':
        case '\t':
            break;

        default:
            printf("[TEST][CMD] unknown '%c'; send h\r\n", (char) ch);
            break;
    }

    return true;
}

void dab_ra8p1_scope_test_exit(void)
{
    dab_pwm_hw_outputs_disable();
    dab_board_force_path_safe();
    printf("[TEST][EXIT] PWM OFF; all eight PWM pins LOW; ADC measurement continues\r\n");
}
