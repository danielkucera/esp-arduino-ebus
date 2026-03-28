#pragma once

#if defined(EBUS_INTERNAL)

#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <string>
#include <unordered_map>

class Cron {
 public:
  bool initFileSystem();

  void start();
  void stop();

  int64_t loadRules();
  int64_t replaceRules(const cJSON* doc);

  const std::string getRulesJson() const;

  static const std::string evaluate(const cJSON* doc);

 private:
  struct Rule {
    std::string id;
    std::string schedule;
    std::string commandKey;
    std::string valueJson;
    bool enabled = true;
    int64_t lastTriggeredMinute = -1;
  };

  std::unordered_map<std::string, Rule> rules;

  volatile bool stopRunner = false;
  TaskHandle_t taskHandle = nullptr;

  mutable portMUX_TYPE rulesMux = portMUX_INITIALIZER_UNLOCKED;

  static Rule ruleFromJson(const cJSON* doc);
  void setRules(std::unordered_map<std::string, Rule>&& nextRules);
  int64_t saveRules() const;
  static void taskFunc(void* arg);
  void tick();
};

extern Cron cron;

#endif
