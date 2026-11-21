#include "dab_dual_core_cpu0.h"

#include "dab_dual_core_config.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#define CPU0_IPC_TASK_STACK_WORDS    (384U)
/* The 10 ms energy/measurement IPC service must not compete with the idle
 * task.  SAVE telemetry can keep the priority-1 application task runnable
 * long enough to starve an idle-priority IPC task, leaving CPU1 with stale
 * Vout and an old charge-current reference.  Priority 3 is above the
 * application and Ethernet receive tasks; this task still blocks every
 * 10 ms after its short, bounded service pass. */
#define CPU0_IPC_TASK_PRIORITY       (tskIDLE_PRIORITY + 3U)
#define CPU0_ENERGY_QUEUE_LENGTH      (32U)
#define CPU0_ENERGY_REFERENCE_STALE_MS (500U)
#define CPU0_ENERGY_CONFIG_RETRY_MS    (200U)
#define CPU0_ENERGY_CONFIG_MAX_RETRIES (10U)

typedef struct st_cpu0_energy_measurements
{
    float vin_v;
    float iin_a;
    float vout_v;
    float iout_a;
    float temp_pri_c;
    float temp_sec_c;
    bool valid;
} cpu0_energy_measurements_t;

volatile dab_cpu0_dual_core_status_t g_dab_cpu0_dual_core_status;

static StaticTask_t s_cpu0_ipc_task_memory;
static StackType_t s_cpu0_ipc_task_stack[CPU0_IPC_TASK_STACK_WORDS];
static StaticQueue_t s_energy_queue_memory;
static uint8_t s_energy_queue_storage[CPU0_ENERGY_QUEUE_LENGTH * sizeof(uint32_t)];
static QueueHandle_t s_energy_queue;
static cpu0_energy_measurements_t s_energy_measurements;
static bool s_energy_config_waiting_ack;
static uint32_t s_energy_config_pending_payload;
static uint32_t s_energy_config_last_send_ms;
static uint32_t s_energy_config_retry_count;

static uint32_t cpu0_quantize(float value, float scale, uint32_t maximum)
{
    if (value <= 0.0F)
    {
        return 0U;
    }
    float const scaled = (value * scale) + 0.5F;
    return (scaled >= (float) maximum) ? maximum : (uint32_t) scaled;
}

static void cpu0_energy_receive(uint32_t now_ms)
{
    static uint32_t generations[14];
    ra8p1_ipc_message_t message;
    if (drv_ra8p1_ipc_latest_get(RA8P1_IPC_MESSAGE_COMMAND_ACK,
                                 &message,
                                 &generations[RA8P1_IPC_MESSAGE_COMMAND_ACK]) &&
        s_energy_config_waiting_ack &&
        (message.payload == s_energy_config_pending_payload))
    {
        uint32_t discarded = 0U;
        (void) xQueueReceive(s_energy_queue, &discarded, 0U);
        s_energy_config_waiting_ack = false;
        s_energy_config_retry_count = 0U;
    }
    if (drv_ra8p1_ipc_latest_get(RA8P1_IPC_MESSAGE_ENERGY_REFERENCE,
                                 &message,
                                 &generations[RA8P1_IPC_MESSAGE_ENERGY_REFERENCE]))
    {
        uint32_t const payload = message.payload;
        g_dab_cpu0_dual_core_status.energy_current_reference_a =
            (float) (payload & 0x7FFUL) * 0.01F;
        g_dab_cpu0_dual_core_status.charge_stage =
            (dab_charge_stage_t) ((payload >> 11U) & 0x0FUL);
        g_dab_cpu0_dual_core_status.energy_mode =
            (dab_energy_mode_t) ((payload >> 15U) & 0x07UL);
        g_dab_cpu0_dual_core_status.energy_reference_valid =
            (0U != (payload & (1UL << 18U)));
        g_dab_cpu0_dual_core_status.mppt_limited =
            (0U != (payload & (1UL << 19U)));
        g_dab_cpu0_dual_core_status.energy_reference_last_ms = now_ms;
        g_dab_cpu0_dual_core_status.energy_reference_updates++;
    }
    if (drv_ra8p1_ipc_latest_get(RA8P1_IPC_MESSAGE_MPPT_STATUS,
                                 &message,
                                 &generations[RA8P1_IPC_MESSAGE_MPPT_STATUS]))
    {
        g_dab_cpu0_dual_core_status.mppt_power_target_w =
            (float) (message.payload & 0x1FFUL);
        g_dab_cpu0_dual_core_status.mppt_voltage_target_v =
            (float) ((message.payload >> 9U) & 0x3FFUL) * 0.1F;
    }
    if (drv_ra8p1_ipc_latest_get(RA8P1_IPC_MESSAGE_CHARGER_STATUS,
                                 &message,
                                 &generations[RA8P1_IPC_MESSAGE_CHARGER_STATUS]))
    {
        g_dab_cpu0_dual_core_status.battery_profile =
            (dab_battery_profile_t) (message.payload & 0x07UL);
        g_dab_cpu0_dual_core_status.charge_stage =
            (dab_charge_stage_t) ((message.payload >> 3U) & 0x0FUL);
        g_dab_cpu0_dual_core_status.charge_voltage_target_v =
            (float) ((message.payload >> 7U) & 0x3FFUL) * 0.05F;
        g_dab_cpu0_dual_core_status.charger_limited =
            (0U != (message.payload & (1UL << 17U)));
    }
    if (drv_ra8p1_ipc_latest_get(RA8P1_IPC_MESSAGE_AI_STATUS,
                                 &message,
                                 &generations[RA8P1_IPC_MESSAGE_AI_STATUS]))
    {
        g_dab_cpu0_dual_core_status.ai_hint_current_a =
            (float) (message.payload & 0x1FFUL) * 0.04F;
        g_dab_cpu0_dual_core_status.ai_confidence =
            (float) ((message.payload >> 9U) & 0xFFUL) * 0.004F;
        g_dab_cpu0_dual_core_status.ai_applied =
            (0U != (message.payload & (1UL << 19U)));
    }
    if (drv_ra8p1_ipc_latest_get(RA8P1_IPC_MESSAGE_AI_MODULATION_STATUS,
                                 &message,
                                 &generations[RA8P1_IPC_MESSAGE_AI_MODULATION_STATUS]))
    {
        g_dab_cpu0_dual_core_status.ai_modulation_hint =
            (uint8_t) (message.payload & 0x03UL);
        g_dab_cpu0_dual_core_status.ai_modulation_confidence =
            (float) ((message.payload >> 2U) & 0xFFUL) * 0.004F;
        g_dab_cpu0_dual_core_status.ai_modulation_ttl_ms =
            ((message.payload >> 10U) & 0x7FUL) * 100U;
        g_dab_cpu0_dual_core_status.ai_modulation_fresh =
            (0U != (message.payload & (1UL << 17U)));
    }
}

