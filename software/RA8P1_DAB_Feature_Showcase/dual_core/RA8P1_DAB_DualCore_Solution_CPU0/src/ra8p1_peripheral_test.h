#ifndef RA8P1_PERIPHERAL_TEST_H
#define RA8P1_PERIPHERAL_TEST_H

#include "ra8p1_peripheral_test_config.h"

#if RA8P1_PERIPHERAL_TEST_ENABLE

/* FreeRTOS task entry supplied by the isolated commissioning module. */
void User_Init_Task_entry(void * pvParameters);

#endif

#endif /* RA8P1_PERIPHERAL_TEST_H */
