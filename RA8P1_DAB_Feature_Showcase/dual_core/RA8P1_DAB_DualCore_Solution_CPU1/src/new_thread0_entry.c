#include "new_thread0.h"

#include "dab_dual_core_config.h"
#include "drv_ra8p1_crc.h"
#include "drv_ra8p1_ipc.h"
#include "drv_ra8p1_sdram.h"
#include "../../shared/dab_energy_control.h"

#include <stdbool.h>
#include <stdint.h>

#define CPU1_TASK_STACK_WORDS       (256U)
#define CPU1_INTEGRITY_PRIORITY     (tskIDLE_PRIORITY + 2U)
#define CPU1_MEMORY_PRIORITY        (tskIDLE_PRIORITY + 1U)
#define CPU1_ENERGY_PRIORITY        (tskIDLE_PRIORITY + 3U)
#define CPU1_STATUS_CRC_OK          (1UL << 0U)
#define CPU1_STATUS_SDRAM_READY     (1UL << 1U)
#define CPU1_STATUS_PEER_SEEN       (1UL << 2U)
#define CPU1_STATUS_TASKS_CREATED   (1UL << 3U)
#define CPU1_STATUS_ENERGY_ACTIVE   (1UL << 4U)
#define CPU1_IPC_STATUS_OK          (1UL << 19U)
#define CPU1_IPC_STATUS_CRC_MASK    (0x0000FFFFUL)

typedef struct st_cpu1_service_status
{
    uint32_t signature;
    uint32_t flags;
    uint32_t ipc_service_cycles;
    uint32_t integrity_cycles;
    uint32_t memory_service_cycles;
    uint32_t energy_service_cycles;
    uint32_t energy_config_applied;
    uint32_t last_crc;
    ra8p1_ipc_status_t ipc;
    ra8p1_crc_status_t crc;
    ra8p1_sdram_status_t sdram;
    dab_energy_output_t energy;
} cpu1_service_status_t;

volatile cpu1_service_status_t g_cpu1_service_status =
{
    .signature = 0x43505531UL, /* "CPU1" */
};

static StaticTask_t s_integrity_task_memory;
static StackType_t s_integrity_task_stack[CPU1_TASK_STACK_WORDS];
static StaticTask_t s_memory_task_memory;
static StackType_t s_memory_task_stack[CPU1_TASK_STACK_WORDS];
static StaticTask_t s_energy_task_memory;
static StackType_t s_energy_task_stack[CPU1_TASK_STACK_WORDS * 2U];
static dab_energy_state_t s_energy_state;

static float cpu1_sensor_decode(uint32_t payload, uint32_t shift, float scale)
{
    return (float) ((payload >> shift) & 0x3FFUL) * scale;
}

static void cpu1_energy_receive(dab_energy_measurement_t * p_measurement,
                                uint32_t now_ms)
{
    static uint32_t generations[13];
    static uint32_t pv_update_ms;
    static uint32_t output_update_ms;
    ra8p1_ipc_message_t message;
    if (drv_ra8p1_ipc_latest_get(RA8P1_IPC_MESSAGE_SENSOR_PV,
                                 &message,
                                 &generations[RA8P1_IPC_MESSAGE_SENSOR_PV]))
    {
        p_measurement->vin_v = cpu1_sensor_decode(message.payload, 0U, 0.1F);
        p_measurement->iin_a = cpu1_sensor_decode(message.payload, 10U, 0.05F);
        pv_update_ms = now_ms;
    }
    if (drv_ra8p1_ipc_latest_get(RA8P1_IPC_MESSAGE_SENSOR_OUTPUT,
                                 &message,
                                 &generations[RA8P1_IPC_MESSAGE_SENSOR_OUTPUT]))
    {
        p_measurement->vout_v = cpu1_sensor_decode(message.payload, 0U, 0.1F);
        p_measurement->iout_a = cpu1_sensor_decode(message.payload, 10U, 0.02F);
        output_update_ms = now_ms;
    }
    if (drv_ra8p1_ipc_latest_get(RA8P1_IPC_MESSAGE_SENSOR_TEMPERATURE,
                                 &message,
                                 &generations[RA8P1_IPC_MESSAGE_SENSOR_TEMPERATURE]))
    {
        p_measurement->temp_pri_c =
            ((float) (message.payload & 0x1FFUL) * 0.5F) - 40.0F;
        p_measurement->temp_sec_c =
            ((float) ((message.payload >> 9U) & 0x1FFUL) * 0.5F) - 40.0F;
        p_measurement->valid = (0U != (message.payload & (1UL << 18U)));
    }
    if (drv_ra8p1_ipc_latest_get(RA8P1_IPC_MESSAGE_ENERGY_CONFIG,
                                 &message,
                                 &generations[RA8P1_IPC_MESSAGE_ENERGY_CONFIG]))
    {
        dab_energy_config_key_t key;
        float value = 0.0F;
        if (dab_energy_config_payload_decode(message.payload, &key, &value) &&
            dab_energy_apply_config(&s_energy_state, key, value, now_ms))
        {
            g_cpu1_service_status.energy_config_applied++;
            (void) drv_ra8p1_ipc_try_send(RA8P1_IPC_MESSAGE_COMMAND_ACK,
                                          message.payload);
        }
    }
    p_measurement->now_ms = now_ms;
    p_measurement->valid = p_measurement->valid &&
        ((uint32_t) (now_ms - pv_update_ms) <= 250U) &&
        ((uint32_t) (now_ms - output_update_ms) <= 250U);
}

