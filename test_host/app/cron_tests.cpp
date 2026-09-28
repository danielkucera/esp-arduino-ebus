#include <catch2/catch_test_macros.hpp>
#include <ctime>
#include <string>

#include "app/command.hpp"
#include "app/command_manager.hpp"
#include "app/detail/cron.hpp"

namespace {

tm makeTime(int minute, int hour, int mday, int mon0, int wday) {
  tm t{};
  t.tm_min = minute;
  t.tm_hour = hour;
  t.tm_mday = mday;
  t.tm_mon = mon0;   // 0-based, like struct tm
  t.tm_wday = wday;  // 0=Sunday
  return t;
}

Cron::Rule makeRule(const std::string& schedule, const std::string& key) {
  Cron::Rule rule;
  rule.id = "test-rule";
  rule.schedule = schedule;
  rule.command_key = key;
  rule.value_json = "25.5";
  return rule;
}

void registerWriteCommand() {
  commandManager.wipeCommands();
  std::string json =
      R"({"key":"c1","name":"SetPoint","read_cmd":"50b509030d3300",)"
      R"("write_cmd":"50b509040e3300","interval":60,"master":false,)"
      R"("fields":[{"name":"value","profile":"data2b_celsius","position":1,)"
      R"("ha_profile":"sensor_temperature"}]})";
  ebus::detail::JsonReader reader(json);
  commandManager.insertCommand(Command::fromJson(reader));
}

}  // namespace

TEST_CASE("Cron matchField handles wildcards, values and lists", "[cron]") {
  using app::detail::cron::matchField;
  REQUIRE(matchField("*", 30, 0, 59, false) == true);
  REQUIRE(matchField("5", 5, 0, 59, false) == true);
  REQUIRE(matchField("5", 6, 0, 59, false) == false);
  REQUIRE(matchField("0,30", 0, 0, 59, false) == true);
  REQUIRE(matchField("0,30", 30, 0, 59, false) == true);
  REQUIRE(matchField("0,30", 15, 0, 59, false) == false);
  REQUIRE(matchField("", 0, 0, 59, false) == false);
  REQUIRE(matchField("a", 0, 0, 59, false) == false);
}

TEST_CASE("Cron matchField handles ranges and steps", "[cron]") {
  using app::detail::cron::matchField;
  REQUIRE(matchField("10-20", 15, 0, 59, false) == true);
  REQUIRE(matchField("10-20", 9, 0, 59, false) == false);
  REQUIRE(matchField("10-20", 21, 0, 59, false) == false);
  REQUIRE(matchField("20-10", 15, 0, 59, false) == false);
  REQUIRE(matchField("*/15", 0, 0, 59, false) == true);
  REQUIRE(matchField("*/15", 30, 0, 59, false) == true);
  REQUIRE(matchField("*/15", 7, 0, 59, false) == false);
  REQUIRE(matchField("*/0", 0, 0, 59, false) == false);
  REQUIRE(matchField("5-55/10", 25, 0, 59, false) == true);
  REQUIRE(matchField("5-55/10", 26, 0, 59, false) == false);
}

TEST_CASE("Cron matchField maps Sunday 7 to 0", "[cron]") {
  using app::detail::cron::matchField;
  REQUIRE(matchField("0", 0, 0, 6, true) == true);
  REQUIRE(matchField("7", 0, 0, 6, true) == true);
  REQUIRE(matchField("6-0", 6, 0, 6, true) == true);
  REQUIRE(matchField("6-0", 0, 0, 6, true) == true);
  REQUIRE(matchField("6-0", 3, 0, 6, true) == false);
}

