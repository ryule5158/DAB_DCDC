#include "../RA8P1_DAB_DualCore_Solution_CPU0/src/dab_can_protocol.h"

#include <stdint.h>

static int s_failures;
#define assert(condition) do { if (!(condition)) { s_failures++; } } while (0)
#define puts(message)     ((void) (message))

static void test_decode(void)
{
    uint8_t ping[8] = {DAB_CAN_PROTOCOL_VERSION, DAB_CAN_OPCODE_PING, 0x5AU};
    dab_can_command_t command = dab_can_command_decode(ping, 8U);
    assert(DAB_CAN_RESULT_OK == command.decode_result);
    assert(DAB_CAN_OPCODE_PING == command.opcode);
    assert(0x5AU == command.sequence);

    uint8_t cp[8] = {DAB_CAN_PROTOCOL_VERSION, DAB_CAN_OPCODE_SET_CP_TARGET,
                     7U, 0xD0U, 0x07U}; /* 2000 deciwatts = 200 W */
    command = dab_can_command_decode(cp, 8U);
    assert(DAB_CAN_RESULT_OK == command.decode_result);
    assert((command.cp_target_w - 200.0F) < 0.01F);

    command = dab_can_command_decode(cp, 4U);
    assert(DAB_CAN_RESULT_BAD_DLC == command.decode_result);
    cp[0] = 2U;
    command = dab_can_command_decode(cp, 8U);
    assert(DAB_CAN_RESULT_BAD_VERSION == command.decode_result);
    cp[0] = DAB_CAN_PROTOCOL_VERSION;
    cp[1] = 0xFFU;
    command = dab_can_command_decode(cp, 8U);
    assert(DAB_CAN_RESULT_BAD_OPCODE == command.decode_result);
}

static void test_encode(void)
{
    dab_can_application_status_t status =
    {
        .state = 1U,
        .architecture = 3U,
        .modulation = 2U,
        .flags = 0x63U,
        .fault_mask = 0x00ABCDEFU
    };
    uint8_t data[8];
    dab_can_status_encode(data, &status);
    assert(DAB_CAN_PROTOCOL_VERSION == data[0]);
    assert(1U == data[1] && 3U == data[2] && 2U == data[3]);
    assert(0xEFU == data[5] && 0xCDU == data[6] && 0xABU == data[7]);

    dab_can_measurements_t measurements =
    {
        .vin_v = 23.44F,
        .vout_v = 41.04F,
        .iout_a = -1.25F,
        .pout_w = 207.4F
    };
    dab_can_measurements_encode(data, &measurements);
    assert(234U == dab_can_u16_le_get(&data[0]));
    assert(410U == dab_can_u16_le_get(&data[2]));
    assert(-125 == (int16_t) dab_can_u16_le_get(&data[4]));
    assert(2074 == (int16_t) dab_can_u16_le_get(&data[6]));

    dab_can_ack_encode(data, DAB_CAN_OPCODE_STOP, 9U, DAB_CAN_RESULT_OK,
                       &status, 2U, 3U);
    assert(DAB_CAN_OPCODE_STOP == data[1] && 9U == data[2]);
    assert(DAB_CAN_RESULT_OK == data[3]);
    assert(2U == data[6] && 3U == data[7]);
}

int main(void)
{
    test_decode();
    test_encode();
    puts("dab_can_protocol tests passed");
    return s_failures;
}
