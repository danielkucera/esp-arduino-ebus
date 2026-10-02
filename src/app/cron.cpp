#if defined(EBUS_INTERNAL)

#include "app/cron.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <ebus/detail/json_reader.hpp>
#include <ebus/static_vector.hpp>
#include <ebus/types.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "app/app_limits.hpp"
#include "app/command_manager.hpp"
#include "app/detail/cron.hpp"
#include "app/ebus_accessor.hpp"
#include "system/logger.hpp"

Cron::Cron(CommandManager& commands) : commands_(commands) {}

Cron cron(command_manager);

namespace {
#ifndef EBUS_CRON_FILE_PATH
constexpr const char* cron_file_path = "/littlefs/cron.json";
#else
constexpr const char* cron_file_path = EBUS_CRON_FILE_PATH;
#endif
constexpr size_t max_cron_rules = 32;

template <size_t Cap>
using FS = ebus::FixedString<Cap>;

ebus::StaticVector<std::string_view, 64> split(std::string_view input,
                                               const char sep) {
  ebus::StaticVector<std::string_view, 64> parts;
  size_t start = 0;
  while (start <= input.size()) {
    size_t end = input.find(sep, start);
    if (end == std::string_view::npos) end = input.size();
    parts.push_back(input.substr(start, end - start));
    if (end == input.size()) break;
    start = end + 1;
  }
  return parts;
}

bool parseInt(std::string_view text, int& out) {
  if (text.empty()) return false;
  auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
  return ec == std::errc{} && ptr == text.data() + text.size();
}

bool inRange(const int value, const int min_value, const int max_value) {
  return value >= min_value && value <= max_value;
}

bool matchSinglePart(std::string_view part, int value, int min_value,
                     int max_value, bool day_of_week) {
  if (part == "*") return true;

  std::string_view base = part;
  int step = 1;
  size_t slash_pos = part.find('/');
  if (slash_pos != std::string_view::npos) {
    base = part.substr(0, slash_pos);
    std::string_view step_part = part.substr(slash_pos + 1);
    if (!parseInt(step_part, step) || step <= 0) return false;
  }

  int start = min_value;
  int end = max_value;

  if (!base.empty() && base != "*") {
    size_t dash_pos = base.find('-');
    if (dash_pos != std::string_view::npos) {
      int parsed_start = 0;
      int parsed_end = 0;
      if (!parseInt(base.substr(0, dash_pos), parsed_start) ||
          !parseInt(base.substr(dash_pos + 1), parsed_end)) {
        return false;
      }
      start = parsed_start;
      end = parsed_end;
    } else {
      int single = 0;
      if (!parseInt(base, single)) return false;
      start = single;
      end = single;
    }
  }

  if (day_of_week) {
    if (start == 7) start = 0;
    if (end == 7) end = 0;

    if (base != "*" && start > end && !(start == 6 && end == 0)) {
      return false;
    }

    if (start == 6 && end == 0) {
      if (value != 6 && value != 0) return false;
      return ((value - start + 7) % 7) % step == 0;
    }
  }

  if (!inRange(start, min_value, max_value) ||
      !inRange(end, min_value, max_value)) {
    return false;
  }
  if (start > end) return false;
  if (value < start || value > end) return false;

  return ((value - start) % step) == 0;
}

bool validateSinglePart(std::string_view part, int min_value, int max_value,
                        bool day_of_week) {
  if (part.empty()) return false;
  if (part == "*") return true;

  std::string_view base = part;
  size_t slash_pos = part.find('/');
  if (slash_pos != std::string_view::npos) {
    base = part.substr(0, slash_pos);
    std::string_view step_part = part.substr(slash_pos + 1);
    int step = 1;
    if (!parseInt(step_part, step) || step <= 0) return false;
  }

  if (base == "*") return true;

  int start = 0;
  int end = 0;
  size_t dash_pos = base.find('-');
  if (dash_pos != std::string_view::npos) {
    if (!parseInt(base.substr(0, dash_pos), start) ||
        !parseInt(base.substr(dash_pos + 1), end)) {
      return false;
    }
  } else {
    if (!parseInt(base, start)) return false;
    end = start;
  }

  if (day_of_week) {
    if (start == 7) start = 0;
    if (end == 7) end = 0;

    if (start == 6 && end == 0) return true;
  }

  if (!inRange(start, min_value, max_value) ||
      !inRange(end, min_value, max_value)) {
    return false;
  }
  if (start > end) return false;

  return true;
}

}  // namespace

