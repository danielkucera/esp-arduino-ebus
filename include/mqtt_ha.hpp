#pragma once

#if defined(EBUS_INTERNAL)
#include <command.hpp>
#include <ha_profile.hpp>
#include <string>

// clang-format off
namespace mqtt_ha_limits {
inline constexpr size_t max_root_topic_length = 32;  // e.g., "esp8406ac/"
inline constexpr size_t max_device_id_length = 32;   // e.g., "ebus8406ac"

// Option/select mapping limits
inline constexpr size_t max_option_value_map_length = 256; // Jinja2 template buffer size
inline constexpr size_t max_option_string_length = 16;     // Individual option string length
inline constexpr size_t max_options_count = 5;             // Maximum key-value pairs per profile
}  // namespace mqtt_ha_limits
// clang-format on

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

  void publishComponent(const Command* command, size_t field_idx,
                        const bool remove) const;
  void publishComponentIfEnabled(const Command* command,
                                 size_t field_idx) const;

  void publishComponents() const;
  void publishComponentsIfEnabled() const;

  void removeComponent(const Command* command) const;
  void removeComponentIfEnabled(const Command* command, size_t field_idx) const;

  void removeComponents() const;

  // Called on MQTT connect/reconnect
  void onMqttConnected() const;

 private:
  std::string unique_id_;           // e.g. "8406ac"
  std::string device_identifiers_;  // e.g. "ebus8406ac"
  std::string root_topic_;          // e.g. "ebus8406ac/"
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
  void createStateTopic(char* out, size_t out_size, std::string_view prefix,
                        std::string_view topic) const;

  struct KeyValueMapping {
    ebus::StaticVector<
        ebus::FixedString<mqtt_ha_limits::max_option_string_length>,
        mqtt_ha_limits::max_options_count>
        options;
    ebus::FixedString<mqtt_ha_limits::max_option_value_map_length> value_map;
    ebus::FixedString<mqtt_ha_limits::max_option_value_map_length> cmd_map;
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