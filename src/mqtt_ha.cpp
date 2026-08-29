#if defined(EBUS_INTERNAL)
#include <algorithm>
#include <cstring>
#include <ebus/detail/json_writer.hpp>
#include <mqtt.hpp>
#include <mqtt_ha.hpp>
#include <string>

#include "command_manager.hpp"

MqttHA mqttha;

namespace {
void formatPrettyName(std::string_view sv, char* out, size_t max_len) {
  if (max_len == 0) return;
  size_t out_idx = 0;
  bool capitalize_next = true;
  for (size_t i = 0; i < sv.size() && out_idx < max_len - 1; ++i) {
    char c = sv[i];
    if (c == '/' || c == '_') {
      out[out_idx++] = ' ';
      capitalize_next = true;
    } else {
      if (capitalize_next && std::islower(static_cast<unsigned char>(c))) {
        out[out_idx++] =
            static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      } else {
        out[out_idx++] = c;
      }
      capitalize_next = false;
    }
  }
  out[out_idx] = '\0';
}
}  // namespace

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
                w.writeField("payload_press", "{\"id\":\"restart\"}");
                w.writeField("entity_category", "config");
              });

  publishDiag("sensor", "reset_code", "Reset Code", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template",
                             "{{value_json.status.reset_code}}");
                w.writeField("icon", "mdi:restart");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "uptime", "Uptime", true,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "s");
                w.writeField("value_template",
                             "{{((value_json.status.uptime|float)/1000)|int}}");
                w.writeField("icon", "mdi:clock-outline");
                w.writeField("entity_category", "diagnostic");
              });

  // Heap
  publishDiag("sensor", "free_heap", "Heap Total Free Bytes", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "B");
                w.writeField("value_template",
                             "{{value_json.heap.total_free_bytes}}");
                w.writeField("icon", "mdi:memory");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "largest_free_block", "Heap Largest Free Block", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "B");
                w.writeField("value_template",
                             "{{value_json.heap.largest_free_block}}");
                w.writeField("icon", "mdi:memory");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "min_free_heap", "Heap Minimum Free Bytes", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "B");
                w.writeField("value_template",
                             "{{value_json.heap.minimum_free_bytes}}");
                w.writeField("icon", "mdi:memory");
                w.writeField("entity_category", "diagnostic");
              });

  // WiFi
  publishDiag("sensor", "rssi", "WiFi RSSI", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "dBm");
                w.writeField("value_template", "{{value_json.wifi.rssi}}");
                w.writeField("icon", "mdi:wifi-strength-4");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "wifi_last_connect", "WiFi Last Connect", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template",
                             "{{value_json.wifi.last_connect}}");
                w.writeField("icon", "mdi:wifi");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "wifi_reconnect_count", "WiFi Reconnect Count", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template",
                             "{{value_json.wifi.reconnect_count}}");
                w.writeField("icon", "mdi:wifi-refresh");
                w.writeField("entity_category", "diagnostic");
              });

  // Firmware
  publishDiag("sensor", "firmware_version", "Firmware Version", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template",
                             "{{value_json.firmware.version}}");
                w.writeField("icon", "mdi:chip");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "sdk_version", "Firmware ESP-IDF Version", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template",
                             "{{value_json.firmware.esp_idf_version}}");
                w.writeField("icon", "mdi:chip");
                w.writeField("entity_category", "diagnostic");
              });

  // Chip
  publishDiag("sensor", "chip_revision", "Chip Revision", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template",
                             "{{value_json.chip.chip_revision}}");
                w.writeField("icon", "mdi:cpu-64-bit");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "flash_size", "Chip Flash Size", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "B");
                w.writeField("value_template",
                             "{{value_json.chip.flash_size}}");
                w.writeField("icon", "mdi:memory");
                w.writeField("entity_category", "diagnostic");
              });

  // eBUS
  publishDiag("sensor", "ebus_pwm", "eBUS PWM", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template", "{{value_json.ebus.pwm}}");
                w.writeField("icon", "mdi:fan");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "ebus_address", "eBUS Address", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template",
                             "{{value_json.ebus.ebus_address}}");
                w.writeField("icon", "mdi:network");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "bus_window", "eBUS Bus Window", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "µs");
                w.writeField("value_template",
                             "{{value_json.ebus.bus_window}}");
                w.writeField("icon", "mdi:timer");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "bus_offset", "eBUS Bus Offset", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("unit_of_measurement", "µs");
                w.writeField("value_template",
                             "{{value_json.ebus.bus_offset}}");
                w.writeField("icon", "mdi:timer");
                w.writeField("entity_category", "diagnostic");
              });

  // Schedule
  publishDiag("sensor", "active_commands", "Schedule Active Commands", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template",
                             "{{value_json.schedule.active_commands}}");
                w.writeField("icon", "mdi:play-circle");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "passive_commands", "Schedule Passive Commands", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template",
                             "{{value_json.schedule.passive_commands}}");
                w.writeField("icon", "mdi:pause-circle");
                w.writeField("entity_category", "diagnostic");
              });

  // Sockets
  publishDiag("sensor", "sockets_detected", "Sockets Detected", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template",
                             "{{value_json.sockets.detected}}");
                w.writeField("icon", "mdi:lan-connect");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "sockets_connected", "Sockets Connected", false,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("state_topic", createStateTopic("", "state"));
                w.writeField("value_template",
                             "{{value_json.sockets.connected}}");
                w.writeField("icon", "mdi:lan-connect");
                w.writeField("entity_category", "diagnostic");
              });
}

