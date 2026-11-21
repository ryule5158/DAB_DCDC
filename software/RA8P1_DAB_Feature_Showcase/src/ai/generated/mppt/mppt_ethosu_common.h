#ifndef DAB_RUHMI_MPPT_MPPT_ETHOSU_COMMON_H_INCLUDED
#define DAB_RUHMI_MPPT_MPPT_ETHOSU_COMMON_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

typedef struct {
  const char* name;
  int32_t tensor_index;
  size_t size;
  const char* region;
  uint32_t address;
} mppt_TensorInfo;

#endif // DAB_RUHMI_MPPT_MPPT_ETHOSU_COMMON_H_INCLUDED
