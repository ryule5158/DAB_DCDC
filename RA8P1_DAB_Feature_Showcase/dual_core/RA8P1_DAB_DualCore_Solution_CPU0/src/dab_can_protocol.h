#ifndef DAB_CAN_PROTOCOL_H
#define DAB_CAN_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Conservative host protocol for the board's classic CAN link.
 *
 * The first release deliberately has no remote START command.  A CAN host can
 * identify the firmware, read telemetry, request an immediate stop, and arm a
 * CP target while the converter is already safe in READY.  Power transfer
 * still requires the existing explicit commissioning path.
 */
#define DAB_CAN_PROTOCOL_VERSION             (1U)
#define DAB_CAN_ID_BOARD_STATUS              (0x180U)
#define DAB_CAN_ID_BOARD_MEASUREMENTS        (0x181U)
#define DAB_CAN_ID_BOARD_ACK                 (0x182U)
#define DAB_CAN_ID_HOST_COMMAND              (0x200U)
#define DAB_CAN_CLASSIC_DLC                  (8U)

typedef enum e_dab_can_opcode
{
    DAB_CAN_OPCODE_PING          = 0x01,
    DAB_CAN_OPCODE_GET_STATUS    = 0x02,
    DAB_CAN_OPCODE_STOP          = 0x10,
    DAB_CAN_OPCODE_SET_CP_TARGET = 0x20
} dab_can_opcode_t;

typedef enum e_dab_can_result
{
    DAB_CAN_RESULT_OK              = 0,
    DAB_CAN_RESULT_BAD_VERSION     = 1,
    DAB_CAN_RESULT_BAD_OPCODE      = 2,
    DAB_CAN_RESULT_BAD_DLC         = 3,
    DAB_CAN_RESULT_BAD_STATE       = 4,
    DAB_CAN_RESULT_OUT_OF_RANGE    = 5,
    DAB_CAN_RESULT_QUEUE_FULL      = 6,
    DAB_CAN_RESULT_TRANSPORT_ERROR = 7,
    DAB_CAN_RESULT_NO_VALIDATED_SEED = 8
} dab_can_result_t;

typedef struct st_dab_can_command
{
    uint8_t          opcode;
    uint8_t          sequence;
    dab_can_result_t decode_result;
    float            cp_target_w;
} dab_can_command_t;

typedef struct st_dab_can_application_status
{
    uint8_t  state;
    uint8_t  architecture;
    uint8_t  modulation;
    uint8_t  flags;
    uint32_t fault_mask;
} dab_can_application_status_t;

typedef struct st_dab_can_measurements
{
    float vin_v;
    float vout_v;
    float iout_a;
    float pout_w;
} dab_can_measurements_t;

static inline uint16_t dab_can_u16_le_get(uint8_t const * const p_data)
{
    return (uint16_t) ((uint16_t) p_data[0] |
                       ((uint16_t) p_data[1] << 8));
}

static inline void dab_can_u16_le_put(uint8_t * const p_data, uint16_t value)
{
    p_data[0] = (uint8_t) value;
    p_data[1] = (uint8_t) (value >> 8);
}

static inline uint16_t dab_can_scale_unsigned(float value, float scale)
{
    if (value <= 0.0F)
    {
        return 0U;
    }
    float const scaled = value * scale;
    return (scaled >= 65535.0F) ? 65535U : (uint16_t) (scaled + 0.5F);
}

static inline int16_t dab_can_scale_signed(float value, float scale)
{
    float const scaled = value * scale;
    if (scaled >= 32767.0F)
    {
        return 32767;
    }
    if (scaled <= -32768.0F)
    {
        return -32768;
    }
    return (int16_t) (scaled + ((scaled >= 0.0F) ? 0.5F : -0.5F));
}

