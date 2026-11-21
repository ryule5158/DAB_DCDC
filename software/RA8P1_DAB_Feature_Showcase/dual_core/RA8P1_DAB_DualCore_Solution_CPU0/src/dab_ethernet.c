#include "dab_ethernet.h"

#include "User_Init_Task.h"
#include "dab_board.h"
#include "dab_remote_control.h"

#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DAB_ETHERNET_TASK_STACK_WORDS       (2048U)
#define DAB_ETHERNET_TASK_PRIORITY          (1U)
#define DAB_ETHERNET_RX_CHUNK_SIZE          (256U)
#define DAB_ETHERNET_JSON_LINE_SIZE         (1024U)
#define DAB_ETHERNET_TX_BUFFER_SIZE         (1536U)
#define DAB_ETHERNET_RECEIVE_TIMEOUT_MS     (200U)
#define DAB_ETHERNET_COMMAND_TIMEOUT_MS     (750U)
#define DAB_ETHERNET_TELEMETRY_PERIOD_MS    (1000U)
#define DAB_ETHERNET_HEARTBEAT_TIMEOUT_MS   (15000U)

static TaskHandle_t       s_ethernet_task;
static volatile BaseType_t s_network_up;
static volatile dab_ethernet_diag_t s_diag;

static uint8_t const s_ip_address[4]      = {192U, 168U, 10U, 100U};
static uint8_t const s_netmask[4]         = {255U, 255U, 255U, 0U};
static uint8_t const s_gateway_address[4] = {192U, 168U, 10U, 1U};
static uint8_t const s_dns_address[4]     = {192U, 168U, 10U, 1U};
static uint8_t const s_mac_address[6]     = {0x02U, 0x52U, 0xA8U, 0x10U, 0x00U, 0x64U};

typedef struct st_dab_jsonl_framer
{
    char   line[DAB_ETHERNET_JSON_LINE_SIZE];
    size_t length;
    bool   dropping_oversize_line;
} dab_jsonl_framer_t;

static void dab_ethernet_task(void * p_context);
static void dab_ethernet_server_run(void);
static void dab_ethernet_client_run(Socket_t client_socket);
static void dab_ethernet_handle_line(Socket_t client_socket, char const * p_line);

static bool dab_socket_send_all(Socket_t socket, char const * p_text)
{
    size_t sent = 0U;
    size_t const length = strlen(p_text);
    while (sent < length)
    {
        BaseType_t const result = FreeRTOS_send(socket,
                                                p_text + sent,
                                                length - sent,
                                                0);
        if (result <= 0)
        {
            return false;
        }
        sent += (size_t) result;
    }
    return true;
}

static char const * dab_json_find_value(char const * p_json, char const * p_key)
{
    char pattern[48];
    int const pattern_length = snprintf(pattern, sizeof(pattern), "\"%s\"", p_key);
    if ((pattern_length <= 0) || ((size_t) pattern_length >= sizeof(pattern)))
    {
        return NULL;
    }

    char const * p_found = strstr(p_json, pattern);
    if (NULL == p_found)
    {
        return NULL;
    }
    p_found += (size_t) pattern_length;
    while ((*p_found == ' ') || (*p_found == '\t'))
    {
        p_found++;
    }
    if (*p_found != ':')
    {
        return NULL;
    }
    p_found++;
    while ((*p_found == ' ') || (*p_found == '\t'))
    {
        p_found++;
    }
    return p_found;
}

static bool dab_json_get_string(char const * p_json,
                                char const * p_key,
                                char * p_value,
                                size_t value_size)
{
    if ((NULL == p_value) || (value_size < 2U))
    {
        return false;
    }

    char const * p_source = dab_json_find_value(p_json, p_key);
    if ((NULL == p_source) || ('\"' != *p_source))
    {
        return false;
    }
    p_source++;

    size_t length = 0U;
    while ((*p_source != '\0') && (*p_source != '\"'))
    {
        if ((*p_source == '\\') || ((unsigned char) *p_source < 0x20U) ||
            (length >= (value_size - 1U)))
        {
            return false;
        }
        p_value[length++] = *p_source++;
    }
    if (*p_source != '\"')
    {
        return false;
    }
    p_value[length] = '\0';
    return true;
}

