#pragma once

#if defined(EBUS_INTERNAL)
#include <mqtt.hpp>
#include <store.hpp>
#include <string>

// Home Assistant MQTT support class for auto discovery

class MqttHA {
 public:
  void setUniqueId(const std::string& id);
  void setRootTopic(const std::string& topic);

  void setEnabled(const bool enable);
  const bool isEnabled() const;

  void publishDeviceInfo() const;

  void publishComponents() const;

  void publishComponent(const Command* command, const bool remove) const;

 private:
  std::string uniqueId;           // e.g. "8406ac"
  std::string deviceIdentifiers;  // e.g. "ebus8406ac"
  std::string rootTopic;          // e.g. "ebus/8406ac/"
  std::string commandTopic;       // e.g. "ebus/8406ac/request"

  bool enabled = false;

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

  Component createComponent(const std::string& component,
                            const std::string& uniqueIdKey,
                            const std::string& name) const;

  Component createSensor(const Command* command) const;
  Component createNumber(const Command* command) const;
  Component createSelect(const Command* command) const;

  Component createButtonRestart() const;

  Component createDiagnostic(const std::string& component,
                             const std::string& uniqueIdKey,
                             const std::string& name) const;

  Component createDiagnosticUptime() const;
  Component createDiagnosticFreeHeap() const;
  Component createDiagnosticLoopDuration() const;
};

extern MqttHA mqttha;
#endif