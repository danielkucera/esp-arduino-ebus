#include "log.hpp"

#include <ArduinoJson.h>

#define MAX_LOG_ENTRIES 35
String logBuffer[MAX_LOG_ENTRIES];
int logIndex = 0;
int logEntries = 0;

static const char* getLogLevelText(LogLevel logLevel) {
  const char* values[] = {"INFO", "WARN", "ERROR"};
  return values[static_cast<int>(logLevel)];
}

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char timestamp[40];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  snprintf(timestamp + strlen(timestamp), sizeof(timestamp) - strlen(timestamp),
           ".%03uZ", millis() % 1000);

  return String(timestamp);
}

void addLog(LogLevel level, String message) {
  std::string payload;
  JsonDocument doc;

  doc["timestamp"] = getTimestamp();
  doc["level"] = getLogLevelText(level);
  doc["message"] = message;

  doc.shrinkToFit();
  serializeJson(doc, payload);

  logBuffer[logIndex] = payload.c_str();

  logIndex = (logIndex + 1) % MAX_LOG_ENTRIES;
  if (logEntries < MAX_LOG_ENTRIES) logEntries++;
}

String getLogs() {
  String response = "[";
  for (int i = 0; i < logEntries; i++) {
    int index = (logIndex - logEntries + i + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
    response += logBuffer[index];
    if (i < logEntries - 1) response += ",";
  }
  response += "]";
  return response;
}
