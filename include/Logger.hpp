#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstdint>
#include <string>
#include <vector>

// Simple circular buffer logger

inline constexpr size_t LOG_MSG_MAX_LEN = 384;

class Logger {
 public:
  explicit Logger(size_t maxEntries = 35);
  ~Logger();

  Logger(const Logger& other) = delete;             // Prevent copying
  Logger& operator=(const Logger& other) = delete;  // Prevent assignment

  void error(std::string message, bool is_json = false, uint32_t session_id = 0,
             uint32_t poll_id = 0);
  void warn(std::string message, bool is_json = false, uint32_t session_id = 0,
            uint32_t poll_id = 0);
  void info(std::string message, bool is_json = false, uint32_t session_id = 0,
            uint32_t poll_id = 0);
  void debug(std::string message, bool is_json = false, uint32_t session_id = 0,
             uint32_t poll_id = 0);

  const std::string getLogs(uint64_t sinceMillis = 0) const;
  const std::string getTimeRelation() const;

  TaskHandle_t getTaskHandle() const { return printTask; }
  size_t getQueueSize() const;

 private:
  enum class LogLevel { DEBUG, INFO, WARN, ERROR };
  struct LogEntry {
    uint64_t timestamp;
    LogLevel level;
    char message[LOG_MSG_MAX_LEN];
    bool is_json_message;
    uint32_t session_id;
    uint32_t poll_id;
  };

  std::vector<LogEntry> buffer_;  // Use std::vector for RAII
  size_t maxEntries;
  size_t index;
  size_t entries;

  static const char* logLevelText(LogLevel logLevel);

  static bool currentMillisTimeRelation(uint64_t& currentMillis,
                                        int64_t& currentTimeMillis);
  static void printTaskEntry(void* arg);
  void printTaskLoop();

  void log(LogLevel level, std::string message, bool is_json,
           uint32_t session_id, uint32_t poll_id);

  mutable portMUX_TYPE mux;  // Mutex for thread safety
  QueueHandle_t printQueue;
  TaskHandle_t printTask;
};

extern Logger logger;
