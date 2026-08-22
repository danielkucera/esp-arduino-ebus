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

class SystemMonitor {
 public:
  struct Stats {
    uint32_t uptime_seconds;
    size_t free_heap;
    size_t min_free_heap;
    size_t largest_free_block;
    int sockets_detected;
    int sockets_connected;
  };

  static bool begin();
  static void stop();
  static TaskHandle_t task_handle();
  static void enqueueLogRequest(std::string_view key);
  static Stats getStats();
  static void collectSocketStats(int& detected, int& connected);
  static size_t getLogQueueSize();
  static size_t getLogQueueCapacity();
  static size_t getLogQueueHighWatermark();

 private:
  static void taskEntry(void* arg);
  static void taskLoop();
  static void processLogRequests();
  static void collectStats();
  static void logSummary();

  static TaskHandle_t task_handle_;
  static QueueHandle_t log_queue_;

  static Stats stats_;
  static portMUX_TYPE stats_mux_;
  static std::atomic<int> sockets_detected_;
  static std::atomic<int> sockets_connected_;
};

#endif
