#if defined(EBUS_INTERNAL)
#include <mqttha.hpp>
#include <sstream>

MqttHA mqttha;

void MqttHA::setUniqueId(const std::string& id) {
  uniqueId = id;
  deviceIdentifiers = "ebus" + uniqueId;
}

void MqttHA::setRootTopic(const std::string& topic) {
  rootTopic = topic;
  commandTopic = rootTopic + "request";
}

void MqttHA::setEnabled(const bool enable) { enabled = enable; }

const bool MqttHA::isEnabled() const { return enabled; }

void MqttHA::setThingName(const std::string& name) { thingName = name; }

void MqttHA::setThingModel(const std::string& model) { thingModel = model; }

void MqttHA::setThingModelId(const std::string& modelId) {
  thingModelId = modelId;
}

void MqttHA::setThingManufacturer(const std::string& manufacturer) {
  thingManufacturer = manufacturer;
}

void MqttHA::setThingSwVersion(const std::string& swVersion) {
  thingSwVersion = swVersion;
}

void MqttHA::setThingHwVersion(const std::string& hwVersion) {
  thingHwVersion = hwVersion;
}

void MqttHA::setThingConfigurationUrl(const std::string& configurationUrl) {
  thingConfigurationUrl = configurationUrl;
}

void MqttHA::publishDeviceInfo() const {
  mqttha.publishComponent(createButtonRestart(), !enabled);

  mqttha.publishComponent(createDiagnosticUptime(), !enabled);
  mqttha.publishComponent(createDiagnosticFreeHeap(), !enabled);
  mqttha.publishComponent(createDiagnosticLoopDuration(), !enabled);
}

void MqttHA::publishComponents() const {
  for (const Command* command : store.getCommands()) {
    if (command->ha) mqtt.enqueueOutgoing(OutgoingAction(command, !enabled));
  }
}

void MqttHA::publishComponent(const Command* command, const bool remove) const {
  if (command->ha_component == "sensor") {
    publishComponent(createSensor(command), remove);
  } else if (command->ha_component == "number") {
    publishComponent(createNumber(command), remove);
  } else if (command->ha_component == "select") {
    publishComponent(createSelect(command), remove);
  }
}

void MqttHA::publishComponent(const Component& c, const bool remove) const {
  std::string topic = "homeassistant/" + c.component + '/' +
                      c.deviceIdentifiers + '/' + c.objectId + "/config";

  // Only publish config if HA support is enabled and not removing
  if (!remove && enabled) {
    std::string payload = getComponentJson(c);
    mqtt.publish(topic.c_str(), 0, true, payload.c_str(), false);
  }
  // Only publish empty payload if removing (to remove entity in HA)
  else if (remove) {
    mqtt.publish(topic.c_str(), 0, true, "", false);
  }
  // Otherwise, do nothing
}

const std::string MqttHA::getComponentJson(const Component& c) const {
  JsonDocument doc;
  doc["unique_id"] = c.uniqueId;
  doc["name"] = c.name;

  // fields
  for (const auto& kv : c.fields) doc[kv.first] = kv.second;

  // options
  if (!c.options.empty()) {
    JsonArray options = doc["options"].to<JsonArray>();
    for (const auto& opt : c.options) options.add(opt);
  }

  // device
  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"] = c.deviceIdentifiers;
  for (const auto& kv : c.device) device[kv.first] = kv.second;

  std::string payload;
  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload;
}

