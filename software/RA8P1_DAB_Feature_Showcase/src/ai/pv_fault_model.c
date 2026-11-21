#include "pv_fault_model.h"

#include "ruhmi_bridge.h"
#include "generated/pv_fault/pv_fault_model.h"

#include <stdbool.h>

#define PV_INPUT_SCALE (0.007843137718737125F)
#define PV_INPUT_ZP    (0)

static float const s_minimum[PV_FAULT_CHANNELS] = {18.0F, 0.0F, 0.0F};
static float const s_maximum[PV_FAULT_CHANNELS] = {32.0F, 31.0F, 555.6F};

static AiStatus_t pv_fault_decode(uint32_t * p_class_id,
                                  float * p_confidence)
{
    if ((NULL == p_class_id) || (NULL == p_confidence))
    {
        return AI_STAT_ERR_INFER_RUNTIME;
    }

    float const * p_output =
        pv_fault_GetModelOutputPtr_StatefulPartitionedCall_0_70012();
    uint32_t best = 0U;
    float best_confidence = -1.0F;
    for (uint32_t i = 0U; i < 3U; ++i)
    {
        if (p_output[i] > best_confidence)
        {
            best_confidence = p_output[i];
            best = i;
        }
    }
    *p_class_id = best;
    *p_confidence = best_confidence;
    if (best_confidence < AI_CONFIDENCE_THRESHOLD)
    {
        return AI_STAT_ERR_LOW_CONFIDENCE;
    }
    return (0U == best) ? AI_MODEL_STAT_OK : AI_STAT_PV_FAULT_ALARM;
}

AiStatus_t pv_fault_model_begin(
    float const frames[PV_FAULT_FRAME_COUNT][PV_FAULT_CHANNELS])
{
    if ((NULL == frames) || !ruhmi_bridge_ready())
    {
        return AI_STAT_NOT_READY;
    }
    if (ruhmi_bridge_busy())
    {
        return AI_STAT_BUSY;
    }

    int8_t * p_input =
        pv_fault_GetModelInputPtr_serving_default_input_1_0();
    uint32_t index = 0U;
    for (uint32_t frame = 0U; frame < PV_FAULT_FRAME_COUNT; ++frame)
    {
        for (uint32_t channel = 0U; channel < PV_FAULT_CHANNELS; ++channel)
        {
            float const value = frames[frame][channel];
            if (!isfinite(value) || (value < s_minimum[channel]) ||
                (value > s_maximum[channel]))
            {
                return AI_STAT_ERR_INPUT_OOR;
            }
            float const normalized =
                2.0F * (value - s_minimum[channel]) /
                (s_maximum[channel] - s_minimum[channel]) - 1.0F;
            p_input[index++] =
                ai_quantize_i8(normalized, PV_INPUT_SCALE, PV_INPUT_ZP);
        }
    }

    return (0 == ruhmi_bridge_start(RUHMI_MODEL_PV_FAULT, false)) ?
           AI_MODEL_STAT_OK : AI_STAT_ERR_INFER_RUNTIME;
}

AiStatus_t pv_fault_model_poll(uint32_t * p_class_id, float * p_confidence)
{
    if (RUHMI_MODEL_PV_FAULT != ruhmi_bridge_active_model())
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
    return pv_fault_decode(p_class_id, p_confidence);
}

AiStatus_t pv_fault_model_infer(
    float const frames[PV_FAULT_FRAME_COUNT][PV_FAULT_CHANNELS],
    uint32_t * p_class_id, float * p_confidence)
{
    AiStatus_t const start = pv_fault_model_begin(frames);
    if (AI_MODEL_STAT_OK != start)
    {
        return start;
    }
    if (0 != ruhmi_bridge_poll(true))
    {
        return AI_STAT_ERR_INFER_RUNTIME;
    }
    return pv_fault_decode(p_class_id, p_confidence);
}

char const * pv_fault_model_class_name(uint32_t class_id)
{
    static char const * const names[] = {"normal", "power-sag", "ripple"};
    return (class_id < 3U) ? names[class_id] : "unknown";
}
