#ifndef DAB_FEATURE_CONFIG_H
#define DAB_FEATURE_CONFIG_H

/*
 * Optional RA8P1 feature policy for this showcase project.
 *
 * The feature drivers are always built so their implementation is checked,
 * but hardware that has not been confirmed on the CPKHMI board stays disabled.
 * Enabling SDRAM also requires matching FSP pin/timing configuration and the
 * populated memory device capacity from the board schematic/BOM.
 */
#define DAB_FEATURE_SDRAM_ENABLE             (0U)
#define DAB_FEATURE_SDRAM_BASE               (0x68000000UL)
#define DAB_FEATURE_SDRAM_CAPACITY_BYTES     (0UL)
#define DAB_FEATURE_SDRAM_TEST_BYTES         (4096UL)

/* CPU0<->CPU1 message FIFO.  Enable only after a matching M33 image exists. */
#define DAB_FEATURE_IPC_ENABLE               (0U)
#define DAB_FEATURE_IPC_CHANNEL              (0U)
#define DAB_FEATURE_IPC_HEARTBEAT_MS         (1000UL)
#define DAB_FEATURE_IPC_PEER_STALE_MS        (3000UL)

#if (DAB_FEATURE_SDRAM_ENABLE != 0U) && (DAB_FEATURE_SDRAM_CAPACITY_BYTES == 0UL)
 #error "Set the confirmed physical SDRAM capacity before enabling SDRAM"
#endif

#if (DAB_FEATURE_IPC_CHANNEL > 1U)
 #error "RA8P1 IPC message FIFO channel must be 0 or 1"
#endif

#endif /* DAB_FEATURE_CONFIG_H */
