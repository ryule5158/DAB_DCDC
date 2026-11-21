#include "dab_power_dsp.h"

#include "dab_accel.h"

#if DAB_HELIUM_FLOAT_ENABLED
 #include <arm_mve.h>
#endif

#include <float.h>
#include <math.h>

static float dab_power_dsp_abs(float value)
{
    return (value < 0.0F) ? -value : value;
}

DAB_ITCM_CODE bool dab_power_dsp_u16_affine(uint16_t const * p_input,
                                             float * p_output,
                                             size_t count,
                                             float scale,
                                             float offset)
{
    if ((NULL == p_input) || (NULL == p_output) || (0U == count))
    {
        return false;
    }

    size_t index = 0U;
#if DAB_HELIUM_FLOAT_ENABLED
    for (; (index + 8U) <= count; index += 8U)
    {
        uint16x8_t const input_0_7 = vld1q_u16(&p_input[index]);
        uint32x4_t const input_0_3 = vmovlbq_u16(input_0_7);
        uint32x4_t const input_4_7 = vmovltq_u16(input_0_7);
        float32x4_t output_0_3 = vcvtq_f32_u32(input_0_3);
        float32x4_t output_4_7 = vcvtq_f32_u32(input_4_7);
        output_0_3 = vaddq_n_f32(vmulq_n_f32(output_0_3, scale), offset);
        output_4_7 = vaddq_n_f32(vmulq_n_f32(output_4_7, scale), offset);
        vst1q_f32(&p_output[index], output_0_3);
        vst1q_f32(&p_output[index + 4U], output_4_7);
    }
#endif
    for (; index < count; index++)
    {
        p_output[index] = ((float) p_input[index] * scale) + offset;
    }

    return true;
}

DAB_ITCM_CODE bool dab_power_dsp_window_analyze(float const * p_samples,
                                                 size_t count,
                                                 dab_power_dsp_window_t * p_result)
{
    if ((NULL == p_samples) || (NULL == p_result) || (0U == count))
    {
        return false;
    }

    float sum = 0.0F;
    float sum_squares = 0.0F;
    float minimum = FLT_MAX;
    float maximum = -FLT_MAX;
    size_t index = 0U;

#if DAB_HELIUM_FLOAT_ENABLED
    float32x4_t vector_sum = vdupq_n_f32(0.0F);
    float32x4_t vector_sum_squares = vdupq_n_f32(0.0F);
    float32x4_t vector_minimum = vdupq_n_f32(FLT_MAX);
    float32x4_t vector_maximum = vdupq_n_f32(-FLT_MAX);
    for (; (index + 4U) <= count; index += 4U)
    {
        float32x4_t const samples = vld1q_f32(&p_samples[index]);
        vector_sum = vaddq_f32(vector_sum, samples);
        vector_sum_squares = vaddq_f32(vector_sum_squares,
                                       vmulq_f32(samples, samples));
        vector_minimum = vminnmq_f32(vector_minimum, samples);
        vector_maximum = vmaxnmq_f32(vector_maximum, samples);
    }

    float lanes[4];
    vst1q_f32(lanes, vector_sum);
    sum = lanes[0] + lanes[1] + lanes[2] + lanes[3];
    vst1q_f32(lanes, vector_sum_squares);
    sum_squares = lanes[0] + lanes[1] + lanes[2] + lanes[3];
    vst1q_f32(lanes, vector_minimum);
    minimum = fminf(fminf(lanes[0], lanes[1]), fminf(lanes[2], lanes[3]));
    vst1q_f32(lanes, vector_maximum);
    maximum = fmaxf(fmaxf(lanes[0], lanes[1]), fmaxf(lanes[2], lanes[3]));
#endif

    for (; index < count; index++)
    {
        float const sample = p_samples[index];
        sum += sample;
        sum_squares += sample * sample;
        minimum = fminf(minimum, sample);
        maximum = fmaxf(maximum, sample);
    }

    p_result->mean = sum / (float) count;
    p_result->rms = sqrtf(sum_squares / (float) count);
    p_result->minimum = minimum;
    p_result->maximum = maximum;
    return true;
}

bool dab_power_dsp_self_test(void)
{
    static uint16_t const raw[9] =
    {
        0U, 2048U, 4095U, 1024U, 3072U, 100U, 1000U, 2000U, 3000U
    };
    float volts[9];
    float const scale = 3.3F / 4095.0F;
    if (!dab_power_dsp_u16_affine(raw, volts, 9U, scale, 0.0F))
    {
        return false;
    }
    if ((dab_power_dsp_abs(volts[0]) > 0.00001F) ||
        (dab_power_dsp_abs(volts[1] - 1.6504F) > 0.001F) ||
        (dab_power_dsp_abs(volts[2] - 3.3F) > 0.001F))
    {
        return false;
    }

    static float const samples[4] = {1.0F, -1.0F, 2.0F, -2.0F};
    dab_power_dsp_window_t result;
    if (!dab_power_dsp_window_analyze(samples, 4U, &result))
    {
        return false;
    }
    return (dab_power_dsp_abs(result.mean) < 0.00001F) &&
           (dab_power_dsp_abs(result.rms - 1.5811388F) < 0.0001F) &&
           (dab_power_dsp_abs(result.minimum + 2.0F) < 0.00001F) &&
           (dab_power_dsp_abs(result.maximum - 2.0F) < 0.00001F);
}

bool dab_power_dsp_helium_enabled(void)
{
    return (0U != DAB_HELIUM_FLOAT_ENABLED);
}
