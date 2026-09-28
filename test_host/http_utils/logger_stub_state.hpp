#pragma once

#include <string>

namespace HttpUtilsTestState {

extern std::string last_warning;
extern std::string last_error;
void clearLoggerMessages();

}  // namespace HttpUtilsTestState
