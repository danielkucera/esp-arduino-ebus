#pragma once

#if defined(EBUS_INTERNAL)
#include <Command.hpp>
#include <HaProfile.hpp>
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

  void publishComponent(const Command* command, const bool remove) const;

 private:
  std::string uniqueId;           // e.g. "8406ac"
  std::string deviceIdentifiers;  // e.g. "ebus8406ac"
  std::string rootTopic;          // e.g. "ebus/8406ac/"
  std::string commandTopic;       // e.g. "ebus/8406ac/request"
  std::string willTopic;          // e.g. "ebus/8406ac/state/available"

  bool enabled = false;

  // Common thing data
  std::string thingName;
  std::string thingModel;
  std::string thingModelId;
  std::string thingManufacturer = "danman.eu";
  std::string thingSwVersion = AUTO_VERSION;
  std::string thingHwVersion;
  std::string thingConfigurationUrl = "http://esp-ebus.local/";

  static void sanitizeObjectId(std::string_view source, char* out,
                               size_t max_len);
  std::string createStateTopic(const std::string& prefix,
                               std::string_view topic) const;

  struct KeyValueMapping {
    ebus::StaticVector<ebus::FixedString<16>, 5> options;
    std::string valueMap;
    std::string cmdMap;
  };

  static KeyValueMapping createOptions(const HAProfile* profile);

  static const HAProfile* resolveProfile(const Command* command);
};

extern MqttHA mqttha;
#endif