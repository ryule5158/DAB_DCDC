#ifndef DAB_REMOTE_CONTROL_H
#define DAB_REMOTE_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#define DAB_REMOTE_TEXT_LENGTH       (24U)
#define DAB_REMOTE_ERROR_LENGTH      (64U)
#define DAB_REMOTE_STATE_LENGTH      (24U)
#define DAB_REMOTE_VERSION_LENGTH    (48U)

typedef enum e_dab_remote_command_type
{
    DAB_REMOTE_COMMAND_NONE = 0,
    DAB_REMOTE_COMMAND_START,
    DAB_REMOTE_COMMAND_STOP,
    DAB_REMOTE_COMMAND_SET_PARAMETER
} dab_remote_command_type_t;

typedef enum e_dab_remote_parameter
{
    DAB_REMOTE_PARAMETER_NONE = 0,
    DAB_REMOTE_PARAMETER_POWER_LIMIT_W,
    DAB_REMOTE_PARAMETER_VOLTAGE_REF_V,
    DAB_REMOTE_PARAMETER_KP_NS_PER_V,
    DAB_REMOTE_PARAMETER_KI_US_PER_VS,
    DAB_REMOTE_PARAMETER_KD_NS_S_PER_V,
    DAB_REMOTE_PARAMETER_PHASE_NS,
    DAB_REMOTE_PARAMETER_MODULATION,
    DAB_REMOTE_PARAMETER_LOAD_MODE,
    DAB_REMOTE_PARAMETER_BENCH_VIN_SET_V,
    DAB_REMOTE_PARAMETER_LOAD_NOMINAL_OHM,
    DAB_REMOTE_PARAMETER_REQUESTED_POWER_LAYER_W,
    DAB_REMOTE_PARAMETER_CONTROL_ARCHITECTURE,
    DAB_REMOTE_PARAMETER_SCHEDULE
} dab_remote_parameter_t;

typedef struct st_dab_remote_command
{
    uint32_t                  ticket;
    dab_remote_command_type_t type;
    dab_remote_parameter_t    parameter;
    float                     value;
    char                      text[DAB_REMOTE_TEXT_LENGTH];
} dab_remote_command_t;

typedef struct st_dab_remote_result
{
    uint32_t ticket;
    bool     ok;
    char     error[DAB_REMOTE_ERROR_LENGTH];
} dab_remote_result_t;

typedef struct st_dab_remote_snapshot
{
    uint32_t uptime_ms;
    uint32_t sequence;
    uint32_t fault_mask;
    uint32_t phase_ns;
    uint32_t raw_phase_ns;
    int32_t  effective_phase_ns;
    uint32_t command_phase_ns;
    uint32_t primary_inner_ns;
    uint32_t secondary_inner_ns;
    uint32_t phase_cap_ns;
    uint32_t table_entry_id;
    uint32_t outer_saturation_updates;
    bool     control_enabled;
    bool     pwm_enabled;
    bool     measurements_valid;
    bool     load_mode_control_active;
    bool     inner_manual;
    bool     outer_saturated;
    bool     schedule_enabled;
    bool     feedforward_enabled;
    float    vin_v;
    float    vout_v;
    float    iin_a;
    float    iout_a;
    float    ipri_a;
    float    isec_a;
    float    pin_w;
    float    pout_w;
    float    temp_pri_c;
    float    temp_sec_c;
    float    power_limit_w;
    float    voltage_ref_v;
    float    kp_ns_per_v;
    float    ki_us_per_vs;
    float    kd_ns_s_per_v;
    float    derivative_filter_alpha;
    float    bench_vin_set_v;
    float    load_nominal_ohm;
    float    requested_power_layer_w;
    float    feedforward_effective_phase_ns;
    uint32_t control_architecture_id;
    char     state[DAB_REMOTE_STATE_LENGTH];
    char     modulation_requested[DAB_REMOTE_TEXT_LENGTH];
    char     modulation_effective[DAB_REMOTE_TEXT_LENGTH];
    char     load_mode[DAB_REMOTE_TEXT_LENGTH];
    char     control_architecture[DAB_REMOTE_VERSION_LENGTH];
    char     schedule_table[DAB_REMOTE_VERSION_LENGTH];
    char     firmware[DAB_REMOTE_VERSION_LENGTH];
} dab_remote_snapshot_t;

bool dab_remote_command_submit(dab_remote_command_t const * p_command,
                               uint32_t * p_ticket);
bool dab_remote_command_take(dab_remote_command_t * p_command);
void dab_remote_command_complete(uint32_t ticket, bool ok, char const * p_error);
bool dab_remote_result_get(uint32_t ticket, dab_remote_result_t * p_result);

void dab_remote_snapshot_publish(dab_remote_snapshot_t const * p_snapshot);
void dab_remote_snapshot_get(dab_remote_snapshot_t * p_snapshot);

#endif /* DAB_REMOTE_CONTROL_H */
