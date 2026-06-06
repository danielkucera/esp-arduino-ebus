#if defined(EBUS_INTERNAL)

#include "Cron.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "Logger.hpp"
#include "Store.hpp"
#include "ebus_accessor.hpp"

Cron cron;

namespace {

void writeCJsonToWriter(ebus::detail::JsonWriter& writer, const cJSON* node) {
  if (!node || cJSON_IsInvalid(node)) return;
  if (cJSON_IsBool(node)) {
    writer.writeValue(cJSON_IsTrue(node) != 0);
  } else if (cJSON_IsNumber(node)) {
    writer.writeValueFloat(static_cast<float>(node->valuedouble));
  } else if (cJSON_IsString(node)) {
    writer.writeValue(node->valuestring);
  } else if (cJSON_IsNull(node)) {
    writer.writeRaw("null");
  } else if (cJSON_IsArray(node)) {
    writer.startArray();
    cJSON* child = nullptr;
    cJSON_ArrayForEach(child, node) { writeCJsonToWriter(writer, child); }
    writer.endArray();
  } else if (cJSON_IsObject(node)) {
    writer.startObject();
    for (cJSON* child = node->child; child != nullptr; child = child->next) {
      writer.appendKey(child->string);
      writeCJsonToWriter(writer, child);
    }
    writer.endObject();
  }
}

constexpr const char* kCronFilePath = "/littlefs/cron.json";

std::string printJson(cJSON* node, const char* fallback) {
  char* printed = cJSON_PrintUnformatted(node);
  std::string out = printed != nullptr ? printed : fallback;
  if (printed != nullptr) cJSON_free(printed);
  return out;
}

std::vector<std::string> split(const std::string& input, const char sep) {
  std::vector<std::string> parts;
  std::string current;
  for (const char c : input) {
    if (c == sep) {
      parts.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  parts.push_back(current);
  return parts;
}

bool parseInt(const std::string& text, int& out) {
  if (text.empty()) return false;
  char* end = nullptr;
  long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0') return false;
  out = static_cast<int>(parsed);
  return true;
}

bool inRange(const int value, const int minValue, const int maxValue) {
  return value >= minValue && value <= maxValue;
}

bool matchSinglePart(const std::string& part, int value, int minValue,
                     int maxValue, bool dayOfWeek) {
  if (part == "*") return true;

  std::string base = part;
  int step = 1;
  size_t slashPos = part.find('/');
  if (slashPos != std::string::npos) {
    base = part.substr(0, slashPos);
    std::string stepPart = part.substr(slashPos + 1);
    if (!parseInt(stepPart, step) || step <= 0) return false;
  }

  int start = minValue;
  int end = maxValue;

  if (!base.empty() && base != "*") {
    size_t dashPos = base.find('-');
    if (dashPos != std::string::npos) {
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

bool matchField(const std::string& expr, int value, int minValue, int maxValue,
                bool dayOfWeek) {
  std::vector<std::string> parts = split(expr, ',');
  if (parts.empty()) return false;

  for (const std::string& part : parts) {
    if (part.empty()) return false;
    if (matchSinglePart(part, value, minValue, maxValue, dayOfWeek))
      return true;
  }
  return false;
}

bool validateSinglePart(const std::string& part, int minValue, int maxValue,
                        bool dayOfWeek) {
  if (part.empty()) return false;
  if (part == "*") return true;

  std::string base = part;
  int step = 1;
  size_t slashPos = part.find('/');
  if (slashPos != std::string::npos) {
    base = part.substr(0, slashPos);
    std::string stepPart = part.substr(slashPos + 1);
    if (!parseInt(stepPart, step) || step <= 0) return false;
  }

  if (base == "*") return true;

  int start = 0;
  int end = 0;
  size_t dashPos = base.find('-');
  if (dashPos != std::string::npos) {
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

bool validateFieldExpression(const std::string& expr, int minValue,
                             int maxValue, bool dayOfWeek) {
  std::vector<std::string> parts = split(expr, ',');
  if (parts.empty()) return false;

  for (const std::string& part : parts) {
    if (!validateSinglePart(part, minValue, maxValue, dayOfWeek)) return false;
  }
  return true;
}

bool matchSchedule(const std::string& schedule, const tm& localTime) {
  std::vector<std::string> fields;
  std::string current;

  for (const char c : schedule) {
    if (c == ' ' || c == '\t') {
      if (!current.empty()) {
        fields.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) fields.push_back(current);

  if (fields.size() != 5) return false;

  return matchField(fields[0], localTime.tm_min, 0, 59, false) &&
         matchField(fields[1], localTime.tm_hour, 0, 23, false) &&
         matchField(fields[2], localTime.tm_mday, 1, 31, false) &&
         matchField(fields[3], localTime.tm_mon + 1, 1, 12, false) &&
         matchField(fields[4], localTime.tm_wday, 0, 6, true);
}

const cJSON* getField(const cJSON* doc, const char* name) {
  return cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(doc), name);
}

std::string getStringField(const cJSON* doc, const char* name) {
  const cJSON* node = getField(doc, name);
  if (!cJSON_IsString(node) || node->valuestring == nullptr) return "";
  return node->valuestring;
}

bool getBoolField(const cJSON* doc, const char* name, bool fallback) {
  const cJSON* node = getField(doc, name);
  if (cJSON_IsBool(node)) return cJSON_IsTrue(node);
  return fallback;
}

}  // namespace

bool Cron::initFileSystem() { return store.initFileSystem(); }

void Cron::start() {
  stop_runner_ = false;
  if (task_handle_ == nullptr) {
    xTaskCreate(&Cron::taskFunc, "cron", 1536, this, 2, &task_handle_);
  }
}

void Cron::stop() { stop_runner_ = true; }

Cron::Rule Cron::ruleFromJson(const cJSON* doc) {
  Rule rule;
  rule.id = getStringField(doc, "id");
  rule.schedule = getStringField(doc, "schedule");
  rule.command_key = getStringField(doc, "command_key");
  rule.enabled = getBoolField(doc, "enabled", true);

  const cJSON* val = getField(doc, "value");
  if (val) {
    ebus::detail::JsonWriter writer(
        [&rule](std::string_view s) { rule.value_json.append(s); });
    writeCJsonToWriter(writer, val);
  } else {
    rule.value_json = "null";
  }

  return rule;
}

void Cron::setRules(std::unordered_map<std::string, Rule>&& nextRules) {
  portENTER_CRITICAL(&rules_mux_);
  rules_ = std::move(nextRules);
  portEXIT_CRITICAL(&rules_mux_);
}

int64_t Cron::loadRules() {
  if (!store.initFileSystem()) return -1;

  FILE* file = std::fopen(kCronFilePath, "rb");
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

  cJSON* doc = cJSON_Parse(payload.c_str());
  if (!cJSON_IsArray(doc)) {
    if (doc != nullptr) cJSON_Delete(doc);
    return -1;
  }

  std::unordered_map<std::string, Rule> nextRules;
  cJSON* entry = nullptr;
  cJSON_ArrayForEach(entry, doc) {
    if (!cJSON_IsObject(entry)) continue;
    if (Cron::evaluate(entry).empty()) {
      Rule rule = ruleFromJson(entry);
      nextRules[rule.id] = std::move(rule);
    }
  }

  setRules(std::move(nextRules));
  cJSON_Delete(doc);
  return static_cast<int64_t>(payload.size());
}

int64_t Cron::replaceRules(std::string_view payload) {
  std::unordered_map<std::string, Rule> nextRules;

  // Iterate through the array without parsing the whole list into heap
  size_t pos = 0;
  while (pos < payload.size()) {
    size_t start_obj = payload.find('{', pos);
    if (start_obj == std::string_view::npos) break;

    int depth = 0;
    size_t end_obj = std::string_view::npos;
    for (size_t i = start_obj; i < payload.size(); ++i) {
      if (payload[i] == '{')
        depth++;
      else if (payload[i] == '}') {
        depth--;
        if (depth == 0) {
          end_obj = i;
          break;
        }
      }
    }
    if (end_obj == std::string_view::npos) break;

    std::string_view rule_sv =
        payload.substr(start_obj, end_obj - start_obj + 1);
    cJSON* doc = cJSON_ParseWithLength(rule_sv.data(), rule_sv.size());
    if (doc) {
      if (evaluate(doc).empty()) {
        Rule rule = ruleFromJson(doc);
        nextRules[rule.id] = std::move(rule);
      }
      cJSON_Delete(doc);
    }
    pos = end_obj + 1;
  }

  setRules(std::move(nextRules));
  return saveRules();
}

void Cron::fetchRulesJson(const ebus::JsonChunkVisitor& visitor) const {
  ebus::detail::JsonWriter writer(visitor);
  writer.startArray();

  std::vector<Rule> ordered;
  portENTER_CRITICAL(&rules_mux_);
  for (const auto& kv : rules_) ordered.push_back(kv.second);
  portEXIT_CRITICAL(&rules_mux_);

  std::sort(ordered.begin(), ordered.end(),
            [](const Rule& a, const Rule& b) { return a.id < b.id; });

  for (const Rule& rule : ordered) {
    writer.startObject();
    writer.writeField("id", rule.id);
    writer.writeField("schedule", rule.schedule);
    writer.writeField("command_key", rule.command_key);
    writer.writeField("enabled", rule.enabled);
    writer.appendKey("value");
    if (rule.value_json == "null")
      writer.writeRaw("null");
    else
      writer.writeRaw(rule.value_json);
    writer.endObject();
  }
  writer.endArray();
}

int64_t Cron::saveRules() const {
  if (!store.initFileSystem()) return -1;

  FILE* file = std::fopen(kCronFilePath, "wb");
  if (file == nullptr) return -1;

  size_t total = 0;
  fetchRulesJson([file, &total](std::string_view s) {
    total += std::fwrite(s.data(), 1, s.size(), file);
  });
  std::fclose(file);

  return static_cast<int64_t>(total);
}

const std::string Cron::evaluate(const cJSON* doc) {
  if (!cJSON_IsObject(doc)) return "Json invalid";

  const std::string id = getStringField(doc, "id");
  const std::string scheduleExpr = getStringField(doc, "schedule");
  const std::string commandKey = getStringField(doc, "command_key");
  const cJSON* valueNode = getField(doc, "value");

  if (id.empty()) return "Missing or invalid 'id'";
  if (scheduleExpr.empty()) return "Missing or invalid 'schedule'";
  if (commandKey.empty()) return "Missing or invalid 'command_key'";
  if (valueNode == nullptr) return "Missing field 'value'";

  tm sample = {};
  sample.tm_min = 0;
  sample.tm_hour = 0;
  sample.tm_mday = 1;
  sample.tm_mon = 0;
  sample.tm_wday = 0;

  if (!matchSchedule(scheduleExpr, sample) &&
      scheduleExpr.find('*') == std::string::npos &&
      scheduleExpr.find('/') == std::string::npos &&
      scheduleExpr.find(',') == std::string::npos &&
      scheduleExpr.find('-') == std::string::npos) {
    return "Invalid schedule expression";
  }

  // Deep validation for cron fields by checking each field shape.
  std::vector<std::string> fields;
  std::string current;
  for (const char c : scheduleExpr) {
    if (c == ' ' || c == '\t') {
      if (!current.empty()) {
        fields.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) fields.push_back(current);
  if (fields.size() != 5) return "Schedule must have 5 fields";

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

  Command* command = store.findCommand(commandKey);
  if (command == nullptr) {
    return std::string("Command key '") + commandKey + "' not found";
  }

  if (command->getWriteCmd().empty()) {
    return std::string("Command '") + commandKey + "' has no write_cmd";
  }

  std::string val_str;
  ebus::detail::JsonWriter writer(
      [&val_str](std::string_view s) { val_str.append(s); });
  writeCJsonToWriter(writer, valueNode);
  const std::vector<uint8_t> valueBytes =
      command->getVectorFromValue(val_str).toVector();

  if (valueBytes.empty()) {
    return std::string("Invalid value for command '") + commandKey + "'";
  }

  return "";
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

  struct PendingRule {
    std::string id;
    std::string commandKey;
    std::string value_json;
  };

  std::vector<PendingRule> pending;

  portENTER_CRITICAL(&rules_mux_);
  for (auto& kv : rules_) {
    Rule& rule = kv.second;
    if (!rule.enabled) continue;
    if (rule.last_triggered_minute == minuteStamp) continue;
    if (!matchSchedule(rule.schedule, localTime)) continue;

    rule.last_triggered_minute = minuteStamp;
    pending.push_back({rule.id, rule.command_key, rule.value_json});
  }
  portEXIT_CRITICAL(&rules_mux_);

  for (const PendingRule& pendingRule : pending) {
    Command* command = store.findCommand(pendingRule.commandKey);
    if (command == nullptr || command->getWriteCmd().empty()) {
      logger.warn(std::string("Cron skipped, command unavailable: ") +
                  pendingRule.commandKey);
      continue;
    }

    std::vector<uint8_t> valueBytes =
        command->getVectorFromValue(pendingRule.value_json).toVector();

    if (valueBytes.empty()) {
      logger.warn(std::string("Cron skipped, value out of range for rule: ") +
                  pendingRule.id);
      continue;
    }

    std::vector<uint8_t> writeCmd = command->getWriteCmd().toVector();
    writeCmd.insert(writeCmd.end(), valueBytes.begin(), valueBytes.end());

    getEbusController().enqueue(PRIO_SEND, writeCmd);

    logger.info(std::string("Cron write triggered: ") + pendingRule.id +
                " -> " + pendingRule.commandKey);
  }
}

#endif
