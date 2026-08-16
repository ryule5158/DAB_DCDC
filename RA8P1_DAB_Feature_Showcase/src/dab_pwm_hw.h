#ifndef DAB_PWM_HW_H
#define DAB_PWM_HW_H

#include "hal_data.h"

#include <stdbool.h>
#include <stdint.h>

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

/* Buffered update; positive phase means the secondary bridge lags primary. */
bool dab_pwm_hw_phase_set(uint32_t phase_counts);

void     dab_pwm_hw_shutdown(void);
bool     dab_pwm_hw_poeg_clear(void);
bool     dab_pwm_hw_poeg_ok(void);
bool     dab_pwm_hw_is_open(void);
bool     dab_pwm_hw_outputs_are_enabled(void);
uint32_t dab_pwm_hw_phase_get(void);

#endif /* DAB_PWM_HW_H */
