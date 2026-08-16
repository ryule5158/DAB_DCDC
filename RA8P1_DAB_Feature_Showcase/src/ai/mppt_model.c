#include "mppt_model.h"

#include "ruhmi_bridge.h"
#include "generated/mppt/mppt_model.h"

#include <stdbool.h>

#define MPPT_INPUT_SCALE  (0.002124353311955929F)
#define MPPT_INPUT_ZP     (-128)
#define MPPT_OUTPUT_SCALE (0.00390625F)
#define MPPT_OUTPUT_ZP    (-128)

static AiStatus_t mppt_decode(uint32_t * p_vmpp_mv, float * p_confidence)
{
    if ((NULL == p_vmpp_mv) || (NULL == p_confidence))
    {
        return AI_STAT_ERR_INFER_RUNTIME;
    }

    int8_t const * p_output =
        mppt_GetModelOutputPtr_StatefulPartitionedCall_0_70011();
    uint32_t best = 0U;
    float best_confidence = -1.0F;
    for (uint32_t i = 0U; i < MPPT_OUTPUT_CLASSES; ++i)
    {
        float const confidence =
            ai_dequantize_i8(p_output[i], MPPT_OUTPUT_SCALE, MPPT_OUTPUT_ZP);
        if (confidence > best_confidence)
        {
            best_confidence = confidence;
            best = i;
        }
    }

    *p_confidence = best_confidence;
    *p_vmpp_mv = MPPT_VOLTAGE_MIN_MV + best * MPPT_VOLTAGE_STEP_MV;
    return (best_confidence >= AI_CONFIDENCE_THRESHOLD) ?
           AI_MODEL_STAT_OK : AI_STAT_ERR_LOW_CONFIDENCE;
}

AiStatus_t mppt_model_begin(float const power_w[MPPT_INPUT_ELEMENTS])
{
    if ((NULL == power_w) || !ruhmi_bridge_ready())
    {
        return AI_STAT_NOT_READY;
    }
    if (ruhmi_bridge_busy())
    {
        return AI_STAT_BUSY;
    }

    int8_t * p_input =
        mppt_GetModelInputPtr_serving_default_power_scan_0();
    for (uint32_t i = 0U; i < MPPT_INPUT_ELEMENTS; ++i)
    {
        if (!isfinite(power_w[i]) || (power_w[i] < MPPT_POWER_MIN_W) ||
            (power_w[i] > MPPT_POWER_MAX_W))
        {
            return AI_STAT_ERR_INPUT_OOR;
        }
        float const normalized = power_w[i] / MPPT_POWER_MAX_W;
        p_input[i] = ai_quantize_i8(normalized, MPPT_INPUT_SCALE,
                                    MPPT_INPUT_ZP);
    }

    return (0 == ruhmi_bridge_start(RUHMI_MODEL_MPPT, false)) ?
           AI_MODEL_STAT_OK : AI_STAT_ERR_INFER_RUNTIME;
}

AiStatus_t mppt_model_poll(uint32_t * p_vmpp_mv, float * p_confidence)
{
    if (RUHMI_MODEL_MPPT != ruhmi_bridge_active_model())
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
    return mppt_decode(p_vmpp_mv, p_confidence);
}

AiStatus_t mppt_model_infer(float const power_w[MPPT_INPUT_ELEMENTS],
                            uint32_t * p_vmpp_mv, float * p_confidence)
{
    AiStatus_t const start = mppt_model_begin(power_w);
    if (AI_MODEL_STAT_OK != start)
    {
        return start;
    }
    if (0 != ruhmi_bridge_poll(true))
    {
        return AI_STAT_ERR_INFER_RUNTIME;
    }
    return mppt_decode(p_vmpp_mv, p_confidence);
}
