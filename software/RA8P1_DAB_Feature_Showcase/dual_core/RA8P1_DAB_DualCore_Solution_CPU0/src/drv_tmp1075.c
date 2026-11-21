#include "drv_tmp1075.h"

#include "ra8p1_peripheral_test_config.h"

#if !RA8P1_PERIPHERAL_TEST_ENABLE

#include "FreeRTOS.h"
#include "task.h"

#define TMP1075_REG_TEMPERATURE    (0x00U)
#define TMP1075_REG_CONFIGURATION  (0x01U)
#define TMP1075_REG_DEVICE_ID      (0x0FU)
#define TMP1075_I2C_TIMEOUT_MS     (25U)

static volatile i2c_master_event_t s_event;
static volatile bool               s_done;
static bool                        s_open;

void i2c_master_callback(i2c_master_callback_args_t * p_args)
{
    if (NULL == p_args)
    {
        return;
    }

    s_event = p_args->event;
    if ((I2C_MASTER_EVENT_TX_COMPLETE == p_args->event) ||
        (I2C_MASTER_EVENT_RX_COMPLETE == p_args->event) ||
        (I2C_MASTER_EVENT_ABORTED == p_args->event))
    {
        s_done = true;
    }
}

static bool wait_for(i2c_master_event_t expected)
{
    TickType_t const deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(TMP1075_I2C_TIMEOUT_MS);
    while (!s_done && ((int32_t) (deadline - xTaskGetTickCount()) > 0))
    {
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
    return s_done && (expected == s_event);
}

static fsp_err_t register_read(uint8_t reg, uint16_t * p_value)
{
    if ((!s_open) || (NULL == p_value))
    {
        return FSP_ERR_NOT_OPEN;
    }

    uint8_t data[2] = {0U, 0U};
    s_done  = false;
    s_event = I2C_MASTER_EVENT_ABORTED;
    fsp_err_t err = R_IIC_MASTER_Write(&g_i2c2_ctrl, &reg, 1U, true);
    if ((FSP_SUCCESS != err) || !wait_for(I2C_MASTER_EVENT_TX_COMPLETE))
    {
        return (FSP_SUCCESS == err) ? FSP_ERR_TIMEOUT : err;
    }

    s_done  = false;
    s_event = I2C_MASTER_EVENT_ABORTED;
    err = R_IIC_MASTER_Read(&g_i2c2_ctrl, data, sizeof(data), false);
    if ((FSP_SUCCESS != err) || !wait_for(I2C_MASTER_EVENT_RX_COMPLETE))
    {
        return (FSP_SUCCESS == err) ? FSP_ERR_TIMEOUT : err;
    }

    *p_value = (uint16_t) (((uint16_t) data[0] << 8U) | data[1]);
    return FSP_SUCCESS;
}

fsp_err_t tmp1075_temperature_read(float * p_temperature_c)
{
    if (NULL == p_temperature_c)
    {
        return FSP_ERR_ASSERTION;
    }

    uint16_t value = 0U;
    fsp_err_t const err = register_read(TMP1075_REG_TEMPERATURE, &value);
    if (FSP_SUCCESS == err)
    {
        int16_t signed_value = (int16_t) value;
        signed_value >>= 4;
        *p_temperature_c = (float) signed_value * 0.0625F;
    }
    return err;
}

fsp_err_t tmp1075_open(tmp1075_info_t * p_info)
{
    if (NULL == p_info)
    {
        return FSP_ERR_ASSERTION;
    }

    fsp_err_t err = R_IIC_MASTER_Open(&g_i2c2_ctrl, &g_i2c2_cfg);
    if ((FSP_SUCCESS != err) && (FSP_ERR_ALREADY_OPEN != err))
    {
        return err;
    }
    s_open = true;

    err = register_read(TMP1075_REG_DEVICE_ID, &p_info->device_id);
    if (FSP_SUCCESS != err)
    {
        s_open = false;
        return err;
    }
    if (0x7500U != p_info->device_id)
    {
        s_open = false;
        return FSP_ERR_INVALID_HW_CONDITION;
    }

    err = register_read(TMP1075_REG_CONFIGURATION, &p_info->configuration);
    if (FSP_SUCCESS != err)
    {
        s_open = false;
        return err;
    }
    err = tmp1075_temperature_read(&p_info->temperature_c);
    if (FSP_SUCCESS != err)
    {
        s_open = false;
    }
    return err;
}

bool tmp1075_is_open(void)
{
    return s_open;
}

#endif /* !RA8P1_PERIPHERAL_TEST_ENABLE */
