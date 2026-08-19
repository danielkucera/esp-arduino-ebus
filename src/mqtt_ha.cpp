#if defined(EBUS_INTERNAL)
#include <algorithm>
#include <cstring>
#include <ebus/detail/json_writer.hpp>
#include <mqtt.hpp>
#include <mqtt_ha.hpp>
#include <string>

#include "store.hpp"

MqttHA mqttha;

void MqttHA::setUniqueId(const std::string& id) {
  unique_id_ = id;
  device_identifiers_ = "ebus" + unique_id_;
}

void MqttHA::setRootTopic(const std::string& topic) {
  root_topic_ = topic;
  command_topic_ = root_topic_ + "request";
}

void MqttHA::setWillTopic(const std::string& topic) { will_topic_ = topic; }

void MqttHA::setEnabled(const bool enable) { enabled_ = enable; }

bool MqttHA::isEnabled() const { return enabled_; }

void MqttHA::setThingName(const std::string& name) { thing_name_ = name; }

void MqttHA::setThingModel(const std::string& model) { thing_model_ = model; }

void MqttHA::setThingModelId(const std::string& modelId) {
  thing_model_id_ = modelId;
}

void MqttHA::setThingHwVersion(const std::string& hwVersion) {
  thing_hw_version_ = hwVersion;
}

void MqttHA::setThingConfigurationUrl(const std::string& configurationUrl) {
  thing_configuration_url_ = configurationUrl;
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
                            const char* name, bool withDeviceInfo,
                            auto writeFields) {
    std::string objectId = name;
    std::transform(objectId.begin(), objectId.end(), objectId.begin(),
                   ::tolower);
    std::replace(objectId.begin(), objectId.end(), '/', '_');
    std::replace(objectId.begin(), objectId.end(), ' ', '_');

    std::string topic = "homeassistant/" + std::string(component) + '/' +
                        device_identifiers_ + '/' + objectId + "/config";

    if (!enabled_) {
      mqtt.publish(topic.c_str(), 0, true, "", false);
      return;
    }

    mqtt.publishStream(
        topic.c_str(), 0, true,
        [&](const ebus::JsonChunkVisitor& v) {
          ebus::detail::JsonWriter writer(v);
          auto root = writer.objectScope();
          writer.writeField("unique_id", device_identifiers_ + "_" + key);
          writer.writeField("name", name);
          writer.writeField("availability_topic", will_topic_);
          writer.writeField("availability_template", "{{value_json.value}}");

          {
            auto device = writer.objectScope("device");
            writer.writeField("identifiers", device_identifiers_);
            if (withDeviceInfo) {
              writer.writeField("name", thing_name_);
              writer.writeField("manufacturer", thing_manufacturer_);
              writer.writeField("model", thing_model_);
              writer.writeField("model_id", thing_model_id_);
              writer.writeField("hw_version", thing_hw_version_);
              writer.writeField("sw_version", thing_sw_version_);
              writer.writeField("configuration_url", thing_configuration_url_);
            }
          }

          writeFields(writer);
        },
        false);
  };

  publishDiag("button", "restart", "Restart", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("command_topic", command_topic_);
                w.writeField("payload_press",
                             "{\"id\":\"restart\",\"value\":true}");
                w.writeField("entity_category", "config");
              });

  publishDiag("sensor", "reset_code", "Reset Code", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template", "{{value_json.reset_code}}");
                w.writeField("icon", "mdi:restart");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "uptime", "Uptime", true,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "s");
                w.writeField("value_template",
                             "{{((value_json.uptime|float)/1000)|int}}");
                w.writeField("icon", "mdi:clock-outline");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "free_heap", "Free Heap", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "B");
                w.writeField("value_template", "{{value_json.free_heap}}");
                w.writeField("icon", "mdi:memory");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "loop_duration", "Loop Duration", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "µs");
                w.writeField("value_template", "{{value_json.loop_duration}}");
                w.writeField("icon", "mdi:timelapse");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "rssi", "WiFi RSSI", false,
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
    for (size_t i = 0; i < command->getFieldCount(); ++i) {
      if (command->getFieldHA(i)) {
        publishComponent(command, i, !enabled_);
      }
    }
  }
}