static bool cpu0_energy_send_sensor(uint32_t slot)
{
    cpu0_energy_measurements_t measurement;
    taskENTER_CRITICAL();
    measurement = s_energy_measurements;
    taskEXIT_CRITICAL();

    uint32_t payload = 0U;
    ra8p1_ipc_message_type_t type = RA8P1_IPC_MESSAGE_SENSOR_PV;
    if (0U == slot)
    {
        payload = cpu0_quantize(measurement.vin_v, 10.0F, 0x3FFU) |
                  (cpu0_quantize(measurement.iin_a, 20.0F, 0x3FFU) << 10U);
    }
    else if (1U == slot)
    {
        type = RA8P1_IPC_MESSAGE_SENSOR_OUTPUT;
        payload = cpu0_quantize(measurement.vout_v, 10.0F, 0x3FFU) |
                  (cpu0_quantize(measurement.iout_a, 50.0F, 0x3FFU) << 10U);
    }
    else
    {
        type = RA8P1_IPC_MESSAGE_SENSOR_TEMPERATURE;
        payload = cpu0_quantize(measurement.temp_pri_c + 40.0F, 2.0F, 0x1FFU) |
                  (cpu0_quantize(measurement.temp_sec_c + 40.0F, 2.0F, 0x1FFU) << 9U) |
                  (measurement.valid ? (1UL << 18U) : 0U);
    }
    return drv_ra8p1_ipc_try_send(type, payload);
}

