#ifndef DAB_AI_SUPERVISOR_H
#define DAB_AI_SUPERVISOR_H

#include "ai/model_common.h"
#include "ai/ruhmi_bridge.h"
#include "dab_measurements.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct st_dab_ai_status
{
    bool          npu_ready;
    bool          enabled;
    bool          busy;
    ruhmi_model_t active_model;
    AiStatus_t    last_status;
    uint32_t      inference_count;
    uint32_t      error_count;
    bool          mppt_valid;
    uint32_t      mppt_vmpp_mv;
    float         mppt_confidence;
    bool          pv_fault_valid;
    uint32_t      pv_fault_class;
    float         pv_fault_confidence;
    bool          thermal_valid;
    uint32_t      thermal_class;
    float         thermal_confidence;
} dab_ai_status_t;

fsp_err_t dab_ai_supervisor_initialize(void);
void dab_ai_supervisor_enable(bool enabled);
bool dab_ai_supervisor_enabled(void);

bool dab_ai_supervisor_mppt_submit(float const power_scan_w[9]);
void dab_ai_supervisor_service(dab_measurements_t const * p_measurements,
                               bool power_active,
                               bool ambient_valid,
                               float ambient_c,
                               bool fan_on,
                               uint32_t now_ms);

float dab_ai_supervisor_power_cap(float requested_w,
                                  dab_measurements_t const * p_measurements);
void dab_ai_supervisor_status_get(dab_ai_status_t * p_status);

#endif /* DAB_AI_SUPERVISOR_H */
