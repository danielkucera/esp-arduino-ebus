#if defined(EBUS_INTERNAL)
#include <algorithm>
#include <cstring>
#include <ebus/detail/json_writer.hpp>
#include <mqtt.hpp>
#include <mqtt_ha.hpp>
#include <string>

#include "command_manager.hpp"

// clang-format off
namespace {
// Reference public limits from header for consistency
inline constexpr size_t max_root_topic_length = mqtt_ha_limits::max_root_topic_length;
inline constexpr size_t max_device_id_length = mqtt_ha_limits::max_device_id_length;

namespace mqtt_ha_buffer_limits {
// Topic and ID limits
inline constexpr size_t max_topic_length = 96;         // MQTT discovery topic (e.g., homeassistant/number/ebusXXXX/32_name/config)
inline constexpr size_t max_state_topic_length = 96;   // State topic (e.g., ebusXXXX/values/commandname)
inline constexpr size_t max_object_id_length = 64;     // Sanitized object ID (e.g., 32_basement_temperature_nightroomsetpoint_value)
inline constexpr size_t max_raw_object_id_length = 64; // Raw object ID before sanitization
inline constexpr size_t max_uid_length = 48;           // Unique ID (e.g., ebusXXXX_32_0)

inline constexpr size_t max_pretty_name_length = 64;    // Pretty-printed name (e.g., Basement Temperature NightRoomSetPoint)
inline constexpr size_t max_entity_name_length = 
    max_pretty_name_length * 2 + 1;                     // Entity name: pretty_cmd + " " + pretty_field
inline constexpr size_t max_value_template_length = 48; // JSON template (e.g., {{value_json.value}})
inline constexpr size_t max_cmd_topic_length = 48;      // Command topic (e.g., ebusXXXX/set/32)

// Select/option mapping buffers
inline constexpr size_t max_payload_buf_length = 8;     // Payload on/off (single digit)
inline constexpr size_t max_option_value_map_length = 
    mqtt_ha_limits::max_option_value_map_length;        // Option value/cmd map templates
inline constexpr size_t max_options_count = 
    mqtt_ha_limits::max_options_count;                  // Maximum key-value pairs per profile

inline constexpr size_t max_lower_buf_length = 64;      // Lowercase conversion buffer in createStateTopic
} // namespace mqtt_ha_buffer_limits
} // namespace
// clang-format on

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
  // Ensure device identifier fits within limits: "ebus" + id + null <=
  // max_device_id_length + 1
  if (id.size() > mqtt_ha_limits::max_device_id_length - 4) {
    unique_id_ = id.substr(0, mqtt_ha_limits::max_device_id_length - 4);
  } else {
    unique_id_ = id;
  }
  device_identifiers_ = "ebus" + unique_id_;
}