namespace app::detail::cron {

bool matchField(std::string_view expr, int value, int min_value, int max_value,
                bool day_of_week) {
  auto parts = split(expr, ',');
  if (parts.empty()) return false;

  for (std::string_view part : parts) {
    if (part.empty()) return false;
    if (matchSinglePart(part, value, min_value, max_value, day_of_week))
      return true;
  }
  return false;
}

bool validateFieldExpression(std::string_view expr, int min_value,
                             int max_value, bool day_of_week) {
  auto parts = split(expr, ',');
  if (parts.empty()) return false;

  return std::all_of(parts.begin(), parts.end(), [&](std::string_view part) {
    return validateSinglePart(part, min_value, max_value, day_of_week);
  });
}

bool matchSchedule(const std::string& schedule, const tm& local_time) {
  FS<64> fields[5];
  size_t field_idx = 0;
  size_t start = 0;
  for (size_t i = 0; i <= schedule.size(); i++) {
    if (i == schedule.size() || schedule[i] == ' ' || schedule[i] == '\t') {
      if (start < i) {
        if (field_idx >= 5) return false;
        fields[field_idx].assign(schedule.substr(start, i - start));
        field_idx++;
      }
      start = i + 1;
    }
  }
  if (field_idx != 5) return false;

  return matchField(std::string_view(fields[0]), local_time.tm_min, 0, 59,
                    false) &&
         matchField(std::string_view(fields[1]), local_time.tm_hour, 0, 23,
                    false) &&
         matchField(std::string_view(fields[2]), local_time.tm_mday, 1, 31,
                    false) &&
         matchField(std::string_view(fields[3]), local_time.tm_mon + 1, 1, 12,
                    false) &&
         matchField(std::string_view(fields[4]), local_time.tm_wday, 0, 6,
                    true);
}

std::string validateRule(const Cron::Rule& rule, CommandManager& commands) {
  if (rule.id.empty()) return "Missing or invalid 'id'";
  if (rule.schedule.empty()) return "Missing or invalid 'schedule'";
  if (rule.command_key.empty()) return "Missing or invalid 'command_key'";
  if (rule.value_json.empty()) return "Missing field 'value'";

  tm sample = {};
  sample.tm_min = 0;
  sample.tm_hour = 0;
  sample.tm_mday = 1;
  sample.tm_mon = 0;
  sample.tm_wday = 0;

  if (!matchSchedule(rule.schedule, sample) &&
      rule.schedule.find('*') == std::string::npos &&
      rule.schedule.find('/') == std::string::npos &&
      rule.schedule.find(',') == std::string::npos &&
      rule.schedule.find('-') == std::string::npos) {
    return "Invalid schedule expression";
  }

  FS<64> fields[5];
  size_t field_idx = 0;
  size_t start = 0;
  for (size_t i = 0; i <= rule.schedule.size(); i++) {
    if (i == rule.schedule.size() || rule.schedule[i] == ' ' ||
        rule.schedule[i] == '\t') {
      if (start < i) {
        if (field_idx >= 5) return "Schedule must have 5 fields";
        fields[field_idx].assign(rule.schedule.substr(start, i - start));
        field_idx++;
      }
      start = i + 1;
    }
  }
  if (field_idx != 5) return "Schedule must have 5 fields";

  if (!validateFieldExpression(fields[0], 0, 59, false))
    return "Invalid minute field";
  if (!validateFieldExpression(fields[1], 0, 23, false))
    return "Invalid hour field";
  if (!validateFieldExpression(fields[2], 1, 31, false))
    return "Invalid day-of-month field";
  if (!validateFieldExpression(fields[3], 1, 12, false))
    return "Invalid month field";
  if (!validateFieldExpression(fields[4], 0, 6, true))
    return "Invalid day-of-week field";

  Command* command = commands.findCommand(rule.command_key);
  if (command == nullptr)
    return "Command key '" + rule.command_key + "' not found";
  if (!command->hasWriteCmd())
    return "Command '" + rule.command_key + "' has no write_cmd";

  ebus::ByteView value_bytes = command->getVectorFromValue(rule.value_json);
  if (value_bytes.empty())
    return "Invalid value for command '" + rule.command_key + "'";

  return "";
}

}  // namespace app::detail::cron

