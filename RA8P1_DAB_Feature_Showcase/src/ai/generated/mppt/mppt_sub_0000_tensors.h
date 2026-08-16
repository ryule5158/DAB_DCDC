#ifndef DAB_RUHMI_MPPT_MPPT_SUB_0000_TENSORS_H_INCLUDED
#define DAB_RUHMI_MPPT_MPPT_SUB_0000_TENSORS_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include "mppt_ethosu_common.h"

extern const mppt_TensorInfo mppt_sub_0000_tensors[];
extern const size_t mppt_sub_0000_tensors_count;

#define mppt_kArenaSize_sub_0000 288

// Addresses for each input and output buffer inside of the arena
extern const uint32_t mppt_sub_0000_address_serving_default_power_scan_0;
extern const uint32_t mppt_sub_0000_address_StatefulPartitionedCall_0_70011;


#endif // DAB_RUHMI_MPPT_MPPT_SUB_0000_TENSORS_H_INCLUDED