TEST_CASE("Cron matchSchedule matches five-field expressions", "[cron]") {
  using app::detail::cron::matchSchedule;
  tm any = makeTime(12, 10, 15, 5, 3);
  REQUIRE(matchSchedule("* * * * *", any) == true);
  REQUIRE(matchSchedule("30 14 * * *", makeTime(30, 14, 1, 0, 0)) == true);
  REQUIRE(matchSchedule("30 14 * * *", makeTime(31, 14, 1, 0, 0)) == false);
  REQUIRE(matchSchedule("*/15 * * * *", makeTime(45, 0, 1, 0, 0)) == true);
  REQUIRE(matchSchedule("*/15 * * * *", makeTime(7, 0, 1, 0, 0)) == false);
  // Month is 1-based in the expression, 0-based in struct tm.
  REQUIRE(matchSchedule("0 0 1 1 *", makeTime(0, 0, 1, 0, 0)) == true);
  REQUIRE(matchSchedule("0 0 1 1 *", makeTime(0, 0, 1, 1, 0)) == false);
  // Monday is wday 1.
  REQUIRE(matchSchedule("0 0 * * 1", makeTime(0, 0, 5, 0, 1)) == true);
  REQUIRE(matchSchedule("0 0 * * 1", makeTime(0, 0, 5, 0, 0)) == false);
  REQUIRE(matchSchedule("0 0 * * 0", makeTime(0, 0, 5, 0, 0)) == true);
}

TEST_CASE("Cron matchSchedule rejects malformed expressions", "[cron]") {
  using app::detail::cron::matchSchedule;
  tm any = makeTime(0, 0, 1, 0, 0);
  REQUIRE(matchSchedule("", any) == false);
  REQUIRE(matchSchedule("* * * *", any) == false);
  REQUIRE(matchSchedule("* * * * * *", any) == false);
  REQUIRE(matchSchedule("not a schedule", any) == false);
}

TEST_CASE("Cron validateFieldExpression checks ranges", "[cron]") {
  using app::detail::cron::validateFieldExpression;
  REQUIRE(validateFieldExpression("*", 0, 59, false) == true);
  REQUIRE(validateFieldExpression("0-59", 0, 59, false) == true);
  REQUIRE(validateFieldExpression("60", 0, 59, false) == false);
  REQUIRE(validateFieldExpression("0,15,30,45", 0, 59, false) == true);
  REQUIRE(validateFieldExpression("0,,30", 0, 59, false) == false);
  REQUIRE(validateFieldExpression("*/15", 0, 59, false) == true);
  REQUIRE(validateFieldExpression("6-0", 0, 6, true) == true);
}

TEST_CASE("Cron validateRule accepts a complete rule", "[cron]") {
  registerWriteCommand();
  REQUIRE(app::detail::cron::validateRule(makeRule("* * * * *", "c1"),
                                          commandManager) == "");
}

TEST_CASE("Cron validateRule reports missing parts", "[cron]") {
  registerWriteCommand();
  Cron::Rule missing_id = makeRule("* * * * *", "c1");
  missing_id.id.clear();
  REQUIRE(app::detail::cron::validateRule(missing_id, commandManager).empty() ==
          false);

  REQUIRE(app::detail::cron::validateRule(makeRule("not a schedule", "c1"),
                                          commandManager) ==
          "Invalid schedule expression");
  REQUIRE(app::detail::cron::validateRule(makeRule("* * * *", "c1"),
                                          commandManager) ==
          "Schedule must have 5 fields");
  REQUIRE(app::detail::cron::validateRule(makeRule("99 99 99 99 99", "c1"),
                                          commandManager) ==
          "Invalid schedule expression");
  REQUIRE(app::detail::cron::validateRule(makeRule("* * * * *", "missing"),
                                          commandManager) ==
          "Command key 'missing' not found");
}

TEST_CASE("Cron validateRule requires a write command", "[cron]") {
  commandManager.wipeCommands();
  std::string json =
      R"({"key":"ro","name":"ReadOnly","read_cmd":"fe070009",)"
      R"("write_cmd":"","interval":60,"master":true,)"
      R"("fields":[{"name":"value","profile":"uint8","position":1,)"
      R"("ha_profile":"sensor_temperature"}]})";
  ebus::detail::JsonReader reader(json);
  commandManager.insertCommand(Command::fromJson(reader));

  REQUIRE(app::detail::cron::validateRule(makeRule("* * * * *", "ro"),
                                          commandManager) ==
          "Command 'ro' has no write_cmd");
}
