#include "dab_usb_console.h"

#include "r_usb_basic.h"
#include "r_usb_pcdc_api.h"

#include <string.h>

#define DAB_USB_RX_PACKET_BYTES   (64U)
#define DAB_USB_RX_RING_BYTES     (1024U)
#define DAB_USB_TX_CHUNK_BYTES    (512U)
#define DAB_USB_TX_RING_BYTES     (16384U)
#define DAB_USB_LINE_CODING_BYTES (7U)
#define DAB_USB_FS_MAX_PACKET      (64U)
#define DAB_USB_ATTACH_DELAY_MS    (20U)

extern usb_descriptor_t g_usb_descriptor;

static usb_instance_ctrl_t s_usb_ctrl;

static uint8_t s_rx_packet[DAB_USB_RX_PACKET_BYTES];
static uint8_t s_rx_ring[DAB_USB_RX_RING_BYTES];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;

static uint8_t s_tx_chunk[DAB_USB_TX_CHUNK_BYTES];
static uint8_t s_tx_ring[DAB_USB_TX_RING_BYTES];
static volatile uint32_t s_tx_head;
static volatile uint32_t s_tx_tail;
static volatile uint32_t s_tx_in_flight;

static volatile bool     s_open;
static volatile bool     s_configured;
static volatile bool     s_read_armed;
static volatile bool     s_tx_busy;
static volatile bool     s_tx_zlp_required;
static volatile bool     s_tx_zlp_in_flight;
static volatile bool     s_disconnect_latched;
static volatile bool     s_reconnect_latched;
static volatile uint32_t s_dropped_bytes;

static usb_pcdc_linecoding_t s_line_coding =
{
    .dw_dte_rate  = 115200U,
    .b_char_format = 0U,
    .b_parity_type = 0U,
    .b_data_bits   = 8U,
    .rsv           = 0U
};

static usb_pcdc_ctrllinestate_t s_control_line_state;

/*
 * Keep this small instance local to the console module.  This lets the code
 * run with the user's current generated hal_data files while configuration.xml
 * records an equivalent FSP instance for future regeneration.
 */
static const usb_cfg_t s_usb_cfg =
{
    .usb_mode           = USB_MODE_PERI,
    .usb_speed          = USB_SPEED_FS,
    .module_number      = USB_IP0,
    .type               = USB_CLASS_PCDC,
    .p_usb_reg          = &g_usb_descriptor,
    .usb_complience_cb  = NULL,
    .irq                = VECTOR_NUMBER_USBFS_INT,
    .irq_r              = VECTOR_NUMBER_USBFS_RESUME,
    .irq_d0             = FSP_INVALID_VECTOR,
    .irq_d1             = FSP_INVALID_VECTOR,
    .hsirq              = FSP_INVALID_VECTOR,
    .hsirq_d0           = FSP_INVALID_VECTOR,
    .hsirq_d1           = FSP_INVALID_VECTOR,
    .ipl                = 12U,
    .ipl_r              = 12U,
    .ipl_d0             = BSP_IRQ_DISABLED,
    .ipl_d1             = BSP_IRQ_DISABLED,
    .hsipl              = BSP_IRQ_DISABLED,
    .hsipl_d0           = BSP_IRQ_DISABLED,
    .hsipl_d1           = BSP_IRQ_DISABLED,
    .p_usb_apl_callback = dab_usb_pcdc_callback,
    .p_context          = NULL,
    .p_transfer_tx      = NULL,
    .p_transfer_rx      = NULL,
    .p_extend           = NULL
};

static void dab_usb_rx_arm(void)
{
    if (s_configured && !s_read_armed)
    {
        if (FSP_SUCCESS == R_USB_Read(&s_usb_ctrl,
                                      s_rx_packet,
                                      sizeof(s_rx_packet),
                                      USB_CLASS_PCDC))
        {
            s_read_armed = true;
        }
    }
}

static void dab_usb_receive_complete(uint32_t length)
{
    if (length > sizeof(s_rx_packet))
    {
        length = sizeof(s_rx_packet);
    }

    for (uint32_t i = 0U; i < length; i++)
    {
        uint32_t const next = (s_rx_head + 1U) & (DAB_USB_RX_RING_BYTES - 1U);
        if (next == s_rx_tail)
        {
            s_dropped_bytes++;
        }
        else
        {
            s_rx_ring[s_rx_head] = s_rx_packet[i];
            s_rx_head = next;
        }
    }
}

