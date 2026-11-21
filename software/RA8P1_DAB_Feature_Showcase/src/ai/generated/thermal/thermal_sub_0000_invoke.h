#ifndef DAB_RUHMI_THERMAL_THERMAL_SUB_0000_INVOKE_H_INCLUDED
#define DAB_RUHMI_THERMAL_THERMAL_SUB_0000_INVOKE_H_INCLUDED

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Declare arenas
extern uint8_t thermal_sub_0000_arena[1792];

// Fast scratch arena not used for Ethos-U55
// We will not create it for now and reuse the address of the other arena
extern uint8_t* thermal_sub_0000_fast_scratch; // size: 1792

int thermal_sub_0000_invoke(bool clean_outputs);


#endif // DAB_RUHMI_THERMAL_THERMAL_SUB_0000_INVOKE_H_INCLUDED
