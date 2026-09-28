#pragma once

#if defined(EBUS_INTERNAL)

#include <ctime>
#include <string>
#include <string_view>

#include "app/cron.hpp"

class CommandManager;

// Pure schedule-expression logic, exposed for host testing.
// (Moved out of cron.cpp's anonymous namespace; zero ESP dependencies.)
namespace app::detail::cron {

bool matchField(std::string_view expr, int value, int minValue, int maxValue,
                bool dayOfWeek);
bool validateFieldExpression(std::string_view expr, int minValue, int maxValue,
                             bool dayOfWeek);
bool matchSchedule(const std::string& schedule, const tm& localTime);
std::string validateRule(const Cron::Rule& rule, CommandManager& commands);

}  // namespace app::detail::cron

#endif