static void dab_usb_link_down(void)
{
    s_configured = false;
    s_read_armed = false;
    s_tx_busy = false;
    s_tx_zlp_required = false;
    s_tx_zlp_in_flight = false;
    s_tx_in_flight = 0U;
    s_tx_head = 0U;
    s_tx_tail = 0U;
    s_disconnect_latched = true;
}

void dab_usb_pcdc_callback(usb_event_info_t * p_event, usb_hdl_t task, usb_onoff_t state)
{
    FSP_PARAMETER_NOT_USED(task);
    FSP_PARAMETER_NOT_USED(state);

    if (NULL == p_event)
    {
        return;
    }

    switch (p_event->event)
    {
        case USB_STATUS_CONFIGURED:
            s_configured = true;
            s_reconnect_latched = true;
            dab_usb_rx_arm();
            break;

        case USB_STATUS_READ_COMPLETE:
            s_read_armed = false;
            dab_usb_receive_complete(p_event->data_size);
            break;

        case USB_STATUS_WRITE_COMPLETE:
            if (s_tx_zlp_in_flight)
            {
                s_tx_zlp_in_flight = false;
            }
            else
            {
                s_tx_tail = (s_tx_tail + s_tx_in_flight) &
                            (DAB_USB_TX_RING_BYTES - 1U);
                s_tx_in_flight = 0U;
            }
            s_tx_busy = false;
            break;

        case USB_STATUS_REQUEST:
        {
            uint16_t const request =
                (uint16_t) (p_event->setup.request_type & USB_BREQUEST);
            if (USB_PCDC_SET_LINE_CODING == request)
            {
                (void) R_USB_PeriControlDataGet(&s_usb_ctrl,
                                                (uint8_t *) &s_line_coding,
                                                DAB_USB_LINE_CODING_BYTES);
            }
            else if (USB_PCDC_GET_LINE_CODING == request)
            {
                (void) R_USB_PeriControlDataSet(&s_usb_ctrl,
                                                (uint8_t *) &s_line_coding,
                                                DAB_USB_LINE_CODING_BYTES);
            }
            else if (USB_PCDC_SET_CONTROL_LINE_STATE == request)
            {
                s_control_line_state.bdtr =
                    (uint16_t) ((p_event->setup.request_value >> 0) & 1U);
                s_control_line_state.brts =
                    (uint16_t) ((p_event->setup.request_value >> 1) & 1U);
                (void) R_USB_PeriControlStatusSet(&s_usb_ctrl,
                                                  USB_SETUP_STATUS_ACK);
            }
            break;
        }

        case USB_STATUS_DETACH:
        case USB_STATUS_SUSPEND:
            dab_usb_link_down();
            break;

        case USB_STATUS_RESUME:
        default:
            break;
    }
}

fsp_err_t dab_usb_console_init(void)
{
    if (s_open)
    {
        return FSP_SUCCESS;
    }

    /*
     * USBFS expansion connector contract:
     *   P407 = USB_VBUS, P814 = USB_DP (USB_P), P815 = USB_DM (USB_N).
     * Pin setup is repeated here so operation does not depend on regenerating
     * the user's pre-existing pin_data.c during this migration.
     */
    uint32_t const peripheral = IOPORT_CFG_PERIPHERAL_PIN |
                                IOPORT_PERIPHERAL_USB_FS;
    fsp_err_t err = R_IOPORT_PinCfg(&g_ioport_ctrl,
                                    BSP_IO_PORT_04_PIN_07,
                                    peripheral);
    if (FSP_SUCCESS == err)
    {
        err = R_IOPORT_PinCfg(&g_ioport_ctrl,
                              BSP_IO_PORT_08_PIN_14,
                              peripheral);
    }
    if (FSP_SUCCESS == err)
    {
        err = R_IOPORT_PinCfg(&g_ioport_ctrl,
                              BSP_IO_PORT_08_PIN_15,
                              peripheral);
    }
    if (FSP_SUCCESS == err)
    {
        err = R_USB_Open(&s_usb_ctrl, &s_usb_cfg);
    }
    if (FSP_SUCCESS == err)
    {
        /*
         * The expansion USB-C powers VIN_5V through AP22615, but an
         * explicit pull-up makes attachment deterministic even if VBUS status
         * was not high during R_USB_Open().
         */
        R_BSP_SoftwareDelay(DAB_USB_ATTACH_DELAY_MS, BSP_DELAY_UNITS_MILLISECONDS);
        err = R_USB_PullUp(&s_usb_ctrl, USB_ON);
    }
    if (FSP_SUCCESS == err)
    {
        s_open = true;
    }
    return err;
}

