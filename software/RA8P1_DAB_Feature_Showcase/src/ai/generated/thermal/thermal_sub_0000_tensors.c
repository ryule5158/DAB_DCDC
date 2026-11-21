#include "thermal_sub_0000_tensors.h"

const thermal_TensorInfo thermal_sub_0000_tensors[] = {
  { "_split_1_command_stream", 1, 2588, "COMMAND_STREAM", 0xffffffff },
  { "_split_1_flash", 2, 82128, "MODEL", 0xffffffff },
  { "_split_1_scratch", 3, 1792, "ARENA", 0x0 },
  { "_split_1_scratch_fast", 4, 1792, "FAST_SCRATCH", 0x0 },
  { "serving_default_thermal_input_0", 5, 8, "INPUT_TENSOR", 0x300 },
  { "StatefulPartitionedCall_0_70014", 0, 3, "OUTPUT_TENSOR", 0x0 },
};

const size_t thermal_sub_0000_tensors_count = sizeof(thermal_sub_0000_tensors) / sizeof(thermal_sub_0000_tensors[0]);

// Addresses for each input and output buffer inside of the arena
const uint32_t thermal_sub_0000_address_serving_default_thermal_input_0 = 0x300;
const uint32_t thermal_sub_0000_address_StatefulPartitionedCall_0_70014 = 0x0;

