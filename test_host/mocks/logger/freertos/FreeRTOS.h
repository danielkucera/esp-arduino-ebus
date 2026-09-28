#pragma once

#include <cstddef>
#include <cstdint>

using TaskHandle_t = void*;
using QueueHandle_t = void*;
using SemaphoreHandle_t = void*;
using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;
using TimeOut_t = uint32_t;

struct portMUX_TYPE {
  volatile uint32_t owner;
  uint32_t owner_cpu;
  uint32_t count;
};

#define portMUX_INITIALIZER_UNLOCKED {0, 0, 0}
#define portMAX_DELAY 0xFFFFFFFFUL
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS pdTRUE
#define pdFAIL pdFALSE
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#define portENTER_CRITICAL(mux) \
  do {                          \
  } while (0)
#define portEXIT_CRITICAL(mux) \
  do {                         \
  } while (0)

inline QueueHandle_t xQueueCreate(size_t, size_t) { return nullptr; }
inline BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t) {
  return pdFAIL;
}
inline BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t) {
  return pdFAIL;
}
inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t) { return 0; }
inline void vQueueDelete(QueueHandle_t) {}
inline BaseType_t xTaskCreate(void (*)(void*), const char*, int, void*, int,
                              TaskHandle_t*) {
  return pdFAIL;
}
inline void vTaskDelete(TaskHandle_t) {}
inline void vTaskDelay(TickType_t) {}
