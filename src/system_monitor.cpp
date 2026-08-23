#if defined(EBUS_INTERNAL)
#include "system_monitor.hpp"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include <cstdio>
#include <cstring>

#include "app_limits.hpp"
#include "command_manager.hpp"
#include "logger.hpp"

namespace {
constexpr uint32_t system_monitor_period_ms = 30000;
constexpr uint32_t log_summary_interval_ms = 300000;
constexpr size_t log_queue_size = 8;

struct LogRequest {
  char key[16];
};
}  // namespace

SystemMonitor::Stats SystemMonitor::stats_ = {};
portMUX_TYPE SystemMonitor::stats_mux_ = {};
std::atomic<int> SystemMonitor::sockets_detected_{0};
std::atomic<int> SystemMonitor::sockets_connected_{0};
TaskHandle_t SystemMonitor::task_handle_ = nullptr;
QueueHandle_t SystemMonitor::log_queue_ = nullptr;

TaskHandle_t SystemMonitor::task_handle() { return task_handle_; }

bool SystemMonitor::begin() {
  stats_mux_ = portMUX_INITIALIZER_UNLOCKED;
  stats_.uptime_seconds = 0;
  stats_.free_heap = 0;
  stats_.min_free_heap = 0;
  stats_.largest_free_block = 0;
  stats_.sockets_detected = 0;
  stats_.sockets_connected = 0;
  sockets_detected_ = 0;
  sockets_connected_ = 0;

  log_queue_ = xQueueCreate(log_queue_size, sizeof(LogRequest));
  if (log_queue_ == nullptr) return false;

  BaseType_t result = xTaskCreate(
      taskEntry, "system_monitor", app::limits::Task::system_monitor_stack,
      nullptr, app::limits::Task::system_monitor_priority, &task_handle_);
  return result == pdPASS;
}

void SystemMonitor::stop() {
  if (task_handle_ != nullptr) {
    vTaskDelete(task_handle_);
    task_handle_ = nullptr;
  }
  if (log_queue_ != nullptr) {
    vQueueDelete(log_queue_);
    log_queue_ = nullptr;
  }
}

void SystemMonitor::enqueueLogRequest(std::string_view key) {
  if (log_queue_ == nullptr) return;
  LogRequest req;
  std::snprintf(req.key, sizeof(req.key), "%.*s", (int)key.size(), key.data());
  xQueueSend(log_queue_, &req, 0);
}

SystemMonitor::Stats SystemMonitor::getStats() {
  Stats copy;
  portENTER_CRITICAL(&stats_mux_);
  copy.uptime_seconds = stats_.uptime_seconds;
  copy.free_heap = stats_.free_heap;
  copy.min_free_heap = stats_.min_free_heap;
  copy.largest_free_block = stats_.largest_free_block;
  copy.sockets_detected = sockets_detected_.load();
  copy.sockets_connected = sockets_connected_.load();
  portEXIT_CRITICAL(&stats_mux_);
  return copy;
}

void SystemMonitor::taskEntry(void* arg) {
  (void)arg;
  taskLoop();
}

void SystemMonitor::taskLoop() {
  uint32_t last_summary = 0;
  TickType_t last_wake = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(system_monitor_period_ms));

    collectStats();
    processLogRequests();

    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - last_summary >= log_summary_interval_ms) {
      last_summary = now;
      logSummary();
    }
  }
}

void SystemMonitor::processLogRequests() {
  LogRequest req;
  while (xQueueReceive(log_queue_, &req, 0) == pdTRUE) {
    const Command* cmd = commandManager.findCommand(req.key);
    if (cmd != nullptr) {
      char buf[256];
      size_t len = cmd->writeLogMessage(buf, sizeof(buf));
      if (len > 0) {
        logger.debug(std::string_view(buf, len));
      }
    }
  }
}

void SystemMonitor::collectStats() {
  portENTER_CRITICAL(&stats_mux_);
  stats_.uptime_seconds = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  stats_.free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  stats_.min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_8BIT);
  stats_.largest_free_block = info.largest_free_block;
  portEXIT_CRITICAL(&stats_mux_);

  int detected = 0;
  int connected = 0;
  collectSocketStats(detected, connected);
  sockets_detected_.store(detected);
  sockets_connected_.store(connected);
}

void SystemMonitor::collectSocketStats(int& detected, int& connected) {
  detected = 0;
  connected = 0;

  for (int fd = 0; fd < 64; ++fd) {
    int socketType = 0;
    socklen_t socketTypeLen = sizeof(socketType);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &socketType, &socketTypeLen) != 0)
      continue;

    detected++;

    sockaddr_in peer{};
    socklen_t len = sizeof(peer);
    if (getpeername(fd, reinterpret_cast<struct sockaddr*>(&peer), &len) == 0) {
      connected++;
    }
  }
}

size_t SystemMonitor::getLogQueueSize() {
  return log_queue_ ? uxQueueMessagesWaiting(log_queue_) : 0;
}

size_t SystemMonitor::getLogQueueCapacity() {
  return log_queue_ ? log_queue_size : 0;
}

size_t SystemMonitor::getLogQueueHighWatermark() { return 0; }

void SystemMonitor::logSummary() {
  Stats s = getStats();

  char buf[256];
  int n = std::snprintf(
      buf, sizeof(buf),
      "[system_monitor] uptime=%lu heap_free=%zu heap_min=%zu heap_largest=%zu "
      "sockets=%d/%d",
      (unsigned long)s.uptime_seconds, s.free_heap, s.min_free_heap,
      s.largest_free_block, s.sockets_connected, s.sockets_detected);

  if (n > 0 && (size_t)n < sizeof(buf)) {
    logger.debug(std::string_view(buf, static_cast<size_t>(n)));
  }
}

#endif
