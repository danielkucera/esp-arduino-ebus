#pragma once

#if defined(EBUS_INTERNAL)

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "command.hpp"
#include "ebus/callbacks.hpp"

class SystemMonitor {
 public:
  struct Status {
    uint32_t uptime_seconds;
    size_t free_heap;
    size_t min_free_heap;
    size_t largest_free_block;
    int sockets_detected;
    int sockets_connected;
  };

  static TaskHandle_t task_handle();

  static bool begin();
  static void stop();

  static void enqueueLogRequest(std::string_view key);
  static void enqueueProtocolInfo(const ebus::ProtocolInfo& info);

  static size_t getLogQueueSize();
  static size_t getLogQueueCapacity();
  static size_t getLogQueueHighWatermark();

  static size_t getProtocolQueueSize();
  static size_t getProtocolQueueCapacity();
  static size_t getProtocolQueueHighWatermark();

  static void getSocketStatus(int& detected, int& connected);

 private:
  static void taskEntry(void* arg);
  static void taskLoop();

  static void processLogRequests();
  static void processProtocolInfo();

  static Status getStatus();
  static void collectStatus();
  static void logSummary();

  static TaskHandle_t task_handle_;
  static QueueHandle_t log_queue_;
  static QueueHandle_t protocol_queue_;

  static Status status_;
  static portMUX_TYPE status_mux_;
  static std::atomic<int> sockets_detected_;
  static std::atomic<int> sockets_connected_;
};

#endif