void MqttHA::setRootTopic(const std::string& topic) {
  // Ensure root topic fits within limits
  if (topic.size() > mqtt_ha_limits::max_root_topic_length) {
    root_topic_ = topic.substr(0, mqtt_ha_limits::max_root_topic_length);
  } else {
    root_topic_ = topic;
  }
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
  char state_topic_buf[mqtt_ha_buffer_limits::max_state_topic_length];
  createStateTopic(state_topic_buf, sizeof(state_topic_buf), "", "state");

  auto publishDiag = [this, &state_topic_buf](
                         const char* component, const char* key,
                         const char* name, bool with_device_info,
                         const char* state_topic, auto write_fields) {
    char object_id_buf[mqtt_ha_buffer_limits::max_object_id_length];
    {
      size_t i = 0;
      size_t nlen = strlen(name);
      for (; i < nlen && i < sizeof(object_id_buf) - 1; ++i) {
        char c = name[i];
        object_id_buf[i] =
            (c == '/' || c == ' ') ? '_' : (char)tolower((unsigned char)c);
      }
      object_id_buf[i] = '\0';
    }

    char topic_buf[mqtt_ha_buffer_limits::max_topic_length];
    snprintf(topic_buf, sizeof(topic_buf), "homeassistant/%s/%s/%s/config",
             component, device_identifiers_.c_str(), object_id_buf);

    if (!enabled_) {
      mqtt.publish(topic_buf, 0, true, "", false);
      return;
    }

    char uid_buf[mqtt_ha_buffer_limits::max_uid_length];
    snprintf(uid_buf, sizeof(uid_buf), "%s_%s", device_identifiers_.c_str(),
             key);

    mqtt.publishStream(
        topic_buf, 0, true,
        [&](const ebus::JsonChunkVisitor& v) {
          ebus::detail::JsonWriter writer(v);
          auto root = writer.objectScope();
          writer.writeField("unique_id", uid_buf);
          writer.writeField("name", name);
          writer.writeField("availability_topic", will_topic_);
          writer.writeField("availability_template", "{{value_json.value}}");

          {
            auto device = writer.objectScope("device");
            writer.writeField("identifiers", device_identifiers_);
            if (with_device_info) {
              writer.writeField("name", thing_name_);
              writer.writeField("manufacturer", thing_manufacturer_);
              writer.writeField("model", thing_model_);
              writer.writeField("model_id", thing_model_id_);
              writer.writeField("hw_version", thing_hw_version_);
              writer.writeField("sw_version", thing_sw_version_);
              writer.writeField("configuration_url", thing_configuration_url_);
            }
          }

          if (state_topic) writer.writeField("state_topic", state_topic);

          write_fields(writer);
        },
        false);
  };

  publishDiag("button", "restart", "Restart", false, nullptr,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("command_topic", command_topic_);
                w.writeField("payload_press", "{\"id\":\"restart\"}");
                w.writeField("entity_category", "config");
              });

  publishDiag("sensor", "reset_code", "Reset Code", false, state_topic_buf,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("value_template",
                             "{{value_json.status.reset_code}}");
                w.writeField("icon", "mdi:restart");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "uptime", "Uptime", true, state_topic_buf,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("unit_of_measurement", "s");
                w.writeField("value_template",
                             "{{((value_json.status.uptime|float)/1000)|int}}");
                w.writeField("icon", "mdi:clock-outline");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "free_heap", "Heap Total Free Bytes", false,
              state_topic_buf, [this](ebus::detail::JsonWriter& w) {
                w.writeField("unit_of_measurement", "B");
                w.writeField("value_template",
                             "{{value_json.heap.total_free_bytes}}");
                w.writeField("icon", "mdi:memory");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "largest_free_block", "Heap Largest Free Block", false,
              state_topic_buf, [this](ebus::detail::JsonWriter& w) {
                w.writeField("unit_of_measurement", "B");
                w.writeField("value_template",
                             "{{value_json.heap.largest_free_block}}");
                w.writeField("icon", "mdi:memory");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "min_free_heap", "Heap Minimum Free Bytes", false,
              state_topic_buf, [this](ebus::detail::JsonWriter& w) {
                w.writeField("unit_of_measurement", "B");
                w.writeField("value_template",
                             "{{value_json.heap.minimum_free_bytes}}");
                w.writeField("icon", "mdi:memory");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "rssi", "WiFi RSSI", false, state_topic_buf,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("unit_of_measurement", "dBm");
                w.writeField("value_template", "{{value_json.wifi.rssi}}");
                w.writeField("icon", "mdi:wifi-strength-4");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "wifi_last_connect", "WiFi Last Connect", false,
              state_topic_buf, [this](ebus::detail::JsonWriter& w) {
                w.writeField("value_template",
                             "{{value_json.wifi.last_connect}}");
                w.writeField("icon", "mdi:wifi");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "wifi_reconnect_count", "WiFi Reconnect Count", false,
              state_topic_buf, [this](ebus::detail::JsonWriter& w) {
                w.writeField("value_template",
                             "{{value_json.wifi.reconnect_count}}");
                w.writeField("icon", "mdi:wifi-refresh");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "firmware_version", "Firmware Version", false,
              state_topic_buf, [this](ebus::detail::JsonWriter& w) {
                w.writeField("value_template",
                             "{{value_json.firmware.version}}");
                w.writeField("icon", "mdi:chip");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "sdk_version", "Firmware ESP-IDF Version", false,
              state_topic_buf, [this](ebus::detail::JsonWriter& w) {
                w.writeField("value_template",
                             "{{value_json.firmware.esp_idf_version}}");
                w.writeField("icon", "mdi:chip");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "chip_revision", "Chip Revision", false,
              state_topic_buf, [this](ebus::detail::JsonWriter& w) {
                w.writeField("value_template",
                             "{{value_json.chip.chip_revision}}");
                w.writeField("icon", "mdi:cpu-64-bit");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "flash_size", "Chip Flash Size", false, state_topic_buf,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("unit_of_measurement", "B");
                w.writeField("value_template",
                             "{{value_json.chip.flash_size}}");
                w.writeField("icon", "mdi:memory");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "ebus_pwm", "eBUS PWM", false, state_topic_buf,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("value_template", "{{value_json.ebus.pwm}}");
                w.writeField("icon", "mdi:fan");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "ebus_address", "eBUS Address", false, state_topic_buf,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("value_template",
                             "{{value_json.ebus.ebus_address}}");
                w.writeField("icon", "mdi:network");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "bus_window", "eBUS Bus Window", false, state_topic_buf,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("unit_of_measurement", "µs");
                w.writeField("value_template",
                             "{{value_json.ebus.bus_window}}");
                w.writeField("icon", "mdi:timer");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "bus_offset", "eBUS Bus Offset", false, state_topic_buf,
              [this](ebus::detail::JsonWriter& w) {
                w.writeField("unit_of_measurement", "µs");
                w.writeField("value_template",
                             "{{value_json.ebus.bus_offset}}");
                w.writeField("icon", "mdi:timer");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "active_commands", "Schedule Active Commands", false,
              state_topic_buf, [this](ebus::detail::JsonWriter& w) {
                w.writeField("value_template",
                             "{{value_json.schedule.active_commands}}");
                w.writeField("icon", "mdi:play-circle");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "passive_commands", "Schedule Passive Commands", false,
              state_topic_buf, [this](ebus::detail::JsonWriter& w) {
                w.writeField("value_template",
                             "{{value_json.schedule.passive_commands}}");
                w.writeField("icon", "mdi:pause-circle");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "sockets_detected", "Sockets Detected", false,
              state_topic_buf, [this](ebus::detail::JsonWriter& w) {
                w.writeField("value_template",
                             "{{value_json.sockets.detected}}");
                w.writeField("icon", "mdi:lan-connect");
                w.writeField("entity_category", "diagnostic");
              });

  publishDiag("sensor", "sockets_connected", "Sockets Connected", false,
              state_topic_buf, [this](ebus::detail::JsonWriter& w) {
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

  std::string_view component = profile->component;

  const std::string& dev_id = device_identifiers_;

  std::string_view field_name_sv = command->getFieldName(field_idx);
  std::string_view key_sv = command->getKey();

  char raw_object_id_buf[mqtt_ha_buffer_limits::max_raw_object_id_length];
  if (!command->getName().empty()) {
    snprintf(raw_object_id_buf, sizeof(raw_object_id_buf), "%.*s_%.*s_%.*s",
             (int)key_sv.size(), key_sv.data(), (int)command->getName().size(),
             command->getName().data(), (int)field_name_sv.size(),
             field_name_sv.data());
  } else {
    snprintf(raw_object_id_buf, sizeof(raw_object_id_buf), "%.*s_%.*s",
             (int)key_sv.size(), key_sv.data(), (int)field_name_sv.size(),
             field_name_sv.data());
  }
  char object_id_buf[mqtt_ha_buffer_limits::max_object_id_length];
  sanitizeObjectId(raw_object_id_buf, object_id_buf, sizeof(object_id_buf));

  char topic_buf[mqtt_ha_buffer_limits::max_topic_length];
  int tlen =
      snprintf(topic_buf, sizeof(topic_buf), "homeassistant/%s/%s/%s/config",
               component.data(), dev_id.c_str(), object_id_buf);
  if (tlen <= 0 || (size_t)tlen >= sizeof(topic_buf)) return;

  if (remove || !enabled_) {
    mqtt.publish(topic_buf, 0, true, "", false);
    return;
  }

  char state_topic_buf[mqtt_ha_buffer_limits::max_state_topic_length];
  createStateTopic(state_topic_buf, sizeof(state_topic_buf), "values",
                   command->getName());

  char value_template_buf[mqtt_ha_buffer_limits::max_value_template_length];

  mqtt.publishStream(
      topic_buf, 0, true,
      [&](const ebus::JsonChunkVisitor& v) {
        ebus::detail::JsonWriter writer(v);
        auto root = writer.objectScope();

        char pretty_cmd_buf[mqtt_ha_buffer_limits::max_pretty_name_length];
        formatPrettyName(command->getName(), pretty_cmd_buf,
                         sizeof(pretty_cmd_buf));

        char entity_name_buf[mqtt_ha_buffer_limits::max_entity_name_length];
        if (command->getFieldCount() > 1 && field_name_sv != "value") {
          char pretty_field_buf[mqtt_ha_buffer_limits::max_pretty_name_length];
          formatPrettyName(field_name_sv, pretty_field_buf,
                           sizeof(pretty_field_buf));
          if (pretty_cmd_buf[0] != '\0') {
            snprintf(entity_name_buf, sizeof(entity_name_buf), "%s %s",
                     pretty_cmd_buf, pretty_field_buf);
          } else {
            snprintf(entity_name_buf, sizeof(entity_name_buf), "%s",
                     pretty_field_buf);
          }
        } else {
          if (pretty_cmd_buf[0] != '\0') {
            snprintf(entity_name_buf, sizeof(entity_name_buf), "%s",
                     pretty_cmd_buf);
          } else {
            char
                pretty_field_buf[mqtt_ha_buffer_limits::max_pretty_name_length];
            formatPrettyName(field_name_sv, pretty_field_buf,
                             sizeof(pretty_field_buf));
            snprintf(entity_name_buf, sizeof(entity_name_buf), "%s",
                     pretty_field_buf);
          }
        }

        // Unique ID includes field_idx to distinguish multiple fields
        char uid_buf[mqtt_ha_buffer_limits::max_uid_length];
        snprintf(uid_buf, sizeof(uid_buf), "%s_%.*s_%zu", dev_id.c_str(),
                 (int)key_sv.size(), key_sv.data(), field_idx);
        writer.writeField("unique_id", uid_buf);
        writer.writeField("name", entity_name_buf);
        writer.writeField("availability_topic", will_topic_);
        writer.writeField("availability_template", "{{value_json.value}}");

        {
          auto device = writer.objectScope("device");
          writer.writeField("identifiers", device_identifiers_);
        }

        // All fields of a command share the same state topic
        writer.writeField("state_topic", state_topic_buf);

        if (profile && profile->device_class && profile->device_class[0])
          writer.writeField("device_class", profile->device_class);
        if (profile && profile->entity_category && profile->entity_category[0])
          writer.writeField("entity_category", profile->entity_category);

        if (component == "binary_sensor" || component == "switch") {
          char payload_on_buf[mqtt_ha_buffer_limits::max_payload_buf_length],
              payload_off_buf[mqtt_ha_buffer_limits::max_payload_buf_length];
          snprintf(payload_on_buf, sizeof(payload_on_buf), "%d",
                   profile ? profile->payload_on : 1);
          snprintf(payload_off_buf, sizeof(payload_off_buf), "%d",
                   profile ? profile->payload_off : 0);
          writer.writeField("payload_on", payload_on_buf);
          writer.writeField("payload_off", payload_off_buf);
          snprintf(value_template_buf, sizeof(value_template_buf),
                   "{{value_json.%.*s}}", (int)field_name_sv.size(),
                   field_name_sv.data());
          writer.writeField("value_template", value_template_buf);
        }

        if (component == "switch" || component == "number" ||
            component == "select") {
          char cmd_topic_buf[mqtt_ha_buffer_limits::max_cmd_topic_length];
          snprintf(cmd_topic_buf, sizeof(cmd_topic_buf), "%sset/%.*s",
                   root_topic_.c_str(), (int)key_sv.size(), key_sv.data());
          writer.writeField("command_topic", cmd_topic_buf);
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
          snprintf(value_template_buf, sizeof(value_template_buf),
                   "{{value_json.%.*s}}", (int)field_name_sv.size(),
                   field_name_sv.data());
          writer.writeField("value_template", value_template_buf);
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
          snprintf(value_template_buf, sizeof(value_template_buf),
                   "{{value_json.%.*s}}", (int)field_name_sv.size(),
                   field_name_sv.data());
          writer.writeField("value_template", value_template_buf);
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

void MqttHA::createStateTopic(char* out, size_t out_size,
                              std::string_view prefix,
                              std::string_view topic) const {
  char lower_buf[mqtt_ha_buffer_limits::max_lower_buf_length];
  size_t tlen = std::min(topic.size(), sizeof(lower_buf) - 1);
  for (size_t i = 0; i < tlen; i++) {
    lower_buf[i] = std::tolower(static_cast<unsigned char>(topic[i]));
  }
  lower_buf[tlen] = '\0';

  int slen =
      snprintf(out, out_size, "%s%s", root_topic_.c_str(), prefix.data());
  if (slen < 0) return;
  if (!prefix.empty()) {
    if ((size_t)slen < out_size - 1) out[slen++] = '/';
  }
  for (size_t i = 0; i < tlen && (size_t)slen < out_size - 1; i++) {
    out[slen++] = lower_buf[i];
  }
  out[slen] = '\0';
}

MqttHA::KeyValueMapping MqttHA::createOptions(const HAProfile* profile,
                                              std::string_view field_name) {
  if (!profile) return KeyValueMapping{};

  // Use stack buffers instead of std::string to reduce heap fragmentation
  int options_keys[mqtt_ha_buffer_limits::max_options_count];
  const char* options_values[mqtt_ha_buffer_limits::max_options_count];
  size_t options_count = 0;

  for (size_t i = 0; i < profile->key_value_count &&
                     i < mqtt_ha_buffer_limits::max_options_count;
       i++) {
    options_keys[i] = profile->key_value_pairs[i].first;
    options_values[i] = profile->key_value_pairs[i].second;
    options_count++;
  }

  int default_option_value = 0;
  if (options_count > 0) {
    default_option_value = options_keys[0];
    for (size_t i = 0; i < options_count; i++) {
      if (options_keys[i] == profile->default_key) {
        default_option_value = options_keys[i];
        break;
      }
    }
  }

  // Build value_template and command_template using char buffers
  // Original: std::string valueMap = "{% set values = {" ... "} %}..."
  // Need to escape % for snprintf: use %% instead of %

  char value_map_buf[mqtt_ha_buffer_limits::max_option_value_map_length];
  char cmd_map_buf[mqtt_ha_buffer_limits::max_option_value_map_length];

  int vm_len = 0;
  int pn_len = 0;

  const char value_prefix[] = "{% set values = {";
  const char cmd_prefix[] = "{% set values = {";
  memcpy(value_map_buf + vm_len, value_prefix, sizeof(value_prefix) - 1);
  vm_len += sizeof(value_prefix) - 1;
  memcpy(cmd_map_buf + pn_len, cmd_prefix, sizeof(cmd_prefix) - 1);
  pn_len += sizeof(cmd_prefix) - 1;

  for (size_t i = 0; i < options_count; i++) {
    if (vm_len < (int)sizeof(value_map_buf)) {
      vm_len += snprintf(value_map_buf + vm_len, sizeof(value_map_buf) - vm_len,
                         "%d:'%s'", options_keys[i], options_values[i]);
      if (i < options_count - 1 && vm_len < (int)sizeof(value_map_buf)) {
        vm_len += snprintf(value_map_buf + vm_len,
                           sizeof(value_map_buf) - vm_len, ",");
      }
    }
    if (pn_len < (int)sizeof(cmd_map_buf)) {
      pn_len += snprintf(cmd_map_buf + pn_len, sizeof(cmd_map_buf) - pn_len,
                         "'%s':%d", options_values[i], options_keys[i]);
      if (i < options_count - 1 && pn_len < (int)sizeof(cmd_map_buf)) {
        pn_len +=
            snprintf(cmd_map_buf + pn_len, sizeof(cmd_map_buf) - pn_len, ",");
      }
    }
  }

  snprintf(value_map_buf + vm_len, sizeof(value_map_buf) - vm_len,
           "} %%}{{ values[value_json.%.*s] if value_json.%.*s in "
           "values.keys() else '%s' }}",
           (int)field_name.size(), field_name.data(), (int)field_name.size(),
           field_name.data(), options_count > 0 ? options_values[0] : "");

  snprintf(cmd_map_buf + pn_len, sizeof(cmd_map_buf) - pn_len,
           "} %%}{{ values[value] if value in values.keys() else "
           "%d }}",
           default_option_value);

  // Build options list using StaticVector<FixedString<16>, max_options_count>
  ebus::StaticVector<ebus::FixedString<16>, mqtt_ha_limits::max_options_count>
      options;
  for (size_t i = 0; i < options_count; i++) {
    options.push_back(
        ebus::FixedString<mqtt_ha_limits::max_option_string_length>(
            options_values[i]));
  }

  KeyValueMapping mapping;
  mapping.options = options;
  mapping.value_map.assign(std::string_view(value_map_buf));
  mapping.cmd_map.assign(std::string_view(cmd_map_buf));
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
