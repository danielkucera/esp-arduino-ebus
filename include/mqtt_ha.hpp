#pragma once

#if defined(EBUS_INTERNAL)
#include <command.hpp>
#include <ha_profile.hpp>
#include <string>

// Home Assistant MQTT class for auto discovery

class MqttHA {
 public:
  MqttHA() = default;
  void setUniqueId(const std::string& id);
  void setRootTopic(const std::string& topic);
  void setWillTopic(const std::string& topic);

  void setEnabled(const bool enable);
  bool isEnabled() const;

  void setThingName(const std::string& name);
  void setThingModel(const std::string& model);
  void setThingModelId(const std::string& modelId);
  void setThingHwVersion(const std::string& hwVersion);
  void setThingConfigurationUrl(const std::string& configurationUrl);

  void publishDeviceInfo() const;

  void publishComponents() const;

  void removeComponent(const Command* command) const;
  void removeComponents() const;

  void publishComponent(const Command* command, size_t field_idx,
                        const bool remove) const;

 private:
  std::string unique_id_;           // e.g. "8406ac"
  std::string device_identifiers_;  // e.g. "ebus8406ac"
  std::string root_topic_;          // e.g. "ebus/8406ac/"
  std::string command_topic_;       // e.g. "ebus/8406ac/request"
  std::string will_topic_;          // e.g. "ebus/8406ac/state/available"

  bool enabled_ = false;

  std::string thing_name_;
  std::string thing_model_;
  std::string thing_model_id_;
  std::string thing_manufacturer_ = "danman.eu";
  std::string thing_sw_version_ = AUTO_VERSION;
  std::string thing_hw_version_;
  std::string thing_configuration_url_ = "http://esp-ebus.local/";

  static void sanitizeObjectId(std::string_view source, char* out,
                               size_t max_len);
  std::string createStateTopic(const std::string& prefix,
                               std::string_view topic) const;

  struct KeyValueMapping {
    ebus::StaticVector<ebus::FixedString<16>, 5> options;
    std::string value_map;
    std::string cmd_map;
  };

  static KeyValueMapping createOptions(const HAProfile* profile,
                                       std::string_view field_name);

  static const HAProfile* resolveProfile(const Command* command,
                                         size_t field_idx);

  // Helper methods for per-field values
  static const char* getFieldUnit(const Command* command, size_t field_idx);
  static float getFieldMin(const Command* command, size_t field_idx);
  static float getFieldMax(const Command* command, size_t field_idx);
};

extern MqttHA mqttha;
#endif