static bool dab_json_get_float(char const * p_json, char const * p_key, float * p_value)
{
    if (NULL == p_value)
    {
        return false;
    }

    char const * p_source = dab_json_find_value(p_json, p_key);
    if (NULL == p_source)
    {
        return false;
    }

    char * p_end = NULL;
    float const value = strtof(p_source, &p_end);
    if ((p_end == p_source) ||
        ((*p_end != ',') && (*p_end != '}') && (*p_end != ' ') && (*p_end != '\t')))
    {
        return false;
    }
    *p_value = value;
    return true;
}

static uint32_t dab_json_get_id(char const * p_json)
{
    char const * p_source = dab_json_find_value(p_json, "id");
    if (NULL == p_source)
    {
        return 0U;
    }

    char * p_end = NULL;
    unsigned long const value = strtoul(p_source, &p_end, 10);
    return (p_end == p_source) ? 0U : (uint32_t) value;
}

static void dab_send_error(Socket_t socket,
                           uint32_t id,
                           char const * p_command,
                           char const * p_error)
{
    char tx[DAB_ETHERNET_TX_BUFFER_SIZE];
    (void) snprintf(tx,
                    sizeof(tx),
                    "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"%s\","
                    "\"ok\":false,\"error\":\"%s\"}\n",
                    (unsigned long) id,
                    (NULL != p_command) ? p_command : "unknown",
                    (NULL != p_error) ? p_error : "error");
    (void) dab_socket_send_all(socket, tx);
}

