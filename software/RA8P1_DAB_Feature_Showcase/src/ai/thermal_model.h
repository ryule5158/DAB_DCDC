#ifndef DAB_THERMAL_MODEL_H
#define DAB_THERMAL_MODEL_H

#include "model_common.h"

#include <stdint.h>

#define THERMAL_INPUT_ELEMENTS (8U)

typedef enum e_thermal_input
{
    THERMAL_INPUT_TPRI = 0,
    THERMAL_INPUT_TSEC,
    THERMAL_INPUT_TAMB,
    THERMAL_INPUT_DTPRI,
    THERMAL_INPUT_DTSEC,
    THERMAL_INPUT_DTAMB,
    THERMAL_INPUT_PIN,
    THERMAL_INPUT_FAN
} thermal_input_t;

AiStatus_t thermal_model_begin(float const sensors[THERMAL_INPUT_ELEMENTS]);
AiStatus_t thermal_model_poll(uint32_t * p_class_id, float * p_confidence);
AiStatus_t thermal_model_infer(float const sensors[THERMAL_INPUT_ELEMENTS],
                               uint32_t * p_class_id, float * p_confidence);
char const * thermal_model_class_name(uint32_t class_id);

#endif /* DAB_THERMAL_MODEL_H */
