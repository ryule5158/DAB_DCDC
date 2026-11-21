#ifndef DAB_CAN_TRANSPORT_H
#define DAB_CAN_TRANSPORT_H

#include "dab_can_protocol.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct st_dab_can_transport_status
{
    bool     initialized;
    bool     bus_off;
    int32_t  open_error;
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t rx_dropped;
    uint32_t tx_dropped;
    uint32_t error_events;
    uint32_t last_error;
} dab_can_transport_status_t;

bool dab_can_transport_initialize(void);
void dab_can_transport_service(void);
bool dab_can_transport_command_take(dab_can_command_t * p_command);
bool dab_can_transport_ack_queue(uint8_t opcode,
                                 uint8_t sequence,
                                 dab_can_result_t result,
                                 dab_can_application_status_t const * p_status);
bool dab_can_transport_status_queue(
    dab_can_application_status_t const * p_status,
    dab_can_measurements_t const * p_measurements);
void dab_can_transport_status_get(dab_can_transport_status_t * p_status);

#endif /* DAB_CAN_TRANSPORT_H */
