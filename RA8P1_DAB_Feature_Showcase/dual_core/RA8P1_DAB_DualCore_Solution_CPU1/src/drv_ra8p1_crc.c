#include "drv_ra8p1_crc.h"

#include "bsp_api.h"

#include <string.h>

#define RA8P1_CRC32_POLYNOMIAL_SELECT    (4U)
#define RA8P1_CRC32_INITIAL_VALUE        (0xFFFFFFFFUL)
#define RA8P1_CRC32_FINAL_XOR            (0xFFFFFFFFUL)
#define RA8P1_CRC32_SELF_TEST_EXPECTED   (0xE0E8FF4DUL)

static ra8p1_crc_status_t s_status;

static void drv_ra8p1_crc_configure(void)
{
    R_BSP_MODULE_START(FSP_IP_CRC, 0U);
    R_CRC->CRCCR0 =
        (uint8_t) ((RA8P1_CRC32_POLYNOMIAL_SELECT << R_CRC_CRCCR0_GPS_Pos) |
                   R_CRC_CRCCR0_LMS_Msk |
                   R_CRC_CRCCR0_DORCLR_Msk);
    R_CRC->CRCCR1 = 0U;
    __DSB();
}

bool drv_ra8p1_crc32(void const * p_data, size_t bytes, uint32_t * p_crc)
{
    if (!s_status.initialized || (NULL == p_data) || (NULL == p_crc) ||
        (0U == bytes) || (0U != (bytes & 3U)))
    {
        return false;
    }

    uint8_t const * p_bytes = (uint8_t const *) p_data;
    R_CRC->CRCDOR = RA8P1_CRC32_INITIAL_VALUE;
    for (size_t offset = 0U; offset < bytes; offset += sizeof(uint32_t))
    {
        uint32_t word;
        memcpy(&word, &p_bytes[offset], sizeof(word));
        R_CRC->CRCDIR = word;
    }
    __DMB();

    *p_crc = R_CRC->CRCDOR ^ RA8P1_CRC32_FINAL_XOR;
    s_status.last_result = *p_crc;
    s_status.calculation_count++;
    return true;
}

bool drv_ra8p1_crc_initialize(void)
{
    static uint32_t const test_words[4] =
    {
        0x44434241UL, 0x48474645UL, 0x4C4B4A49UL, 0x504F4E4DUL
    };
    uint32_t crc = 0U;

    memset(&s_status, 0, sizeof(s_status));
    drv_ra8p1_crc_configure();
    s_status.initialized = true;
    s_status.self_test_passed =
        drv_ra8p1_crc32(test_words, sizeof(test_words), &crc) &&
        (RA8P1_CRC32_SELF_TEST_EXPECTED == crc);
    return s_status.self_test_passed;
}

void drv_ra8p1_crc_status_get(ra8p1_crc_status_t * p_status)
{
    if (NULL != p_status)
    {
        *p_status = s_status;
    }
}
