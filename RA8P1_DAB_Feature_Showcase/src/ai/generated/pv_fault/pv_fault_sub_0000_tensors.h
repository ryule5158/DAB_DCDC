#ifndef DAB_RUHMI_PV_FAULT_PV_FAULT_SUB_0000_TENSORS_H_INCLUDED
#define DAB_RUHMI_PV_FAULT_PV_FAULT_SUB_0000_TENSORS_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include "pv_fault_ethosu_common.h"

extern const pv_fault_TensorInfo pv_fault_sub_0000_tensors[];
extern const size_t pv_fault_sub_0000_tensors_count;

#define pv_fault_kArenaSize_sub_0000 4096

// Addresses for each input and output buffer inside of the arena
extern const uint32_t pv_fault_sub_0000_address_serving_default_input_1_0;
extern const uint32_t pv_fault_sub_0000_address_sequential_quant_dense_Softmax_70011;


#endif // DAB_RUHMI_PV_FAULT_PV_FAULT_SUB_0000_TENSORS_H_INCLUDED
