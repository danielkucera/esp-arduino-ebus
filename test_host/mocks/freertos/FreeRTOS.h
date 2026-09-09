#pragma once

// Minimal FreeRTOS type definitions for host testing

#ifdef __cplusplus
extern "C" {
#endif

#include <cstddef>
#include <cstdint>

typedef void* TaskHandle_t;
typedef void* QueueHandle_t;
typedef void* SemaphoreHandle_t;

typedef struct {
  volatile uint32_t owner;
  uint32_t owner_cpu;
  uint32_t count;
} portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED {0, 0, 0}

#define portMAX_DELAY 0xFFFFFFFFUL
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS pdTRUE
#define pdFAIL pdFALSE

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t TimeOut_t;

#define pdMS_TO_TICKS(x) ((TickType_t)(x))

#define portENTER_CRITICAL(mux) \
  do {                          \
  } while (0)
#define portEXIT_CRITICAL(mux) \
  do {                         \
  } while (0)

inline BaseType_t xQueueCreate(size_t len, size_t size) { return pdFAIL; }
inline BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t t) {
  return pdFAIL;
}
inline BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t t) {
  return pdFAIL;
}
inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return 0; }
inline void vQueueDelete(QueueHandle_t q) {}
inline BaseType_t xTaskCreate(void (*fn)(void*), const char* name, int stack,
                              void* arg, int prio, TaskHandle_t* handle) {
  return pdFAIL;
}
inline void vTaskDelete(TaskHandle_t h) {}
inline void vTaskDelay(TickType_t t) {}
inline BaseType_t xSemaphoreCreateMutex(void) { return pdFAIL; }
inline BaseType_t xSemaphoreTake(void* m, TickType_t t) { return pdFAIL; }
inline BaseType_t xSemaphoreGive(void* m) { return pdFAIL; }
inline void vSemaphoreDelete(void* m) {}

#ifdef __cplusplus
}
#endif
