#ifndef DAB_BOARD_H
#define DAB_BOARD_H

#include "hal_data.h"

#include <stdbool.h>

typedef struct st_dab_board_inputs
{
    bool raw_fault_n;
    bool ipri_oc_n;
    bool isec_oc_n;
    bool pri_hw_ok;
    bool sec_hw_ok;
    bool temp_amb_ot_n;
} dab_board_inputs_t;

typedef struct st_dab_board_outputs
{
    bool input_precharge_en;
    bool input_main_en;
    bool output_protection_en;
    bool protection_reset_n;
    bool run_request;
} dab_board_outputs_t;

/* Sets RUN_REQ, all power-path controls, reset, fan, and LEDs LOW. */
void dab_board_initialize_safe(void);

/* Fail-closed order: RUN_REQ first, then every power-path control. */
void dab_board_force_path_safe(void);

void dab_board_run_request_set(bool enable);
void dab_board_protection_reset_set(bool released);
void dab_board_input_precharge_set(bool enable);
void dab_board_input_main_set(bool enable);
void dab_board_output_protection_set(bool enable);
void dab_board_fan_set(bool enable);
void dab_board_status_led_set(bool enable);
void dab_board_user_led_set(bool enable);
void dab_board_phy_reset_set(bool released);

void dab_board_inputs_get(dab_board_inputs_t * p_inputs);
void dab_board_outputs_get(dab_board_outputs_t * p_outputs);
bool dab_board_fast_faults_clear(void);

#endif /* DAB_BOARD_H */
