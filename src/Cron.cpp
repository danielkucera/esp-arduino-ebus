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

Cron cron;

namespace {
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
  stopRunner = false;
  if (taskHandle == nullptr) {
    xTaskCreate(&Cron::taskFunc, "cronRunner", 4096, this, 2, &taskHandle);
  }
}

void Cron::stop() { stopRunner = true; }

Cron::Rule Cron::ruleFromJson(const cJSON* doc) {
  Rule rule;
  rule.id = getStringField(doc, "id");
  rule.schedule = getStringField(doc, "schedule");
  rule.commandKey = getStringField(doc, "command_key");
  rule.enabled = getBoolField(doc, "enabled", true);

  const cJSON* valueNode = getField(doc, "value");
  if (valueNode != nullptr) {
    cJSON* clone = cJSON_Duplicate(const_cast<cJSON*>(valueNode), 1);
    rule.valueJson = printJson(clone, "null");
    if (clone != nullptr) cJSON_Delete(clone);
  } else {
    rule.valueJson = "null";
  }

  return rule;
}

void Cron::setRules(std::unordered_map<std::string, Rule>&& nextRules) {
  portENTER_CRITICAL(&rulesMux);
  rules = std::move(nextRules);
  portEXIT_CRITICAL(&rulesMux);
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

int64_t Cron::replaceRules(const cJSON* doc) {
  if (!cJSON_IsArray(doc)) return -1;

  std::unordered_map<std::string, Rule> nextRules;
  cJSON* entry = nullptr;
  cJSON_ArrayForEach(entry, doc) {
    if (!cJSON_IsObject(entry)) continue;
    Rule rule = ruleFromJson(entry);
    nextRules[rule.id] = std::move(rule);
  }

  setRules(std::move(nextRules));
  return saveRules();
}

int64_t Cron::saveRules() const {
  if (!store.initFileSystem()) return -1;

  std::string payload = getRulesJson();
  size_t size = payload.size();

  FILE* file = std::fopen(kCronFilePath, "wb");
  if (file == nullptr) return -1;

  size_t bytesWritten = std::fwrite(payload.data(), 1, size, file);
  std::fclose(file);
  if (bytesWritten != size) return -1;

  return static_cast<int64_t>(size);
}

const std::string Cron::getRulesJson() const {
  cJSON* root = cJSON_CreateArray();

  std::vector<Rule> ordered;
  portENTER_CRITICAL(&rulesMux);
  for (const auto& kv : rules) ordered.push_back(kv.second);
  portEXIT_CRITICAL(&rulesMux);

  std::sort(ordered.begin(), ordered.end(),
            [](const Rule& a, const Rule& b) { return a.id < b.id; });

  for (const Rule& rule : ordered) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "id", rule.id.c_str());
    cJSON_AddStringToObject(item, "schedule", rule.schedule.c_str());
    cJSON_AddStringToObject(item, "command_key", rule.commandKey.c_str());
    cJSON_AddBoolToObject(item, "enabled", rule.enabled);

    cJSON* valueNode = cJSON_Parse(rule.valueJson.c_str());
    if (valueNode != nullptr)
      cJSON_AddItemToObject(item, "value", valueNode);
    else
      cJSON_AddNullToObject(item, "value");

    cJSON_AddItemToArray(root, item);
  }

  std::string payload = printJson(root, "[]");
  cJSON_Delete(root);
  return payload;
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

  cJSON* wrapper = cJSON_CreateObject();
  cJSON_AddItemToObject(wrapper, "value",
                        cJSON_Duplicate(const_cast<cJSON*>(valueNode), 1));
  const std::vector<uint8_t> valueBytes =
      command->getVectorFromJson(wrapper).toVector();
  cJSON_Delete(wrapper);

  if (valueBytes.empty()) {
    return std::string("Invalid value for command '") + commandKey + "'";
  }

  return "";
}

void Cron::taskFunc(void* arg) {
  Cron* self = static_cast<Cron*>(arg);
  for (;;) {
    if (self->stopRunner) {
      self->taskHandle = nullptr;
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
    std::string valueJson;
  };

  std::vector<PendingRule> pending;

  portENTER_CRITICAL(&rulesMux);
  for (auto& kv : rules) {
    Rule& rule = kv.second;
    if (!rule.enabled) continue;
    if (rule.lastTriggeredMinute == minuteStamp) continue;
    if (!matchSchedule(rule.schedule, localTime)) continue;

    rule.lastTriggeredMinute = minuteStamp;
    pending.push_back({rule.id, rule.commandKey, rule.valueJson});
  }
  portEXIT_CRITICAL(&rulesMux);

  for (const PendingRule& pendingRule : pending) {
    Command* command = store.findCommand(pendingRule.commandKey);
    if (command == nullptr || command->getWriteCmd().empty()) {
      logger.warn(std::string("Cron skipped, command unavailable: ") +
                  pendingRule.commandKey);
      continue;
    }

    cJSON* valueNode = cJSON_Parse(pendingRule.valueJson.c_str());
    if (valueNode == nullptr) {
      logger.warn(std::string("Cron skipped, invalid value for rule: ") +
                  pendingRule.id);
      continue;
    }

    cJSON* wrapper = cJSON_CreateObject();
    cJSON_AddItemToObject(wrapper, "value", valueNode);

    std::vector<uint8_t> valueBytes =
        command->getVectorFromJson(wrapper).toVector();
    cJSON_Delete(wrapper);

    if (valueBytes.empty()) {
      logger.warn(std::string("Cron skipped, value out of range for rule: ") +
                  pendingRule.id);
      continue;
    }

    std::vector<uint8_t> writeCmd = command->getWriteCmd().toVector();
    writeCmd.insert(writeCmd.end(), valueBytes.begin(), valueBytes.end());

    // schedule.handleWrite(writeCmd);

    logger.info(std::string("Cron write triggered: ") + pendingRule.id +
                " -> " + pendingRule.commandKey);
  }
}

#endif
