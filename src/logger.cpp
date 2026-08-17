#include "logger.hpp"

#include <esp_timer.h>
#include <sys/time.h>

#include <cstring>
#include <ebus/detail/json_writer.hpp>
#include <ebus/utils.hpp>

Logger logger;

Logger::Logger(size_t maxEntries)
    : max_entries_(maxEntries),
      index_(0),
      entries_(0),
      mux_(portMUX_INITIALIZER_UNLOCKED),
      print_queue_(nullptr),
      print_task_(nullptr) {
  buffer_ = std::vector<LogEntry>(max_entries_);
  print_queue_ = xQueueCreate(print_queue_entries, max_msg_length);
  if (print_queue_ != nullptr) {
    xTaskCreate(Logger::printTaskEntry, "logger", 3072, this, 1, &print_task_);
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

void Logger::error(std::string_view message, bool is_json, uint32_t sid,
                   uint32_t pid) {
  log(LogLevel::ERROR, message, is_json, sid, pid);
}
void Logger::warn(std::string_view message, bool is_json, uint32_t sid,
                  uint32_t pid) {
  log(LogLevel::WARN, message, is_json, sid, pid);
}
void Logger::info(std::string_view message, bool is_json, uint32_t sid,
                  uint32_t pid) {
  log(LogLevel::INFO, message, is_json, sid, pid);
}
void Logger::debug(std::string_view message, bool is_json, uint32_t sid,
                   uint32_t pid) {
  log(LogLevel::DEBUG, message, is_json, sid, pid);
}

void Logger::fetchLogs(const ebus::JsonChunkVisitor& visitor,
                       uint64_t sinceMillis) const {
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
      const size_t logIndex =
          (current_index - current_entries + i + max_entries_) % max_entries_;
      LogEntry entry;
      portENTER_CRITICAL(&mux_);
      entry = buffer_[logIndex];
      portEXIT_CRITICAL(&mux_);

      if (entry.timestamp < sinceMillis) continue;

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

void Logger::fetchTimeRelation(const ebus::JsonChunkVisitor& visitor) const {
  uint64_t currentMillis = 0;
  int64_t currentTimeMillis = 0;
  const bool hasTimeRelation =
      currentMillisTimeRelation(currentMillis, currentTimeMillis);

  ebus::detail::JsonWriter writer(visitor);
  auto root = writer.objectScope();
  if (hasTimeRelation) {
    auto relation = writer.objectScope("timeRelation");
    writer.writeField("millis", currentMillis);
    writer.writeField("time", currentTimeMillis);
  } else {
    writer.writeField("millis", currentMillis);
  }
}

const char* Logger::logLevelText(LogLevel logLevel) {
  const char* values[] = {"DEBUG", "INFO", "WARN", "ERROR"};
  return values[static_cast<int>(logLevel)];
}

bool Logger::currentMillisTimeRelation(uint64_t& currentMillis,
                                       int64_t& currentTimeMillis) {
  currentMillis = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);

  struct timeval tv;
  gettimeofday(&tv, nullptr);
  currentTimeMillis = static_cast<int64_t>(tv.tv_sec) * 1000LL +
                      static_cast<int64_t>(tv.tv_usec) / 1000LL;

  constexpr int64_t min_valid_epoch_ms = 1577836800000LL;  // 2020-01-01 UTC
  return currentTimeMillis >= min_valid_epoch_ms;
}

void Logger::log(LogLevel level, std::string_view message, bool is_json,
                 uint32_t session_id, uint32_t poll_id) {
  if (print_queue_ != nullptr && print_task_ != nullptr) {
    char msg[max_msg_length]{};
    size_t len = std::min(message.size(), sizeof(msg) - 1);
    std::memcpy(msg, message.data(), len);
    msg[len] = '\0';
    if (xQueueSend(print_queue_, msg, 0) == pdPASS) {
      ebus::updateMaxAtomic(max_queue_size_,
                            uxQueueMessagesWaiting(print_queue_));
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
  index_ = (index_ + 1) % max_entries_;
  if (entries_ < max_entries_) entries_++;
  portEXIT_CRITICAL(&mux_);
}

void Logger::printTaskEntry(void* arg) {
  Logger* self = static_cast<Logger*>(arg);
  self->printTaskLoop();
}

void Logger::printTaskLoop() {
  while (true) {
    char msg[max_msg_length]{};
    if (xQueueReceive(print_queue_, msg, portMAX_DELAY) == pdTRUE) {
      printf("%s\n", msg);
    }
  }
}
