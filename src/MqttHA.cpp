#if defined(EBUS_INTERNAL)
#include <Mqtt.hpp>
#include <MqttHA.hpp>
#include <algorithm>
#include <cstring>
#include <ebus/detail/json_writer.hpp>

#include "Store.hpp"

MqttHA mqttha;

void MqttHA::setUniqueId(const std::string& id) {
  uniqueId = id;
  deviceIdentifiers = "ebus" + uniqueId;
}

void MqttHA::setRootTopic(const std::string& topic) {
  rootTopic = topic;
  commandTopic = rootTopic + "request";
}

void MqttHA::setWillTopic(const std::string& topic) { willTopic = topic; }

void MqttHA::setEnabled(const bool enable) { enabled = enable; }

bool MqttHA::isEnabled() const { return enabled; }

void MqttHA::setThingName(const std::string& name) { thingName = name; }

void MqttHA::setThingModel(const std::string& model) { thingModel = model; }

void MqttHA::setThingModelId(const std::string& modelId) {
  thingModelId = modelId;
}

void MqttHA::setThingHwVersion(const std::string& hwVersion) {
  thingHwVersion = hwVersion;
}

void MqttHA::setThingConfigurationUrl(const std::string& configurationUrl) {
  thingConfigurationUrl = configurationUrl;
}

void MqttHA::sanitizeObjectId(std::string_view source, char* out,
                              size_t max_len) {
  size_t i = 0;
  for (; i < source.length() && i < max_len - 1; ++i) {
    char c = source[i];
    if (c == '/' || c == ' ')
      out[i] = '_';
    else
      out[i] = (char)tolower((unsigned char)c);
  }
  out[i] = '\0';
}

void MqttHA::publishDeviceInfo() const {
  auto publishDiag = [this](const char* component, const char* key,
                            const char* name, auto writeFields) {
    std::string objectId = name;
    std::transform(objectId.begin(), objectId.end(), objectId.begin(),
                   ::tolower);
    std::replace(objectId.begin(), objectId.end(), '/', '_');
    std::replace(objectId.begin(), objectId.end(), ' ', '_');

    std::string topic = "homeassistant/" + std::string(component) + '/' +
                        deviceIdentifiers + '/' + objectId + "/config";

    if (!enabled) {
      mqtt.publish(topic.c_str(), 0, true, "", false);
      return;
    }

    mqtt.publishStream(
        topic.c_str(), 0, true,
        [&](const ebus::JsonChunkVisitor& v) {
          ebus::detail::JsonWriter writer(v);
          auto root = writer.objectScope();
          writer.writeField("unique_id", deviceIdentifiers + "_" + key);
          writer.writeField("name", name);
          writer.writeField("availability_topic", willTopic);
          writer.writeField("availability_template", "{{value_json.value}}");

          {
            auto device = writer.objectScope("device");
            writer.writeField("identifiers", deviceIdentifiers);
            writer.writeField("name", thingName);
            writer.writeField("manufacturer", thingManufacturer);
            writer.writeField("model", thingModel);
            writer.writeField("model_id", thingModelId);
            writer.writeField("hw_version", thingHwVersion);
            writer.writeField("sw_version", thingSwVersion);
            writer.writeField("configuration_url", thingConfigurationUrl);
          }

          writeFields(writer);
        },
        false);
  };

  publishDiag(
      "button", "restart", "Restart", [this](ebus::detail::JsonWriter& w) {
        w.writeField("command_topic", commandTopic);
        w.writeField("payload_press", "{\"id\":\"restart\",\"value\":true}");
        w.writeField("entity_category", "config");
      });

  publishDiag("sensor", "reset_code", "Reset Code",
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template", "{{value_json.reset_code}}");
                w.writeField("icon", "mdi:restart");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "uptime", "Uptime",
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "s");
                w.writeField("value_template",
                             "{{((value_json.uptime|float)/1000)|int}}");
                w.writeField("icon", "mdi:clock-outline");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "free_heap", "Free Heap",
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "B");
                w.writeField("value_template", "{{value_json.free_heap}}");
                w.writeField("icon", "mdi:memory");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "loop_duration", "Loop Duration",
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "µs");
                w.writeField("value_template", "{{value_json.loop_duration}}");
                w.writeField("icon", "mdi:timelapse");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "rssi", "WiFi RSSI",
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "dBm");
                w.writeField("value_template", "{{value_json.rssi}}");
                w.writeField("icon", "mdi:wifi-strength-4");
                w.writeField("entity_category", "diagnostic");
              });
}

void MqttHA::publishComponents() const {
  for (const Command* command : store.getCommands()) {
    if (command->getHA())  // Check if HA config exists and is enabled
      mqtt.enqueueOutgoing(OutgoingAction(command, !enabled));
  }
}