void MqttHA::publishComponent(const Command* command, size_t field_idx,
                              const bool remove) const {
  const HAProfile* profile = resolveProfile(command, field_idx);
  if (!profile) return;

  std::string component = profile->component;

  const std::string& dev_id = device_identifiers_;

  std::string_view field_name_sv = command->getFieldName(field_idx);
  std::string_view key_sv = command->getKey();

  char rawObjectIdBuf[128];
  if (!command->getName().empty()) {
    snprintf(rawObjectIdBuf, sizeof(rawObjectIdBuf), "%.*s_%.*s_%.*s",
             (int)key_sv.size(), key_sv.data(), (int)command->getName().size(),
             command->getName().data(), (int)field_name_sv.size(),
             field_name_sv.data());
  } else {
    snprintf(rawObjectIdBuf, sizeof(rawObjectIdBuf), "%.*s_%.*s",
             (int)key_sv.size(), key_sv.data(), (int)field_name_sv.size(),
             field_name_sv.data());
  }
  char objectIdBuf[128];
  sanitizeObjectId(rawObjectIdBuf, objectIdBuf, sizeof(objectIdBuf));

  char topicBuf[128];
  int tlen =
      snprintf(topicBuf, sizeof(topicBuf), "homeassistant/%s/%s/%s/config",
               component.c_str(), dev_id.c_str(), objectIdBuf);
  if (tlen <= 0 || (size_t)tlen >= sizeof(topicBuf)) return;

  if (remove || !enabled_) {
    mqtt.publish(topicBuf, 0, true, "", false);
    return;
  }

  // Use command-level state topic (all fields share same state topic)
  std::string state_topic = createStateTopic("values", command->getName());

  mqtt.publishStream(
      topicBuf, 0, true,
      [&](const ebus::JsonChunkVisitor& v) {
        ebus::detail::JsonWriter writer(v);
        auto root = writer.objectScope();

        char prettyCmdBuf[64];
        formatPrettyName(command->getName(), prettyCmdBuf,
                         sizeof(prettyCmdBuf));

        char entityNameBuf[128];
        if (command->getFieldCount() > 1 && field_name_sv != "value") {
          char prettyFieldBuf[64];
          formatPrettyName(field_name_sv, prettyFieldBuf,
                           sizeof(prettyFieldBuf));
          if (prettyCmdBuf[0] != '\0') {
            snprintf(entityNameBuf, sizeof(entityNameBuf), "%s %s",
                     prettyCmdBuf, prettyFieldBuf);
          } else {
            snprintf(entityNameBuf, sizeof(entityNameBuf), "%s",
                     prettyFieldBuf);
          }
        } else {
          if (prettyCmdBuf[0] != '\0') {
            snprintf(entityNameBuf, sizeof(entityNameBuf), "%s", prettyCmdBuf);
          } else {
            char prettyFieldBuf[64];
            formatPrettyName(field_name_sv, prettyFieldBuf,
                             sizeof(prettyFieldBuf));
            snprintf(entityNameBuf, sizeof(entityNameBuf), "%s",
                     prettyFieldBuf);
          }
        }

        // Unique ID includes field_idx to distinguish multiple fields
        char uidBuf[96];
        snprintf(uidBuf, sizeof(uidBuf), "%s_%.*s_%zu", dev_id.c_str(),
                 (int)key_sv.size(), key_sv.data(), field_idx);
        writer.writeField("unique_id", uidBuf);
        writer.writeField("name", entityNameBuf);
        writer.writeField("availability_topic", will_topic_);
        writer.writeField("availability_template", "{{value_json.value}}");

        {
          auto device = writer.objectScope("device");
          writer.writeField("identifiers", device_identifiers_);
        }

        // All fields of a command share the same state topic
        writer.writeField("state_topic", state_topic);

        if (profile && profile->device_class && profile->device_class[0])
          writer.writeField("device_class", profile->device_class);
        if (profile && profile->entity_category && profile->entity_category[0])
          writer.writeField("entity_category", profile->entity_category);

        if (component == "binary_sensor" || component == "switch") {
          writer.writeField("payload_on",
                            std::to_string(profile ? profile->payload_on : 1));
          writer.writeField("payload_off",
                            std::to_string(profile ? profile->payload_off : 0));
          writer.writeField(
              "value_template",
              "{{value_json." + std::string(field_name_sv) + "}}");
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
          // Use field-specific unit
          writer.writeField("unit_of_measurement",
                            getFieldUnit(command, field_idx));
        }

        if (component == "number") {
          writer.writeField("unit_of_measurement",
                            getFieldUnit(command, field_idx));
          writer.writeField(
              "value_template",
              "{{value_json." + std::string(field_name_sv) + "}}");
          writer.writeField("command_template", "{{value}}");
          writer.writeFieldFloat("min", getFieldMin(command, field_idx));
          writer.writeFieldFloat("max", getFieldMax(command, field_idx));
          writer.writeFieldFloat("step", profile ? profile->step : 1);
          writer.writeField("mode", profile && profile->mode && profile->mode[0]
                                        ? profile->mode
                                        : "auto");
        }

        if (component == "switch") {
          writer.writeField("command_template", "{{value}}");
        }

        if (profile && profile->key_value_count > 0) {
          auto opt = createOptions(profile, field_name_sv);
          if (component == "select") {
            {
              auto options = writer.arrayScope("options");
              for (const auto& s : opt.options) writer.writeValue(s);
            }
            writer.writeField("command_template", opt.cmd_map);
          }
          writer.writeField("value_template", opt.value_map);
        } else if (component == "sensor") {
          writer.writeField(
              "value_template",
              "{{value_json." + std::string(field_name_sv) + "}}");
        }
      },
      false);
}