static bool dab_remote_execute(dab_remote_command_t const * p_command,
                               dab_remote_result_t * p_result)
{
    uint32_t ticket = 0U;
    if (!dab_remote_command_submit(p_command, &ticket))
    {
        p_result->ok = false;
        (void) strncpy(p_result->error, "device_busy", sizeof(p_result->error) - 1U);
        p_result->error[sizeof(p_result->error) - 1U] = '\0';
        return false;
    }

    TickType_t const deadline = xTaskGetTickCount() +
                                pdMS_TO_TICKS(DAB_ETHERNET_COMMAND_TIMEOUT_MS);
    do
    {
        if (dab_remote_result_get(ticket, p_result))
        {
            return p_result->ok;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
    while ((int32_t) (deadline - xTaskGetTickCount()) > 0);

    p_result->ticket = ticket;
    p_result->ok = false;
    (void) strncpy(p_result->error, "application_timeout", sizeof(p_result->error) - 1U);
    p_result->error[sizeof(p_result->error) - 1U] = '\0';
    return false;
}

static void dab_send_status(Socket_t socket, uint32_t id, char const * p_command)
{
    dab_remote_snapshot_t snapshot;
    dab_remote_snapshot_get(&snapshot);

    char tx[DAB_ETHERNET_TX_BUFFER_SIZE];
    (void) snprintf(tx,
                    sizeof(tx),
                    "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"%s\",\"ok\":true,"
                    "\"status\":{\"firmware\":\"%s\",\"uptime_ms\":%lu,\"state\":\"%s\","
                    "\"control_enabled\":%s,\"pwm_enabled\":%s,\"fault_mask\":%lu,"
                    "\"measurements_valid\":%s,\"load_mode\":\"%s\","
                    "\"load_mode_control_active\":%s,\"modulation_requested\":\"%s\","
                    "\"modulation_effective\":\"%s\",\"power_limit_w\":%.2f,"
                    "\"voltage_ref_v\":%.2f,\"phase_ns\":%lu,"
                    "\"control_architecture\":\"%s\",\"kp_ns_per_v\":%.3f,"
                    "\"ki_us_per_vs\":%.4f,\"kd_ns_s_per_v\":%.6f,"
                    "\"bench_vin_set_v\":%.2f,\"load_nominal_ohm\":%.3f,"
                    "\"requested_power_layer_w\":%.2f,\"schedule_enabled\":%s,"
                    "\"table_entry_id\":%lu,\"schedule_table\":\"%s\"}}}\n",
                    (unsigned long) id,
                    p_command,
                    snapshot.firmware,
                    (unsigned long) snapshot.uptime_ms,
                    snapshot.state,
                    snapshot.control_enabled ? "true" : "false",
                    snapshot.pwm_enabled ? "true" : "false",
                    (unsigned long) snapshot.fault_mask,
                    snapshot.measurements_valid ? "true" : "false",
                    snapshot.load_mode,
                    snapshot.load_mode_control_active ? "true" : "false",
                    snapshot.modulation_requested,
                    snapshot.modulation_effective,
                    (double) snapshot.power_limit_w,
                    (double) snapshot.voltage_ref_v,
                    (unsigned long) snapshot.phase_ns,
                    snapshot.control_architecture,
                    (double) snapshot.kp_ns_per_v,
                    (double) snapshot.ki_us_per_vs,
                    (double) snapshot.kd_ns_s_per_v,
                    (double) snapshot.bench_vin_set_v,
                    (double) snapshot.load_nominal_ohm,
                    (double) snapshot.requested_power_layer_w,
                    snapshot.schedule_enabled ? "true" : "false",
                    (unsigned long) snapshot.table_entry_id,
                    snapshot.schedule_table);
    (void) dab_socket_send_all(socket, tx);
}

static void dab_send_parameter(Socket_t socket,
                               uint32_t id,
                               char const * p_name,
                               dab_remote_snapshot_t const * p_snapshot)
{
    char tx[DAB_ETHERNET_TX_BUFFER_SIZE];
    int length = -1;
    if (0 == strcmp(p_name, "power_limit_w"))
    {
        length = snprintf(tx, sizeof(tx), "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"get_parameter\",\"ok\":true,\"name\":\"%s\",\"value\":%.3f}\n", (unsigned long) id, p_name, (double) p_snapshot->power_limit_w);
    }
    else if (0 == strcmp(p_name, "voltage_ref_v"))
    {
        length = snprintf(tx, sizeof(tx), "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"get_parameter\",\"ok\":true,\"name\":\"%s\",\"value\":%.3f}\n", (unsigned long) id, p_name, (double) p_snapshot->voltage_ref_v);
    }
    else if (0 == strcmp(p_name, "kp_ns_per_v"))
    {
        length = snprintf(tx, sizeof(tx), "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"get_parameter\",\"ok\":true,\"name\":\"%s\",\"value\":%.3f}\n", (unsigned long) id, p_name, (double) p_snapshot->kp_ns_per_v);
    }
    else if (0 == strcmp(p_name, "ki_us_per_vs"))
    {
        length = snprintf(tx, sizeof(tx), "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"get_parameter\",\"ok\":true,\"name\":\"%s\",\"value\":%.3f}\n", (unsigned long) id, p_name, (double) p_snapshot->ki_us_per_vs);
    }
    else if (0 == strcmp(p_name, "kd_ns_s_per_v"))
    {
        length = snprintf(tx, sizeof(tx), "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"get_parameter\",\"ok\":true,\"name\":\"%s\",\"value\":%.6f}\n", (unsigned long) id, p_name, (double) p_snapshot->kd_ns_s_per_v);
    }
    else if (0 == strcmp(p_name, "phase_ns"))
    {
        length = snprintf(tx, sizeof(tx), "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"get_parameter\",\"ok\":true,\"name\":\"%s\",\"value\":%lu}\n", (unsigned long) id, p_name, (unsigned long) p_snapshot->phase_ns);
    }
    else if (0 == strcmp(p_name, "modulation"))
    {
        length = snprintf(tx, sizeof(tx), "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"get_parameter\",\"ok\":true,\"name\":\"%s\",\"value\":\"%s\"}\n", (unsigned long) id, p_name, p_snapshot->modulation_requested);
    }
    else if (0 == strcmp(p_name, "load_mode"))
    {
        length = snprintf(tx, sizeof(tx), "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"get_parameter\",\"ok\":true,\"name\":\"%s\",\"value\":\"%s\",\"control_profile_active\":false}\n", (unsigned long) id, p_name, p_snapshot->load_mode);
    }
    else if (0 == strcmp(p_name, "bench_vin_set_v"))
    {
        length = snprintf(tx, sizeof(tx), "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"get_parameter\",\"ok\":true,\"name\":\"%s\",\"value\":%.3f}\n", (unsigned long) id, p_name, (double) p_snapshot->bench_vin_set_v);
    }
    else if (0 == strcmp(p_name, "load_nominal_ohm"))
    {
        length = snprintf(tx, sizeof(tx), "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"get_parameter\",\"ok\":true,\"name\":\"%s\",\"value\":%.3f}\n", (unsigned long) id, p_name, (double) p_snapshot->load_nominal_ohm);
    }
    else if (0 == strcmp(p_name, "requested_power_layer_w"))
    {
        length = snprintf(tx, sizeof(tx), "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"get_parameter\",\"ok\":true,\"name\":\"%s\",\"value\":%.3f}\n", (unsigned long) id, p_name, (double) p_snapshot->requested_power_layer_w);
    }
    else if (0 == strcmp(p_name, "control_architecture"))
    {
        length = snprintf(tx, sizeof(tx), "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"get_parameter\",\"ok\":true,\"name\":\"%s\",\"value\":\"%s\"}\n", (unsigned long) id, p_name, p_snapshot->control_architecture);
    }
    else if (0 == strcmp(p_name, "schedule"))
    {
        length = snprintf(tx, sizeof(tx), "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"get_parameter\",\"ok\":true,\"name\":\"%s\",\"enabled\":%s,\"entry_id\":%lu,\"table\":\"%s\"}\n", (unsigned long) id, p_name, p_snapshot->schedule_enabled ? "true" : "false", (unsigned long) p_snapshot->table_entry_id, p_snapshot->schedule_table);
    }

    if ((length <= 0) || ((size_t) length >= sizeof(tx)))
    {
        dab_send_error(socket, id, "get_parameter", "unknown_parameter");
        return;
    }
    (void) dab_socket_send_all(socket, tx);
}

static bool dab_parameter_parse(char const * p_json, dab_remote_command_t * p_remote)
{
    char name[DAB_REMOTE_TEXT_LENGTH];
    if (!dab_json_get_string(p_json, "name", name, sizeof(name)))
    {
        return false;
    }

    if (0 == strcmp(name, "power_limit_w"))
    {
        p_remote->parameter = DAB_REMOTE_PARAMETER_POWER_LIMIT_W;
        return dab_json_get_float(p_json, "value", &p_remote->value);
    }
    if (0 == strcmp(name, "voltage_ref_v"))
    {
        p_remote->parameter = DAB_REMOTE_PARAMETER_VOLTAGE_REF_V;
        return dab_json_get_float(p_json, "value", &p_remote->value);
    }
    if (0 == strcmp(name, "kp_ns_per_v"))
    {
        p_remote->parameter = DAB_REMOTE_PARAMETER_KP_NS_PER_V;
        return dab_json_get_float(p_json, "value", &p_remote->value);
    }
    if (0 == strcmp(name, "ki_us_per_vs"))
    {
        p_remote->parameter = DAB_REMOTE_PARAMETER_KI_US_PER_VS;
        return dab_json_get_float(p_json, "value", &p_remote->value);
    }
    if (0 == strcmp(name, "kd_ns_s_per_v"))
    {
        p_remote->parameter = DAB_REMOTE_PARAMETER_KD_NS_S_PER_V;
        return dab_json_get_float(p_json, "value", &p_remote->value);
    }
    if (0 == strcmp(name, "phase_ns"))
    {
        p_remote->parameter = DAB_REMOTE_PARAMETER_PHASE_NS;
        return dab_json_get_float(p_json, "value", &p_remote->value);
    }
    if (0 == strcmp(name, "modulation"))
    {
        p_remote->parameter = DAB_REMOTE_PARAMETER_MODULATION;
        return dab_json_get_string(p_json, "value", p_remote->text, sizeof(p_remote->text));
    }
    if (0 == strcmp(name, "load_mode"))
    {
        p_remote->parameter = DAB_REMOTE_PARAMETER_LOAD_MODE;
        return dab_json_get_string(p_json, "value", p_remote->text, sizeof(p_remote->text));
    }
    if (0 == strcmp(name, "bench_vin_set_v"))
    {
        p_remote->parameter = DAB_REMOTE_PARAMETER_BENCH_VIN_SET_V;
        return dab_json_get_float(p_json, "value", &p_remote->value);
    }
    if (0 == strcmp(name, "load_nominal_ohm"))
    {
        p_remote->parameter = DAB_REMOTE_PARAMETER_LOAD_NOMINAL_OHM;
        return dab_json_get_float(p_json, "value", &p_remote->value);
    }
    if (0 == strcmp(name, "requested_power_layer_w"))
    {
        p_remote->parameter = DAB_REMOTE_PARAMETER_REQUESTED_POWER_LAYER_W;
        return dab_json_get_float(p_json, "value", &p_remote->value);
    }
    if (0 == strcmp(name, "control_architecture"))
    {
        p_remote->parameter = DAB_REMOTE_PARAMETER_CONTROL_ARCHITECTURE;
        return dab_json_get_string(p_json, "value", p_remote->text, sizeof(p_remote->text));
    }
    if (0 == strcmp(name, "schedule"))
    {
        p_remote->parameter = DAB_REMOTE_PARAMETER_SCHEDULE;
        return dab_json_get_string(p_json, "value", p_remote->text, sizeof(p_remote->text));
    }
    return false;
}

static void dab_ethernet_handle_line(Socket_t client_socket, char const * p_line)
{
    uint32_t const id = dab_json_get_id(p_line);
    char command[DAB_REMOTE_TEXT_LENGTH];
    if ((p_line[0] != '{') ||
        !dab_json_get_string(p_line, "cmd", command, sizeof(command)))
    {
        dab_send_error(client_socket, id, "unknown", "invalid_json_or_cmd");
        return;
    }

    if (0 == strcmp(command, "ping"))
    {
        char tx[192];
        (void) snprintf(tx,
                        sizeof(tx),
                        "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"ping\","
                        "\"ok\":true,\"reply\":\"pong\",\"uptime_ms\":%lu}\n",
                        (unsigned long) id,
                        (unsigned long) (xTaskGetTickCount() * portTICK_PERIOD_MS));
        (void) dab_socket_send_all(client_socket, tx);
        return;
    }

    if (0 == strcmp(command, "get_status"))
    {
        dab_send_status(client_socket, id, command);
        return;
    }

    if (0 == strcmp(command, "get_parameter"))
    {
        char name[DAB_REMOTE_TEXT_LENGTH];
        if (!dab_json_get_string(p_line, "name", name, sizeof(name)))
        {
            dab_send_error(client_socket, id, command, "missing_parameter_name");
            return;
        }
        dab_remote_snapshot_t snapshot;
        dab_remote_snapshot_get(&snapshot);
        dab_send_parameter(client_socket, id, name, &snapshot);
        return;
    }

    dab_remote_command_t remote = {0};
    if (0 == strcmp(command, "set_parameter"))
    {
        remote.type = DAB_REMOTE_COMMAND_SET_PARAMETER;
        if (!dab_parameter_parse(p_line, &remote))
        {
            dab_send_error(client_socket, id, command, "unknown_parameter_or_invalid_value");
            return;
        }
    }
    else if (0 == strcmp(command, "start"))
    {
        remote.type = DAB_REMOTE_COMMAND_START;
    }
    else if (0 == strcmp(command, "stop"))
    {
        remote.type = DAB_REMOTE_COMMAND_STOP;
    }
    else
    {
        dab_send_error(client_socket, id, command, "unknown_command");
        return;
    }

    dab_remote_result_t result = {0};
    bool const ok = dab_remote_execute(&remote, &result);
    if (!ok)
    {
        dab_send_error(client_socket, id, command, result.error);
        return;
    }

    char tx[256];
    (void) snprintf(tx,
                    sizeof(tx),
                    "{\"type\":\"response\",\"id\":%lu,\"cmd\":\"%s\","
                    "\"ok\":true,\"applied\":true}\n",
                    (unsigned long) id,
                    command);
    (void) dab_socket_send_all(client_socket, tx);
}

static bool dab_send_telemetry(Socket_t client_socket)
{
    dab_remote_snapshot_t snapshot;
    dab_remote_snapshot_get(&snapshot);

    char tx[DAB_ETHERNET_TX_BUFFER_SIZE];
    int const length = snprintf(tx,
                                sizeof(tx),
                                "{\"type\":\"telemetry\",\"seq\":%lu,\"uptime_ms\":%lu,"
                                "\"state\":\"%s\",\"control_enabled\":%s,\"pwm_enabled\":%s,"
                                "\"fault_mask\":%lu,\"valid\":%s,\"vin_v\":%.3f,\"vout_v\":%.3f,"
                                "\"iin_a\":%.3f,\"iout_a\":%.3f,\"ipri_a\":%.3f,\"isec_a\":%.3f,"
                                "\"pin_w\":%.3f,\"pout_w\":%.3f,\"temp_pri_c\":%.2f,"
                                "\"temp_sec_c\":%.2f,\"raw_phase_ns\":%lu,"
                                "\"effective_phase_ns\":%ld,\"command_phase_ns\":%lu,"
                                "\"primary_inner_ns\":%lu,\"secondary_inner_ns\":%lu,"
                                "\"phase_cap_ns\":%lu,\"power_limit_w\":%.2f,"
                                "\"voltage_ref_v\":%.2f,\"load_mode\":\"%s\","
                                "\"modulation\":\"%s\",\"control_architecture\":\"%s\","
                                "\"kp_ns_per_v\":%.3f,\"ki_us_per_vs\":%.4f,"
                                "\"kd_ns_s_per_v\":%.6f,\"d_filter_alpha\":%.5f,"
                                "\"bench_vin_set_v\":%.2f,\"load_nominal_ohm\":%.3f,"
                                "\"requested_power_layer_w\":%.2f,\"inner_manual\":%s,"
                                "\"outer_saturated\":%s,\"outer_saturation_updates\":%lu,"
                                "\"schedule_enabled\":%s,\"table_entry_id\":%lu,"
                                "\"schedule_table\":\"%s\",\"feedforward_enabled\":%s,"
                                "\"feedforward_effective_phase_ns\":%.2f}\n",
                                (unsigned long) snapshot.sequence,
                                (unsigned long) snapshot.uptime_ms,
                                snapshot.state,
                                snapshot.control_enabled ? "true" : "false",
                                snapshot.pwm_enabled ? "true" : "false",
                                (unsigned long) snapshot.fault_mask,
                                snapshot.measurements_valid ? "true" : "false",
                                (double) snapshot.vin_v,
                                (double) snapshot.vout_v,
                                (double) snapshot.iin_a,
                                (double) snapshot.iout_a,
                                (double) snapshot.ipri_a,
                                (double) snapshot.isec_a,
                                (double) snapshot.pin_w,
                                (double) snapshot.pout_w,
                                (double) snapshot.temp_pri_c,
                                (double) snapshot.temp_sec_c,
                                (unsigned long) snapshot.raw_phase_ns,
                                (long) snapshot.effective_phase_ns,
                                (unsigned long) snapshot.command_phase_ns,
                                (unsigned long) snapshot.primary_inner_ns,
                                (unsigned long) snapshot.secondary_inner_ns,
                                (unsigned long) snapshot.phase_cap_ns,
                                (double) snapshot.power_limit_w,
                                (double) snapshot.voltage_ref_v,
                                snapshot.load_mode,
                                snapshot.modulation_effective,
                                snapshot.control_architecture,
                                (double) snapshot.kp_ns_per_v,
                                (double) snapshot.ki_us_per_vs,
                                (double) snapshot.kd_ns_s_per_v,
                                (double) snapshot.derivative_filter_alpha,
                                (double) snapshot.bench_vin_set_v,
                                (double) snapshot.load_nominal_ohm,
                                (double) snapshot.requested_power_layer_w,
                                snapshot.inner_manual ? "true" : "false",
                                snapshot.outer_saturated ? "true" : "false",
                                (unsigned long) snapshot.outer_saturation_updates,
                                snapshot.schedule_enabled ? "true" : "false",
                                (unsigned long) snapshot.table_entry_id,
                                snapshot.schedule_table,
                                snapshot.feedforward_enabled ? "true" : "false",
                                (double) snapshot.feedforward_effective_phase_ns);
    return (length > 0) && ((size_t) length < sizeof(tx)) &&
           dab_socket_send_all(client_socket, tx);
}

static bool dab_jsonl_consume(Socket_t socket,
                              dab_jsonl_framer_t * p_framer,
                              char const * p_data,
                              size_t data_length)
{
    for (size_t index = 0U; index < data_length; index++)
    {
        char const character = p_data[index];
        if (p_framer->dropping_oversize_line)
        {
            if ('\n' == character)
            {
                p_framer->dropping_oversize_line = false;
                dab_send_error(socket, 0U, "unknown", "line_too_long");
            }
            continue;
        }

        if ('\n' == character)
        {
            if (p_framer->length > 0U)
            {
                p_framer->line[p_framer->length] = '\0';
                dab_ethernet_handle_line(socket, p_framer->line);
                p_framer->length = 0U;
            }
        }
        else if ('\r' == character)
        {
            continue;
        }
        else if (p_framer->length < (sizeof(p_framer->line) - 1U))
        {
            p_framer->line[p_framer->length++] = character;
        }
        else
        {
            p_framer->length = 0U;
            p_framer->dropping_oversize_line = true;
        }
    }
    return true;
}

static void dab_ethernet_client_run(Socket_t client_socket)
{
    TickType_t receive_timeout = pdMS_TO_TICKS(DAB_ETHERNET_RECEIVE_TIMEOUT_MS);
    (void) FreeRTOS_setsockopt(client_socket,
                               0,
                               FREERTOS_SO_RCVTIMEO,
                               &receive_timeout,
                               sizeof(receive_timeout));
    (void) FreeRTOS_setsockopt(client_socket,
                               0,
                               FREERTOS_SO_SNDTIMEO,
                               &receive_timeout,
                               sizeof(receive_timeout));

    char hello[320];
    (void) snprintf(hello,
                    sizeof(hello),
                    "{\"type\":\"hello\",\"protocol\":\"dab-jsonl/1\","
                    "\"ip\":\"%s\",\"port\":%u,\"telemetry_ms\":%u,"
                    "\"heartbeat_timeout_ms\":%u}\n",
                    DAB_ETHERNET_IP_ADDRESS,
                    DAB_ETHERNET_TCP_PORT,
                    DAB_ETHERNET_TELEMETRY_PERIOD_MS,
                    DAB_ETHERNET_HEARTBEAT_TIMEOUT_MS);
    if (!dab_socket_send_all(client_socket, hello))
    {
        return;
    }

    dab_jsonl_framer_t framer = {0};
    TickType_t last_receive = xTaskGetTickCount();
    TickType_t last_telemetry = last_receive;
    char rx[DAB_ETHERNET_RX_CHUNK_SIZE];

    while (pdTRUE == s_network_up)
    {
        BaseType_t const received = FreeRTOS_recv(client_socket, rx, sizeof(rx), 0);
        TickType_t const now = xTaskGetTickCount();
        if (received > 0)
        {
            last_receive = now;
            (void) dab_jsonl_consume(client_socket,
                                     &framer,
                                     rx,
                                     (size_t) received);
        }
        else if (0 == received)
        {
            break;
        }

        if ((now - last_telemetry) >=
            pdMS_TO_TICKS(DAB_ETHERNET_TELEMETRY_PERIOD_MS))
        {
            last_telemetry = now;
            if (!dab_send_telemetry(client_socket))
            {
                break;
            }
        }

        if ((now - last_receive) >=
            pdMS_TO_TICKS(DAB_ETHERNET_HEARTBEAT_TIMEOUT_MS))
        {
            dab_send_error(client_socket, 0U, "heartbeat", "heartbeat_timeout");
            break;
        }
    }
}

static void dab_ethernet_server_run(void)
{
    Socket_t server_socket = FreeRTOS_socket(FREERTOS_AF_INET,
                                              FREERTOS_SOCK_STREAM,
                                              FREERTOS_IPPROTO_TCP);
    if (FREERTOS_INVALID_SOCKET == server_socket)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
        return;
    }

    TickType_t accept_timeout = pdMS_TO_TICKS(1000U);
    (void) FreeRTOS_setsockopt(server_socket,
                               0,
                               FREERTOS_SO_RCVTIMEO,
                               &accept_timeout,
                               sizeof(accept_timeout));

    struct freertos_sockaddr address = {0};
    address.sin_port = FreeRTOS_htons(DAB_ETHERNET_TCP_PORT);
    if ((0 != FreeRTOS_bind(server_socket, &address, sizeof(address))) ||
        (0 != FreeRTOS_listen(server_socket, 1)))
    {
        FreeRTOS_closesocket(server_socket);
        vTaskDelay(pdMS_TO_TICKS(1000U));
        return;
    }

    while (pdTRUE == s_network_up)
    {
        struct freertos_sockaddr client_address = {0};
        socklen_t client_length = sizeof(client_address);
        Socket_t const client_socket = FreeRTOS_accept(server_socket,
                                                       &client_address,
                                                       &client_length);
        if (FREERTOS_INVALID_SOCKET != client_socket)
        {
            dab_ethernet_client_run(client_socket);
            FreeRTOS_closesocket(client_socket);
        }
    }
    FreeRTOS_closesocket(server_socket);
}

static void dab_ethernet_task(void * p_context)
{
    FSP_PARAMETER_NOT_USED(p_context);

    s_diag.task_started = true;

    /* The pin starts LOW in dab_board_initialize_safe().  Keep it asserted
     * beyond LAN8720A's 25 ms power-up requirement and pulse width, then let
     * r_rmac_phy perform the standard Clause-22 software reset/autonegotiation. */
    vTaskDelay(pdMS_TO_TICKS(25U));
    dab_board_phy_reset_set(false);
    vTaskDelay(pdMS_TO_TICKS(1U));
    dab_board_phy_reset_set(true);
    s_diag.reset_released = true;
    vTaskDelay(pdMS_TO_TICKS(2U));

    s_diag.ip_init_called = true;
    BaseType_t const initialized = FreeRTOS_IPInit(s_ip_address,
                                                   s_netmask,
                                                   s_gateway_address,
                                                   s_dns_address,
                                                   s_mac_address);
    s_diag.ip_init_returned = true;
    s_diag.ip_init_ok       = (pdFALSE != initialized);
    if (pdFALSE == initialized)
    {
        dab_board_phy_reset_set(false);
        s_ethernet_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (true)
    {
        if (pdTRUE == s_network_up)
        {
            dab_ethernet_server_run();
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(250U));
        }
    }
}

bool dab_ethernet_start(void)
{
    if (NULL != s_ethernet_task)
    {
        return true;
    }

    BaseType_t const created = xTaskCreate(dab_ethernet_task,
                                           "eth_jsonl",
                                           DAB_ETHERNET_TASK_STACK_WORDS,
                                           NULL,
                                           DAB_ETHERNET_TASK_PRIORITY,
                                           &s_ethernet_task);
    return pdPASS == created;
}

void dab_ethernet_diag_get (dab_ethernet_diag_t * p_diag)
{
    if (NULL == p_diag)
    {
        return;
    }

    taskENTER_CRITICAL();
    *p_diag = s_diag;
    taskEXIT_CRITICAL();

    /* These generated driver control fields are read-only diagnostics.  Word
     * reads are atomic on Cortex-M85 and do not initiate any RMII/MDIO access,
     * so they cannot contend with the FreeRTOS+TCP link-monitor task. */
    p_diag->tcp_open              = g_freertos_plus_tcp0.p_ctrl->open;
    p_diag->rmac_open             = g_ether0_ctrl.open;
    p_diag->phy_open              = g_rmac_phy0_ctrl.open;
    p_diag->phy_interface_status  = (uint32_t) g_rmac_phy0_ctrl.interface_status;
    p_diag->link_establish_status = (uint32_t) g_ether0_ctrl.link_establish_status;
}

void vApplicationIPNetworkEventHook(eIPCallbackEvent_t event)
{
    s_network_up = (eNetworkUp == event) ? pdTRUE : pdFALSE;
    s_diag.network_up = (pdTRUE == s_network_up);
    s_diag.network_event_count++;
}

uint32_t ulApplicationGetNextSequenceNumber(uint32_t source_address,
                                            uint16_t source_port,
                                            uint32_t destination_address,
                                            uint16_t destination_port)
{
    static uint32_t state = 0x8A10C0DEUL;
    taskENTER_CRITICAL();
    state ^= source_address ^ destination_address;
    state ^= ((uint32_t) source_port << 16) | destination_port;
    state ^= (uint32_t) xTaskGetTickCount();
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    uint32_t const result = state;
    taskEXIT_CRITICAL();
    return result;
}
