#include "system/logger.hpp"

#include <esp_timer.h>
#include <sys/time.h>

#include <cstring>
#include <ebus/detail/json_writer.hpp>
#include <ebus/utils.hpp>

#include "app/app_limits.hpp"

Logger logger;

Logger::Logger(size_t capacity)
    : index_(0),
      entries_(0),
      capacity_(capacity > 0 && capacity <= max_entries ? capacity
                                                        : max_entries),
      mux_(portMUX_INITIALIZER_UNLOCKED),
      print_queue_(xQueueCreate(print_queue_entries, sizeof(LogPrintItem))),
      print_task_(nullptr) {
  if (print_queue_ != nullptr) {
    xTaskCreate(Logger::printTaskEntry, "logger",
                app::limits::Task::logger_stack, this,
                app::limits::Task::logger_priority, &print_task_);
  }
}

Logger::~Logger() {
  if (print_task_ != nullptr) {
    vTaskDelete(print_task_);
    print_task_ = nullptr;
  }
  if (print_queue_ != nullptr) {
    vQueueDelete(print_queue_);
    print_queue_ = nullptr;
  }
}

size_t Logger::getQueueSize() const {
  if (print_queue_ == nullptr) return 0;
  return uxQueueMessagesWaiting(print_queue_);
}

size_t Logger::getQueueHighWatermark() const {
  if (print_queue_ == nullptr) return 0;
  return max_queue_size_.load(std::memory_order_relaxed);
}

void Logger::error(std::string_view message, bool is_json, uint32_t session_id,
                   uint16_t poll_id) {
  log(LogLevel::error, message, is_json, session_id, poll_id);
}
void Logger::warn(std::string_view message, bool is_json, uint32_t session_id,
                  uint16_t poll_id) {
  log(LogLevel::warn, message, is_json, session_id, poll_id);
}
void Logger::info(std::string_view message, bool is_json, uint32_t session_id,
                  uint16_t poll_id) {
  log(LogLevel::info, message, is_json, session_id, poll_id);
}
void Logger::debug(std::string_view message, bool is_json, uint32_t session_id,
                   uint16_t poll_id) {
  log(LogLevel::debug, message, is_json, session_id, poll_id);
}

void Logger::fetchLogs(const ebus::JsonChunkVisitor& visitor,
                       uint64_t since_millis) const {
  // Iterate through logs one by one to avoid massive heap spikes from vector
  // copies.
  size_t current_entries;
  size_t current_index;
  portENTER_CRITICAL(&mux_);
  current_entries = entries_;
  current_index = index_;
  portEXIT_CRITICAL(&mux_);

  ebus::detail::JsonWriter writer(visitor);
  auto root = writer.objectScope();
  writer.appendKey("logs");
  {
    auto array = writer.arrayScope();

    for (size_t i = 0; i < current_entries; i++) {
      const size_t log_index =
          (current_index - current_entries + i + capacity_) % capacity_;
      LogEntry entry;
      portENTER_CRITICAL(&mux_);
      entry = buffer_[log_index];
      portEXIT_CRITICAL(&mux_);

      if (entry.timestamp < since_millis) continue;

      auto item = writer.objectScope();
      writer.writeField("millis", entry.timestamp);
      writer.writeField("level", logLevelText(entry.level));
      if (entry.session_id > 0) writer.writeField("sid", entry.session_id);
      if (entry.poll_id > 0) writer.writeField("pid", entry.poll_id);
      writer.appendKey("message");
      if (entry.is_json_message) {
        writer.writeRaw(entry.message);
      } else {
        writer.writeValue(entry.message);
      }
    }
  }
}

void Logger::fetchTimeRelation(const ebus::JsonChunkVisitor& visitor) {
  uint64_t current_millis = 0;
  int64_t current_time_millis = 0;
  const bool has_time_relation =
      currentMillisTimeRelation(current_millis, current_time_millis);

  ebus::detail::JsonWriter writer(visitor);
  auto root = writer.objectScope();
  if (has_time_relation) {
    auto relation = writer.objectScope("timeRelation");
    writer.writeField("millis", current_millis);
    writer.writeField("time", current_time_millis);
  } else {
    writer.writeField("millis", current_millis);
  }
}