std::string MqttHA::createStateTopic(const std::string& prefix,
                                     const std::string& topic) const {
  std::string stateTopic = topic;
  std::transform(stateTopic.begin(), stateTopic.end(), stateTopic.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return rootTopic + prefix + (prefix.empty() ? "" : "/") + stateTopic;
}

MqttHA::Component MqttHA::createComponent(const std::string& component,
                                          const std::string& uniqueIdKey,
                                          const std::string& name) const {
  std::string objectId = name;
  std::transform(objectId.begin(), objectId.end(), objectId.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  std::replace(objectId.begin(), objectId.end(), '/', '_');
  std::replace(objectId.begin(), objectId.end(), ' ', '_');

  std::string key = uniqueIdKey;
  std::transform(key.begin(), key.end(), key.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  std::replace(key.begin(), key.end(), '/', '_');
  std::replace(key.begin(), key.end(), ' ', '_');

  std::string prettyName = name;
  std::replace(prettyName.begin(), prettyName.end(), '/', ' ');
  std::replace(prettyName.begin(), prettyName.end(), '_', ' ');

  Component c;
  c.component = component;
  c.objectId = objectId;
  c.uniqueId = deviceIdentifiers + '_' + key;
  c.name = prettyName;
  c.deviceIdentifiers = deviceIdentifiers;
  return c;
}

MqttHA::Component MqttHA::createSensor(const Command* command) const {
  Component c = createComponent("sensor", command->key, command->name);
  c.fields["state_topic"] = createStateTopic("values", command->name);
  if (!command->ha_device_class.empty())
    c.fields["device_class"] = command->ha_device_class;
  c.fields["unit_of_measurement"] = command->unit;
  c.fields["value_template"] = "{{value_json.value}}";
  return c;
}

MqttHA::Component MqttHA::createNumber(const Command* command) const {
  Component c = createComponent("number", command->key, command->name);
  c.fields["state_topic"] = createStateTopic("values", command->name);
  if (!command->ha_device_class.empty())
    c.fields["device_class"] = command->ha_device_class;
  if (!command->ha_entity_category.empty())
    c.fields["entity_category"] = command->ha_entity_category;
  c.fields["unit_of_measurement"] = command->unit;
  c.fields["value_template"] = "{{value_json.value}}";
  c.fields["command_topic"] = commandTopic;
  c.fields["command_template"] =
      "{\"id\":\"write\",\"key\":\"" + command->key + "\",\"value\":{{value}}}";
  c.fields["min"] = std::to_string(command->min);
  c.fields["max"] = std::to_string(command->max);
  c.fields["step"] = std::to_string(command->ha_number_step);
  c.fields["mode"] = command->ha_number_mode;  // "box" or "slider"
  return c;
}

MqttHA::Component MqttHA::createSelect(const Command* command) const {
  Component c = createComponent("select", command->key, command->name);
  c.fields["state_topic"] = createStateTopic("values", command->name);
  if (!command->ha_device_class.empty())
    c.fields["device_class"] = command->ha_device_class;
  if (!command->ha_entity_category.empty())
    c.fields["entity_category"] = command->ha_entity_category;
  c.fields["command_topic"] = commandTopic;

  // Parse ha_select_options string into vector of pairs
  std::vector<std::pair<std::string, int>> optionsVec;
  std::istringstream ss(command->ha_select_options);
  std::string token;
  while (std::getline(ss, token, ',')) {
    size_t sep = token.find(':');
    if (sep != std::string::npos) {
      std::string name = token.substr(0, sep);
      int value = std::stoi(token.substr(sep + 1));
      optionsVec.push_back({name, value});
      c.options.push_back(name);
    }
  }

  // Determine default option name and value
  const auto defaultIt =
      std::find_if(optionsVec.begin(), optionsVec.end(),
                   [&](const std::pair<std::string, int>& opt) {
                     return opt.first == command->ha_select_options_default;
                   });

  std::string defaultOptionName = optionsVec.empty() ? "" : optionsVec[0].first;
  int defaultOptionValue = optionsVec.empty() ? 0 : optionsVec[0].second;
  if (defaultIt != optionsVec.end()) {
    defaultOptionName = defaultIt->first;
    defaultOptionValue = defaultIt->second;
  }

  // Build value_template for displaying option name from value
  std::string valueMap = "{% set values = {";
  for (size_t i = 0; i < optionsVec.size(); ++i) {
    valueMap +=
        std::to_string(optionsVec[i].second) + ":'" + optionsVec[i].first + "'";
    if (i < optionsVec.size() - 1) valueMap += ",";
  }
  valueMap +=
      "} %}{{ values[value_json.value] if value_json.value in values.keys() "
      "else '" +
      defaultOptionName + "' }}";
  c.fields["value_template"] = valueMap;

  // Build command_template for sending value from option name
  std::string cmdMap = "{% set values = {";
  for (size_t i = 0; i < optionsVec.size(); ++i) {
    cmdMap +=
        "'" + optionsVec[i].first + "':" + std::to_string(optionsVec[i].second);
    if (i < optionsVec.size() - 1) cmdMap += ",";
  }
  cmdMap += "} %}{{ values[value] if value in values.keys() else " +
            std::to_string(defaultOptionValue) + " }}";
  c.fields["command_template"] = "{\"id\":\"write\",\"key\":\"" + command->key +
                                 "\",\"value\":" + cmdMap + "}";

  return c;
}

MqttHA::Component MqttHA::createButtonRestart() const {
  Component c = createComponent("button", "restart", "Restart");
  c.fields["command_topic"] = commandTopic;
  c.fields["payload_press"] = "{\"id\":\"restart\",\"value\":true}";
  c.fields["entity_category"] = "config";
  return c;
}

MqttHA::Component MqttHA::createDiagnostic(const std::string& component,
                                           const std::string& uniqueIdKey,
                                           const std::string& name) const {
  Component c = createComponent(component, uniqueIdKey, name);
  c.fields["entity_category"] = "diagnostic";
  return c;
}

MqttHA::Component MqttHA::createDiagnosticUptime() const {
  Component c = createDiagnostic("sensor", "uptime", "Uptime");
  c.fields["state_topic"] = createStateTopic("state", "uptime");
  c.fields["value_template"] =
      "{{timedelta(seconds=((value|float)/1000)|int)}}";

  c.device["name"] = thingName;
  c.device["manufacturer"] = thingManufacturer;
  c.device["model"] = thingModel;
  c.device["model_id"] = thingModelId;
  c.device["serial_number"] = uniqueId;
  c.device["hw_version"] = thingHwVersion;
  c.device["sw_version"] = thingSwVersion;
  c.device["configuration_url"] = thingConfigurationUrl;
  return c;
}

MqttHA::Component MqttHA::createDiagnosticFreeHeap() const {
  Component c = createDiagnostic("sensor", "free_heap", "Free Heap");
  c.fields["state_topic"] = createStateTopic("state", "free_heap");
  c.fields["unit_of_measurement"] = "B";
  c.fields["value_template"] = "{{value|int}}";
  return c;
}

MqttHA::Component MqttHA::createDiagnosticLoopDuration() const {
  Component c = createDiagnostic("sensor", "loop_duration", "Loop Duration");
  c.fields["state_topic"] = createStateTopic("state", "loop_duration");
  c.fields["unit_of_measurement"] = "µs";
  c.fields["value_template"] = "{{value|int}}";
  return c;
}

#endif