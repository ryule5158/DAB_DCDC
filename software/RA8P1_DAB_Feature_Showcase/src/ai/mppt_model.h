#ifndef DAB_MPPT_MODEL_H
#define DAB_MPPT_MODEL_H

#include "model_common.h"

#include <stdint.h>

#define MPPT_INPUT_ELEMENTS   (9U)
#define MPPT_OUTPUT_CLASSES   (13U)
#define MPPT_POWER_MIN_W      (0.0F)
#define MPPT_POWER_MAX_W      (555.6F)
#define MPPT_VOLTAGE_MIN_MV   (19000UL)
#define MPPT_VOLTAGE_STEP_MV  (1000UL)

AiStatus_t mppt_model_begin(float const power_w[MPPT_INPUT_ELEMENTS]);
AiStatus_t mppt_model_poll(uint32_t * p_vmpp_mv, float * p_confidence);
AiStatus_t mppt_model_infer(float const power_w[MPPT_INPUT_ELEMENTS],
                            uint32_t * p_vmpp_mv, float * p_confidence);

#endif /* DAB_MPPT_MODEL_H */
