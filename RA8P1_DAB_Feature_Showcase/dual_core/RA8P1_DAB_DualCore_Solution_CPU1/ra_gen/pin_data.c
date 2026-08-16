/* generated pin source file - do not edit */
#include "bsp_api.h"
#include "r_ioport.h"

#if BSP_TZ_SECURE_BUILD

void R_BSP_PinCfgSecurityInit(void);

/* Initialize SAR registers for secure pins. */
void R_BSP_PinCfgSecurityInit(void)
{
 #if (2U == BSP_FEATURE_IOPORT_VERSION)
    uint32_t pmsar[BSP_FEATURE_BSP_NUM_PMSAR];
 #else
    uint16_t pmsar[BSP_FEATURE_BSP_NUM_PMSAR];
 #endif
    memset(pmsar, 0xFF, BSP_FEATURE_BSP_NUM_PMSAR * sizeof(R_PMISC->PMSAR[0]));

}
#endif