bool Cron::initFileSystem() { return command_manager.initFileSystem(); }

void Cron::start() {
  stop_runner_ = false;
  if (task_handle_ == nullptr) {
    xTaskCreate(&Cron::taskFunc, "cron", app::limits::Task::cron_stack, this,
                app::limits::Task::cron_priority, &task_handle_);
  }
}

void Cron::stop() {
  if (stop_runner_ == false) {
    stop_runner_ = true;
  }
}

Cron::Rule Cron::ruleFromReader(ebus::detail::JsonReader& reader) {
  Rule rule;
  while (true) {
    auto token = reader.next();
    if (token == ebus::detail::JsonReader::Token::object_end ||
        token == ebus::detail::JsonReader::Token::end ||
        token == ebus::detail::JsonReader::Token::error)
      break;

    if (token == ebus::detail::JsonReader::Token::key) {
      std::string_view key = reader.value();
      if (key == "value") {
        rule.value_json = std::string(reader.rawValue());
      } else {
        auto value_token = reader.next();
        if (key == "id")
          rule.id = std::string(reader.value());
        else if (key == "schedule")
          rule.schedule = std::string(reader.value());
        else if (key == "command_key")
          rule.command_key = std::string(reader.value());
        else if (key == "enabled")
          rule.enabled = reader.asBool();
        else
          reader.skipComposite(value_token);
      }
    }
  }
  return rule;
}

void Cron::setRules(std::unordered_map<std::string, Rule>&& next_rules) {
  std::lock_guard<std::mutex> lock(rules_mutex_);
  rules_ = std::move(next_rules);
}

int64_t Cron::loadRules() {
  if (!command_manager.initFileSystem()) return -1;

  FILE* file = std::fopen(cron_file_path, "rb");
  if (file == nullptr) {
    if (errno == ENOENT) return 0;
    return -1;
  }

  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return -1;
  }

  long size = std::ftell(file);
  if (size <= 0 || std::fseek(file, 0, SEEK_SET) != 0) {
    if (size == 0) {
      std::fclose(file);
      setRules({});
      return 0;
    }
    std::fclose(file);
    return -1;
  }

  std::string payload;
  payload.resize(static_cast<size_t>(size));
  size_t bytes_read = std::fread(payload.data(), 1, payload.size(), file);
  std::fclose(file);
  if (bytes_read != payload.size()) return -1;

  ebus::detail::JsonReader reader(payload);
  if (reader.next() != ebus::detail::JsonReader::Token::array_start) return -1;

  std::unordered_map<std::string, Rule> next_rules;
  while (true) {
    auto token = reader.next();
    if (token == ebus::detail::JsonReader::Token::array_end ||
        token == ebus::detail::JsonReader::Token::end ||
        token == ebus::detail::JsonReader::Token::error)
      break;

    if (token == ebus::detail::JsonReader::Token::object_start) {
      Rule rule = ruleFromReader(reader);
      if (app::detail::cron::validateRule(rule, commands_).empty()) {
        next_rules[rule.id] = std::move(rule);
      }
    }
  }

  setRules(std::move(next_rules));
  return static_cast<int64_t>(payload.size());
}

