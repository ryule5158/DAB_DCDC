#ifndef DAB_AI_MODEL_COMMON_H
#define DAB_AI_MODEL_COMMON_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

typedef enum e_ai_status
{
    AI_MODEL_STAT_OK = 0,
    AI_STAT_ERR_INPUT_OOR,
    AI_STAT_ERR_INFER_RUNTIME,
    AI_STAT_ERR_LOW_CONFIDENCE,
    AI_STAT_BUSY,
    AI_STAT_NOT_READY,
    AI_STAT_PV_FAULT_ALARM,
    AI_STAT_THERMAL_DERATE
} AiStatus_t;

#define AI_CONFIDENCE_THRESHOLD (0.70F)

static inline int8_t ai_quantize_i8(float normalized, float scale,
                                    int32_t zero_point)
{
    float const quantized = normalized / scale + (float) zero_point;
    long rounded = lroundf(quantized);
    if (rounded > 127L)
    {
        rounded = 127L;
    }
    else if (rounded < -128L)
    {
        rounded = -128L;
    }
    return (int8_t) rounded;
}

static inline float ai_dequantize_i8(int8_t value, float scale,
                                     int32_t zero_point)
{
    return ((float) value - (float) zero_point) * scale;
}

#endif /* DAB_AI_MODEL_COMMON_H */
