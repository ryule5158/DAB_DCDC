#ifndef DAB_DUAL_CORE_CPU0_H
#define DAB_DUAL_CORE_CPU0_H

#include "drv_ra8p1_ipc.h"
#include "../../shared/dab_energy_control.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct st_dab_cpu0_dual_core_status
{
    bool task_created;
    bool energy_queue_created;
    uint32_t service_cycles;
    uint32_t energy_config_queued;
    uint32_t energy_config_dropped;
    uint32_t energy_reference_updates;
    uint32_t energy_reference_last_ms;
    float energy_current_reference_a;
    float mppt_power_target_w;
    float mppt_voltage_target_v;
    float charge_voltage_target_v;
    float ai_hint_current_a;
    float ai_confidence;
    uint8_t ai_modulation_hint;
    float ai_modulation_confidence;
    uint32_t ai_modulation_ttl_ms;
    dab_energy_mode_t energy_mode;
    dab_charge_stage_t charge_stage;
    dab_battery_profile_t battery_profile;
    bool energy_reference_valid;
    bool mppt_limited;
    bool charger_limited;
    bool ai_applied;
    bool ai_modulation_fresh;
    ra8p1_ipc_status_t ipc;
} dab_cpu0_dual_core_status_t;

extern volatile dab_cpu0_dual_core_status_t g_dab_cpu0_dual_core_status;

/* Call only after CPU0 has forced every DAB output to its safe state. */
void dab_dual_core_cpu0_start(void);
void dab_dual_core_cpu0_measurements_publish(float vin_v,
                                             float iin_a,
                                             float vout_v,
                                             float iout_a,
                                             float temp_pri_c,
                                             float temp_sec_c,
                                             bool valid);
bool dab_dual_core_cpu0_energy_config_set(dab_energy_config_key_t key,
                                          float value);
bool dab_dual_core_cpu0_energy_reference_get(float * p_current_a,
                                              uint32_t * p_age_ms);

#endif /* DAB_DUAL_CORE_CPU0_H */
