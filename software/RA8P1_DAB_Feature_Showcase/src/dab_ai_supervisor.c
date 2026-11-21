#include "dab_ai_supervisor.h"

#include "ai/mppt_model.h"
#include "ai/pv_fault_model.h"
#include "ai/thermal_model.h"

#include <math.h>
#include <string.h>

#define DAB_AI_PV_SAMPLE_MS       (200UL)
#define DAB_AI_THERMAL_SAMPLE_MS  (1000UL)
#define DAB_AI_NPU_POLL_MS        (1UL)
#define DAB_AI_DERATE_MIN_W       (20.0F)

typedef struct st_dab_ai_context
{
    dab_ai_status_t public_status;
    uint32_t last_poll_ms;
    uint32_t last_pv_sample_ms;
    uint32_t last_thermal_sample_ms;
    float pv_frames[PV_FAULT_FRAME_COUNT][PV_FAULT_CHANNELS];
    uint32_t pv_frame_count;
    bool pv_pending;
    float thermal_inputs[THERMAL_INPUT_ELEMENTS];
    bool thermal_pending;
    float previous_temperatures[3];
    uint32_t previous_temperature_ms;
    bool previous_temperature_valid;
    float mppt_power_scan[MPPT_INPUT_ELEMENTS];
    bool mppt_pending;
} dab_ai_context_t;

static dab_ai_context_t s_ai;

static float dab_ai_min(float a, float b)
{
    return (a < b) ? a : b;
}

static float dab_ai_clamp(float value, float minimum, float maximum)
{
    if (value < minimum)
    {
        return minimum;
    }
    if (value > maximum)
    {
        return maximum;
    }
    return value;
}

static void dab_ai_complete(AiStatus_t result, ruhmi_model_t model,
                            uint32_t class_or_voltage, float confidence)
{
    s_ai.public_status.last_status = result;
    if ((AI_MODEL_STAT_OK == result) ||
        (AI_STAT_PV_FAULT_ALARM == result) ||
        (AI_STAT_THERMAL_DERATE == result))
    {
        s_ai.public_status.inference_count++;
        if (RUHMI_MODEL_MPPT == model)
        {
            s_ai.public_status.mppt_valid = true;
            s_ai.public_status.mppt_vmpp_mv = class_or_voltage;
            s_ai.public_status.mppt_confidence = confidence;
        }
        else if (RUHMI_MODEL_PV_FAULT == model)
        {
            s_ai.public_status.pv_fault_valid = true;
            s_ai.public_status.pv_fault_class = class_or_voltage;
            s_ai.public_status.pv_fault_confidence = confidence;
        }
        else if (RUHMI_MODEL_THERMAL == model)
        {
            s_ai.public_status.thermal_valid = true;
            s_ai.public_status.thermal_class = class_or_voltage;
            s_ai.public_status.thermal_confidence = confidence;
        }
    }
    else
    {
        s_ai.public_status.error_count++;
        if (RUHMI_MODEL_MPPT == model)
        {
            s_ai.public_status.mppt_valid = false;
        }
        else if (RUHMI_MODEL_PV_FAULT == model)
        {
            s_ai.public_status.pv_fault_valid = false;
        }
        else if (RUHMI_MODEL_THERMAL == model)
        {
            s_ai.public_status.thermal_valid = false;
        }
    }
}

static void dab_ai_poll(void)
{
    ruhmi_model_t const model = ruhmi_bridge_active_model();
    uint32_t value = 0U;
    float confidence = 0.0F;
    AiStatus_t result = AI_STAT_NOT_READY;

    if (RUHMI_MODEL_MPPT == model)
    {
        result = mppt_model_poll(&value, &confidence);
    }
    else if (RUHMI_MODEL_PV_FAULT == model)
    {
        result = pv_fault_model_poll(&value, &confidence);
    }
    else if (RUHMI_MODEL_THERMAL == model)
    {
        result = thermal_model_poll(&value, &confidence);
    }

    if ((RUHMI_MODEL_NONE != model) && (AI_STAT_BUSY != result))
    {
        dab_ai_complete(result, model, value, confidence);
    }
}

