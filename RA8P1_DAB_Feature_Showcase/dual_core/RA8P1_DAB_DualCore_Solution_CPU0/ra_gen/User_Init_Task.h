/* generated thread header file - do not edit */
#ifndef USER_INIT_TASK_H_
#define USER_INIT_TASK_H_
#include "bsp_api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hal_data.h"
#ifdef __cplusplus
                extern "C" void User_Init_Task_entry(void * pvParameters);
                #else
extern void User_Init_Task_entry(void *pvParameters);
#endif
FSP_HEADER
FSP_FOOTER
#endif /* USER_INIT_TASK_H_ */