void dab_usb_console_service(void)
{
    dab_usb_rx_arm();

    if (!s_configured || s_tx_busy)
    {
        return;
    }

    if (s_tx_zlp_required)
    {
        /* FSP does not append the required ZLP after a 64-byte multiple. */
        s_tx_zlp_required = false;
        s_tx_zlp_in_flight = true;
        s_tx_busy = true;
        if (FSP_SUCCESS != R_USB_Write(&s_usb_ctrl,
                                       s_tx_chunk,
                                       USB_NULL,
                                       USB_CLASS_PCDC))
        {
            s_tx_zlp_in_flight = false;
            s_tx_zlp_required = true;
            s_tx_busy = false;
        }
        return;
    }

    if (s_tx_head == s_tx_tail)
    {
        return;
    }

    uint32_t length = (s_tx_head - s_tx_tail) & (DAB_USB_TX_RING_BYTES - 1U);
    if (length > sizeof(s_tx_chunk))
    {
        length = sizeof(s_tx_chunk);
    }

    uint32_t first = DAB_USB_TX_RING_BYTES - s_tx_tail;
    if (first > length)
    {
        first = length;
    }
    memcpy(s_tx_chunk, &s_tx_ring[s_tx_tail], first);
    if (first < length)
    {
        memcpy(&s_tx_chunk[first], s_tx_ring, length - first);
    }

    s_tx_in_flight = length;
    s_tx_zlp_required = (0U == (length % DAB_USB_FS_MAX_PACKET));
    s_tx_busy = true;
    if (FSP_SUCCESS != R_USB_Write(&s_usb_ctrl,
                                   s_tx_chunk,
                                   length,
                                   USB_CLASS_PCDC))
    {
        s_tx_in_flight = 0U;
        s_tx_zlp_required = false;
        s_tx_busy = false;
    }
}

size_t dab_usb_console_write(void const * p_data, size_t length)
{
    if ((NULL == p_data) || (0U == length))
    {
        return 0U;
    }

    uint32_t const used =
        (s_tx_head - s_tx_tail) & (DAB_USB_TX_RING_BYTES - 1U);
    uint32_t const free = (DAB_USB_TX_RING_BYTES - 1U) - used;
    if (length > free)
    {
        s_dropped_bytes += (uint32_t) length;
        return 0U;
    }

    uint8_t const * p_bytes = (uint8_t const *) p_data;
    for (size_t i = 0U; i < length; i++)
    {
        s_tx_ring[s_tx_head] = p_bytes[i];
        s_tx_head = (s_tx_head + 1U) & (DAB_USB_TX_RING_BYTES - 1U);
    }

    dab_usb_console_service();
    return length;
}

bool dab_usb_console_read_byte(uint8_t * p_data)
{
    if ((NULL == p_data) || (s_rx_tail == s_rx_head))
    {
        return false;
    }

    *p_data = s_rx_ring[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1U) & (DAB_USB_RX_RING_BYTES - 1U);
    return true;
}

bool dab_usb_console_connected(void)
{
    return s_configured;
}

bool dab_usb_console_take_disconnect(void)
{
    bool const latched = s_disconnect_latched;
    s_disconnect_latched = false;
    return latched;
}

bool dab_usb_console_take_reconnect(void)
{
    bool const latched = s_reconnect_latched;
    s_reconnect_latched = false;
    return latched;
}

uint32_t dab_usb_console_dropped_bytes(void)
{
    return s_dropped_bytes;
}