static void dab_ai_start_pending(void)
{
    AiStatus_t result = AI_STAT_NOT_READY;
    bool * p_pending = NULL;

    if (s_ai.pv_pending)
    {
        p_pending = &s_ai.pv_pending;
        result = pv_fault_model_begin(s_ai.pv_frames);
        if (AI_MODEL_STAT_OK == result)
        {
            s_ai.pv_pending = false;
        }
    }
    else if (s_ai.thermal_pending)
    {
        p_pending = &s_ai.thermal_pending;
        result = thermal_model_begin(s_ai.thermal_inputs);
        if (AI_MODEL_STAT_OK == result)
        {
            s_ai.thermal_pending = false;
        }
    }
    else if (s_ai.mppt_pending)
    {
        p_pending = &s_ai.mppt_pending;
        result = mppt_model_begin(s_ai.mppt_power_scan);
        if (AI_MODEL_STAT_OK == result)
        {
            s_ai.mppt_pending = false;
        }
    }

    if ((AI_MODEL_STAT_OK != result) &&
        (AI_STAT_NOT_READY != result) &&
        (AI_STAT_BUSY != result))
    {
        if (NULL != p_pending)
        {
            *p_pending = false;
        }
        s_ai.public_status.last_status = result;
        s_ai.public_status.error_count++;
    }
}

static void dab_ai_collect_pv(dab_measurements_t const * p_measurements,
                              bool power_active, uint32_t now_ms)
{
    if (!power_active)
    {
        s_ai.pv_frame_count = 0U;
        s_ai.pv_pending = false;
        return;
    }
    if ((NULL == p_measurements) || !p_measurements->valid ||
        ((uint32_t) (now_ms - s_ai.last_pv_sample_ms) <
         DAB_AI_PV_SAMPLE_MS))
    {
        return;
    }
    s_ai.last_pv_sample_ms = now_ms;

    float const frame[PV_FAULT_CHANNELS] =
        {p_measurements->vin_v, p_measurements->iin_a,
         p_measurements->pin_w};
    if ((frame[0] < 18.0F) || (frame[0] > 32.0F) ||
        (frame[1] < 0.0F) || (frame[1] > 31.0F) ||
        (frame[2] < 0.0F) || (frame[2] > 555.6F))
    {
        s_ai.pv_frame_count = 0U;
        return;
    }

    memcpy(s_ai.pv_frames[s_ai.pv_frame_count], frame, sizeof(frame));
    s_ai.pv_frame_count++;
    if (PV_FAULT_FRAME_COUNT == s_ai.pv_frame_count)
    {
        s_ai.pv_frame_count = 0U;
        s_ai.pv_pending = true;
    }
}

static void dab_ai_collect_thermal(
    dab_measurements_t const * p_measurements, bool ambient_valid,
    float ambient_c, bool fan_on, uint32_t now_ms)
{
    if (!ambient_valid || (NULL == p_measurements) ||
        !p_measurements->valid ||
        ((uint32_t) (now_ms - s_ai.last_thermal_sample_ms) <
         DAB_AI_THERMAL_SAMPLE_MS))
    {
        return;
    }

    float const temperatures[3] =
        {p_measurements->temp_pri_c, p_measurements->temp_sec_c, ambient_c};
    float derivatives[3] = {0.0F, 0.0F, 0.0F};
    if (s_ai.previous_temperature_valid)
    {
        float const dt_seconds =
            (float) (now_ms - s_ai.previous_temperature_ms) * 0.001F;
        if (dt_seconds > 0.0F)
        {
            for (uint32_t i = 0U; i < 3U; ++i)
            {
                derivatives[i] = dab_ai_clamp(
                    (temperatures[i] - s_ai.previous_temperatures[i]) /
                    dt_seconds, -10.0F, 10.0F);
            }
        }
    }

    s_ai.thermal_inputs[THERMAL_INPUT_TPRI] = temperatures[0];
    s_ai.thermal_inputs[THERMAL_INPUT_TSEC] = temperatures[1];
    s_ai.thermal_inputs[THERMAL_INPUT_TAMB] = temperatures[2];
    s_ai.thermal_inputs[THERMAL_INPUT_DTPRI] = derivatives[0];
    s_ai.thermal_inputs[THERMAL_INPUT_DTSEC] = derivatives[1];
    s_ai.thermal_inputs[THERMAL_INPUT_DTAMB] = derivatives[2];
    s_ai.thermal_inputs[THERMAL_INPUT_PIN] =
        dab_ai_clamp(p_measurements->pin_w, 0.0F, 500.0F);
    s_ai.thermal_inputs[THERMAL_INPUT_FAN] = fan_on ? 1.0F : 0.0F;
    memcpy(s_ai.previous_temperatures, temperatures, sizeof(temperatures));
    s_ai.previous_temperature_ms = now_ms;
    s_ai.previous_temperature_valid = true;
    s_ai.last_thermal_sample_ms = now_ms;
    s_ai.thermal_pending = true;
}

