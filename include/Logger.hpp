#pragma once

#include <Arduino.h>

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

static const char* getLogLevelText(LogLevel logLevel) {
  const char* values[] = {"DEBUG", "INFO", "WARN", "ERROR"};
  return values[static_cast<int>(logLevel)];
}

// Simple circular buffer logger

class Logger {
 public:
  Logger(size_t maxEntries = 35);
  ~Logger();

  void add(LogLevel level, String message);

  String get();

 private:
  String* buffer;
  size_t maxEntries;
  size_t index;
  size_t entries;

  String timestamp();
};

extern Logger logger;
