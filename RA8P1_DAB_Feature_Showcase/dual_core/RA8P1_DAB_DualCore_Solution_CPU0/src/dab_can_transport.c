#include "dab_can_transport.h"

#include "hal_data.h"
#include "drv_tcan3413.h"

#include <string.h>

#define DAB_CAN_RX_QUEUE_DEPTH    (8U)
#define DAB_CAN_TX_QUEUE_DEPTH    (8U)

static can_frame_t          s_rx_queue[DAB_CAN_RX_QUEUE_DEPTH];
static volatile uint8_t     s_rx_head;
static volatile uint8_t     s_rx_tail;
static can_frame_t          s_tx_queue[DAB_CAN_TX_QUEUE_DEPTH];
static uint8_t              s_tx_head;
static uint8_t              s_tx_tail;
static volatile bool        s_tx_busy;
static volatile dab_can_transport_status_t s_status;

const canfd_afl_entry_t p_canfd0_afl[CANFD_CFG_AFL_CH0_RULE_NUM] =
{
    {
        .id =
        {
            .id         = DAB_CAN_ID_HOST_COMMAND,
            .frame_type = CAN_FRAME_TYPE_DATA,
            .id_mode    = CAN_ID_MODE_STANDARD
        },
        .mask =
        {
            .mask_id         = 0x7FFU,
            .mask_frame_type = 1U,
            .mask_id_mode    = 1U
        },
        .destination =
        {
            .minimum_dlc     = CANFD_MINIMUM_DLC_0,
            .rx_buffer       = CANFD_RX_MB_NONE,
            .fifo_select_flags = CANFD_RX_FIFO_0
        }
    }
};

static uint8_t dab_can_next(uint8_t current, uint8_t depth)
{
    return (uint8_t) ((current + 1U) % depth);
}

static bool dab_can_tx_queue(uint32_t id,
                             uint8_t const data[DAB_CAN_CLASSIC_DLC])
{
    if (!s_status.initialized)
    {
        return false;
    }

    uint8_t const next = dab_can_next(s_tx_head, DAB_CAN_TX_QUEUE_DEPTH);
    if (next == s_tx_tail)
    {
        s_status.tx_dropped++;
        return false;
    }

    can_frame_t * const p_frame = &s_tx_queue[s_tx_head];
    p_frame->id = id;
    p_frame->id_mode = CAN_ID_MODE_STANDARD;
    p_frame->type = CAN_FRAME_TYPE_DATA;
    p_frame->data_length_code = DAB_CAN_CLASSIC_DLC;
    p_frame->options = 0U;
    memcpy(p_frame->data, data, DAB_CAN_CLASSIC_DLC);
    s_tx_head = next;
    return true;
}

bool dab_can_transport_initialize(void)
{
    s_status.initialized = false;
    s_status.bus_off = false;
    s_status.open_error = 0;
    s_status.rx_frames = 0U;
    s_status.tx_frames = 0U;
    s_status.rx_dropped = 0U;
    s_status.tx_dropped = 0U;
    s_status.error_events = 0U;
    s_status.last_error = 0U;
    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_tx_head = 0U;
    s_tx_tail = 0U;
    s_tx_busy = false;

    if (!tcan3413_board_normal_mode())
    {
        s_status.open_error = (int32_t) FSP_ERR_INVALID_MODE;
        return false;
    }

    fsp_err_t const err = R_CANFD_Open(&g_canfd0_ctrl, &g_canfd0_cfg);
    s_status.open_error = (int32_t) err;
    s_status.initialized = (FSP_SUCCESS == err);
    return s_status.initialized;
}

void dab_can_transport_service(void)
{
    if (!s_status.initialized || s_tx_busy || (s_tx_head == s_tx_tail))
    {
        return;
    }

    s_tx_busy = true;
    fsp_err_t const err = R_CANFD_Write(&g_canfd0_ctrl,
                                        CANFD_TX_MB_0,
                                        &s_tx_queue[s_tx_tail]);
    if (FSP_SUCCESS == err)
    {
        s_tx_tail = dab_can_next(s_tx_tail, DAB_CAN_TX_QUEUE_DEPTH);
    }
    else
    {
        s_tx_busy = false;
        if (FSP_ERR_CAN_TRANSMIT_NOT_READY != err)
        {
            s_status.error_events++;
            s_status.last_error = (uint32_t) err;
        }
    }
}

