#include "Logger.hpp"

#include <inttypes.h>
#include <ctime>
#include <cstring>
#include <esp_timer.h>


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
  buffer = new std::string[maxEntries];
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
  delete[] buffer;
}

void Logger::error(std::string message) { log(LogLevel::ERROR, message); }

void Logger::warn(std::string message) { log(LogLevel::WARN, message); }

void Logger::info(std::string message) { log(LogLevel::INFO, message); }

void Logger::debug(std::string message) { log(LogLevel::DEBUG, message); }

const std::string Logger::getLogs() const {
  std::string response = "[";

  portENTER_CRITICAL(&mux);
  for (size_t i = 0; i < entries; i++) {
    size_t logIndex = (index - entries + i + maxEntries) % maxEntries;
    response += buffer[logIndex];
    if (i < entries - 1) response += ",";
  }
  portEXIT_CRITICAL(&mux);

  response += "]";
  return response;
}

const char* Logger::logLevelText(LogLevel logLevel) {
  const char* values[] = {"DEBUG", "INFO", "WARN", "ERROR"};
  return values[static_cast<int>(logLevel)];
}

const std::string Logger::timestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char timestamp[40];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  const uint32_t millis =
      static_cast<uint32_t>((esp_timer_get_time() / 1000ULL) % 1000ULL);
  snprintf(timestamp + strlen(timestamp), sizeof(timestamp) - strlen(timestamp),
           ".%03" PRIu32 "Z", millis);

  return std::string(timestamp);
}

void Logger::log(LogLevel level, std::string message) {
  const std::string ts = timestamp();
  const std::string escapedMessage = jsonEscape(message);
  std::string payload;
  payload.reserve(ts.size() + escapedMessage.size() + 64);
  payload += "{\"timestamp\":\"";
  payload += ts;
  payload += "\",\"level\":\"";
  payload += logLevelText(level);
  payload += "\",\"message\":\"";
  payload += escapedMessage;
  payload += "\"}";

  if (printQueue != nullptr) {
    char msg[kPrintMsgMaxLen]{};
    std::strncpy(msg, message.c_str(), sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = '\0';
    xQueueSend(printQueue, msg, 0);
  }

  portENTER_CRITICAL(&mux);
  buffer[index] = payload;
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
