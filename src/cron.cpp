#if defined(EBUS_INTERNAL)

#include "cron.hpp"

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

#include "app_limits.hpp"
#include "command_manager.hpp"
#include "ebus_accessor.hpp"
#include "logger.hpp"

Cron cron;

namespace {
constexpr const char* cron_file_path = "/littlefs/cron.json";
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

bool inRange(const int value, const int minValue, const int maxValue) {
  return value >= minValue && value <= maxValue;
}

bool matchSinglePart(std::string_view part, int value, int minValue,
                     int maxValue, bool dayOfWeek) {
  if (part == "*") return true;

  std::string_view base = part;
  int step = 1;
  size_t slashPos = part.find('/');
  if (slashPos != std::string_view::npos) {
    base = part.substr(0, slashPos);
    std::string_view stepPart = part.substr(slashPos + 1);
    if (!parseInt(stepPart, step) || step <= 0) return false;
  }

  int start = minValue;
  int end = maxValue;

  if (!base.empty() && base != "*") {
    size_t dashPos = base.find('-');
    if (dashPos != std::string_view::npos) {
      int parsedStart = 0;
      int parsedEnd = 0;
      if (!parseInt(base.substr(0, dashPos), parsedStart) ||
          !parseInt(base.substr(dashPos + 1), parsedEnd)) {
        return false;
      }
      start = parsedStart;
      end = parsedEnd;
    } else {
      int single = 0;
      if (!parseInt(base, single)) return false;
      start = single;
      end = single;
    }
  }

  if (dayOfWeek) {
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

  if (!inRange(start, minValue, maxValue) ||
      !inRange(end, minValue, maxValue)) {
    return false;
  }
  if (start > end) return false;
  if (value < start || value > end) return false;

  return ((value - start) % step) == 0;
}

bool matchField(std::string_view expr, int value, int minValue, int maxValue,
                bool dayOfWeek) {
  auto parts = split(expr, ',');
  if (parts.empty()) return false;

  for (std::string_view part : parts) {
    if (part.empty()) return false;
    if (matchSinglePart(part, value, minValue, maxValue, dayOfWeek))
      return true;
  }
  return false;
}

bool validateSinglePart(std::string_view part, int minValue, int maxValue,
                        bool dayOfWeek) {
  if (part.empty()) return false;
  if (part == "*") return true;

  std::string_view base = part;
  size_t slashPos = part.find('/');
  if (slashPos != std::string_view::npos) {
    base = part.substr(0, slashPos);
    std::string_view stepPart = part.substr(slashPos + 1);
    int step = 1;
    if (!parseInt(stepPart, step) || step <= 0) return false;
  }

  if (base == "*") return true;

  int start = 0;
  int end = 0;
  size_t dashPos = base.find('-');
  if (dashPos != std::string_view::npos) {
    if (!parseInt(base.substr(0, dashPos), start) ||
        !parseInt(base.substr(dashPos + 1), end)) {
      return false;
    }
  } else {
    if (!parseInt(base, start)) return false;
    end = start;
  }

  if (dayOfWeek) {
    if (start == 7) start = 0;
    if (end == 7) end = 0;

    if (start == 6 && end == 0) return true;
  }

  if (!inRange(start, minValue, maxValue) ||
      !inRange(end, minValue, maxValue)) {
    return false;
  }
  if (start > end) return false;

  return true;
}

bool validateFieldExpression(std::string_view expr, int minValue, int maxValue,
                             bool dayOfWeek) {
  auto parts = split(expr, ',');
  if (parts.empty()) return false;

  return std::all_of(parts.begin(), parts.end(), [&](std::string_view part) {
    return validateSinglePart(part, minValue, maxValue, dayOfWeek);
  });
}

bool matchSchedule(const std::string& schedule, const tm& localTime) {
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

  return matchField(std::string_view(fields[0]), localTime.tm_min, 0, 59,
                    false) &&
         matchField(std::string_view(fields[1]), localTime.tm_hour, 0, 23,
                    false) &&
         matchField(std::string_view(fields[2]), localTime.tm_mday, 1, 31,
                    false) &&
         matchField(std::string_view(fields[3]), localTime.tm_mon + 1, 1, 12,
                    false) &&
         matchField(std::string_view(fields[4]), localTime.tm_wday, 0, 6, true);
}

std::string validateRule(const Cron::Rule& rule) {
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

  Command* command = commandManager.findCommand(rule.command_key);
  if (command == nullptr)
    return "Command key '" + rule.command_key + "' not found";
  if (!command->hasWriteCmd())
    return "Command '" + rule.command_key + "' has no write_cmd";

  ebus::ByteView valueBytes = command->getVectorFromValue(rule.value_json);
  if (valueBytes.empty())
    return "Invalid value for command '" + rule.command_key + "'";

  return "";
}

}  // namespace

bool Cron::initFileSystem() { return commandManager.initFileSystem(); }

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
        auto vToken = reader.next();
        if (key == "id")
          rule.id = std::string(reader.value());
        else if (key == "schedule")
          rule.schedule = std::string(reader.value());
        else if (key == "command_key")
          rule.command_key = std::string(reader.value());
        else if (key == "enabled")
          rule.enabled = reader.asBool();
        else
          reader.skipComposite(vToken);
      }
    }
  }
  return rule;
}

