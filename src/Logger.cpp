#include "Logger.hpp"

#include <ArduinoJson.h>

Logger logger;

Logger::Logger(size_t maxEntries)
    : maxEntries(maxEntries), index(0), entries(0) {
  buffer = new String[maxEntries];
}

Logger::~Logger() { delete[] buffer; }

void Logger::add(LogLevel level, String message) {
  String payload;
  JsonDocument doc;

  doc["timestamp"] = timestamp();
  doc["level"] = getLogLevelText(level);
  doc["message"] = message;

  doc.shrinkToFit();
  serializeJson(doc, payload);

  buffer[index] = payload;

  index = (index + 1) % maxEntries;
  if (entries < maxEntries) entries++;
}

String Logger::get() {
  String response = "[";
  for (size_t i = 0; i < entries; i++) {
    size_t index = (index - entries + i + maxEntries) % maxEntries;
    response += buffer[index];
    if (i < entries - 1) response += ",";
  }
  response += "]";
  return response;
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