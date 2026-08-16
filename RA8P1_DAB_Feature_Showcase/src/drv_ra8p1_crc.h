#ifndef DRV_RA8P1_CRC_H
#define DRV_RA8P1_CRC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct st_ra8p1_crc_status
{
    bool initialized;
    bool self_test_passed;
    uint32_t calculation_count;
    uint32_t last_result;
} ra8p1_crc_status_t;

/*
 * Hardware CRC-32/ISO-HDLC: reflected input, seed 0xFFFFFFFF and final XOR.
 * The peripheral is global, so calls must remain serialized by the application.
 */
bool drv_ra8p1_crc_initialize(void);
bool drv_ra8p1_crc32(void const * p_data, size_t bytes, uint32_t * p_crc);
bool drv_ra8p1_crc_self_test(void);
void drv_ra8p1_crc_status_get(ra8p1_crc_status_t * p_status);

#endif /* DRV_RA8P1_CRC_H */
