#ifndef DAB_ACCEL_H
#define DAB_ACCEL_H

/*
 * RA8P1 Cortex-M85 acceleration policy.
 *
 * Hot deterministic code is copied from internal flash to ITCM by the FSP
 * startup copy table.  Hot CPU-owned state is zeroed in DTCM.  DMA targets
 * must remain in non-cacheable system SRAM because bus masters do not share
 * the CPU-local TCM path.
 */
#if defined(__GNUC__)
 #define DAB_ITCM_CODE  __attribute__((section(".itcm_code_from_flash"), noinline))
 #define DAB_DTCM_ZERO  __attribute__((section(".dtcm"), aligned(16)))
 #define DAB_DTCM_INIT  __attribute__((section(".dtcm_from_flash"), aligned(16)))
 #define DAB_DMA_RAM    __attribute__((section(".ram_nocache"), aligned(32)))
#else
 #define DAB_ITCM_CODE
 #define DAB_DTCM_ZERO
 #define DAB_DTCM_INIT
 #define DAB_DMA_RAM
#endif

#if defined(__ARM_FEATURE_MVE) && ((__ARM_FEATURE_MVE & 2) != 0)
 #define DAB_HELIUM_FLOAT_ENABLED  (1U)
#else
 #define DAB_HELIUM_FLOAT_ENABLED  (0U)
#endif

#endif /* DAB_ACCEL_H */
