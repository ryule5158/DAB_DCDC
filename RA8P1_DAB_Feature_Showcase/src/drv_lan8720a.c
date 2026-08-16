#include "drv_lan8720a.h"

#define LAN8720_REG_BMCR          (0U)
#define LAN8720_REG_BMSR          (1U)
#define LAN8720_REG_ID1           (2U)
#define LAN8720_REG_ID2           (3U)
#define LAN8720_BMCR_RESET        (1U << 15)
#define LAN8720_BMCR_AUTONEG      (1U << 12)
#define LAN8720_BMCR_RESTART_AN   (1U << 9)
#define LAN8720_BMSR_LINK         (1U << 2)

static fsp_err_t validate(lan8720a_t const * p_phy)
{
    return ((NULL != p_phy) && (NULL != p_phy->read) &&
            (NULL != p_phy->write)) ? FSP_SUCCESS : FSP_ERR_ASSERTION;
}

fsp_err_t lan8720a_ids_read(lan8720a_t const * p_phy,
                            uint16_t * p_id1,
                            uint16_t * p_id2)
{
    fsp_err_t err = validate(p_phy);
    if ((FSP_SUCCESS != err) || (NULL == p_id1) || (NULL == p_id2))
    {
        return FSP_ERR_ASSERTION;
    }
    err = p_phy->read(p_phy->phy_address, LAN8720_REG_ID1, p_id1);
    return (FSP_SUCCESS == err) ?
           p_phy->read(p_phy->phy_address, LAN8720_REG_ID2, p_id2) : err;
}

fsp_err_t lan8720a_init(lan8720a_t const * p_phy)
{
    fsp_err_t err = validate(p_phy);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = p_phy->write(p_phy->phy_address, LAN8720_REG_BMCR,
                       LAN8720_BMCR_RESET);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    uint16_t bmcr = LAN8720_BMCR_RESET;
    for (uint32_t wait_ms = 0U; wait_ms < 100U; wait_ms++)
    {
        R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
        err = p_phy->read(p_phy->phy_address, LAN8720_REG_BMCR, &bmcr);
        if ((FSP_SUCCESS == err) && (0U == (bmcr & LAN8720_BMCR_RESET)))
        {
            break;
        }
    }
    if (0U != (bmcr & LAN8720_BMCR_RESET))
    {
        return FSP_ERR_TIMEOUT;
    }

    uint16_t id1 = 0U;
    uint16_t id2 = 0U;
    err = lan8720a_ids_read(p_phy, &id1, &id2);
    if ((FSP_SUCCESS != err) || (0U == id1) || (0xFFFFU == id1))
    {
        return FSP_ERR_INVALID_HW_CONDITION;
    }
    return p_phy->write(p_phy->phy_address, LAN8720_REG_BMCR,
                        LAN8720_BMCR_AUTONEG | LAN8720_BMCR_RESTART_AN);
}

fsp_err_t lan8720a_link_get(lan8720a_t const * p_phy, bool * p_link_up)
{
    if ((FSP_SUCCESS != validate(p_phy)) || (NULL == p_link_up))
    {
        return FSP_ERR_ASSERTION;
    }

    uint16_t bmsr = 0U;
    fsp_err_t err = p_phy->read(p_phy->phy_address, LAN8720_REG_BMSR, &bmsr);
    if (FSP_SUCCESS == err)
    {
        err = p_phy->read(p_phy->phy_address, LAN8720_REG_BMSR, &bmsr);
    }
    if (FSP_SUCCESS == err)
    {
        *p_link_up = (0U != (bmsr & LAN8720_BMSR_LINK));
    }
    return err;
}
