#include "log.hpp"

#define MAX_LOG_ENTRIES 35
String logBuffer[MAX_LOG_ENTRIES];
int logIndex = 0;
int logEntries = 0;

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char buffer[30];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, millis() % 1000);

  return String(buffer);
}

void addLog(String entry) {
  String timestampedEntry = getTimestamp() + " " + entry;

  logBuffer[logIndex] = timestampedEntry;

  logIndex = (logIndex + 1) % MAX_LOG_ENTRIES;

  if (logEntries < MAX_LOG_ENTRIES) logEntries++;
}

String getLog() {
  String response = "";
  for (int i = 0; i < logEntries; i++) {
    int index = (logIndex - logEntries + i + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
    response += logBuffer[index] + "\n";
  }
  return response;
}