static void dab_dual_core_cpu0_task(void * pv_parameters)
{
    FSP_PARAMETER_NOT_USED(pv_parameters);
    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        uint32_t const now_ms = (uint32_t) xTaskGetTickCount() *
                                (uint32_t) portTICK_PERIOD_MS;
        drv_ra8p1_ipc_service(now_ms);
        cpu0_energy_receive(now_ms);
        if (g_dab_cpu0_dual_core_status.energy_reference_valid &&
            ((uint32_t) (now_ms -
             g_dab_cpu0_dual_core_status.energy_reference_last_ms) >
             CPU0_ENERGY_REFERENCE_STALE_MS))
        {
            g_dab_cpu0_dual_core_status.energy_reference_valid = false;
        }

        static uint32_t sensor_slot;
        uint32_t payload = 0U;
        bool sent_config = false;
        if ((NULL != s_energy_queue) &&
            (pdTRUE == xQueuePeek(s_energy_queue, &payload, 0U)))
        {
            bool const retry_due = s_energy_config_waiting_ack &&
                ((uint32_t) (now_ms - s_energy_config_last_send_ms) >=
                 CPU0_ENERGY_CONFIG_RETRY_MS);
            if (retry_due &&
                (s_energy_config_retry_count >= CPU0_ENERGY_CONFIG_MAX_RETRIES))
            {
                uint32_t discarded = 0U;
                (void) xQueueReceive(s_energy_queue, &discarded, 0U);
                s_energy_config_waiting_ack = false;
                s_energy_config_retry_count = 0U;
                g_dab_cpu0_dual_core_status.energy_config_dropped++;
            }
            else if ((!s_energy_config_waiting_ack || retry_due) &&
                drv_ra8p1_ipc_try_send(RA8P1_IPC_MESSAGE_ENERGY_CONFIG, payload))
            {
                if (s_energy_config_waiting_ack)
                {
                    s_energy_config_retry_count++;
                }
                else
                {
                    s_energy_config_retry_count = 0U;
                }
                s_energy_config_pending_payload = payload;
                s_energy_config_waiting_ack = true;
                s_energy_config_last_send_ms = now_ms;
                sent_config = true;
            }
        }
        if (!sent_config && cpu0_energy_send_sensor(sensor_slot))
        {
            sensor_slot = (sensor_slot + 1U) % 3U;
        }
        ra8p1_ipc_status_t ipc;
        drv_ra8p1_ipc_status_get(&ipc);
        g_dab_cpu0_dual_core_status.ipc = ipc;
        g_dab_cpu0_dual_core_status.service_cycles++;
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(DAB_DUAL_CORE_SERVICE_PERIOD_MS));
    }
}

void dab_dual_core_cpu0_start(void)
{
    drv_ra8p1_ipc_initialize();

    s_energy_queue = xQueueCreateStatic(CPU0_ENERGY_QUEUE_LENGTH,
                                        sizeof(uint32_t),
                                        s_energy_queue_storage,
                                        &s_energy_queue_memory);
    g_dab_cpu0_dual_core_status.energy_queue_created = (NULL != s_energy_queue);

    /* CPU1 VTOR comes from the official solution's CPU1 flash partition. */
    R_BSP_SecondaryCoreStart();

    TaskHandle_t const task = xTaskCreateStatic(dab_dual_core_cpu0_task,
                                                 "CPU0 IPC",
                                                 CPU0_IPC_TASK_STACK_WORDS,
                                                 NULL,
                                                 CPU0_IPC_TASK_PRIORITY,
                                                 s_cpu0_ipc_task_stack,
                                                 &s_cpu0_ipc_task_memory);
    g_dab_cpu0_dual_core_status.task_created = (NULL != task);
}

void dab_dual_core_cpu0_measurements_publish(float vin_v,
                                             float iin_a,
                                             float vout_v,
                                             float iout_a,
                                             float temp_pri_c,
                                             float temp_sec_c,
                                             bool valid)
{
    taskENTER_CRITICAL();
    s_energy_measurements.vin_v = vin_v;
    s_energy_measurements.iin_a = iin_a;
    s_energy_measurements.vout_v = vout_v;
    s_energy_measurements.iout_a = iout_a;
    s_energy_measurements.temp_pri_c = temp_pri_c;
    s_energy_measurements.temp_sec_c = temp_sec_c;
    s_energy_measurements.valid = valid;
    taskEXIT_CRITICAL();
}

bool dab_dual_core_cpu0_energy_config_set(dab_energy_config_key_t key,
                                          float value)
{
    uint32_t payload = 0U;
    if ((NULL == s_energy_queue) ||
        !dab_energy_config_payload_encode(key, value, &payload))
    {
        g_dab_cpu0_dual_core_status.energy_config_dropped++;
        return false;
    }
    if (pdTRUE != xQueueSend(s_energy_queue, &payload, 0U))
    {
        g_dab_cpu0_dual_core_status.energy_config_dropped++;
        return false;
    }
    g_dab_cpu0_dual_core_status.energy_config_queued++;
    return true;
}

bool dab_dual_core_cpu0_energy_reference_get(float * p_current_a,
                                              uint32_t * p_age_ms)
{
    if ((NULL == p_current_a) || (NULL == p_age_ms) ||
        !g_dab_cpu0_dual_core_status.energy_reference_valid)
    {
        return false;
    }
    uint32_t const now_ms = (uint32_t) xTaskGetTickCount() *
                            (uint32_t) portTICK_PERIOD_MS;
    *p_current_a = g_dab_cpu0_dual_core_status.energy_current_reference_a;
    *p_age_ms = now_ms - g_dab_cpu0_dual_core_status.energy_reference_last_ms;
    return true;
}
