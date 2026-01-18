#pragma once

#include <Arduino.h>

// Simple circular buffer logger

class Logger {
 public:
  Logger(size_t maxEntries = 35);
  ~Logger();

  void error(String message);
  void warn(String message);
  void info(String message);
  void debug(String message);

  String getLogs();

 private:
  String* buffer;
  size_t maxEntries;
  size_t index;
  size_t entries;

  enum class LogLevel { DEBUG, INFO, WARN, ERROR };
  static const char* logLevelText(LogLevel logLevel);

  String timestamp();

  void log(LogLevel level, String message);
};

extern Logger logger;
