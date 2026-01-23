#pragma once

#include <WString.h>

// Simple circular buffer logger

class Logger {
 public:
  explicit Logger(size_t maxEntries = 35);
  ~Logger();

  Logger(const Logger& other) = delete;             // Prevent copying
  Logger& operator=(const Logger& other) = delete;  // Prevent assignment

  void error(String message);
  void warn(String message);
  void info(String message);
  void debug(String message);

  const String getLogs() const;

 private:
  String* buffer;
  size_t maxEntries;
  size_t index;
  size_t entries;

  enum class LogLevel { DEBUG, INFO, WARN, ERROR };
  static const char* logLevelText(LogLevel logLevel);

  static const String timestamp();

  void log(LogLevel level, String message);
};

extern Logger logger;
