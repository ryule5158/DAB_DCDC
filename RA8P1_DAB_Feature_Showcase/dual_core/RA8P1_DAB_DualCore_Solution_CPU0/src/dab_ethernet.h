#ifndef DAB_ETHERNET_H
#define DAB_ETHERNET_H

#include <stdbool.h>

#define DAB_ETHERNET_IP_ADDRESS    "192.168.10.100"
#define DAB_ETHERNET_TCP_PORT      (5000U)

bool dab_ethernet_start(void);

#endif /* DAB_ETHERNET_H */