int64_t Cron::replaceRules(std::string_view payload) {
  ebus::detail::JsonReader reader(payload);
  if (reader.next() != ebus::detail::JsonReader::Token::array_start) return -1;

  std::unordered_map<std::string, Rule> next_rules;

  while (true) {
    auto token = reader.next();
    if (token == ebus::detail::JsonReader::Token::array_end ||
        token == ebus::detail::JsonReader::Token::end ||
        token == ebus::detail::JsonReader::Token::error)
      break;

    if (token == ebus::detail::JsonReader::Token::object_start) {
      Rule rule = ruleFromReader(reader);
      if (app::detail::cron::validateRule(rule, commands_).empty()) {
        next_rules[rule.id] = std::move(rule);
      }
    }
  }

  setRules(std::move(next_rules));
  return saveRules();
}

void Cron::fetchRules(const ebus::JsonChunkVisitor& visitor) const {
  ebus::detail::JsonWriter writer(visitor);
  auto array_scope = writer.arrayScope();

  ebus::StaticVector<const Rule*, max_cron_rules> ordered;
  {
    std::lock_guard<std::mutex> lock(rules_mutex_);
    for (const auto& kv : rules_) {
      // cppcheck-suppress useStlAlgorithm
      if (!ordered.push_back(&kv.second)) {
        logger.warn("Cron rule limit exceeded, some rules omitted");
        break;
      }
    }
  }

  std::sort(ordered.begin(), ordered.begin() + ordered.size(),
            [](const Rule* a, const Rule* b) { return a->id < b->id; });

  for (const Rule* rule : ordered) {
    auto obj_scope = writer.objectScope();
    writer.writeField("id", rule->id);
    writer.writeField("schedule", rule->schedule);
    writer.writeField("command_key", rule->command_key);
    writer.writeField("enabled", rule->enabled);
    writer.appendKey("value");
    if (rule->value_json == "null")
      writer.writeRaw("null");
    else
      writer.writeRaw(rule->value_json);
  }
}

int64_t Cron::saveRules() const {
  if (!command_manager.initFileSystem()) return -1;

  FILE* file = std::fopen(cron_file_path, "wb");
  if (file == nullptr) return -1;

  size_t total = 0;
  fetchRules([file, &total](std::string_view s) {
    total += std::fwrite(s.data(), 1, s.size(), file);
  });
  std::fclose(file);

  return static_cast<int64_t>(total);
}

const std::string Cron::evaluate(ebus::detail::JsonReader& reader) {
  return app::detail::cron::validateRule(ruleFromReader(reader), commands_);
}

void Cron::taskFunc(void* arg) {
  Cron* self = static_cast<Cron*>(arg);
  for (;;) {
    if (self->stop_runner_) {
      self->task_handle_ = nullptr;
      vTaskDelete(nullptr);
    }
    self->tick();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void Cron::tick() {
  std::time_t now = std::time(nullptr);
  if (now <= 0) return;

  tm local_time = {};
  localtime_r(&now, &local_time);

  const int64_t minute_stamp = static_cast<int64_t>(now / 60);

  {
    std::lock_guard<std::mutex> lock(rules_mutex_);
    for (auto& kv : rules_) {
      Rule& rule = kv.second;
      if (!rule.enabled) continue;
      if (rule.last_triggered_minute == minute_stamp) continue;
      if (!app::detail::cron::matchSchedule(rule.schedule, local_time))
        continue;

      rule.last_triggered_minute = minute_stamp;

      Command* command = commands_.findCommand(rule.command_key);
      if (command == nullptr || !command->hasWriteCmd()) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Cron skipped, command unavailable: %s",
                 rule.command_key.c_str());
        logger.warn(buf);
        continue;
      }

      ebus::Sequence value_bytes = command->getVectorFromValue(rule.value_json);
      if (value_bytes.empty()) {
        char warn_buf[160];
        snprintf(warn_buf, sizeof(warn_buf),
                 "Cron skipped, value out of range for rule: %s",
                 rule.id.c_str());
        logger.warn(warn_buf);
        continue;
      }

      ebus::Sequence full_write =
          ebus::makeSequence(command->getWriteCmd(command_manager));
      full_write.append(value_bytes);

      getEbusController().enqueue(app::priority::send, full_write);

      char info_buf[160];
      snprintf(info_buf, sizeof(info_buf), "Cron write triggered: %s -> %s",
               rule.id.c_str(), rule.command_key.c_str());
      logger.info(info_buf);
    }
  }
}

#endif
