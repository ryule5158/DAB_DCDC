#include "mppt_sub_0000_tensors.h"

const mppt_TensorInfo mppt_sub_0000_tensors[] = {
  { "_split_1_command_stream", 1, 2220, "COMMAND_STREAM", 0xffffffff },
  { "_split_1_flash", 2, 4736, "MODEL", 0xffffffff },
  { "_split_1_scratch", 3, 288, "ARENA", 0x0 },
  { "_split_1_scratch_fast", 4, 288, "FAST_SCRATCH", 0x0 },
  { "serving_default_power_scan_0", 5, 9, "INPUT_TENSOR", 0x90 },
  { "StatefulPartitionedCall_0_70011", 0, 13, "OUTPUT_TENSOR", 0x0 },
};

const size_t mppt_sub_0000_tensors_count = sizeof(mppt_sub_0000_tensors) / sizeof(mppt_sub_0000_tensors[0]);

// Addresses for each input and output buffer inside of the arena
const uint32_t mppt_sub_0000_address_serving_default_power_scan_0 = 0x90;
const uint32_t mppt_sub_0000_address_StatefulPartitionedCall_0_70011 = 0x0;

