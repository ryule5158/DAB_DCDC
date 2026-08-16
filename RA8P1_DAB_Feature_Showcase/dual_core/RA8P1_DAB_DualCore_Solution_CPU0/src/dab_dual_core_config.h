#ifndef DAB_DUAL_CORE_CONFIG_H
#define DAB_DUAL_CORE_CONFIG_H

/* Hardware FIFO channel shared by the two images in this FSP solution. */
#define DAB_DUAL_CORE_IPC_CHANNEL             (0U)
#define DAB_DUAL_CORE_HEARTBEAT_MS            (1000UL)
#define DAB_DUAL_CORE_PEER_STALE_MS           (3000UL)
#define DAB_DUAL_CORE_SERVICE_PERIOD_MS       (10UL)

#if (DAB_DUAL_CORE_IPC_CHANNEL > 1U)
 #error "RA8P1 IPC message FIFO channel must be 0 or 1"
#endif

#endif /* DAB_DUAL_CORE_CONFIG_H */
