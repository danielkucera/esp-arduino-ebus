#pragma once

// Mock Logger for host testing - no FreeRTOS dependency.
// MIRROR of the include path: quoted #include "system/logger.hpp" resolves
// here on host (mocks/ precedes include/) and to the real header on device.
// Keep signatures in sync with include/system/logger.hpp.

#include <cstdint>
#include <ebus/types.hpp>
#include <string>
#include <string_view>

class Logger {
 public:
  explicit Logger(size_t = 5) {}
  ~Logger() = default;

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  void error(std::string_view, bool = false, uint32_t = 0, uint16_t = 0) {}
  void warn(std::string_view, bool = false, uint32_t = 0, uint16_t = 0) {}
  void info(std::string_view, bool = false, uint32_t = 0, uint16_t = 0) {}
  void debug(std::string_view, bool = false, uint32_t = 0, uint16_t = 0) {}

  void fetchLogs(const ebus::JsonChunkVisitor& visitor,
                 uint64_t since_millis = 0) const {
    last_since_millis_ = since_millis;
    ++fetch_count_;
    if (visitor) visitor(R"({"logs":[]})");
  }
  static void fetchTimeRelation(const ebus::JsonChunkVisitor& visitor) {
    if (visitor) visitor(R"({"time_relation":true})");
  }
  uint64_t lastSinceMillis() const { return last_since_millis_; }
  size_t fetchCount() const { return fetch_count_; }

  void* getTaskHandle() const { return nullptr; }
  size_t getQueueSize() const { return 0; }
  size_t getQueueCapacity() const { return 5; }
  size_t getQueueHighWatermark() const { return 0; }
  uint32_t getRingOverwriteCount() const { return 0; }
  uint32_t getPrintDropCount() const { return 0; }

 private:
  mutable uint64_t last_since_millis_ = 0;
  mutable size_t fetch_count_ = 0;
};

extern Logger logger;