static bool cpu1_energy_send_status(uint32_t slot)
{
    dab_energy_output_t const * const out = &s_energy_state.output;
    uint32_t payload = 0U;
    ra8p1_ipc_message_type_t type = RA8P1_IPC_MESSAGE_ENERGY_REFERENCE;
    if (0U == slot)
    {
        uint32_t const current = (uint32_t) ((out->requested_current_a * 100.0F) + 0.5F);
        payload = (current & 0x7FFUL) |
                  (((uint32_t) out->charge_stage & 0x0FUL) << 11U) |
                  (((uint32_t) out->mode & 0x07UL) << 15U) |
                  (out->valid ? (1UL << 18U) : 0U) |
                  (out->mppt_limited ? (1UL << 19U) : 0U);
    }
    else if (1U == slot)
    {
        type = RA8P1_IPC_MESSAGE_MPPT_STATUS;
        uint32_t const power = (uint32_t) (out->mppt_power_target_w + 0.5F);
        uint32_t const voltage = (uint32_t) ((out->mppt_voltage_target_v * 10.0F) + 0.5F);
        payload = (power & 0x1FFUL) | ((voltage & 0x3FFUL) << 9U) |
                  (out->mppt_limited ? (1UL << 19U) : 0U);
    }
    else if (2U == slot)
    {
        type = RA8P1_IPC_MESSAGE_CHARGER_STATUS;
        uint32_t const voltage =
            (uint32_t) ((out->charge_voltage_target_v * 20.0F) + 0.5F);
        payload = ((uint32_t) out->battery_profile & 0x07UL) |
                  (((uint32_t) out->charge_stage & 0x0FUL) << 3U) |
                  ((voltage & 0x3FFUL) << 7U) |
                  (out->charger_limited ? (1UL << 17U) : 0U);
    }
    else if (3U == slot)
    {
        type = RA8P1_IPC_MESSAGE_AI_STATUS;
        uint32_t const current =
            (uint32_t) ((s_energy_state.config.ai_hint_current_a * 25.0F) + 0.5F);
        uint32_t const confidence =
            (uint32_t) ((s_energy_state.config.ai_confidence * 250.0F) + 0.5F);
        payload = (current & 0x1FFUL) | ((confidence & 0xFFUL) << 9U) |
                  (out->ai_applied ? (1UL << 19U) : 0U);
    }
    else
    {
        type = RA8P1_IPC_MESSAGE_AI_MODULATION_STATUS;
        uint32_t const confidence = (uint32_t)
            ((s_energy_state.config.ai_modulation_confidence * 250.0F) + 0.5F);
        uint32_t const ttl_100ms =
            (s_energy_state.config.ai_modulation_ttl_ms + 50U) / 100U;
        payload = ((uint32_t) s_energy_state.config.ai_hint_modulation & 0x03UL) |
                  ((confidence & 0xFFUL) << 2U) |
                  ((ttl_100ms & 0x7FUL) << 10U) |
                  (out->ai_modulation_fresh ? (1UL << 17U) : 0U);
    }
    return drv_ra8p1_ipc_try_send(type, payload);
}

static void cpu1_energy_task(void * pv_parameters)
{
    FSP_PARAMETER_NOT_USED(pv_parameters);
    dab_energy_measurement_t measurement = {0};
    dab_energy_initialize(&s_energy_state);
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t status_slot = 0U;
    uint32_t last_algorithm_ms = 0U;

    for (;;)
    {
        uint32_t const now_ms = (uint32_t) xTaskGetTickCount() *
                                (uint32_t) portTICK_PERIOD_MS;
        cpu1_energy_receive(&measurement, now_ms);
        if ((uint32_t) (now_ms - last_algorithm_ms) >= 100U)
        {
            last_algorithm_ms = now_ms;
            dab_energy_update(&s_energy_state, &measurement);
            g_cpu1_service_status.energy = s_energy_state.output;
            g_cpu1_service_status.energy_service_cycles++;
        }
        if (cpu1_energy_send_status(status_slot))
        {
            status_slot = (status_slot + 1U) % 5U;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10U));
    }
}

