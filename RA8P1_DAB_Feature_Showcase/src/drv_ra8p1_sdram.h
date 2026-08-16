#ifndef DRV_RA8P1_SDRAM_H
#define DRV_RA8P1_SDRAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum e_ra8p1_sdram_state
{
    RA8P1_SDRAM_DISABLED = 0,
    RA8P1_SDRAM_FSP_DISABLED,
    RA8P1_SDRAM_CONTROLLER_OFF,
    RA8P1_SDRAM_READY,
    RA8P1_SDRAM_TEST_FAILED
} ra8p1_sdram_state_t;

typedef struct st_ra8p1_sdram_status
{
    ra8p1_sdram_state_t state;
    uintptr_t base;
    size_t capacity_bytes;
    size_t allocated_bytes;
    uint32_t last_test_words;
    uint32_t last_test_failures;
} ra8p1_sdram_status_t;

void drv_ra8p1_sdram_initialize(void);
void drv_ra8p1_sdram_status_get(ra8p1_sdram_status_t * p_status);

/* Bump allocator for waveform/history/model buffers; no free operation. */
bool drv_ra8p1_sdram_buffer_claim(size_t bytes, size_t alignment, void ** pp_buffer);

/* Save/write/read/restore a bounded region. Call only with power paths OFF. */
bool drv_ra8p1_sdram_quick_test(void);

char const * drv_ra8p1_sdram_state_name(ra8p1_sdram_state_t state);

#endif /* DRV_RA8P1_SDRAM_H */
