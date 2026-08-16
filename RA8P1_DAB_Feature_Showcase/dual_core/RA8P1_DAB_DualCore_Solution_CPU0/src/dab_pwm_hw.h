#ifndef DAB_PWM_HW_H
#define DAB_PWM_HW_H

#include "hal_data.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct st_dab_pwm_hw_diag
{
    bool     outputs_requested;
    bool     poeg_ok;
    uint8_t  counter_running_mask;
    uint8_t  counter_moving_mask;
    uint8_t  pin_seen_high_mask;
    uint8_t  pin_seen_low_mask;
    uint32_t counter[4];
    uint32_t gtior[4];
} dab_pwm_hw_diag_t;

/*
 * Opens POEG0 and GPT0/1/5/8, configures the tested 300 kHz waveform, and
 * leaves all eight physical pins owned by GPIO LOW.
 */
fsp_err_t dab_pwm_hw_open(void);

/* Starts synchronized GPT counters while the eight pins remain GPIO LOW. */
fsp_err_t dab_pwm_hw_clock_start(void);

/* Muxes/unmuxes the fixed PWM pins. RUN_REQ is deliberately not changed. */
bool dab_pwm_hw_outputs_enable(void);
void dab_pwm_hw_outputs_disable(void);

/*
 * Production primary full-bridge waveform on Q1..Q4 only.  The primary uses
 * the same GPT edge schedule and dead time as normal power transfer; all four
 * secondary inputs stay forced GPIO LOW.
 */
bool dab_pwm_hw_primary_outputs_enable(void);

/*
 * Primary half-bridge diagnosis.  bridge_index 0 exposes only Q1/Q2 (PRI A)
 * and bridge_index 1 exposes only Q3/Q4 (PRI B); the other six gates remain
 * forced GPIO LOW.  The selected pair keeps the production dead time.
 */
bool dab_pwm_hw_primary_half_bridge_outputs_enable(uint32_t bridge_index);

/*
 * No-power gate diagnosis: leaves GPT disconnected, forces seven PWM pins LOW,
 * and drives exactly one selected input HIGH.  RUN_REQ is controlled by the
 * caller and must be dropped before/after use.
 */
bool dab_pwm_hw_single_gate_set(uint32_t gate_index);

/* Buffered update; positive phase means the secondary bridge lags primary. */
bool dab_pwm_hw_phase_set(uint32_t phase_counts);
bool dab_pwm_hw_modulation_set(uint32_t phase_counts,
                               uint32_t primary_inner_counts,
                               uint32_t secondary_inner_counts);

void     dab_pwm_hw_shutdown(void);
bool     dab_pwm_hw_poeg_clear(void);
bool     dab_pwm_hw_poeg_ok(void);
bool     dab_pwm_hw_is_open(void);
bool     dab_pwm_hw_outputs_are_enabled(void);
void     dab_pwm_hw_diag_get(dab_pwm_hw_diag_t * p_diag);
uint32_t dab_pwm_hw_phase_get(void);
uint32_t dab_pwm_hw_primary_inner_get(void);
uint32_t dab_pwm_hw_secondary_inner_get(void);

#endif /* DAB_PWM_HW_H */
