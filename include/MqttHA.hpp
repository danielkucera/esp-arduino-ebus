#pragma once

#if defined(EBUS_INTERNAL)
#include <Store.hpp>
#include <string>

// Home Assistant MQTT class for auto discovery

class MqttHA {
 public:
  void setUniqueId(const std::string& id);
  void setRootTopic(const std::string& topic);
  void setWillTopic(const std::string& topic);

  void setEnabled(const bool enable);
  const bool isEnabled() const;

  void setThingName(const std::string& name);
  void setThingModel(const std::string& model);
  void setThingModelId(const std::string& modelId);
  void setThingManufacturer(const std::string& manufacturer);
  void setThingSwVersion(const std::string& swVersion);
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
  std::string thingManufacturer;
  std::string thingSwVersion;
  std::string thingHwVersion;
  std::string thingConfigurationUrl;

  struct Component {
    // Mandatory Home Assistant config fields
    std::string component;  // "sensor", "number", "select"
    std::string objectId;   // name (lowercase, space and / replaced by  _)
    std::string uniqueId;   // uniqueId + key / postfix
    std::string name;       // display name
    std::string deviceIdentifiers;  // e.g. "ebus8406ac" (node identifier)

    // Home Assistant config fields
    std::map<std::string, std::string> fields;
    std::vector<std::string> options;
    std::map<std::string, std::string> device;
  };

  void publishComponent(const Component& c, const bool remove) const;

  const std::string getComponentJson(const Component& c) const;

  std::string createStateTopic(const std::string& prefix,
                               const std::string& topic) const;

  struct KeyValueMapping {
    std::vector<std::string> options;
    std::string valueMap;
    std::string cmdMap;
  };

  static KeyValueMapping createOptions(
      const std::map<int, std::string>& ha_key_value_map,
      const int& ha_default_key);

  Component createComponent(const std::string& component,
                            const std::string& uniqueIdKey,
                            const std::string& name) const;

  Component createBinarySensor(const Command* command) const;
  Component createSensor(const Command* command) const;
  Component createNumber(const Command* command) const;
  Component createSelect(const Command* command) const;
  Component createSwitch(const Command* command) const;

  Component createButtonRestart() const;

  Component createDiagnostic(const std::string& component,
                             const std::string& uniqueIdKey,
                             const std::string& name) const;

  Component createDiagnosticResetCode() const;
  Component createDiagnosticUptime() const;
  Component createDiagnosticFreeHeap() const;
  Component createDiagnosticLoopDuration() const;
  Component createDiagnosticRSSI() const;
  Component createDiagnosticTxPower() const;
};

extern MqttHA mqttha;
#endif