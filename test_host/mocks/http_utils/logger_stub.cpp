#include "logger_stub_state.hpp"
#include "system/logger.hpp"

namespace HttpUtilsTestState {

std::string last_warning;
std::string last_error;

void clearLoggerMessages() {
  last_warning.clear();
  last_error.clear();
}

}  // namespace HttpUtilsTestState

Logger logger;

Logger::Logger(size_t capacity)
    : index_(0),
      entries_(0),
      capacity_(capacity > 0 && capacity <= ::max_entries ? capacity
                                                          : ::max_entries),
      mux_(portMUX_INITIALIZER_UNLOCKED) {}

Logger::~Logger() = default;

void Logger::error(std::string_view message, bool, uint32_t, uint16_t) {
  HttpUtilsTestState::last_error.assign(message);
}

void Logger::warn(std::string_view message, bool, uint32_t, uint16_t) {
  HttpUtilsTestState::last_warning.assign(message);
}
