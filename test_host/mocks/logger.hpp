#pragma once

// Mock Logger for host testing - no FreeRTOS dependency

#include <cstdint>
#include <string>
#include <string_view>

class Logger {
 public:
  explicit Logger(size_t = 5) {}
  ~Logger() = default;

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  void error(std::string_view, bool = false, uint32_t = 0, uint32_t = 0) {}
  void warn(std::string_view, bool = false, uint32_t = 0, uint32_t = 0) {}
  void info(std::string_view, bool = false, uint32_t = 0, uint32_t = 0) {}
  void debug(std::string_view, bool = false, uint32_t = 0, uint32_t = 0) {}

  void fetchLogs(const void* visitor = nullptr, uint64_t = 0) const {}
  void fetchTimeRelation(const void* visitor = nullptr) const {}

  void* getTaskHandle() const { return nullptr; }
  size_t getQueueSize() const { return 0; }
  size_t getQueueCapacity() const { return 5; }
  size_t getQueueHighWatermark() const { return 0; }
};

extern Logger logger;
