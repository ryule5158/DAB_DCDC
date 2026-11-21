#ifndef DRV_RA8P1_IPC_H
#define DRV_RA8P1_IPC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum e_ra8p1_ipc_message_type
{
    RA8P1_IPC_MESSAGE_HEARTBEAT = 1,
    RA8P1_IPC_MESSAGE_STATUS = 2,
    RA8P1_IPC_MESSAGE_FAULT = 3,
    RA8P1_IPC_MESSAGE_COMMAND_ACK = 4,
    RA8P1_IPC_MESSAGE_SENSOR_PV = 5,
    RA8P1_IPC_MESSAGE_SENSOR_OUTPUT = 6,
    RA8P1_IPC_MESSAGE_SENSOR_TEMPERATURE = 7,
    RA8P1_IPC_MESSAGE_ENERGY_CONFIG = 8,
    RA8P1_IPC_MESSAGE_ENERGY_REFERENCE = 9,
    RA8P1_IPC_MESSAGE_MPPT_STATUS = 10,
    RA8P1_IPC_MESSAGE_CHARGER_STATUS = 11,
    RA8P1_IPC_MESSAGE_AI_STATUS = 12,
    RA8P1_IPC_MESSAGE_AI_MODULATION_STATUS = 13
} ra8p1_ipc_message_type_t;

typedef struct st_ra8p1_ipc_message
{
    ra8p1_ipc_message_type_t type;
    uint8_t sequence;
    uint32_t payload;
} ra8p1_ipc_message_t;

typedef struct st_ra8p1_ipc_status
{
    bool enabled;
    bool peer_seen;
    bool peer_stale;
    uint8_t local_core;
    uint8_t channel;
    uint8_t tx_sequence;
    uint8_t last_rx_sequence;
    uint32_t tx_count;
    uint32_t tx_full_count;
    uint32_t rx_count;
    uint32_t last_rx_ms;
    ra8p1_ipc_message_t last_rx;
} ra8p1_ipc_status_t;

void drv_ra8p1_ipc_initialize(void);
void drv_ra8p1_ipc_service(uint32_t now_ms);
bool drv_ra8p1_ipc_try_send(ra8p1_ipc_message_type_t type, uint32_t payload);
bool drv_ra8p1_ipc_try_receive(ra8p1_ipc_message_t * p_message);
bool drv_ra8p1_ipc_latest_get(ra8p1_ipc_message_type_t type,
                              ra8p1_ipc_message_t * p_message,
                              uint32_t * p_generation);
void drv_ra8p1_ipc_status_get(ra8p1_ipc_status_t * p_status);

#endif /* DRV_RA8P1_IPC_H */