void MqttHA::publishComponentIfEnabled(const Command* command,
                                       size_t field_idx) const {
  if (!enabled_) return;
  if (command && field_idx < command->getFieldCount() &&
      command->hasFieldHA(field_idx)) {
    publishComponent(command, field_idx, false);
  }
}

void MqttHA::publishComponents() const {
  commandManager.forEachCommand([this](const Command* command) {
    for (size_t i = 0; i < command->getFieldCount(); ++i) {
      if (command->hasFieldHA(i)) {
        publishComponent(command, i, !enabled_);
      }
    }
  });
}

void MqttHA::publishComponentsIfEnabled() const {
  if (!enabled_) return;
  publishComponents();
}

void MqttHA::removeComponent(const Command* command) const {
  if (!command) return;
  for (size_t i = 0; i < command->getFieldCount(); ++i) {
    if (command->hasFieldHA(i)) {
      publishComponent(command, i, true);
    }
  }
}

void MqttHA::removeComponents() const {
  commandManager.forEachCommand(
      [this](const Command* command) { removeComponent(command); });
}

void MqttHA::removeComponentIfEnabled(const Command* command,
                                      size_t field_idx) const {
  if (!enabled_) return;
  if (command && field_idx < command->getFieldCount() &&
      command->hasFieldHA(field_idx)) {
    publishComponent(command, field_idx, true);
  }
}

void MqttHA::onMqttConnected() const {
  if (!enabled_) return;
  publishDeviceInfo();
  publishComponents();
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

std::string MqttHA::createStateTopic(const std::string& prefix,
                                     std::string_view topic) const {
  char buf[128];
  char lowerBuf[128];
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

MqttHA::KeyValueMapping MqttHA::createOptions(const HAProfile* profile,
                                              std::string_view field_name) {
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

  char value_map_buf[256];
  char cmd_map_buf[256];

  int vmLen = 0;
  int pnLen = 0;

  const char value_prefix[] = "{% set values = {";
  const char cmd_prefix[] = "{% set values = {";
  memcpy(value_map_buf + vmLen, value_prefix, sizeof(value_prefix) - 1);
  vmLen += sizeof(value_prefix) - 1;
  memcpy(cmd_map_buf + pnLen, cmd_prefix, sizeof(cmd_prefix) - 1);
  pnLen += sizeof(cmd_prefix) - 1;

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

  snprintf(value_map_buf + vmLen, sizeof(value_map_buf) - vmLen,
           "} %%}{{ values[value_json.%.*s] if value_json.%.*s in "
           "values.keys() else '%s' }}",
           (int)field_name.size(), field_name.data(), (int)field_name.size(),
           field_name.data(), options_count > 0 ? options_values[0] : "");

  snprintf(cmd_map_buf + pnLen, sizeof(cmd_map_buf) - pnLen,
           "} %%}{{ values[value] if value in values.keys() else "
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
      !command->hasFieldHA(field_idx))
    return nullptr;
  return command->getFieldHAProfile(field_idx);
}

const char* MqttHA::getFieldUnit(const Command* command, size_t field_idx) {
  if (!command || field_idx >= command->getFieldCount()) return "";
  return command->getFieldUnit(field_idx);
}

float MqttHA::getFieldMin(const Command* command, size_t field_idx) {
  if (!command || field_idx >= command->getFieldCount()) return 0.0f;
  return command->getFieldMin(field_idx);
}

float MqttHA::getFieldMax(const Command* command, size_t field_idx) {
  if (!command || field_idx >= command->getFieldCount()) return 0.0f;
  return command->getFieldMax(field_idx);
}

#endif
