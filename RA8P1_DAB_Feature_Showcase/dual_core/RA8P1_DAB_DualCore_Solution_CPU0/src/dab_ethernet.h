#ifndef DAB_ETHERNET_H
#define DAB_ETHERNET_H

#include <stdbool.h>
#include <stdint.h>

#define DAB_ETHERNET_IP_ADDRESS    "192.168.10.100"
#define DAB_ETHERNET_TCP_PORT      (5000U)

typedef struct st_dab_ethernet_diag
{
    bool     task_started;
    bool     reset_released;
    bool     ip_init_called;
    bool     ip_init_returned;
    bool     ip_init_ok;
    bool     network_up;
    uint32_t network_event_count;
    uint32_t tcp_open;
    uint32_t rmac_open;
    uint32_t phy_open;
    uint32_t phy_interface_status;
    uint32_t link_establish_status;
} dab_ethernet_diag_t;

bool dab_ethernet_start(void);
void dab_ethernet_diag_get(dab_ethernet_diag_t * p_diag);

#endif /* DAB_ETHERNET_H */
