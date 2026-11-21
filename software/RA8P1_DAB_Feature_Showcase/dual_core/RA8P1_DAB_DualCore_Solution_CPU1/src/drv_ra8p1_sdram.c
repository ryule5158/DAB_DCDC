#include "drv_ra8p1_sdram.h"

#include "bsp_api.h"
#include "dab_dual_core_config.h"

#include <string.h>

#define SDRAM_DIAGNOSTIC_RESERVED_BYTES    (4096UL)
#define SDRAM_TEST_PATTERN_A               (0x55AA33CCUL)
#define SDRAM_TEST_PATTERN_B               (0xAA55CC33UL)

static ra8p1_sdram_status_t s_status;

static bool drv_ra8p1_sdram_controller_enabled(void)
{
    return (0U != R_BUS->SDRAM.SDCCR_b.EXENB);
}

void drv_ra8p1_sdram_initialize(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.base = (uintptr_t) DAB_CPU1_SDRAM_BASE;
    s_status.capacity_bytes = (size_t) DAB_CPU1_SDRAM_CAPACITY_BYTES;

    if (0U == DAB_CPU1_SDRAM_ENABLE)
    {
        s_status.state = RA8P1_SDRAM_DISABLED;
    }
    else if (0U == BSP_CFG_SDRAM_ENABLED)
    {
        s_status.state = RA8P1_SDRAM_FSP_DISABLED;
    }
    else if (!drv_ra8p1_sdram_controller_enabled())
    {
        s_status.state = RA8P1_SDRAM_CONTROLLER_OFF;
    }
    else
    {
        s_status.state = RA8P1_SDRAM_READY;
    }
}

void drv_ra8p1_sdram_status_get(ra8p1_sdram_status_t * p_status)
{
    if (NULL != p_status)
    {
        *p_status = s_status;
    }
}

bool drv_ra8p1_sdram_buffer_claim(size_t bytes, size_t alignment, void ** pp_buffer)
{
    if ((NULL == pp_buffer) || (0U == bytes) || (0U == alignment) ||
        (0U != (alignment & (alignment - 1U))) ||
        (RA8P1_SDRAM_READY != s_status.state))
    {
        return false;
    }

    size_t const start = (SDRAM_DIAGNOSTIC_RESERVED_BYTES +
                          s_status.allocated_bytes + alignment - 1U) &
                         ~(alignment - 1U);
    if ((start > s_status.capacity_bytes) ||
        (bytes > (s_status.capacity_bytes - start)))
    {
        return false;
    }

    *pp_buffer = (void *) (s_status.base + start);
    s_status.allocated_bytes = (start + bytes) - SDRAM_DIAGNOSTIC_RESERVED_BYTES;
    return true;
}

bool drv_ra8p1_sdram_quick_test(void)
{
    s_status.last_test_words = 0U;
    s_status.last_test_failures = 0U;
    if ((RA8P1_SDRAM_READY != s_status.state) ||
        !drv_ra8p1_sdram_controller_enabled() ||
        (DAB_CPU1_SDRAM_TEST_BYTES > s_status.capacity_bytes))
    {
        return false;
    }

    volatile uint32_t * const memory =
        (volatile uint32_t *) (uintptr_t) s_status.base;
    uint32_t const words =
        (uint32_t) (DAB_CPU1_SDRAM_TEST_BYTES / sizeof(uint32_t));

    for (uint32_t index = 0U; index < words; index++)
    {
        uint32_t const saved = memory[index];
        uint32_t const pattern = ((0U == (index & 1U)) ? SDRAM_TEST_PATTERN_A :
                                                            SDRAM_TEST_PATTERN_B) ^ index;
        memory[index] = pattern;
        __DMB();
        if (memory[index] != pattern)
        {
            s_status.last_test_failures++;
        }
        memory[index] = saved;
        s_status.last_test_words++;
    }
    __DSB();

    if (0U != s_status.last_test_failures)
    {
        s_status.state = RA8P1_SDRAM_TEST_FAILED;
        return false;
    }
    return true;
}
