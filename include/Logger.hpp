#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <string>

// Simple circular buffer logger

class Logger {
 public:
  explicit Logger(size_t maxEntries = 35);
  ~Logger();

  Logger(const Logger& other) = delete;             // Prevent copying
  Logger& operator=(const Logger& other) = delete;  // Prevent assignment

  void error(std::string message);
  void warn(std::string message);
  void info(std::string message);
  void debug(std::string message);

  const std::string getLogs() const;

 private:
  std::string* buffer;
  size_t maxEntries;
  size_t index;
  size_t entries;

  enum class LogLevel { DEBUG, INFO, WARN, ERROR };
  static const char* logLevelText(LogLevel logLevel);

  static const std::string timestamp();
  static void printTaskEntry(void* arg);
  void printTaskLoop();

  void log(LogLevel level, std::string message);

  mutable portMUX_TYPE mux;  // Mutex for thread safety
  QueueHandle_t printQueue;
  TaskHandle_t printTask;
};

extern Logger logger;
