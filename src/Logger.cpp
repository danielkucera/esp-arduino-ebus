#include "Logger.hpp"

#include <esp_timer.h>
#include <sys/time.h>

#include <cstring>

namespace {
constexpr size_t kPrintQueueLen = 32;
constexpr size_t kPrintMsgMaxLen = 384;

std::string jsonEscape(const std::string& input) {
  std::string escaped;
  escaped.reserve(input.size() + 8);
  for (char c : input) {
    switch (c) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += c;
        break;
    }
  }
  return escaped;
}
}  // namespace

Logger logger;

Logger::Logger(size_t maxEntries)
    : maxEntries(maxEntries),
      index(0),
      entries(0),
      mux(portMUX_INITIALIZER_UNLOCKED),
      printQueue(nullptr),
      printTask(nullptr) {
  buffer_ = std::vector<LogEntry>(maxEntries);  // Initialize std::vector
  printQueue = xQueueCreate(kPrintQueueLen, kPrintMsgMaxLen);
  if (printQueue != nullptr) {
    xTaskCreate(Logger::printTaskEntry, "logger_print", 4096, this, 1,
                &printTask);
  }
}

Logger::~Logger() {
  if (printTask != nullptr) {
    vTaskDelete(printTask);
    printTask = nullptr;
  }
  if (printQueue != nullptr) {
    vQueueDelete(printQueue);
    printQueue = nullptr;
  }
}

void Logger::error(std::string message, bool is_json, uint32_t sid,
                   uint32_t pid) {
  log(LogLevel::ERROR, message, is_json, sid, pid);
}
void Logger::warn(std::string message, bool is_json, uint32_t sid,
                  uint32_t pid) {
  log(LogLevel::WARN, message, is_json, sid, pid);
}
void Logger::info(std::string message, bool is_json, uint32_t sid,
                  uint32_t pid) {
  log(LogLevel::INFO, message, is_json, sid, pid);
}
void Logger::debug(std::string message, bool is_json, uint32_t sid,
                   uint32_t pid) {
  log(LogLevel::DEBUG, message, is_json, sid, pid);
}

const std::string Logger::getLogs(uint64_t sinceMillis) const {
  std::string response = "{\"logs\":[";

  bool first = true;
  portENTER_CRITICAL(&mux);
  for (size_t i = 0; i < entries; i++) {
    const size_t logIndex = (index - entries + i + maxEntries) % maxEntries;
    const LogEntry& entry = buffer_[logIndex];
    if (entry.timestamp < sinceMillis) continue;

    if (!first) response += ",";
    first = false;
    response += "{\"millis\":" + std::to_string(entry.timestamp);
    response += ",\"level\":\"" + std::string(logLevelText(entry.level)) + "\"";
    if (entry.session_id > 0)
      response += ",\"sid\":" + std::to_string(entry.session_id);
    if (entry.poll_id > 0)
      response += ",\"pid\":" + std::to_string(entry.poll_id);
    response += ",\"message\":";
    if (entry.is_json_message) {
      // If it's already JSON, insert it raw (assuming it's valid JSON)
      response += entry.message;
    } else {
      response += "\"" + jsonEscape(entry.message) + "\"";
    }
    response += "}";
  }
  portEXIT_CRITICAL(&mux);

  response += "]}";
  return response;
}

const std::string Logger::getTimeRelation() const {
  uint64_t currentMillis = 0;
  int64_t currentTimeMillis = 0;
  const bool hasTimeRelation =
      currentMillisTimeRelation(currentMillis, currentTimeMillis);

  std::string response = "{";
  if (hasTimeRelation) {
    response += "\"timeRelation\":{\"millis\":";
    response += std::to_string(currentMillis);
    response += ",\"time\":";
    response += std::to_string(currentTimeMillis);
    response += "}";
  } else {
    response += "\"millis\":";
    response += std::to_string(currentMillis);
  }
  response += "}";
  return response;
}

const char* Logger::logLevelText(LogLevel logLevel) {
  const char* values[] = {"DEBUG", "INFO", "WARN", "ERROR"};
  return values[static_cast<int>(logLevel)];
}

bool Logger::currentMillisTimeRelation(uint64_t& currentMillis,
                                       int64_t& currentTimeMillis) {
  currentMillis = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);

  struct timeval tv;
  gettimeofday(&tv, nullptr);
  currentTimeMillis = static_cast<int64_t>(tv.tv_sec) * 1000LL +
                      static_cast<int64_t>(tv.tv_usec) / 1000LL;

  constexpr int64_t kMinValidEpochMs = 1577836800000LL;  // 2020-01-01 UTC
  return currentTimeMillis >= kMinValidEpochMs;
}

void Logger::log(LogLevel level, std::string message, bool is_json,
                 uint32_t session_id, uint32_t poll_id) {
  if (printQueue != nullptr &&
      printTask != nullptr) {  // Ensure task is running before sending to queue
    char msg[kPrintMsgMaxLen]{};
    std::strncpy(msg, message.c_str(), sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = '\0';
    xQueueSend(printQueue, msg, 0);
  }

  portENTER_CRITICAL(&mux);
  buffer_[index].timestamp =
      static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
  buffer_[index].level = level;
  buffer_[index].message = std::move(message);
  buffer_[index].is_json_message = is_json;
  buffer_[index].session_id = session_id;
  buffer_[index].poll_id = poll_id;
  index = (index + 1) % maxEntries;
  if (entries < maxEntries) entries++;
  portEXIT_CRITICAL(&mux);
}

void Logger::printTaskEntry(void* arg) {
  Logger* self = static_cast<Logger*>(arg);
  self->printTaskLoop();
}

void Logger::printTaskLoop() {
  while (true) {
    char msg[kPrintMsgMaxLen]{};
    if (xQueueReceive(printQueue, msg, portMAX_DELAY) == pdTRUE) {
      printf("%s\n", msg);
    }
  }
}
