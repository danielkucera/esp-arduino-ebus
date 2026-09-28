#if defined(EBUS_INTERNAL)
#include "system/system_monitor.hpp"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ebus/address.hpp>
#include <ebus/detail/json_writer.hpp>
#include <ebus/detail/protocol_limits.hpp>

#include "app/app_limits.hpp"
#include "app/command_manager.hpp"
#include "app/mqtt.hpp"
#include "system/logger.hpp"

namespace {
constexpr uint32_t system_monitor_period_ms = 30000;
constexpr uint32_t log_summary_interval_ms = 300000;
constexpr size_t log_queue_size = 8;
constexpr size_t protocol_queue_size = 8;

struct LogRequestItem {
  // key_id != 0: decoded command log request (existing path).
  // key_id == 0: raw telegram — every correctly received message, logged
  // as-is (master hex + optional slave hex), whether or not a configured
  // command matches it.
  uint8_t key_id = 0;
  // Per-side views (CRC/ACK excluded) peak at 21/17 bytes; model_capacity
  // (24) matches ErrorEntry/ProtocolEvent holding the same views.
  ebus::StaticSequence<ebus::detail::SequenceLimits::model_capacity> master;
  ebus::StaticSequence<ebus::detail::SequenceLimits::model_capacity> slave;
  uint32_t session_id = 0;
  uint16_t poll_id = 0;
};

struct ProtocolInfoItem {
  ebus::ProtocolInfo info;
  // Per-side views (CRC/ACK excluded) peak at 21/17 bytes; model_capacity
  // (24) matches ErrorEntry/ProtocolEvent holding the same views.
  ebus::StaticSequence<ebus::detail::SequenceLimits::model_capacity> master;
  ebus::StaticSequence<ebus::detail::SequenceLimits::model_capacity> slave;
};

#if EBUS_BUS_TAP
// Byte-tap store: reactor thread (trace callback) pushes, /api/v1/app/tap
// formats on fetch. Dedicated store (never the shared log ring/serial —
// tap rate would evict session lines). Static .bss (512 x 16 B = 8 KB);
// drops counted, never blocks the bus path.
struct TapItem {
  uint64_t boot_us = 0;
  uint8_t byte = 0;
};
constexpr size_t tap_capacity = 512;
TapItem tap_ring[tap_capacity];
size_t tap_head = 0;
size_t tap_tail = 0;
portMUX_TYPE tap_mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> tap_drops{0};
#endif

// Static queue storage (shared, .bss): kept out of the instance on purpose —
// SystemMonitor instances live on constrained task stacks, and a second
// instance never exists (single ownership by App).
static uint8_t log_queue_storage[log_queue_size * sizeof(LogRequestItem)];
static StaticQueue_t log_queue_cb;
static uint8_t
    protocol_queue_storage[protocol_queue_size * sizeof(ProtocolInfoItem)];
static StaticQueue_t protocol_queue_cb;

}  // namespace

TaskHandle_t SystemMonitor::task_handle() const { return task_handle_; }

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
      this, app::limits::Task::system_monitor_priority, &task_handle_);
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

size_t SystemMonitor::getLogQueueCapacity() const {
  return log_queue_ ? log_queue_size : 0;
}

size_t SystemMonitor::getLogQueueHighWatermark() { return 0; }

size_t SystemMonitor::getProtocolQueueSize() {
  return protocol_queue_ ? uxQueueMessagesWaiting(protocol_queue_) : 0;
}

