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
#include "mqtt.hpp"

namespace {
constexpr uint32_t system_monitor_period_ms = 30000;
constexpr uint32_t log_summary_interval_ms = 300000;
constexpr size_t log_queue_size = 8;
constexpr size_t protocol_queue_size = 8;

struct LogRequestItem {
  uint8_t key_id;
};

struct ProtocolInfoItem {
  ebus::ProtocolInfo info;
  ebus::StaticSequence<64> master;
  ebus::StaticSequence<64> slave;
};

// Static storage for queues
static uint8_t log_queue_storage[log_queue_size * sizeof(LogRequestItem)];
static StaticQueue_t log_queue_cb;
static uint8_t
    protocol_queue_storage[protocol_queue_size * sizeof(ProtocolInfoItem)];
static StaticQueue_t protocol_queue_cb;

}  // namespace

SystemMonitor::Status SystemMonitor::status_ = {};
portMUX_TYPE SystemMonitor::status_mux_ = {};
std::atomic<int> SystemMonitor::sockets_detected_{0};
std::atomic<int> SystemMonitor::sockets_connected_{0};
TaskHandle_t SystemMonitor::task_handle_ = nullptr;
QueueHandle_t SystemMonitor::log_queue_ = nullptr;
QueueHandle_t SystemMonitor::protocol_queue_ = nullptr;

TaskHandle_t SystemMonitor::task_handle() { return task_handle_; }

bool SystemMonitor::begin() {
  status_mux_ = portMUX_INITIALIZER_UNLOCKED;
  status_.uptime_seconds = 0;
  status_.free_heap = 0;
  status_.min_free_heap = 0;
  status_.largest_free_block = 0;
  status_.sockets_detected = 0;
  status_.sockets_connected = 0;
  sockets_detected_ = 0;
  sockets_connected_ = 0;

  log_queue_ = xQueueCreateStatic(log_queue_size, sizeof(LogRequestItem),
                                  log_queue_storage, &log_queue_cb);
  if (log_queue_ == nullptr) return false;

  protocol_queue_ =
      xQueueCreateStatic(protocol_queue_size, sizeof(ProtocolInfoItem),
                         protocol_queue_storage, &protocol_queue_cb);
  if (protocol_queue_ == nullptr) return false;

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
}

void SystemMonitor::enqueueLogRequest(std::string_view key) {
  if (log_queue_ == nullptr) return;

  uint8_t key_id = StringPool::instance().intern(key);
  if (key_id == 0) return;

  LogRequestItem req{};
  req.key_id = key_id;
  xQueueSend(log_queue_, &req, 0);
  if (task_handle_ != nullptr) {
    xTaskNotifyGive(task_handle_);
  }
}

void SystemMonitor::enqueueProtocolInfo(const ebus::ProtocolInfo& info) {
  if (protocol_queue_ == nullptr) return;

  ProtocolInfoItem item{};
  item.info = info;

  if (!info.master_view.empty()) {
    item.master.assign(info.master_view.data(), info.master_view.size());
  } else {
    item.master.clear();
  }

  if (!info.slave_view.empty()) {
    item.slave.assign(info.slave_view.data(), info.slave_view.size());
  } else {
    item.slave.clear();
  }

  xQueueSend(protocol_queue_, &item, 0);
  if (task_handle_ != nullptr) {
    xTaskNotifyGive(task_handle_);
  }
}

size_t SystemMonitor::getLogQueueSize() {
  return log_queue_ ? uxQueueMessagesWaiting(log_queue_) : 0;
}

size_t SystemMonitor::getLogQueueCapacity() {
  return log_queue_ ? log_queue_size : 0;
}

size_t SystemMonitor::getLogQueueHighWatermark() { return 0; }

size_t SystemMonitor::getProtocolQueueSize() {
  return protocol_queue_ ? uxQueueMessagesWaiting(protocol_queue_) : 0;
}

size_t SystemMonitor::getProtocolQueueCapacity() {
  return protocol_queue_ ? protocol_queue_size : 0;
}

size_t SystemMonitor::getProtocolQueueHighWatermark() { return 0; }

void SystemMonitor::getSocketStatus(int& detected, int& connected) {
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

void SystemMonitor::taskEntry(void* arg) {
  (void)arg;
  taskLoop();
}

void SystemMonitor::taskLoop() {
  uint32_t last_summary = 0;

  for (;;) {
    if (xTaskNotifyWait(0, 0, nullptr,
                        pdMS_TO_TICKS(system_monitor_period_ms)) == pdTRUE) {
      processLogRequests();
      processProtocolInfo();
    }

    collectStatus();

    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - last_summary >= log_summary_interval_ms) {
      last_summary = now;
      logSummary();
    }
  }
}

void SystemMonitor::processLogRequests() {
  LogRequestItem req;
  while (xQueueReceive(log_queue_, &req, 0) == pdTRUE) {
    std::string_view key = StringPool::instance().lookup(req.key_id);
    const Command* cmd = commandManager.findCommand(key);
    if (cmd != nullptr) {
      char buf[256];
      size_t len = cmd->writeLogMessage(buf, sizeof(buf));
      if (len > 0) {
        logger.debug(std::string_view(buf, len), false, cmd->getSessionId(),
                     cmd->getPollId());
      }
    }
  }
}

void SystemMonitor::processProtocolInfo() {
  ProtocolInfoItem item;
  while (protocol_queue_ &&
         xQueueReceive(protocol_queue_, &item, 0) == pdTRUE) {
    if (!item.master.empty()) {
      item.info.master_view = item.master;
    } else {
      item.info.master_view = ebus::ByteView(nullptr, 0);
    }
    if (!item.slave.empty()) {
      item.info.slave_view = item.slave;
    } else {
      item.info.slave_view = ebus::ByteView(nullptr, 0);
    }

    if (item.info.is_error) {
      Mqtt::publishError(item.info);
    } else {
      commandManager.updateData(item.info);
    }
  }
}

SystemMonitor::Status SystemMonitor::getStatus() {
  Status copy;
  portENTER_CRITICAL(&status_mux_);
  copy.uptime_seconds = status_.uptime_seconds;
  copy.free_heap = status_.free_heap;
  copy.min_free_heap = status_.min_free_heap;
  copy.largest_free_block = status_.largest_free_block;
  copy.sockets_detected = sockets_detected_.load();
  copy.sockets_connected = sockets_connected_.load();
  portEXIT_CRITICAL(&status_mux_);
  return copy;
}

void SystemMonitor::collectStatus() {
  portENTER_CRITICAL(&status_mux_);
  status_.uptime_seconds =
      static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  status_.free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  status_.min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_8BIT);
  status_.largest_free_block = info.largest_free_block;
  portEXIT_CRITICAL(&status_mux_);

  int detected = 0;
  int connected = 0;
  getSocketStatus(detected, connected);
  sockets_detected_.store(detected);
  sockets_connected_.store(connected);
}

void SystemMonitor::logSummary() {
  Status status = getStatus();

  char buf[256];
  int n = std::snprintf(
      buf, sizeof(buf),
      "[system_monitor] uptime=%lu heap_free=%zu heap_min=%zu heap_largest=%zu "
      "sockets=%d/%d",
      (unsigned long)status.uptime_seconds, status.free_heap,
      status.min_free_heap, status.largest_free_block, status.sockets_connected,
      status.sockets_detected);

  if (n > 0 && (size_t)n < sizeof(buf)) {
    logger.debug(std::string_view(buf, static_cast<size_t>(n)));
  }
}

#endif
