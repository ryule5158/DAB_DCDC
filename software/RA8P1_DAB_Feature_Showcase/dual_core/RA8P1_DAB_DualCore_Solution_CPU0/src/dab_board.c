#include "dab_board.h"

/*
 * Fixed, netlist-verified CN2 contract (2026-07-22 hardware):
 *   PB00 CN2.97 IN_PRECHARGE_EN     PB02 CN2.99 IN_MAIN_EN
 *   P706 CN2.45 OUT_PROT_EN         PB03 CN2.47 PROT_RESET_N
 *   P707 CN2.57 RUN_REQ             P402 CN2.75 status LED
 *   P401 CN2.59/61 RAW_FAULT_N wired-OR and POEG0 GTETRGA
 *   P400 CN2.79 IPRI_OC_N           P403 CN2.55 ISEC_OC_N
 *   PB01 CN2.49 SEC_HW_OK_CTRL      PB06 CN2.95 PRI_HW_OK_CTRL
 *   PB04 CN2.113 FAN                PB05 CN2.115 TEMP_AMB_OT_N
 *   P513 CN2.22 ETH_PHY_RESET_N
 */
#define PIN_IN_PRECHARGE_EN    BSP_IO_PORT_11_PIN_00
#define PIN_SEC_HW_OK          BSP_IO_PORT_11_PIN_01
#define PIN_IN_MAIN_EN         BSP_IO_PORT_11_PIN_02
#define PIN_PROT_RESET_N       BSP_IO_PORT_11_PIN_03
#define PIN_FAN_EN             BSP_IO_PORT_11_PIN_04
#define PIN_PRI_HW_OK          BSP_IO_PORT_11_PIN_06
#define PIN_TEMP_AMB_OT_N      BSP_IO_PORT_11_PIN_05
#define PIN_OUT_PROT_EN        BSP_IO_PORT_07_PIN_06
#define PIN_RUN_REQ            BSP_IO_PORT_07_PIN_07
#define PIN_IPRI_OC_N          BSP_IO_PORT_04_PIN_00
#define PIN_RAW_FAULT_N        BSP_IO_PORT_04_PIN_01
#define PIN_DAB_STATUS_LED     BSP_IO_PORT_04_PIN_02
#define PIN_ISEC_OC_N          BSP_IO_PORT_04_PIN_03
#define PIN_USER_LED           BSP_IO_PORT_01_PIN_10
#define PIN_ETH_PHY_RESET_N    BSP_IO_PORT_05_PIN_13

static void dab_board_pin_write(bsp_io_port_pin_t pin, bool high)
{
    (void) R_IOPORT_PinWrite(&g_ioport_ctrl, pin,
                             high ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
}

static bool dab_board_pin_read(bsp_io_port_pin_t pin)
{
    bsp_io_level_t level = BSP_IO_LEVEL_LOW;
    (void) R_IOPORT_PinRead(&g_ioport_ctrl, pin, &level);
    return BSP_IO_LEVEL_HIGH == level;
}

void dab_board_initialize_safe(void)
{
    dab_board_force_path_safe();
    dab_board_fan_set(false);
    dab_board_status_led_set(false);
    dab_board_user_led_set(false);
    dab_board_phy_reset_set(false);
}

void dab_board_force_path_safe(void)
{
    dab_board_run_request_set(false);
    dab_board_output_protection_set(false);
    dab_board_input_main_set(false);
    dab_board_input_precharge_set(false);
    dab_board_protection_reset_set(false);
}

void dab_board_run_request_set(bool enable)
{
    dab_board_pin_write(PIN_RUN_REQ, enable);
}

void dab_board_protection_reset_set(bool released)
{
    dab_board_pin_write(PIN_PROT_RESET_N, released);
}

void dab_board_input_precharge_set(bool enable)
{
    dab_board_pin_write(PIN_IN_PRECHARGE_EN, enable);
}

void dab_board_input_main_set(bool enable)
{
    dab_board_pin_write(PIN_IN_MAIN_EN, enable);
}

void dab_board_output_protection_set(bool enable)
{
    dab_board_pin_write(PIN_OUT_PROT_EN, enable);
}

void dab_board_fan_set(bool enable)
{
    dab_board_pin_write(PIN_FAN_EN, enable);
}

void dab_board_status_led_set(bool enable)
{
    dab_board_pin_write(PIN_DAB_STATUS_LED, enable);
}

void dab_board_user_led_set(bool enable)
{
    dab_board_pin_write(PIN_USER_LED, enable);
}

void dab_board_phy_reset_set(bool released)
{
    dab_board_pin_write(PIN_ETH_PHY_RESET_N, released);
}

void dab_board_inputs_get(dab_board_inputs_t * p_inputs)
{
    if (NULL == p_inputs)
    {
        return;
    }
    p_inputs->raw_fault_n = dab_board_pin_read(PIN_RAW_FAULT_N);
    p_inputs->ipri_oc_n   = dab_board_pin_read(PIN_IPRI_OC_N);
    p_inputs->isec_oc_n   = dab_board_pin_read(PIN_ISEC_OC_N);
    p_inputs->pri_hw_ok   = dab_board_pin_read(PIN_PRI_HW_OK);
    p_inputs->sec_hw_ok   = dab_board_pin_read(PIN_SEC_HW_OK);
    p_inputs->temp_amb_ot_n = dab_board_pin_read(PIN_TEMP_AMB_OT_N);
}

void dab_board_outputs_get(dab_board_outputs_t * p_outputs)
{
    if (NULL == p_outputs)
    {
        return;
    }
    p_outputs->input_precharge_en = dab_board_pin_read(PIN_IN_PRECHARGE_EN);
    p_outputs->input_main_en = dab_board_pin_read(PIN_IN_MAIN_EN);
    p_outputs->output_protection_en = dab_board_pin_read(PIN_OUT_PROT_EN);
    p_outputs->protection_reset_n = dab_board_pin_read(PIN_PROT_RESET_N);
    p_outputs->run_request = dab_board_pin_read(PIN_RUN_REQ);
}

bool dab_board_fast_faults_clear(void)
{
    return dab_board_pin_read(PIN_RAW_FAULT_N) &&
           dab_board_pin_read(PIN_IPRI_OC_N) &&
           dab_board_pin_read(PIN_ISEC_OC_N);
}
