#pragma once

// Mock Logger for host testing - no FreeRTOS dependency

#include <string>
#include <string_view>

namespace {

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
};

}  // namespace

inline Logger& getLogger() {
  static Logger instance;
  return instance;
}

#define logger getLogger()