void Cron::setRules(std::unordered_map<std::string, Rule>&& nextRules) {
  std::lock_guard<std::mutex> lock(rules_mutex_);
  rules_ = std::move(nextRules);
}

int64_t Cron::loadRules() {
  if (!commandManager.initFileSystem()) return -1;

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
  size_t bytesRead = std::fread(payload.data(), 1, payload.size(), file);
  std::fclose(file);
  if (bytesRead != payload.size()) return -1;

  ebus::detail::JsonReader reader(payload);
  if (reader.next() != ebus::detail::JsonReader::Token::array_start) return -1;

  std::unordered_map<std::string, Rule> nextRules;
  while (true) {
    auto token = reader.next();
    if (token == ebus::detail::JsonReader::Token::array_end ||
        token == ebus::detail::JsonReader::Token::end ||
        token == ebus::detail::JsonReader::Token::error)
      break;

    if (token == ebus::detail::JsonReader::Token::object_start) {
      Rule rule = ruleFromReader(reader);
      if (validateRule(rule).empty()) {
        nextRules[rule.id] = std::move(rule);
      }
    }
  }

  setRules(std::move(nextRules));
  return static_cast<int64_t>(payload.size());
}

int64_t Cron::replaceRules(std::string_view payload) {
  ebus::detail::JsonReader reader(payload);
  if (reader.next() != ebus::detail::JsonReader::Token::array_start) return -1;

  std::unordered_map<std::string, Rule> nextRules;

  while (true) {
    auto token = reader.next();
    if (token == ebus::detail::JsonReader::Token::array_end ||
        token == ebus::detail::JsonReader::Token::end ||
        token == ebus::detail::JsonReader::Token::error)
      break;

    if (token == ebus::detail::JsonReader::Token::object_start) {
      Rule rule = ruleFromReader(reader);
      if (validateRule(rule).empty()) {
        nextRules[rule.id] = std::move(rule);
      }
    }
  }

  setRules(std::move(nextRules));
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
  if (!commandManager.initFileSystem()) return -1;

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
  return validateRule(ruleFromReader(reader));
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

  tm localTime = {};
  localtime_r(&now, &localTime);

  const int64_t minuteStamp = static_cast<int64_t>(now / 60);

  {
    std::lock_guard<std::mutex> lock(rules_mutex_);
    for (auto& kv : rules_) {
      Rule& rule = kv.second;
      if (!rule.enabled) continue;
      if (rule.last_triggered_minute == minuteStamp) continue;
      if (!matchSchedule(rule.schedule, localTime)) continue;

      rule.last_triggered_minute = minuteStamp;

      Command* command = commandManager.findCommand(rule.command_key);
      if (command == nullptr || !command->hasWriteCmd()) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Cron skipped, command unavailable: %s",
                 rule.command_key.c_str());
        logger.warn(buf);
        continue;
      }

      ebus::Sequence valueBytes = command->getVectorFromValue(rule.value_json);
      if (valueBytes.empty()) {
        char warn_buf[160];
        snprintf(warn_buf, sizeof(warn_buf),
                 "Cron skipped, value out of range for rule: %s",
                 rule.id.c_str());
        logger.warn(warn_buf);
        continue;
      }

      ebus::Sequence fullWrite =
          ebus::makeSequence(command->getWriteCmd(commandManager));
      fullWrite.append(valueBytes);

      getEbusController().enqueue(prio_send, fullWrite);

      char info_buf[160];
      snprintf(info_buf, sizeof(info_buf), "Cron write triggered: %s -> %s",
               rule.id.c_str(), rule.command_key.c_str());
      logger.info(info_buf);
    }
  }
}

#endif