bool dab_can_transport_command_take(dab_can_command_t * const p_command)
{
    if ((NULL == p_command) || (s_rx_head == s_rx_tail))
    {
        return false;
    }

    can_frame_t const frame = s_rx_queue[s_rx_tail];
    s_rx_tail = dab_can_next(s_rx_tail, DAB_CAN_RX_QUEUE_DEPTH);
    *p_command = dab_can_command_decode(frame.data, frame.data_length_code);
    return true;
}

bool dab_can_transport_ack_queue(
    uint8_t opcode,
    uint8_t sequence,
    dab_can_result_t result,
    dab_can_application_status_t const * const p_status)
{
    uint8_t data[DAB_CAN_CLASSIC_DLC];
    dab_can_ack_encode(data,
                       opcode,
                       sequence,
                       result,
                       p_status,
                       (uint8_t) s_status.rx_dropped,
                       (uint8_t) s_status.error_events);
    return dab_can_tx_queue(DAB_CAN_ID_BOARD_ACK, data);
}

bool dab_can_transport_status_queue(
    dab_can_application_status_t const * const p_status,
    dab_can_measurements_t const * const p_measurements)
{
    uint8_t data[DAB_CAN_CLASSIC_DLC];
    dab_can_status_encode(data, p_status);
    bool const status_queued = dab_can_tx_queue(DAB_CAN_ID_BOARD_STATUS, data);
    dab_can_measurements_encode(data, p_measurements);
    bool const measurements_queued =
        dab_can_tx_queue(DAB_CAN_ID_BOARD_MEASUREMENTS, data);
    return status_queued && measurements_queued;
}

void dab_can_transport_status_get(dab_can_transport_status_t * const p_status)
{
    if (NULL != p_status)
    {
        p_status->initialized = s_status.initialized;
        p_status->bus_off = s_status.bus_off;
        p_status->open_error = s_status.open_error;
        p_status->rx_frames = s_status.rx_frames;
        p_status->tx_frames = s_status.tx_frames;
        p_status->rx_dropped = s_status.rx_dropped;
        p_status->tx_dropped = s_status.tx_dropped;
        p_status->error_events = s_status.error_events;
        p_status->last_error = s_status.last_error;
    }
}

void dab_canfd_callback(can_callback_args_t * const p_args)
{
    if (NULL == p_args)
    {
        return;
    }

    if (0U != ((uint32_t) p_args->event & (uint32_t) CAN_EVENT_RX_COMPLETE))
    {
        uint8_t const next = dab_can_next(s_rx_head, DAB_CAN_RX_QUEUE_DEPTH);
        if (next == s_rx_tail)
        {
            s_status.rx_dropped++;
        }
        else if ((DAB_CAN_ID_HOST_COMMAND == p_args->frame.id) &&
                 (CAN_ID_MODE_STANDARD == p_args->frame.id_mode) &&
                 (CAN_FRAME_TYPE_DATA == p_args->frame.type) &&
                 (0U == p_args->frame.options))
        {
            s_rx_queue[s_rx_head] = p_args->frame;
            s_rx_head = next;
            s_status.rx_frames++;
        }
    }

    if (0U != ((uint32_t) p_args->event & (uint32_t) CAN_EVENT_TX_COMPLETE))
    {
        s_tx_busy = false;
        s_status.tx_frames++;
    }
    if (0U != ((uint32_t) p_args->event & (uint32_t) CAN_EVENT_ERR_BUS_OFF))
    {
        s_tx_busy = false;
        s_status.bus_off = true;
    }
    if (0U != ((uint32_t) p_args->event & (uint32_t) CAN_EVENT_BUS_RECOVERY))
    {
        s_status.bus_off = false;
    }

    uint32_t const error_events = (uint32_t) p_args->event &
        ((uint32_t) CAN_EVENT_ERR_WARNING |
         (uint32_t) CAN_EVENT_ERR_PASSIVE |
         (uint32_t) CAN_EVENT_ERR_BUS_OFF |
         (uint32_t) CAN_EVENT_MAILBOX_MESSAGE_LOST |
         (uint32_t) CAN_EVENT_ERR_BUS_LOCK |
         (uint32_t) CAN_EVENT_ERR_CHANNEL |
         (uint32_t) CAN_EVENT_TX_ABORTED |
         (uint32_t) CAN_EVENT_ERR_GLOBAL |
         (uint32_t) CAN_EVENT_FIFO_MESSAGE_LOST);
    if (0U != error_events)
    {
        s_status.error_events++;
        s_status.last_error = p_args->error;
    }
}
