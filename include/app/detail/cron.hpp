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

bool matchField(std::string_view expr, int value, int min_value, int max_value,
                bool day_of_week);
bool validateFieldExpression(std::string_view expr, int min_value,
                             int max_value, bool day_of_week);
bool matchSchedule(const std::string& schedule, const tm& local_time);
std::string validateRule(const Cron::Rule& rule, CommandManager& commands);

}  // namespace app::detail::cron

#endif
