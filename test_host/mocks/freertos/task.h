#pragma once
#include <cstring>

#include "FreeRTOS.h"

#ifndef configMAX_TASK_NAME_LEN
#define configMAX_TASK_NAME_LEN 16
#endif

typedef uint32_t StackType_t;
typedef struct {
  char pcTaskName[configMAX_TASK_NAME_LEN];
  uint32_t ulRunTimeCounter;
  UBaseType_t uxCurrentPriority;
} TaskStatus_t;

inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 0; }
inline UBaseType_t uxTaskGetNumberOfTasks() { return 0; }
inline UBaseType_t uxTaskGetSystemState(TaskStatus_t*, UBaseType_t,
                                        uint32_t* total_time) {
  if (total_time != nullptr) *total_time = 0;
  return 0;
}
