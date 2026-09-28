#pragma once

#if defined(EBUS_INTERNAL)

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

#include "app/command.hpp"
#include "ebus/callbacks.hpp"

// Bus byte tap (every bus event to console for captures): off by default,
// enable capture builds with -DEBUS_BUS_TAP=1. The push path is POD-only
// and safe at bus rate; formatting/logging happens on the monitor task.
#ifndef EBUS_BUS_TAP
#define EBUS_BUS_TAP 0
#endif

// System health monitor: drains log/protocol queues fed by ebus library
// threads and reports heap, socket and task telemetry.
// Instance owned by App; all methods are thread-safe (called from the
// reactor, HTTP and main tasks).
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

  struct HeapSample {
    uint32_t uptime_seconds = 0;
    size_t free_bytes = 0;
    size_t min_bytes = 0;
    size_t largest_block = 0;
  };
  static constexpr size_t heap_trend_capacity = 24;

  // Copies up to capacity trend samples (oldest first), returns count.
  size_t fetchHeapTrend(HeapSample* out, size_t capacity) const;

  TaskHandle_t task_handle() const;

  bool begin();
  void stop();

  void enqueueLogRequest(std::string_view key);
  void enqueueProtocolInfo(const ebus::ProtocolInfo& info);

  // Bus byte tap: reactor thread pushes (POD only), fetched as JSON via
  // /api/v1/app/tap. Dedicated store: tap traffic must never churn the
  // shared log ring or serial (250 lines/s evicts session lines).
  // No-op unless EBUS_BUS_TAP=1.
  void tapBusByte(uint64_t boot_us, uint8_t byte);
  void fetchTap(const ebus::JsonChunkVisitor& visitor,
                uint64_t sinceWallMs) const;

  size_t getLogQueueSize();
  size_t getLogQueueCapacity() const;
  static size_t getLogQueueHighWatermark();

  size_t getProtocolQueueSize();
  size_t getProtocolQueueCapacity() const;
  static size_t getProtocolQueueHighWatermark();

  static void getSocketStatus(int& detected, int& connected);

 private:
  static void taskEntry(void* arg);
  void taskLoop();

  void processLogRequests();
  void processProtocolInfo();

  void enqueueTelegram(ebus::ByteView master, ebus::ByteView slave,
                       uint32_t session_id, uint16_t poll_id);
  static void logRawTelegram(ebus::ByteView master, ebus::ByteView slave,
                             uint32_t session_id, uint16_t poll_id);

  Status getStatus();
  void collectStatus();
  void logSummary();

  TaskHandle_t task_handle_ = nullptr;
  QueueHandle_t log_queue_ = nullptr;
  QueueHandle_t protocol_queue_ = nullptr;

  Status status_{};
  portMUX_TYPE status_mux_ = portMUX_INITIALIZER_UNLOCKED;
  std::atomic<int> sockets_detected_{0};
  std::atomic<int> sockets_connected_{0};
  HeapSample heap_trend_[heap_trend_capacity] = {};
  size_t heap_trend_index_ = 0;
  size_t heap_trend_count_ = 0;
  uint32_t heap_trend_tick_ = 0;
};

#endif
