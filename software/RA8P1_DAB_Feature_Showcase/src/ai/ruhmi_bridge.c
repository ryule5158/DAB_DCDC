#include "ruhmi_bridge.h"

#include "common_data.h"
#include "ethosu_driver.h"

#include "generated/mppt/mppt_model.h"
#include "generated/mppt/mppt_sub_0000_command_stream.h"
#include "generated/mppt/mppt_sub_0000_invoke.h"
#include "generated/mppt/mppt_sub_0000_model_data.h"
#include "generated/mppt/mppt_sub_0000_tensors.h"

#include "generated/pv_fault/pv_fault_compute_sub_0001.h"
#include "generated/pv_fault/pv_fault_model.h"
#include "generated/pv_fault/pv_fault_sub_0000_command_stream.h"
#include "generated/pv_fault/pv_fault_sub_0000_invoke.h"
#include "generated/pv_fault/pv_fault_sub_0000_model_data.h"
#include "generated/pv_fault/pv_fault_sub_0000_tensors.h"

#include "generated/thermal/thermal_model.h"
#include "generated/thermal/thermal_sub_0000_command_stream.h"
#include "generated/thermal/thermal_sub_0000_invoke.h"
#include "generated/thermal/thermal_sub_0000_model_data.h"
#include "generated/thermal/thermal_sub_0000_tensors.h"

#include <stddef.h>
#include <string.h>

typedef struct st_ruhmi_descriptor
{
    uint8_t       * p_arena;
    size_t          arena_size;
    uint8_t const * p_model;
    size_t          model_size;
    uint8_t const * p_command;
    size_t          command_size;
    uint32_t        input_offset;
    size_t          input_size;
    uint32_t        output_offset;
    size_t          output_size;
} ruhmi_descriptor_t;

/*
 * ethosu_invoke_async() retains these arrays until ethosu_wait() completes;
 * therefore they must not live on a stack frame.
 */
static uint64_t s_base_addresses[5];
static size_t   s_base_sizes[5];
static ruhmi_model_t s_active_model;
static bool s_ready;
static uint8_t s_pv_cpu_arena[pv_fault_kBufferSize_sub_0001];

static bool ruhmi_descriptor_get(ruhmi_model_t model,
                                 ruhmi_descriptor_t * p_descriptor)
{
    if (NULL == p_descriptor)
    {
        return false;
    }

    switch (model)
    {
        case RUHMI_MODEL_MPPT:
            *p_descriptor = (ruhmi_descriptor_t)
            {
                .p_arena       = mppt_sub_0000_arena,
                .arena_size    = mppt_kArenaSize_sub_0000,
                .p_model       = mppt_sub_0000_model_data,
                .model_size    = mppt_sub_0000_model_data_size,
                .p_command     = mppt_sub_0000_command_stream,
                .command_size  = mppt_sub_0000_command_stream_size,
                .input_offset  =
                    mppt_sub_0000_address_serving_default_power_scan_0,
                .input_size    = 9U,
                .output_offset =
                    mppt_sub_0000_address_StatefulPartitionedCall_0_70011,
                .output_size   = 13U,
            };
            return true;

        case RUHMI_MODEL_PV_FAULT:
            *p_descriptor = (ruhmi_descriptor_t)
            {
                .p_arena       = pv_fault_sub_0000_arena,
                .arena_size    = pv_fault_kArenaSize_sub_0000,
                .p_model       = pv_fault_sub_0000_model_data,
                .model_size    = pv_fault_sub_0000_model_data_size,
                .p_command     = pv_fault_sub_0000_command_stream,
                .command_size  = pv_fault_sub_0000_command_stream_size,
                .input_offset  =
                    pv_fault_sub_0000_address_serving_default_input_1_0,
                .input_size    = 96U,
                .output_offset =
                    pv_fault_sub_0000_address_sequential_quant_dense_Softmax_70011,
                .output_size   = 3U,
            };
            return true;

        case RUHMI_MODEL_THERMAL:
            *p_descriptor = (ruhmi_descriptor_t)
            {
                .p_arena       = thermal_sub_0000_arena,
                .arena_size    = thermal_kArenaSize_sub_0000,
                .p_model       = thermal_sub_0000_model_data,
                .model_size    = thermal_sub_0000_model_data_size,
                .p_command     = thermal_sub_0000_command_stream,
                .command_size  = thermal_sub_0000_command_stream_size,
                .input_offset  =
                    thermal_sub_0000_address_serving_default_thermal_input_0,
                .input_size    = 8U,
                .output_offset =
                    thermal_sub_0000_address_StatefulPartitionedCall_0_70014,
                .output_size   = 3U,
            };
            return true;

        default:
            return false;
    }
}

