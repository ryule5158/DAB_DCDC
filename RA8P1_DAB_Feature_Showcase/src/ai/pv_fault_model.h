#ifndef DAB_PV_FAULT_MODEL_H
#define DAB_PV_FAULT_MODEL_H

#include "model_common.h"

#include <stdint.h>

#define PV_FAULT_FRAME_COUNT (32U)
#define PV_FAULT_CHANNELS    (3U)

AiStatus_t pv_fault_model_begin(
    float const frames[PV_FAULT_FRAME_COUNT][PV_FAULT_CHANNELS]);
AiStatus_t pv_fault_model_poll(uint32_t * p_class_id, float * p_confidence);
AiStatus_t pv_fault_model_infer(
    float const frames[PV_FAULT_FRAME_COUNT][PV_FAULT_CHANNELS],
    uint32_t * p_class_id, float * p_confidence);
char const * pv_fault_model_class_name(uint32_t class_id);

#endif /* DAB_PV_FAULT_MODEL_H */