void MqttHA::publishComponent(const Command* command, size_t field_idx,
                              const bool remove) const {
  const HAProfile* profile = resolveProfile(command, field_idx);
  if (!profile && !remove) return;

  const std::string& component = profile ? profile->component : "";

  const std::string& dev_id = device_identifiers_;

  std::string_view field_name_sv = command->getFieldName(field_idx);
  std::string_view key_sv = command->getKey();

  char objectIdBuf[32];
  sanitizeObjectId(field_name_sv, objectIdBuf, sizeof(objectIdBuf));

  char topicBuf[128];
  int tlen =
      snprintf(topicBuf, sizeof(topicBuf), "homeassistant/%s/%s/%s/config",
               component.c_str(), dev_id.c_str(), objectIdBuf);
  if (tlen <= 0 || (size_t)tlen >= sizeof(topicBuf)) return;

  if (remove || !enabled_) {
    mqtt.publish(topicBuf, 0, true, "", false);
    return;
  }

  mqtt.publishStream(
      topicBuf, 0, true,
      [&](const ebus::JsonChunkVisitor& v) {
        ebus::detail::JsonWriter writer(v);
        auto root = writer.objectScope();

        char prettyNameBuf[32];
        size_t pn_len =
            std::min(field_name_sv.size(), sizeof(prettyNameBuf) - 1);
        for (size_t i = 0; i < pn_len; ++i) {
          char c = field_name_sv[i];
          prettyNameBuf[i] = (c == '/' || c == '_') ? ' ' : c;
        }
        prettyNameBuf[pn_len] = '\0';

        char uidBuf[96];
        snprintf(uidBuf, sizeof(uidBuf), "%s_%.*s_%zu", dev_id.c_str(),
                 (int)key_sv.size(), key_sv.data(), field_idx);
        writer.writeField("unique_id", uidBuf);
        writer.writeField("name", std::string_view(prettyNameBuf, pn_len));
        writer.writeField("availability_topic", will_topic_);
        writer.writeField("availability_template", "{{value_json.value}}");

        {
          auto device = writer.objectScope("device");
          writer.writeField("identifiers", device_identifiers_);
        }

        writer.writeField("state_topic",
                          createStateTopic("values", field_name_sv));

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
                   root_topic_.c_str(), (int)key_sv.size(), key_sv.data());
          writer.writeField("command_topic", cmdTopicBuf);
        }

        if (component == "sensor") {
          if (profile && profile->state_class && profile->state_class[0])
            writer.writeField("state_class", profile->state_class);
          if (command->getFieldCount() > 0 &&
              !std::string(command->getFieldUnit(0)).empty())
            writer.writeField("unit_of_measurement", command->getFieldUnit(0));
        }

        if (component == "number") {
          if (command->getFieldCount() > 0 &&
              !std::string(command->getFieldUnit(0)).empty())
            writer.writeField("unit_of_measurement", command->getFieldUnit(0));
          writer.writeField("value_template", "{{value_json.value}}");
          writer.writeField("command_template", "{{value}}");
          writer.writeFieldFloat("min", command->getFieldMin(0));
          writer.writeFieldFloat("max", command->getFieldMax(0));
          writer.writeFieldFloat("step", profile ? profile->step : 1);
          writer.writeField("mode", profile && profile->mode && profile->mode[0]
                                        ? profile->mode
                                        : "auto");
        }

        if (component == "switch") {
          writer.writeField("command_template", "{{value}}");
        }

        if (profile && profile->key_value_count > 0) {
          auto opt = createOptions(profile);
          if (component == "select") {
            {
              auto options = writer.arrayScope("options");
              for (const auto& s : opt.options) writer.writeValue(s);
            }
            writer.writeField("command_template", opt.cmd_map);
          }
          writer.writeField("value_template", opt.value_map);
        } else if (component == "sensor") {
          writer.writeField("value_template", "{{value_json.value}}");
        }
      },
      false);
}

