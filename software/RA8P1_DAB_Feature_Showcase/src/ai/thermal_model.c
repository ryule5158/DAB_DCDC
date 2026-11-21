#include "thermal_model.h"

#include "ruhmi_bridge.h"
#include "generated/thermal/thermal_model.h"

#include <stdbool.h>

#define THERMAL_INPUT_SCALE  (0.003921568859368563F)
#define THERMAL_INPUT_ZP     (-128)
#define THERMAL_OUTPUT_SCALE (0.00390625F)
#define THERMAL_OUTPUT_ZP    (-128)

static float const s_minimum[THERMAL_INPUT_ELEMENTS] =
    {-20.0F, -20.0F, -20.0F, -10.0F, -10.0F, -10.0F, 0.0F, 0.0F};
static float const s_maximum[THERMAL_INPUT_ELEMENTS] =
    {150.0F, 150.0F, 100.0F, 10.0F, 10.0F, 10.0F, 500.0F, 1.0F};

static AiStatus_t thermal_decode(uint32_t * p_class_id,
                                 float * p_confidence)
{
    if ((NULL == p_class_id) || (NULL == p_confidence))
    {
        return AI_STAT_ERR_INFER_RUNTIME;
    }

    int8_t const * p_output =
        thermal_GetModelOutputPtr_StatefulPartitionedCall_0_70014();
    uint32_t best = 0U;
    float best_confidence = -1.0F;
    for (uint32_t i = 0U; i < 3U; ++i)
    {
        float const confidence = ai_dequantize_i8(
            p_output[i], THERMAL_OUTPUT_SCALE, THERMAL_OUTPUT_ZP);
        if (confidence > best_confidence)
        {
            best_confidence = confidence;
            best = i;
        }
    }
    *p_class_id = best;
    *p_confidence = best_confidence;
    if (best_confidence < AI_CONFIDENCE_THRESHOLD)
    {
        return AI_STAT_ERR_LOW_CONFIDENCE;
    }
    return (2U == best) ? AI_STAT_THERMAL_DERATE : AI_MODEL_STAT_OK;
}

AiStatus_t thermal_model_begin(float const sensors[THERMAL_INPUT_ELEMENTS])
{
    if ((NULL == sensors) || !ruhmi_bridge_ready())
    {
        return AI_STAT_NOT_READY;
    }
    if (ruhmi_bridge_busy())
    {
        return AI_STAT_BUSY;
    }

    int8_t * p_input =
        thermal_GetModelInputPtr_serving_default_thermal_input_0();
    for (uint32_t i = 0U; i < THERMAL_INPUT_ELEMENTS; ++i)
    {
        float const value = sensors[i];
        if (!isfinite(value) || (value < s_minimum[i]) ||
            (value > s_maximum[i]))
        {
            return AI_STAT_ERR_INPUT_OOR;
        }
        float const normalized =
            (value - s_minimum[i]) / (s_maximum[i] - s_minimum[i]);
        p_input[i] = ai_quantize_i8(normalized, THERMAL_INPUT_SCALE,
                                    THERMAL_INPUT_ZP);
    }

    return (0 == ruhmi_bridge_start(RUHMI_MODEL_THERMAL, false)) ?
           AI_MODEL_STAT_OK : AI_STAT_ERR_INFER_RUNTIME;
}

AiStatus_t thermal_model_poll(uint32_t * p_class_id, float * p_confidence)
{
    if (RUHMI_MODEL_THERMAL != ruhmi_bridge_active_model())
    {
        return AI_STAT_NOT_READY;
    }
    int const result = ruhmi_bridge_poll(false);
    if (result > 0)
    {
        return AI_STAT_BUSY;
    }
    if (result < 0)
    {
        return AI_STAT_ERR_INFER_RUNTIME;
    }
    return thermal_decode(p_class_id, p_confidence);
}

AiStatus_t thermal_model_infer(float const sensors[THERMAL_INPUT_ELEMENTS],
                               uint32_t * p_class_id, float * p_confidence)
{
    AiStatus_t const start = thermal_model_begin(sensors);
    if (AI_MODEL_STAT_OK != start)
    {
        return start;
    }
    if (0 != ruhmi_bridge_poll(true))
    {
        return AI_STAT_ERR_INFER_RUNTIME;
    }
    return thermal_decode(p_class_id, p_confidence);
}

char const * thermal_model_class_name(uint32_t class_id)
{
    static char const * const names[] = {"safe", "warning", "derate"};
    return (class_id < 3U) ? names[class_id] : "unknown";
}