static inline dab_can_command_t dab_can_command_decode(uint8_t const * const p_data,
                                                        uint8_t data_length)
{
    dab_can_command_t command = {0};
    command.decode_result = DAB_CAN_RESULT_BAD_DLC;
    if ((NULL == p_data) || (DAB_CAN_CLASSIC_DLC != data_length))
    {
        return command;
    }

    command.opcode = p_data[1];
    command.sequence = p_data[2];
    if (DAB_CAN_PROTOCOL_VERSION != p_data[0])
    {
        command.decode_result = DAB_CAN_RESULT_BAD_VERSION;
        return command;
    }

    switch (command.opcode)
    {
        case DAB_CAN_OPCODE_PING:
        case DAB_CAN_OPCODE_GET_STATUS:
        case DAB_CAN_OPCODE_STOP:
            command.decode_result = DAB_CAN_RESULT_OK;
            break;

        case DAB_CAN_OPCODE_SET_CP_TARGET:
            if (data_length < 5U)
            {
                command.decode_result = DAB_CAN_RESULT_BAD_DLC;
            }
            else
            {
                command.cp_target_w =
                    (float) dab_can_u16_le_get(&p_data[3]) * 0.1F;
                command.decode_result = DAB_CAN_RESULT_OK;
            }
            break;

        default:
            command.decode_result = DAB_CAN_RESULT_BAD_OPCODE;
            break;
    }
    return command;
}

static inline void dab_can_ack_encode(uint8_t p_data[DAB_CAN_CLASSIC_DLC],
                                      uint8_t opcode,
                                      uint8_t sequence,
                                      dab_can_result_t result,
                                      dab_can_application_status_t const * const p_status,
                                      uint8_t rx_dropped,
                                      uint8_t error_events)
{
    p_data[0] = DAB_CAN_PROTOCOL_VERSION;
    p_data[1] = opcode;
    p_data[2] = sequence;
    p_data[3] = (uint8_t) result;
    p_data[4] = (NULL != p_status) ? p_status->state : 0U;
    p_data[5] = (NULL != p_status) ? p_status->architecture : 0U;
    p_data[6] = rx_dropped;
    p_data[7] = error_events;
}

static inline void dab_can_status_encode(
    uint8_t p_data[DAB_CAN_CLASSIC_DLC],
    dab_can_application_status_t const * const p_status)
{
    uint32_t const faults = (NULL != p_status) ? p_status->fault_mask : 0U;
    p_data[0] = DAB_CAN_PROTOCOL_VERSION;
    p_data[1] = (NULL != p_status) ? p_status->state : 0U;
    p_data[2] = (NULL != p_status) ? p_status->architecture : 0U;
    p_data[3] = (NULL != p_status) ? p_status->modulation : 0U;
    p_data[4] = (NULL != p_status) ? p_status->flags : 0U;
    p_data[5] = (uint8_t) faults;
    p_data[6] = (uint8_t) (faults >> 8);
    p_data[7] = (uint8_t) (faults >> 16);
}

static inline void dab_can_measurements_encode(
    uint8_t p_data[DAB_CAN_CLASSIC_DLC],
    dab_can_measurements_t const * const p_measurements)
{
    float const vin = (NULL != p_measurements) ? p_measurements->vin_v : 0.0F;
    float const vout = (NULL != p_measurements) ? p_measurements->vout_v : 0.0F;
    float const iout = (NULL != p_measurements) ? p_measurements->iout_a : 0.0F;
    float const pout = (NULL != p_measurements) ? p_measurements->pout_w : 0.0F;
    dab_can_u16_le_put(&p_data[0], dab_can_scale_unsigned(vin, 10.0F));
    dab_can_u16_le_put(&p_data[2], dab_can_scale_unsigned(vout, 10.0F));
    dab_can_u16_le_put(&p_data[4], (uint16_t) dab_can_scale_signed(iout, 100.0F));
    dab_can_u16_le_put(&p_data[6], (uint16_t) dab_can_scale_signed(pout, 10.0F));
}

#endif /* DAB_CAN_PROTOCOL_H */
