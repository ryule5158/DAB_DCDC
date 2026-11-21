#ifndef DRV_TMP1075_H
#define DRV_TMP1075_H

#include "hal_data.h"

#include <stdbool.h>
#include <stdint.h>

#define TMP1075_DEFAULT_ADDRESS  (0x48U)

typedef struct st_tmp1075_info
{
    uint16_t device_id;
    uint16_t configuration;
    float    temperature_c;
} tmp1075_info_t;

fsp_err_t tmp1075_open(tmp1075_info_t * p_info);
fsp_err_t tmp1075_temperature_read(float * p_temperature_c);
bool      tmp1075_is_open(void);

/* FSP callback configured for g_i2c2. */
void i2c_master_callback(i2c_master_callback_args_t * p_args);

#endif /* DRV_TMP1075_H */