const char* Logger::logLevelText(LogLevel log_level) {
  const char* values[] = {"DEBUG", "INFO", "WARN", "ERROR"};
  return values[static_cast<int>(log_level)];
}

bool Logger::currentMillisTimeRelation(uint64_t& current_millis,
                                       int64_t& current_time_millis) {
  current_millis = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);

  struct timeval tv;
  gettimeofday(&tv, nullptr);
  current_time_millis = static_cast<int64_t>(tv.tv_sec) * 1000LL +
                        static_cast<int64_t>(tv.tv_usec) / 1000LL;

  constexpr int64_t min_valid_epoch_ms = 1577836800000LL;  // 2020-01-01 UTC
  return current_time_millis >= min_valid_epoch_ms;
}

void Logger::log(LogLevel level, std::string_view message, bool is_json,
                 uint32_t session_id, uint16_t poll_id) {
  if (print_queue_ != nullptr && print_task_ != nullptr) {
    LogPrintItem item{};
    item.boot_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
    struct timeval tv;
    if (gettimeofday(&tv, nullptr) == 0) {
      const int64_t wall =
          static_cast<int64_t>(tv.tv_sec) * 1000LL + tv.tv_usec / 1000LL;
      constexpr int64_t min_valid_epoch_ms = 1577836800000LL;  // 2020-01-01
      if (wall >= min_valid_epoch_ms) item.wall_ms = wall;
    }
    size_t len = std::min(message.size(), sizeof(item.msg) - 1);
    std::memcpy(item.msg, message.data(), len);
    item.msg[len] = '\0';
    if (xQueueSend(print_queue_, &item, 0) == pdPASS) {
      ebus::updateMaxAtomic(
          max_queue_size_,
          static_cast<size_t>(uxQueueMessagesWaiting(print_queue_)));
    } else {
      print_drops_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  portENTER_CRITICAL(&mux_);
  buffer_[index_].timestamp =
      static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
  buffer_[index_].level = level;

  size_t msg_len =
      std::min(message.size(), static_cast<size_t>(max_msg_length - 1));
  std::memcpy(buffer_[index_].message, message.data(), msg_len);
  buffer_[index_].message[msg_len] = '\0';

  buffer_[index_].is_json_message = is_json;
  buffer_[index_].session_id = session_id;
  buffer_[index_].poll_id = poll_id;
  index_ = (index_ + 1) % capacity_;
  if (entries_ < capacity_) {
    entries_++;
  } else {
    ring_overwrites_.fetch_add(1, std::memory_order_relaxed);
  }
  portEXIT_CRITICAL(&mux_);
}

void Logger::printTaskEntry(void* arg) {
  Logger* self = static_cast<Logger*>(arg);
  self->printTaskLoop();
}

void Logger::printTaskLoop() {
  while (true) {
    LogPrintItem item{};
    if (xQueueReceive(print_queue_, &item, portMAX_DELAY) == pdTRUE) {
      // Timestamp and message printed as separate args: no truncation
      // analysis on the (already bounded) message buffer. Wall part via
      // strftime (immune to -Wformat-truncation); numeric parts use
      // provably-bounded ranges only.
      char ts[26]{};
      if (item.wall_ms > 0) {
        // ebusd-style wall timestamp (TZ from SNTP config): console lines
        // diff directly against ebusd/ebusread logs.
        const time_t sec = static_cast<time_t>(item.wall_ms / 1000);
        struct tm tm;
        localtime_r(&sec, &tm);
        const size_t n = strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
        if (n > 0 && n + 5 < sizeof(ts)) {
          std::snprintf(ts + n, sizeof(ts) - n, ".%03d",
                        static_cast<int>(item.wall_ms % 1000));
        }
      } else {
        // No wall clock yet (SNTP unsynced): boot-relative seconds.
        std::snprintf(ts, sizeof(ts), "[+%lu.%03u]",
                      static_cast<unsigned long>(item.boot_ms / 1000ULL),
                      static_cast<unsigned>(item.boot_ms % 1000ULL));
      }
      printf("%s %s\n", ts, item.msg);
    }
  }
}
