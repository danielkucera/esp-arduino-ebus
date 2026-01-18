#include "Logger.hpp"

#include <ArduinoJson.h>

Logger logger;

Logger::Logger(size_t maxEntries)
    : maxEntries(maxEntries), index(0), entries(0) {
  buffer = new String[maxEntries];
}

Logger::~Logger() { delete[] buffer; }

void Logger::error(String message) { log(LogLevel::ERROR, message); }

void Logger::warn(String message) { log(LogLevel::WARN, message); }

void Logger::info(String message) { log(LogLevel::INFO, message); }

void Logger::debug(String message) { log(LogLevel::DEBUG, message); }

String Logger::getLogs() {
  String response = "[";
  for (size_t i = 0; i < entries; i++) {
    size_t index = (index - entries + i + maxEntries) % maxEntries;
    response += buffer[index];
    if (i < entries - 1) response += ",";
  }
  response += "]";
  return response;
}

const char* Logger::logLevelText(LogLevel logLevel) {
  const char* values[] = {"DEBUG", "INFO", "WARN", "ERROR"};
  return values[static_cast<int>(logLevel)];
}

String Logger::timestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char timestamp[40];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  snprintf(timestamp + strlen(timestamp), sizeof(timestamp) - strlen(timestamp),
           ".%03uZ", millis() % 1000);

  return String(timestamp);
}

void Logger::log(LogLevel level, String message) {
  String payload;
  JsonDocument doc;

  doc["timestamp"] = timestamp();
  doc["level"] = logLevelText(level);
  doc["message"] = message;

  doc.shrinkToFit();
  serializeJson(doc, payload);

  buffer[index] = payload;

  index = (index + 1) % maxEntries;
  if (entries < maxEntries) entries++;
}