std::string MqttHA::createStateTopic(const std::string& prefix,
                                     std::string_view topic) const {
  char buf[128];
  char lowerBuf[32];
  size_t tlen = std::min(topic.size(), sizeof(lowerBuf) - 1);
  for (size_t i = 0; i < tlen; i++) {
    lowerBuf[i] = std::tolower(static_cast<unsigned char>(topic[i]));
  }
  lowerBuf[tlen] = '\0';

  int slen = snprintf(buf, sizeof(buf), "%s%s%s", root_topic_.c_str(),
                      prefix.c_str(), prefix.empty() ? "" : "/");
  // Append lowercased topic
  for (size_t i = 0; i < tlen && slen < (int)sizeof(buf) - 1; i++) {
    buf[slen++] = lowerBuf[i];
  }
  buf[slen] = '\0';
  return std::string(buf);
}

MqttHA::KeyValueMapping MqttHA::createOptions(const HAProfile* profile) {
  if (!profile) return KeyValueMapping{};

  // Use stack buffers instead of std::string to reduce heap fragmentation
  int options_keys[5];
  const char* options_values[5];
  size_t options_count = 0;

  for (size_t i = 0; i < profile->key_value_count && i < 5; i++) {
    options_keys[i] = profile->key_value_pairs[i].first;
    options_values[i] = profile->key_value_pairs[i].second;
    options_count++;
  }

  int defaultOptionValue = 0;
  if (options_count > 0) {
    defaultOptionValue = options_keys[0];
    for (size_t i = 0; i < options_count; i++) {
      if (options_keys[i] == profile->default_key) {
        defaultOptionValue = options_keys[i];
        break;
      }
    }
  }

  // Build value_template and command_template using char buffers
  // Original: std::string valueMap = "{% set values = {" ... "} %}..."
  // Need to escape % for snprintf: use %% instead of %

  char value_map_buf[512];
  char cmd_map_buf[512];

  int vmLen = 0;
  int pnLen = 0;

  vmLen += snprintf(value_map_buf + vmLen, sizeof(value_map_buf) - vmLen,
                    "%%{ set values = {");
  pnLen += snprintf(cmd_map_buf + pnLen, sizeof(cmd_map_buf) - pnLen,
                    "%%{ set values = {");

  for (size_t i = 0; i < options_count; i++) {
    if (vmLen < (int)sizeof(value_map_buf)) {
      vmLen += snprintf(value_map_buf + vmLen, sizeof(value_map_buf) - vmLen,
                        "%d:'%s'", options_keys[i], options_values[i]);
      if (i < options_count - 1 && vmLen < (int)sizeof(value_map_buf)) {
        vmLen +=
            snprintf(value_map_buf + vmLen, sizeof(value_map_buf) - vmLen, ",");
      }
    }
    if (pnLen < (int)sizeof(cmd_map_buf)) {
      pnLen += snprintf(cmd_map_buf + pnLen, sizeof(cmd_map_buf) - pnLen,
                        "'%s':%d", options_values[i], options_keys[i]);
      if (i < options_count - 1 && pnLen < (int)sizeof(cmd_map_buf)) {
        pnLen +=
            snprintf(cmd_map_buf + pnLen, sizeof(cmd_map_buf) - pnLen, ",");
      }
    }
  }

  vmLen += snprintf(value_map_buf + vmLen, sizeof(value_map_buf) - vmLen,
                    " %%}{{ values[value_json.value] if value_json.value in "
                    "values.keys() else '%s' }}",
                    options_count > 0 ? options_values[0] : "");

  pnLen += snprintf(cmd_map_buf + pnLen, sizeof(cmd_map_buf) - pnLen,
                    " %%}{{ values[value] if value in values.keys() else "
                    "%d }}",
                    defaultOptionValue);

  // Build options list using StaticVector<FixedString<16>, 5>
  ebus::StaticVector<ebus::FixedString<16>, 5> options;
  for (size_t i = 0; i < options_count; i++) {
    options.push_back(ebus::FixedString<16>(options_values[i]));
  }

  KeyValueMapping mapping;
  mapping.options = options;
  mapping.value_map = std::string(value_map_buf);
  mapping.cmd_map = std::string(cmd_map_buf);
  return mapping;
}

const HAProfile* MqttHA::resolveProfile(const Command* command,
                                        size_t field_idx) {
  if (!command || field_idx >= command->getFieldCount() ||
      !command->getFieldHA(field_idx))
    return nullptr;
  return command->getFieldHAProfile(field_idx);
}

#endif