size_t SystemMonitor::getProtocolQueueCapacity() const {
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

void SystemMonitor::tapBusByte(uint64_t boot_us, uint8_t byte) {
#if EBUS_BUS_TAP
  // Pure push: no notify (nothing drains eagerly; /api/v1/app/tap reads
  // the store on demand). Keeps bus-rate pushes free of task wakeups.
  portENTER_CRITICAL(&tap_mux);
  const size_t next = (tap_head + 1) % tap_capacity;
  if (next == tap_tail) {
    tap_drops.fetch_add(1, std::memory_order_relaxed);
  } else {
    tap_ring[tap_head].boot_us = boot_us;
    tap_ring[tap_head].byte = byte;
    tap_head = next;
  }
  portEXIT_CRITICAL(&tap_mux);
#else
  (void)boot_us;
  (void)byte;
#endif
}

// Formats the tap store as JSON, chunked straight to the HTTP socket:
// {"tap":[{t:<wall-ms>,b:"xx"},...],"dropped":N,"capacity":512}.
// Wall offset sampled once per fetch. Reads copy out in small critical
// sections (never hold a critical across formatting: ms of disabled
// interrupts would break bus timing).
void SystemMonitor::fetchTap(const ebus::JsonChunkVisitor& visitor,
                             uint64_t sinceWallMs) const {
#if EBUS_BUS_TAP
  int64_t offset_ms = 0;
  bool have_wall = false;
  {
    struct timeval tv;
    if (gettimeofday(&tv, nullptr) == 0) {
      const int64_t wall =
          static_cast<int64_t>(tv.tv_sec) * 1000LL + tv.tv_usec / 1000LL;
      constexpr int64_t min_valid_epoch_ms = 1577836800000LL;  // 2020-01-01
      if (wall >= min_valid_epoch_ms) {
        offset_ms = wall - static_cast<int64_t>(esp_timer_get_time() / 1000ULL);
        have_wall = true;
      }
    }
  }

  size_t head = 0;
  size_t tail = 0;
  portENTER_CRITICAL(&tap_mux);
  head = tap_head;
  tail = tap_tail;
  portEXIT_CRITICAL(&tap_mux);
  static constexpr char hex_chars[] = "0123456789abcdef";
  ebus::detail::JsonWriter writer(visitor);
  auto root = writer.objectScope();
  writer.appendKey("tap");
  {
    auto array = writer.arrayScope();
    size_t count = (head + tap_capacity - tail) % tap_capacity;
    size_t idx = tail;
    TapItem chunk[64];
    while (count > 0) {
      size_t n = count < 64 ? count : 64;
      portENTER_CRITICAL(&tap_mux);
      for (size_t i = 0; i < n; ++i) {
        chunk[i] = tap_ring[(idx + i) % tap_capacity];
      }
      portEXIT_CRITICAL(&tap_mux);
      for (size_t i = 0; i < n; ++i) {
        // Without wall clock there is nothing to filter against: emit.
        const uint64_t wall = have_wall ? chunk[i].boot_us / 1000ULL +
                                              static_cast<uint64_t>(offset_ms)
                                        : chunk[i].boot_us / 1000ULL;
        if (!have_wall || wall >= sinceWallMs) {
          char hex[3] = {hex_chars[chunk[i].byte >> 4],
                         hex_chars[chunk[i].byte & 0xf], '\0'};
          auto item = writer.objectScope();
          writer.writeField("t", wall);
          writer.writeField("b", hex);
        }
      }
      idx = (idx + n) % tap_capacity;
      count -= n;
    }
  }
  writer.writeField("dropped", tap_drops.load(std::memory_order_relaxed));
  writer.writeField("capacity", tap_capacity);
#else
  (void)sinceWallMs;
  ebus::detail::JsonWriter writer(visitor);
  auto root = writer.objectScope();
  writer.appendKey("tap");
  {
    auto array = writer.arrayScope();
  }
  writer.writeField("dropped", 0);
  writer.writeField("capacity", 0);
#endif
}

void SystemMonitor::taskEntry(void* arg) {
  static_cast<SystemMonitor*>(arg)->taskLoop();
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
    if (req.key_id == 0) {
      // Raw telegram: raw bytes only, no decoding or type tags.
      logRawTelegram(ebus::ByteView(req.master.data(), req.master.size()),
                     ebus::ByteView(req.slave.data(), req.slave.size()),
                     req.session_id, req.poll_id);
      continue;
    }
    std::string_view key = StringPool::instance().lookup(req.key_id);
    const Command* cmd = commandManager.findCommand(key);
    if (cmd != nullptr) {
      char buf[256];
      size_t len = cmd->writeLogMessage(buf, sizeof(buf));
      if (len > 0) {
        logger.info(std::string_view(buf, len), false, cmd->getSessionId(),
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
      // Every correctly received message is queued for logging, whether
      // or not a configured command matches it (unmatched telegrams
      // previously vanished silently inside updateData). Formatting
      // happens in processLogRequests; this path stays a memcpy.
      enqueueTelegram(item.info.master_view, item.info.slave_view,
                      item.info.session_id, item.info.poll_id);
      commandManager.updateData(item.info);
    }
  }
}

void SystemMonitor::enqueueTelegram(ebus::ByteView master, ebus::ByteView slave,
                                    uint32_t session_id, uint16_t poll_id) {
  if (log_queue_ == nullptr) return;

  LogRequestItem req{};
  if (!master.empty()) req.master.assign(master.data(), master.size());
  if (!slave.empty()) req.slave.assign(slave.data(), slave.size());
  req.session_id = session_id;
  req.poll_id = poll_id;
  xQueueSend(log_queue_, &req, 0);
  if (task_handle_ != nullptr) {
    xTaskNotifyGive(task_handle_);
  }
}

// Raw telegram line in aligned format: master + optional slave.
// The views already exclude CRC (master: header + NN + data, slave:
// NN + data, no ACKs), so they log as-is — stripping here ate the last
// data byte.
// (Timestamp/level prefix is added by the logger itself.)
void SystemMonitor::logRawTelegram(ebus::ByteView master, ebus::ByteView slave,
                                   uint32_t session_id, uint16_t poll_id) {
  char buf[256];
  char* p = buf;
  const char* end_buf = buf + sizeof(buf);

  static constexpr char hex_chars[] = "0123456789abcdef";
  auto appendHex = [&](ebus::ByteView data) {
    for (uint8_t b : data) {
      if (p + 2 >= end_buf) break;
      *p++ = hex_chars[b >> 4];
      *p++ = hex_chars[b & 0xf];
    }
  };

  appendHex(master);
  if (!slave.empty()) {
    if (p < end_buf - 3) {
      *p++ = ' ';
      *p++ = '/';
      *p++ = ' ';
    }
    appendHex(slave);
  }

  if (p > buf) {
    logger.debug(std::string_view(buf, static_cast<size_t>(p - buf)), false,
                 session_id, poll_id);
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

size_t SystemMonitor::fetchHeapTrend(HeapSample* out, size_t capacity) const {
  if (out == nullptr || capacity == 0) return 0;
  portENTER_CRITICAL(&status_mux_);
  size_t n = heap_trend_count_ < capacity ? heap_trend_count_ : capacity;
  size_t start = (heap_trend_index_ + heap_trend_capacity - heap_trend_count_) %
                 heap_trend_capacity;
  for (size_t i = 0; i < n; ++i) {
    out[i] = heap_trend_[(start + i) % heap_trend_capacity];
  }
  portEXIT_CRITICAL(&status_mux_);
  return n;
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
  // Hourly trend sample (collectStatus runs every 30 s): answers
  // leak-vs-fragmentation without polling.
  if (++heap_trend_tick_ >= 120) {
    heap_trend_tick_ = 0;
    heap_trend_[heap_trend_index_] = {status_.uptime_seconds, status_.free_heap,
                                      status_.min_free_heap,
                                      status_.largest_free_block};
    heap_trend_index_ = (heap_trend_index_ + 1) % heap_trend_capacity;
    if (heap_trend_count_ < heap_trend_capacity) heap_trend_count_++;
  }
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
