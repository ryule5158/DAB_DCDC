#ifndef DAB_DUAL_CORE_CONFIG_H
#define DAB_DUAL_CORE_CONFIG_H

#define DAB_DUAL_CORE_IPC_CHANNEL             (0U)
#define DAB_DUAL_CORE_HEARTBEAT_MS            (1000UL)
#define DAB_DUAL_CORE_PEER_STALE_MS           (3000UL)
#define DAB_DUAL_CORE_SERVICE_PERIOD_MS       (10UL)
#define DAB_DUAL_CORE_INTEGRITY_PERIOD_MS     (1000UL)
#define DAB_DUAL_CORE_MEMORY_PERIOD_MS        (5000UL)

/*
 * The solution reserves an SDRAM partition for CPU1, but the board's populated
 * SDRAM part and timing have not been confirmed.  Keep access disabled while
 * still compiling the complete driver.
 */
#define DAB_CPU1_SDRAM_ENABLE                 (0U)
#define DAB_CPU1_SDRAM_BASE                   (0x6C000000UL)
#define DAB_CPU1_SDRAM_CAPACITY_BYTES         (0UL)
#define DAB_CPU1_SDRAM_TEST_BYTES             (4096UL)
#define DAB_CPU1_SDRAM_PERIODIC_TEST_ENABLE   (0U)

#if (DAB_DUAL_CORE_IPC_CHANNEL > 1U)
 #error "RA8P1 IPC message FIFO channel must be 0 or 1"
#endif

#if (DAB_CPU1_SDRAM_ENABLE != 0U) && (DAB_CPU1_SDRAM_CAPACITY_BYTES == 0UL)
 #error "Confirm the populated SDRAM part and capacity before enabling access"
#endif

#endif /* DAB_DUAL_CORE_CONFIG_H */
