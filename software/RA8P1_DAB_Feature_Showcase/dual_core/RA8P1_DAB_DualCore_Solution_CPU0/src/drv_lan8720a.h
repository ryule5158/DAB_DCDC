#ifndef DRV_LAN8720A_H
#define DRV_LAN8720A_H

#include "hal_data.h"

#include <stdbool.h>
#include <stdint.h>

typedef fsp_err_t (*lan8720a_mdio_read_t)(uint8_t phy, uint8_t reg,
                                          uint16_t * value);
typedef fsp_err_t (*lan8720a_mdio_write_t)(uint8_t phy, uint8_t reg,
                                           uint16_t value);

typedef struct st_lan8720a
{
    uint8_t phy_address;
    lan8720a_mdio_read_t  read;
    lan8720a_mdio_write_t write;
} lan8720a_t;

fsp_err_t lan8720a_init(lan8720a_t const * p_phy);
fsp_err_t lan8720a_link_get(lan8720a_t const * p_phy, bool * p_link_up);
fsp_err_t lan8720a_ids_read(lan8720a_t const * p_phy,
                            uint16_t * p_id1,
                            uint16_t * p_id2);

#endif /* DRV_LAN8720A_H */