fsp_err_t dab_ai_supervisor_initialize(void)
{
    memset(&s_ai, 0, sizeof(s_ai));
    fsp_err_t const err = ruhmi_bridge_open();
    s_ai.public_status.npu_ready = (FSP_SUCCESS == err);
    s_ai.public_status.last_status =
        s_ai.public_status.npu_ready ? AI_MODEL_STAT_OK :
                                       AI_STAT_NOT_READY;
    return err;
}

void dab_ai_supervisor_enable(bool enabled)
{
    s_ai.public_status.enabled =
        enabled && s_ai.public_status.npu_ready;
    if (!s_ai.public_status.enabled)
    {
        s_ai.pv_frame_count = 0U;
        s_ai.pv_pending = false;
        s_ai.thermal_pending = false;
        s_ai.mppt_pending = false;
    }
}

bool dab_ai_supervisor_enabled(void)
{
    return s_ai.public_status.enabled;
}

bool dab_ai_supervisor_mppt_submit(float const power_scan_w[9])
{
    if ((NULL == power_scan_w) || !s_ai.public_status.npu_ready)
    {
        return false;
    }
    for (uint32_t i = 0U; i < MPPT_INPUT_ELEMENTS; ++i)
    {
        if (!isfinite(power_scan_w[i]) ||
            (power_scan_w[i] < MPPT_POWER_MIN_W) ||
            (power_scan_w[i] > MPPT_POWER_MAX_W))
        {
            return false;
        }
    }
    memcpy(s_ai.mppt_power_scan, power_scan_w,
           sizeof(s_ai.mppt_power_scan));
    s_ai.mppt_pending = true;
    return true;
}

void dab_ai_supervisor_service(dab_measurements_t const * p_measurements,
                               bool power_active,
                               bool ambient_valid,
                               float ambient_c,
                               bool fan_on,
                               uint32_t now_ms)
{
    if (!s_ai.public_status.enabled)
    {
        /* A disabled supervisor never starts new work, but it must drain an
         * inference that was already accepted by the NPU before disable. */
        if (ruhmi_bridge_busy() &&
            ((uint32_t) (now_ms - s_ai.last_poll_ms) >= DAB_AI_NPU_POLL_MS))
        {
            s_ai.last_poll_ms = now_ms;
            dab_ai_poll();
        }
        s_ai.public_status.busy = ruhmi_bridge_busy();
        s_ai.public_status.active_model = ruhmi_bridge_active_model();
        return;
    }

    dab_ai_collect_pv(p_measurements, power_active, now_ms);
    dab_ai_collect_thermal(p_measurements, ambient_valid, ambient_c,
                           fan_on, now_ms);

    if (ruhmi_bridge_busy())
    {
        if ((uint32_t) (now_ms - s_ai.last_poll_ms) >= DAB_AI_NPU_POLL_MS)
        {
            s_ai.last_poll_ms = now_ms;
            dab_ai_poll();
        }
    }
    else
    {
        dab_ai_start_pending();
    }

    s_ai.public_status.busy = ruhmi_bridge_busy();
    s_ai.public_status.active_model = ruhmi_bridge_active_model();
}

float dab_ai_supervisor_power_cap(float requested_w,
                                  dab_measurements_t const * p_measurements)
{
    if (!s_ai.public_status.enabled)
    {
        return requested_w;
    }

    float cap = requested_w;
    if (s_ai.public_status.pv_fault_valid &&
        (s_ai.public_status.pv_fault_class > 0U))
    {
        cap = dab_ai_min(cap, DAB_AI_DERATE_MIN_W);
    }
    if (s_ai.public_status.thermal_valid)
    {
        if (s_ai.public_status.thermal_class >= 2U)
        {
            cap = dab_ai_min(cap, DAB_AI_DERATE_MIN_W);
        }
        else if (1U == s_ai.public_status.thermal_class)
        {
            cap = dab_ai_min(cap, requested_w * 0.75F);
        }
    }
    if (s_ai.public_status.mppt_valid && (NULL != p_measurements) &&
        p_measurements->valid)
    {
        float const vmpp_v = (float) s_ai.public_status.mppt_vmpp_mv * 0.001F;
        float const deficit_v = vmpp_v - p_measurements->vin_v;
        if (deficit_v > 0.25F)
        {
            float const fraction =
                dab_ai_clamp(1.0F - 0.12F * deficit_v, 0.25F, 1.0F);
            cap = dab_ai_min(cap, requested_w * fraction);
        }
    }
    return dab_ai_clamp(cap, DAB_AI_DERATE_MIN_W, requested_w);
}

void dab_ai_supervisor_status_get(dab_ai_status_t * p_status)
{
    if (NULL != p_status)
    {
        *p_status = s_ai.public_status;
        p_status->busy = ruhmi_bridge_busy();
        p_status->active_model = ruhmi_bridge_active_model();
    }
}
