#ifndef DAB_RUHMI_BRIDGE_H
#define DAB_RUHMI_BRIDGE_H

#include "hal_data.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum e_ruhmi_model
{
    RUHMI_MODEL_NONE = 0,
    RUHMI_MODEL_MPPT,
    RUHMI_MODEL_PV_FAULT,
    RUHMI_MODEL_THERMAL
} ruhmi_model_t;

fsp_err_t ruhmi_bridge_open(void);
bool      ruhmi_bridge_ready(void);
bool      ruhmi_bridge_busy(void);
ruhmi_model_t ruhmi_bridge_active_model(void);

int ruhmi_bridge_start(ruhmi_model_t model, bool clean_outputs);
int ruhmi_bridge_poll(bool block);

#endif /* DAB_RUHMI_BRIDGE_H */
