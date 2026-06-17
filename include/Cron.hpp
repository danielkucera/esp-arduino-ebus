#pragma once

#if defined(EBUS_INTERNAL)

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <ebus/detail/json_reader.hpp>
#include <ebus/types.hpp>
#include <string>
#include <unordered_map>

class Cron {
 public:
  struct Rule {
    std::string id;
    std::string schedule;
    std::string command_key;
    std::string value_json;
    bool enabled = true;
    int64_t last_triggered_minute = -1;
  };

  bool initFileSystem();

  void start();
  void stop();

  int64_t loadRules();
  int64_t replaceRules(std::string_view payload);

  void fetchRules(const ebus::JsonChunkVisitor& visitor) const;

  static const std::string evaluate(ebus::detail::JsonReader& reader);

  TaskHandle_t getTaskHandle() const { return task_handle_; }
  size_t getRulesCount() const { return rules_.size(); }

  static Rule ruleFromReader(ebus::detail::JsonReader& reader);

 private:
  std::unordered_map<std::string, Rule> rules_;

  volatile bool stop_runner_ = false;
  TaskHandle_t task_handle_ = nullptr;

  mutable portMUX_TYPE rules_mux_ = portMUX_INITIALIZER_UNLOCKED;
  void setRules(std::unordered_map<std::string, Rule>&& nextRules);
  int64_t saveRules() const;
  static void taskFunc(void* arg);
  void tick();
};

extern Cron cron;

#endif