void MqttHA::publishComponent(const Command* command, const bool remove) const {
  const HAProfile* profile = resolveProfile(command);
  if (!profile && !remove) return;

  const std::string& component = profile ? profile->component : "";

  const std::string& dev_id = deviceIdentifiers;

  std::string_view name_sv = command->getName();
  std::string_view key_sv = command->getKey();

  char objectIdBuf[32];
  sanitizeObjectId(name_sv, objectIdBuf, sizeof(objectIdBuf));

  char topicBuf[128];
  int tlen =
      snprintf(topicBuf, sizeof(topicBuf), "homeassistant/%s/%s/%s/config",
               component.c_str(), dev_id.c_str(), objectIdBuf);
  if (tlen <= 0 || (size_t)tlen >= sizeof(topicBuf)) return;

  if (remove || !enabled) {
    mqtt.publish(topicBuf, 0, true, "", false);
    return;
  }

  mqtt.publishStream(
      topicBuf, 0, true,
      [&](const ebus::JsonChunkVisitor& v) {
        ebus::detail::JsonWriter writer(v);
        auto root = writer.objectScope();

        char prettyNameBuf[32];
        size_t pn_len = std::min(name_sv.size(), sizeof(prettyNameBuf) - 1);
        for (size_t i = 0; i < pn_len; ++i) {
          char c = name_sv[i];
          prettyNameBuf[i] = (c == '/' || c == '_') ? ' ' : c;
        }
        prettyNameBuf[pn_len] = '\0';

        char uidBuf[96];
        snprintf(uidBuf, sizeof(uidBuf), "%s_%.*s", dev_id.c_str(),
                 (int)key_sv.size(), key_sv.data());
        writer.writeField("unique_id", uidBuf);
        writer.writeField("name", std::string_view(prettyNameBuf, pn_len));
        writer.writeField("availability_topic", willTopic);
        writer.writeField("availability_template", "{{value_json.value}}");

        {
          auto device = writer.objectScope("device");
          writer.writeField("identifiers", deviceIdentifiers);
        }

        writer.writeField("state_topic", createStateTopic("values", name_sv));

        if (profile && profile->device_class && profile->device_class[0])
          writer.writeField("device_class", profile->device_class);
        if (profile && profile->entity_category && profile->entity_category[0])
          writer.writeField("entity_category", profile->entity_category);

        if (component == "binary_sensor" || component == "switch") {
          writer.writeField("payload_on",
                            std::to_string(profile ? profile->payload_on : 1));
          writer.writeField("payload_off",
                            std::to_string(profile ? profile->payload_off : 0));
          writer.writeField("value_template", "{{value_json.value}}");
        }

        if (component == "switch" || component == "number" ||
            component == "select") {
          char cmdTopicBuf[96];
          snprintf(cmdTopicBuf, sizeof(cmdTopicBuf), "%sset/%.*s",
                   rootTopic.c_str(), (int)key_sv.size(), key_sv.data());
          writer.writeField("command_topic", cmdTopicBuf);
        }

        if (component == "sensor") {
          if (profile && profile->state_class && profile->state_class[0])
            writer.writeField("state_class", profile->state_class);
          if (!command->getUnit().empty())
            writer.writeField("unit_of_measurement", command->getUnit());
        }

        if (component == "number") {
          if (!command->getUnit().empty())
            writer.writeField("unit_of_measurement", command->getUnit());
          writer.writeField("value_template", "{{value_json.value}}");
          writer.writeField("command_template", "{{value}}");
          writer.writeFieldFloat("min", command->getMin());
          writer.writeFieldFloat("max", command->getMax());
          writer.writeFieldFloat("step", profile ? profile->step : 1);
          writer.writeField("mode", profile && profile->mode && profile->mode[0]
                                        ? profile->mode
                                        : "auto");
        }

        if (component == "switch") {
          writer.writeField("command_template", "{{value}}");
        }

        if (!command->getHAKeyValueMap().empty()) {
          auto opt = createOptions(command->getHAKeyValueMap(),
                                   command->getHADefaultKey());
          if (component == "select") {
            {
              auto options = writer.arrayScope("options");
              for (const auto& s : opt.options) writer.writeValue(s);
            }
            writer.writeField("command_template", opt.cmdMap);
          }
          writer.writeField("value_template", opt.valueMap);
        } else if (component == "sensor") {
          writer.writeField("value_template", "{{value_json.value}}");
        }
      },
      false);
}

std::string MqttHA::createStateTopic(const std::string& prefix,
                                     std::string_view topic) const {
  std::string stateTopic = std::string(topic);
  std::transform(stateTopic.begin(), stateTopic.end(), stateTopic.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return rootTopic + prefix + (prefix.empty() ? "" : "/") + stateTopic;
}

MqttHA::KeyValueMapping MqttHA::createOptions(
    const command_types::HAKeyValueMap& ha_key_value_map,
    const int& ha_default_key) {
  // Create a vector of options names and a vector of pairs
  std::vector<std::pair<std::string, int>> optionsVec;
  std::vector<std::string> options;

  // Populate optionsVec and options from the map
  for (const auto& kv : ha_key_value_map) {
    optionsVec.emplace_back(std::string(kv.second), kv.first);
    options.push_back(std::string(kv.second));
  }

  // Determine default option name and value
  const auto defaultIt =
      std::find_if(optionsVec.begin(), optionsVec.end(),
                   [&](const std::pair<std::string, int>& opt) {
                     return opt.second == ha_default_key;
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

  // Build command_template for sending value from option name
  std::string cmdMap = "{% set values = {";
  for (size_t i = 0; i < optionsVec.size(); ++i) {
    cmdMap +=
        "'" + optionsVec[i].first + "':" + std::to_string(optionsVec[i].second);
    if (i < optionsVec.size() - 1) cmdMap += ",";
  }
  cmdMap += "} %}{{ values[value] if value in values.keys() else " +
            std::to_string(defaultOptionValue) + " }}";

  return KeyValueMapping{options, valueMap, cmdMap};
}

const HAProfile* MqttHA::resolveProfile(const Command* command) {
  if (!command || !command->getHA()) return nullptr;
  return findHAProfile(command->getHAProfile());
}

#endif
