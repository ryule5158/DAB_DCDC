#include "drv_ra8p1_ipc.h"

#include "bsp_api.h"
#include "dab_feature_config.h"

#include <string.h>

#define IPC_TYPE_SHIFT       (28U)
#define IPC_SEQUENCE_SHIFT   (20U)
#define IPC_TYPE_MASK        (0x0FUL)
#define IPC_SEQUENCE_MASK    (0xFFUL)
#define IPC_PAYLOAD_MASK     (0x000FFFFFUL)
#define IPC_CLEAR_ERRORS     ((1UL << 24U) | (1UL << 25U))

static ra8p1_ipc_status_t s_status;
static uint32_t s_last_heartbeat_ms;

static volatile R_IPC_IPC_CH_Type * drv_ra8p1_ipc_tx_channel(void)
{
#if BSP_CFG_CPU_CORE == 0
    return &R_IPC->IPC1.CH[DAB_FEATURE_IPC_CHANNEL];
#else
    return &R_IPC->IPC0.CH[DAB_FEATURE_IPC_CHANNEL];
#endif
}

static volatile R_IPC_IPC_CH_Type * drv_ra8p1_ipc_rx_channel(void)
{
#if BSP_CFG_CPU_CORE == 0
    return &R_IPC->IPC0.CH[DAB_FEATURE_IPC_CHANNEL];
#else
    return &R_IPC->IPC1.CH[DAB_FEATURE_IPC_CHANNEL];
#endif
}

static uint32_t drv_ra8p1_ipc_pack(ra8p1_ipc_message_type_t type,
                                   uint8_t sequence,
                                   uint32_t payload)
{
    return ((((uint32_t) type) & IPC_TYPE_MASK) << IPC_TYPE_SHIFT) |
           ((((uint32_t) sequence) & IPC_SEQUENCE_MASK) << IPC_SEQUENCE_SHIFT) |
           (payload & IPC_PAYLOAD_MASK);
}

static ra8p1_ipc_message_t drv_ra8p1_ipc_unpack(uint32_t word)
{
    ra8p1_ipc_message_t message =
    {
        .type = (ra8p1_ipc_message_type_t) ((word >> IPC_TYPE_SHIFT) & IPC_TYPE_MASK),
        .sequence = (uint8_t) ((word >> IPC_SEQUENCE_SHIFT) & IPC_SEQUENCE_MASK),
        .payload = word & IPC_PAYLOAD_MASK,
    };
    return message;
}

void drv_ra8p1_ipc_initialize(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.enabled = (0U != DAB_FEATURE_IPC_ENABLE);
    s_status.local_core = (uint8_t) BSP_CFG_CPU_CORE;
    s_status.channel = (uint8_t) DAB_FEATURE_IPC_CHANNEL;
    s_last_heartbeat_ms = 0U;
    if (s_status.enabled)
    {
        drv_ra8p1_ipc_tx_channel()->CLR = IPC_CLEAR_ERRORS;
        drv_ra8p1_ipc_rx_channel()->CLR = IPC_CLEAR_ERRORS;
        __DSB();
    }
}

bool drv_ra8p1_ipc_try_send(ra8p1_ipc_message_type_t type, uint32_t payload)
{
    if (!s_status.enabled || ((uint32_t) type > IPC_TYPE_MASK) ||
        (0U == (uint32_t) type))
    {
        return false;
    }

    volatile R_IPC_IPC_CH_Type * const channel = drv_ra8p1_ipc_tx_channel();
    if (0U != channel->STA_b.FULL)
    {
        s_status.tx_full_count++;
        return false;
    }

    uint8_t const sequence = s_status.tx_sequence++;
    channel->TXD = drv_ra8p1_ipc_pack(type, sequence, payload);
    __DMB();
    s_status.tx_count++;
    return true;
}

bool drv_ra8p1_ipc_try_receive(ra8p1_ipc_message_t * p_message)
{
    if (!s_status.enabled || (NULL == p_message))
    {
        return false;
    }

    volatile R_IPC_IPC_CH_Type * const channel = drv_ra8p1_ipc_rx_channel();
    if (0U == channel->STA_b.RDY)
    {
        return false;
    }

    uint32_t const word = channel->RXD;
    __DMB();
    *p_message = drv_ra8p1_ipc_unpack(word);
    s_status.last_rx = *p_message;
    s_status.last_rx_sequence = p_message->sequence;
    s_status.rx_count++;
    s_status.peer_seen = true;
    return true;
}

void drv_ra8p1_ipc_service(uint32_t now_ms)
{
    if (!s_status.enabled)
    {
        return;
    }

    ra8p1_ipc_message_t message;
    while (drv_ra8p1_ipc_try_receive(&message))
    {
        s_status.last_rx_ms = now_ms;
    }
    s_status.peer_stale = s_status.peer_seen &&
        ((uint32_t) (now_ms - s_status.last_rx_ms) > DAB_FEATURE_IPC_PEER_STALE_MS);

    if ((uint32_t) (now_ms - s_last_heartbeat_ms) >= DAB_FEATURE_IPC_HEARTBEAT_MS)
    {
        s_last_heartbeat_ms = now_ms;
        (void) drv_ra8p1_ipc_try_send(RA8P1_IPC_MESSAGE_HEARTBEAT,
                                      now_ms & IPC_PAYLOAD_MASK);
    }
}

bool drv_ra8p1_ipc_fault_report(uint32_t fault_mask)
{
    return drv_ra8p1_ipc_try_send(RA8P1_IPC_MESSAGE_FAULT, fault_mask);
}

void drv_ra8p1_ipc_status_get(ra8p1_ipc_status_t * p_status)
{
    if (NULL != p_status)
    {
        *p_status = s_status;
    }
}