fsp_err_t ruhmi_bridge_open(void)
{
    if (s_ready)
    {
        return FSP_SUCCESS;
    }

    fsp_err_t const err =
        RM_ETHOSU_Open(&g_rm_ethosu0_ctrl, &g_rm_ethosu0_cfg);
    if (FSP_SUCCESS == err)
    {
        s_ready = true;
    }
    return err;
}

bool ruhmi_bridge_ready(void)
{
    return s_ready;
}

bool ruhmi_bridge_busy(void)
{
    return RUHMI_MODEL_NONE != s_active_model;
}

ruhmi_model_t ruhmi_bridge_active_model(void)
{
    return s_active_model;
}

int ruhmi_bridge_start(ruhmi_model_t model, bool clean_outputs)
{
    ruhmi_descriptor_t descriptor;
    if (!s_ready || ruhmi_bridge_busy() ||
        !ruhmi_descriptor_get(model, &descriptor))
    {
        return -1;
    }

    if (clean_outputs)
    {
        memset(descriptor.p_arena + descriptor.output_offset, 0,
               descriptor.output_size);
    }

    s_base_addresses[0] = (uint64_t) (uintptr_t) descriptor.p_model;
    s_base_sizes[0]     = descriptor.model_size;
    s_base_addresses[1] = (uint64_t) (uintptr_t) descriptor.p_arena;
    s_base_sizes[1]     = descriptor.arena_size;
    s_base_addresses[2] = (uint64_t) (uintptr_t) descriptor.p_arena;
    s_base_sizes[2]     = descriptor.arena_size;
    s_base_addresses[3] =
        (uint64_t) (uintptr_t) (descriptor.p_arena + descriptor.input_offset);
    s_base_sizes[3] = descriptor.input_size;
    s_base_addresses[4] =
        (uint64_t) (uintptr_t) (descriptor.p_arena + descriptor.output_offset);
    s_base_sizes[4] = descriptor.output_size;

    int const result = ethosu_invoke_async(
        &g_ethosu0, descriptor.p_command, (int) descriptor.command_size,
        s_base_addresses, s_base_sizes, 5, NULL);
    if (0 == result)
    {
        s_active_model = model;
    }
    return result;
}

int ruhmi_bridge_poll(bool block)
{
    ruhmi_model_t const completed_model = s_active_model;
    if (RUHMI_MODEL_NONE == completed_model)
    {
        return -2;
    }

    int const result = ethosu_wait(&g_ethosu0, block);
    if (0 == result)
    {
        if (RUHMI_MODEL_PV_FAULT == completed_model)
        {
            int8_t const * p_npu_output =
                pv_fault_sub_0000_arena +
                pv_fault_sub_0000_address_sequential_quant_dense_Softmax_70011;
            pv_fault_compute_sub_0001(
                s_pv_cpu_arena, p_npu_output,
                pv_fault_GetModelOutputPtr_StatefulPartitionedCall_0_70012());
        }
        s_active_model = RUHMI_MODEL_NONE;
    }
    else if (result < 0)
    {
        s_active_model = RUHMI_MODEL_NONE;
    }
    return result;
}
