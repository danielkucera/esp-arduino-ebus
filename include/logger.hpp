#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <ebus/types.hpp>
#include <string>
#include <string_view>

// Simple circular buffer logger

namespace {  // Consider reducing these if memory is extremely tight
// Maximum number of log entries to keep in memory
inline constexpr size_t max_entries = 5;
// Maximum length of a log message, including null terminator
inline constexpr size_t max_msg_length = 768;
// Maximum log message for the print queue
inline constexpr size_t print_queue_entries = 5;
}  // namespace

class Logger {
 public:
  explicit Logger(size_t maxEntries = max_entries);
  ~Logger();

  Logger(const Logger& other) = delete;             // Prevent copying
  Logger& operator=(const Logger& other) = delete;  // Prevent assignment

  void error(std::string_view message, bool is_json = false,
             uint32_t session_id = 0, uint16_t poll_id = 0);
  void warn(std::string_view message, bool is_json = false,
            uint32_t session_id = 0, uint16_t poll_id = 0);
  void info(std::string_view message, bool is_json = false,
            uint32_t session_id = 0, uint16_t poll_id = 0);
  void debug(std::string_view message, bool is_json = false,
             uint32_t session_id = 0, uint16_t poll_id = 0);

  void fetchLogs(const ebus::JsonChunkVisitor& visitor,
                 uint64_t sinceMillis = 0) const;
  static void fetchTimeRelation(const ebus::JsonChunkVisitor& visitor);

  TaskHandle_t getTaskHandle() const { return print_task_; }
  size_t getQueueSize() const;
  size_t getQueueCapacity() const { return max_entries; }
  size_t getQueueHighWatermark() const;

 private:
  enum class LogLevel { DEBUG, INFO, WARN, ERROR };
  struct LogEntry {
    uint64_t timestamp;
    LogLevel level;
    char message[max_msg_length];
    bool is_json_message;
    uint32_t session_id;
    uint32_t poll_id;
  };

  LogEntry buffer_[max_entries];
  size_t index_;
  size_t entries_;

  static const char* logLevelText(LogLevel logLevel);

  static bool currentMillisTimeRelation(uint64_t& currentMillis,
                                        int64_t& currentTimeMillis);
  static void printTaskEntry(void* arg);
  void printTaskLoop();

  void log(LogLevel level, std::string_view message, bool is_json,
           uint32_t session_id, uint16_t poll_id);

  mutable portMUX_TYPE mux_;  // Mutex for thread safety
  QueueHandle_t print_queue_ = nullptr;
  TaskHandle_t print_task_ = nullptr;
  std::atomic<size_t> max_queue_size_ = 0;
};

extern Logger logger;
