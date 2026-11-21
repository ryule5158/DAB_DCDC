#ifndef DAB_PRODUCTION_H
#define DAB_PRODUCTION_H

#include "dab_board.h"
#include "dab_measurements.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Public policy types for the production DAB application.
 *
 * Hardware drivers deliberately do not depend on this file.  This keeps the
 * netlist-verified pin/PWM/ADC layer usable by the isolated commissioning
 * image while the production application owns all energisation policy.
 */
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
    DAB_FAULT_TEMP          = 1UL << 20,
    DAB_FAULT_USB_LINK      = 1UL << 21
} dab_fault_t;

typedef enum e_dab_start_block
{
    DAB_START_OK = 0,
    DAB_START_BLOCK_HARDWARE,
    DAB_START_BLOCK_POEG,
    DAB_START_BLOCK_ADC,
    DAB_START_BLOCK_ADC_STALE,
    DAB_START_BLOCK_VIN,
    DAB_START_BLOCK_CURRENT,
    DAB_START_BLOCK_TEMPERATURE
} dab_start_block_t;

typedef struct st_dab_production_request
{
    float power_limit_w;
    float vref_v;
    float kp_ns_per_v;
    float ki_us_per_vs;
    bool  high_power_unlocked;
} dab_production_request_t;

typedef struct st_dab_production_controller
{
    float integral_counts;
    float phase_counts;
    bool  current_limited;
    bool  power_limited;
} dab_production_controller_t;

typedef struct st_dab_production_control_result
{
    uint32_t phase_counts;
    bool     current_limited;
    bool     power_limited;
} dab_production_control_result_t;

typedef struct st_dab_production_safety_snapshot
{
    dab_board_inputs_t  inputs;
    dab_measurements_t  measurements;
    bool                poeg_ok;
    bool                adc_fresh;
} dab_production_safety_snapshot_t;

void dab_production_controller_reset(dab_production_controller_t * p_controller);

float dab_production_ipri_soft_limit(dab_production_request_t const * p_request);
float dab_production_isec_soft_limit(dab_production_request_t const * p_request);
float dab_production_iin_hard_limit(dab_production_request_t const * p_request,
                                    dab_measurements_t const * p_measurements);
float dab_production_iout_hard_limit(dab_production_request_t const * p_request,
                                     dab_measurements_t const * p_measurements);
uint32_t dab_production_phase_limit(dab_production_request_t const * p_request,
                                    dab_measurements_t const * p_measurements);

uint32_t dab_production_faults_evaluate(
    dab_production_safety_snapshot_t const * p_snapshot,
    dab_production_request_t const * p_request,
    bool evaluate_dynamic_power);

bool dab_production_start_ready(
    dab_production_safety_snapshot_t const * p_snapshot,
    dab_start_block_t * p_block);

void dab_production_control_step(
    dab_production_controller_t * p_controller,
    dab_production_request_t const * p_request,
    dab_measurements_t const * p_measurements,
    uint32_t phase_limit_counts,
    dab_production_control_result_t * p_result);

const char * dab_production_fault_name(uint32_t fault_mask);
const char * dab_production_start_block_name(dab_start_block_t block);

#endif /* DAB_PRODUCTION_H */
