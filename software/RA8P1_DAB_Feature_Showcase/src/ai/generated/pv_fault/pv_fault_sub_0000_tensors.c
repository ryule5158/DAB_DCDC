#include "pv_fault_sub_0000_tensors.h"

const pv_fault_TensorInfo pv_fault_sub_0000_tensors[] = {
  { "_split_1_command_stream", 0, 2468, "COMMAND_STREAM", 0xffffffff },
  { "_split_1_flash", 1, 22544, "MODEL", 0xffffffff },
  { "_split_1_scratch", 2, 4096, "ARENA", 0x0 },
  { "_split_1_scratch_fast", 3, 4096, "FAST_SCRATCH", 0x0 },
  { "serving_default_input_1_0", 5, 96, "INPUT_TENSOR", 0x800 },
  { "sequential_quant_dense_Softmax_70011", 4, 3, "OUTPUT_TENSOR", 0x0 },
};

const size_t pv_fault_sub_0000_tensors_count = sizeof(pv_fault_sub_0000_tensors) / sizeof(pv_fault_sub_0000_tensors[0]);

// Addresses for each input and output buffer inside of the arena
const uint32_t pv_fault_sub_0000_address_serving_default_input_1_0 = 0x800;
const uint32_t pv_fault_sub_0000_address_sequential_quant_dense_Softmax_70011 = 0x0;