static void cpu1_integrity_task(void * pv_parameters)
{
    FSP_PARAMETER_NOT_USED(pv_parameters);
    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        uint32_t protected_words[4];
        protected_words[0] = g_cpu1_service_status.signature;
        protected_words[1] = g_cpu1_service_status.flags;
        protected_words[2] = g_cpu1_service_status.ipc_service_cycles;
        protected_words[3] = g_cpu1_service_status.integrity_cycles;

        uint32_t crc = 0U;
        if (drv_ra8p1_crc32(protected_words, sizeof(protected_words), &crc))
        {
            g_cpu1_service_status.last_crc = crc;
        }
        ra8p1_crc_status_t crc_status;
        drv_ra8p1_crc_status_get(&crc_status);
        g_cpu1_service_status.crc = crc_status;
        g_cpu1_service_status.integrity_cycles++;

        uint32_t payload = crc & CPU1_IPC_STATUS_CRC_MASK;
        if (g_cpu1_service_status.crc.self_test_passed)
        {
            payload |= CPU1_IPC_STATUS_OK;
        }
        (void) drv_ra8p1_ipc_try_send(RA8P1_IPC_MESSAGE_STATUS, payload);
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(DAB_DUAL_CORE_INTEGRITY_PERIOD_MS));
    }
}

static void cpu1_memory_task(void * pv_parameters)
{
    FSP_PARAMETER_NOT_USED(pv_parameters);
    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
#if DAB_CPU1_SDRAM_PERIODIC_TEST_ENABLE
        (void) drv_ra8p1_sdram_quick_test();
#endif
        ra8p1_sdram_status_t sdram;
        drv_ra8p1_sdram_status_get(&sdram);
        g_cpu1_service_status.sdram = sdram;
        g_cpu1_service_status.memory_service_cycles++;
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(DAB_DUAL_CORE_MEMORY_PERIOD_MS));
    }
}

void new_thread0_entry(void * pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    drv_ra8p1_ipc_initialize();
    bool const crc_ok = drv_ra8p1_crc_initialize();
    drv_ra8p1_sdram_initialize();
    dab_energy_initialize(&s_energy_state);
    ra8p1_sdram_status_t sdram;
    drv_ra8p1_sdram_status_get(&sdram);
    g_cpu1_service_status.sdram = sdram;

    if (crc_ok)
    {
        g_cpu1_service_status.flags |= CPU1_STATUS_CRC_OK;
    }
    if (RA8P1_SDRAM_READY == g_cpu1_service_status.sdram.state)
    {
        g_cpu1_service_status.flags |= CPU1_STATUS_SDRAM_READY;
    }

    TaskHandle_t const integrity_task =
        xTaskCreateStatic(cpu1_integrity_task, "CPU1 Integrity",
                          CPU1_TASK_STACK_WORDS, NULL,
                          CPU1_INTEGRITY_PRIORITY,
                          s_integrity_task_stack, &s_integrity_task_memory);
    TaskHandle_t const memory_task =
        xTaskCreateStatic(cpu1_memory_task, "CPU1 Memory",
                          CPU1_TASK_STACK_WORDS, NULL,
                          CPU1_MEMORY_PRIORITY,
                          s_memory_task_stack, &s_memory_task_memory);
    TaskHandle_t const energy_task =
        xTaskCreateStatic(cpu1_energy_task, "CPU1 Energy",
                          CPU1_TASK_STACK_WORDS * 2U, NULL,
                          CPU1_ENERGY_PRIORITY,
                          s_energy_task_stack, &s_energy_task_memory);
    if ((NULL != integrity_task) && (NULL != memory_task) &&
        (NULL != energy_task))
    {
        g_cpu1_service_status.flags |= CPU1_STATUS_TASKS_CREATED;
    }
    if (NULL != energy_task)
    {
        g_cpu1_service_status.flags |= CPU1_STATUS_ENERGY_ACTIVE;
    }

    TickType_t last_wake = xTaskGetTickCount();
    for (;;)
    {
        uint32_t const now_ms = (uint32_t) xTaskGetTickCount() *
                                (uint32_t) portTICK_PERIOD_MS;
        drv_ra8p1_ipc_service(now_ms);

        ra8p1_ipc_status_t ipc;
        drv_ra8p1_ipc_status_get(&ipc);
        g_cpu1_service_status.ipc = ipc;
        if (ipc.peer_seen)
        {
            g_cpu1_service_status.flags |= CPU1_STATUS_PEER_SEEN;
        }
        g_cpu1_service_status.ipc_service_cycles++;
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(DAB_DUAL_CORE_SERVICE_PERIOD_MS));
    }
}
