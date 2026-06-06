#if defined(EBUS_INTERNAL)
#include "Command.hpp"

#include <cerrno>
#include <cmath>
#include <charconv>
#include <cstdlib>
#include <ebus/detail/json_writer.hpp>
#include <ebus/utils.hpp>
#include <limits>
#include <regex>

const uint32_t& Command::getPollId() const { return poll_id; }

void Command::setPollId(const uint32_t id) { poll_id = id; }

const uint32_t& Command::getLast() const { return last; }

void Command::setLast(const uint32_t time) { last = time; }

const ebus::Sequence& Command::getData() const { return data; }

void Command::setData(ebus::ByteView data) { this->data.assign(data); }

size_t Command::getLength() const { return length; }

bool Command::getNumeric() const { return numeric; }

const std::string& Command::getKey() const { return key; }

const std::string& Command::getName() const { return name; }

const ebus::Sequence& Command::getReadCmd() const { return read_cmd; }

const ebus::Sequence& Command::getWriteCmd() const { return write_cmd; }

const bool& Command::getActive() const { return active; }

const uint32_t& Command::getInterval() const { return interval; }

const bool& Command::getMaster() const { return master; }

const size_t& Command::getPosition() const { return position; }

const ebus::DataType& Command::getDatatype() const { return datatype; }

const float& Command::getDivider() const { return divider; }

const float& Command::getMin() const { return min; }

const float& Command::getMax() const { return max; }

const uint8_t& Command::getDigits() const { return digits; }

const std::string& Command::getUnit() const { return unit; }

const bool& Command::getHA() const { return ha; }

const std::string& Command::getHAComponent() const { return ha_component; }

const std::string& Command::getHADeviceClass() const { return ha_device_class; }

const std::string& Command::getHAEntityCategory() const {
  return ha_entity_category;
}

const std::string& Command::getHAMode() const { return ha_mode; }

const std::map<int, std::string>& Command::getHAKeyValueMap() const {
  return ha_key_value_map;
}

const int& Command::getHADefaultKey() const { return ha_default_key; }

const uint8_t& Command::getHAPayloadOn() const { return ha_payload_on; }

const uint8_t& Command::getHAPayloadOff() const { return ha_payload_off; }

const std::string& Command::getHAStateClass() const { return ha_state_class; }

const float& Command::getHAStep() const { return ha_step; }

bool Command::matches(ebus::ByteView master_view) const {
  return ebus::contains(master_view, read_cmd);
}

void Command::getValueJson(ebus::detail::JsonWriter& writer) const {
  writer.startObject();
  writer.appendKey("value");

  auto decoded = ebus::decode(datatype, data);
  if (!decoded || ebus::isNull(*decoded)) {
    writer.writeRaw("null");
  } else {
    if (numeric) {
      // Optimization: Calculate directly from the decoded value to avoid double decoding
      float val = ebus::roundDigits(ebus::asFloat(*decoded) / divider, digits);
      writer.writeValueFloat(val);
    } else {
      const auto meta = getMetaCached();
      if (meta && std::string_view(meta->name).find("HEX") == 0) {
        // Use hex stringifier; this still creates a temporary string but avoids redundant metadata lookup
        writer.writeValue(ebus::toHexString(*decoded, 0));
      } else {
        // asString returns a const reference to the string inside the variant; zero-copy to JsonWriter
        writer.writeValue(ebus::asString(*decoded));
      }
    }
  }
  writer.endObject();
}

ebus::Sequence Command::getVectorFromJson(std::string_view json) const {
  return getVectorFromValue(ebus::extract(json, "value"));
}

ebus::Sequence Command::getVectorFromValue(std::string_view val_view) const {
  ebus::Sequence result;
  if (val_view.empty()) return result;

  if (numeric) {
    // val_view is the raw JSON representation of a number. std::atof is okay for ESP-IDF
    double val = std::atof(std::string(val_view).c_str());
    if ((val >= min) && (val <= max)) {
      result = getVectorFromDouble(val);
    }
  } else {
    // For strings, strip potential quotes
    if (val_view.size() >= 2 && val_view.front() == '"' &&
        val_view.back() == '"') {
      val_view.remove_prefix(1);
      val_view.remove_suffix(1);
    }
    result = getVectorFromString(std::string(val_view));
  }

  return result;
}

double Command::getDoubleFromVector() const {
  if (data.empty()) return 0.0;
  auto decoded = ebus::decode(datatype, data);
  if (!decoded) return 0.0;

  return ebus::roundDigits(ebus::asFloat(*decoded) / divider, digits);
}

const std::string Command::getStringFromVector() const {
  if (data.empty()) return "";
  auto decoded = ebus::decode(datatype, data);
  if (!decoded) return "";

  const auto meta = getMetaCached();
  if (meta && std::string(meta->name).find("HEX") == 0) {
    return ebus::toHexString(*decoded, 0);
  }
  return ebus::asString(*decoded);
}

void Command::toJson(ebus::detail::JsonWriter& writer) const {
  writer.startObject();
  // Command Fields
  writer.writeField("key", key);
  writer.writeField("name", name);
  writer.writeHexField("read_cmd", read_cmd);
  writer.writeHexField("write_cmd", write_cmd);
  writer.writeField("active", active);
  writer.writeField("interval", interval);

  // Data Fields
  writer.writeField("master", master);
  writer.writeField("position", position);
  writer.writeField("datatype", ebus::dataTypeToString(datatype));
  writer.writeFieldFloat("divider", divider);
  writer.writeFieldFloat("min", min);
  writer.writeFieldFloat("max", max);
  writer.writeField("digits", digits);
  writer.writeField("unit", unit);

  // Home Assistant
  writer.writeField("ha", ha);
  writer.writeField("ha_component", ha_component);
  writer.writeField("ha_device_class", ha_device_class);
  writer.writeField("ha_entity_category", ha_entity_category);
  writer.writeField("ha_mode", ha_mode);

  writer.appendKey("ha_key_value_map");
  writer.startObject();
  for (const auto& kv : ha_key_value_map) {
    // Optimization: Avoid std::to_string heap allocation for every map entry
    char keyBuf[12];
    auto [ptr, ec] = std::to_chars(keyBuf, keyBuf + sizeof(keyBuf), kv.first);
    if (ec == std::errc{}) {
      writer.writeField(std::string_view(keyBuf, ptr - keyBuf), kv.second);
    }
  }
  writer.endObject();

  writer.writeField("ha_default_key", ha_default_key);
  writer.writeField("ha_payload_on", ha_payload_on);
  writer.writeField("ha_payload_off", ha_payload_off);
  writer.writeField("ha_state_class", ha_state_class);
  writer.writeFieldFloat("ha_step", ha_step);
  writer.endObject();
}

// This method is still needed by Store::serializeCommands, so we keep it.
// It can now internally use the JsonWriter version to build the string.
const std::string Command::toJson() const {
  std::string json_str;
  json_str.reserve(512);  // Avoid reallocations for a typical command payload
  ebus::detail::JsonWriter writer(
      [&json_str](std::string_view s) { json_str.append(s); });
  toJson(writer);  // Call the JsonWriter version
  return json_str;
}

void Command::writePersistenceRow(ebus::detail::JsonWriter& writer) const {
  writer.startArray();
  writer.writeValue(key);                               // 0
  writer.writeValue(name);                              // 1
  writer.writeValue(read_cmd.toString());               // 2
  writer.writeValue(write_cmd.toString());              // 3
  writer.writeValue(active);                            // 4
  writer.writeValue(interval);                          // 5
  writer.writeValue(master);                            // 6
  writer.writeValue(position);                          // 7
  writer.writeValue(ebus::dataTypeToString(datatype));  // 8
  writer.writeValueFloat(divider);                      // 9
  writer.writeValueFloat(min);                          // 10
  writer.writeValueFloat(max);                          // 11
  writer.writeValue(digits);                           // 12
  writer.writeValue(unit);                              // 13
  writer.writeValue(ha);                                // 14
  writer.writeValue(ha_component);                      // 15
  writer.writeValue(ha_device_class);                   // 16
  writer.writeValue(ha_entity_category);                // 17
  writer.writeValue(ha_mode);                           // 18
  
  // ha_key_value_map (index 19)
  writer.startObject();
  for (const auto& kv : ha_key_value_map) {
    char keyBuf[12];
    auto [ptr, ec] = std::to_chars(keyBuf, keyBuf + sizeof(keyBuf), kv.first);
    if (ec == std::errc{}) writer.writeField(std::string_view(keyBuf, ptr - keyBuf), kv.second);
  }
  writer.endObject();

  writer.writeValue(ha_default_key);                    // 20
  writer.writeValue(ha_payload_on);                     // 21
  writer.writeValue(ha_payload_off);                    // 22
  writer.writeValue(ha_state_class);                    // 23
  writer.writeValueFloat(ha_step);                      // 24
  writer.endArray();
}

Command Command::fromJson(const cJSON* doc) {
  Command command;
  if (doc == nullptr) return command;

  auto getString = [doc](const char* key, const std::string& def = "") {
    cJSON* node = cJSON_GetObjectItemCaseSensitive(doc, key);
    if (cJSON_IsString(node) && node->valuestring != nullptr)
      return std::string(node->valuestring);
    return def;
  };

  auto getBool = [doc](const char* key, bool def = false) {
    cJSON* node = cJSON_GetObjectItemCaseSensitive(doc, key);
    if (cJSON_IsBool(node)) return cJSON_IsTrue(node) != 0;
    return def;
  };

  // Command Fields
  command.key = getString("key");
  command.name = getString("name");
  command.read_cmd.assign(ebus::toVector(getString("read_cmd")));

  std::string writeCmd = getString("write_cmd");
  if (!writeCmd.empty()) command.write_cmd.assign(ebus::toVector(writeCmd));

  command.active = getBool("active", false);
  cJSON* intervalNode = cJSON_GetObjectItemCaseSensitive(doc, "interval");
  if (cJSON_IsNumber(intervalNode) && intervalNode->valuedouble >= 0)
    command.interval = static_cast<uint32_t>(intervalNode->valuedouble);
  command.last = 0;
  command.poll_id = 0;
  command.data.clear();

  // Data Fields
  command.master = getBool("master", false);
  cJSON* positionNode = cJSON_GetObjectItemCaseSensitive(doc, "position");
  if (cJSON_IsNumber(positionNode) && positionNode->valuedouble >= 0)
    command.position = static_cast<size_t>(positionNode->valuedouble);

  command.datatype = ebus::stringToDataType(getString("datatype").c_str());
  command.length = ebus::sizeOfDataType(command.datatype);
  command.numeric = ebus::isNumeric(command.datatype);

  cJSON* dividerNode = cJSON_GetObjectItemCaseSensitive(doc, "divider");
  if (cJSON_IsNumber(dividerNode) && dividerNode->valuedouble > 0)
    command.divider = static_cast<float>(dividerNode->valuedouble);

  cJSON* minNode = cJSON_GetObjectItemCaseSensitive(doc, "min");
  if (cJSON_IsNumber(minNode))
    command.min = static_cast<float>(minNode->valuedouble);

  cJSON* maxNode = cJSON_GetObjectItemCaseSensitive(doc, "max");
  if (cJSON_IsNumber(maxNode))
    command.max = static_cast<float>(maxNode->valuedouble);

  cJSON* digitsNode = cJSON_GetObjectItemCaseSensitive(doc, "digits");
  if (cJSON_IsNumber(digitsNode) && digitsNode->valuedouble >= 0)
    command.digits = static_cast<uint8_t>(digitsNode->valuedouble);

  command.unit = getString("unit");

  // Home Assistant
  command.ha = getBool("ha", false);

  if (command.ha) {
    command.ha_component = getString("ha_component", command.ha_component);
    command.ha_device_class =
        getString("ha_device_class", command.ha_device_class);
    command.ha_entity_category =
        getString("ha_entity_category", command.ha_entity_category);
    command.ha_mode = getString("ha_mode", command.ha_mode);

    cJSON* haMap = cJSON_GetObjectItemCaseSensitive(doc, "ha_key_value_map");
    if (cJSON_IsObject(haMap)) {
      for (cJSON* item = haMap->child; item != nullptr; item = item->next) {
        if (item->string != nullptr && cJSON_IsString(item) &&
            item->valuestring != nullptr) {
          command.ha_key_value_map[std::stoi(item->string)] = item->valuestring;
        }
      }
    }

    cJSON* defaultKeyNode =
        cJSON_GetObjectItemCaseSensitive(doc, "ha_default_key");
    if (cJSON_IsNumber(defaultKeyNode))
      command.ha_default_key = static_cast<int>(defaultKeyNode->valuedouble);

    cJSON* payloadOnNode =
        cJSON_GetObjectItemCaseSensitive(doc, "ha_payload_on");
    if (cJSON_IsNumber(payloadOnNode) && payloadOnNode->valuedouble >= 0)
      command.ha_payload_on = static_cast<uint8_t>(payloadOnNode->valuedouble);

    cJSON* payloadOffNode =
        cJSON_GetObjectItemCaseSensitive(doc, "ha_payload_off");
    if (cJSON_IsNumber(payloadOffNode) && payloadOffNode->valuedouble >= 0)
      command.ha_payload_off =
          static_cast<uint8_t>(payloadOffNode->valuedouble);

    command.ha_state_class =
        getString("ha_state_class", command.ha_state_class);

    cJSON* stepNode = cJSON_GetObjectItemCaseSensitive(doc, "ha_step");
    if (cJSON_IsNumber(stepNode) && stepNode->valuedouble > 0)
      command.ha_step = static_cast<float>(stepNode->valuedouble);
  }

  return command;
}

const std::string Command::evaluate(const cJSON* doc) {
  // Define the fields to evaluate
  const FieldEvaluation fields[] = {{"key", true, FT_String},
                                    {"name", true, FT_String},
                                    {"read_cmd", true, FT_HexString},
                                    {"write_cmd", false, FT_HexString},
                                    {"active", true, FT_Bool},
                                    {"interval", false, FT_Uint32T},
                                    // Data Fields
                                    {"master", true, FT_Bool},
                                    {"position", true, FT_SizeT},
                                    {"datatype", true, FT_DataType},
                                    {"divider", false, FT_Float},
                                    {"min", false, FT_Float},
                                    {"max", false, FT_Float},
                                    {"digits", false, FT_Uint8T},
                                    {"unit", false, FT_String},
                                    // Home Assistant
                                    {"ha", false, FT_Bool},
                                    {"ha_component", false, FT_String},
                                    {"ha_device_class", false, FT_String},
                                    {"ha_entity_category", false, FT_String},
                                    {"ha_mode", false, FT_String},
                                    {"ha_key_value_map", false, FT_KeyValueMap},
                                    {"ha_default_key", false, FT_Int},
                                    {"ha_payload_on", false, FT_Uint8T},
                                    {"ha_payload_off", false, FT_Uint8T},
                                    {"ha_state_class", false, FT_String},
                                    {"ha_step", false, FT_Float}};

  // Evaluate each field in a loop
  for (const auto& field : fields) {
    std::string error =
        isFieldValid(doc, field.name, field.required, field.type);
    if (!error.empty()) return error;
  }

  return "";
}

const std::string Command::isFieldValid(const cJSON* doc,
                                        const std::string& field, bool required,
                                        FieldType type) {
  if (doc == nullptr || !cJSON_IsObject(doc)) {
    return "Invalid json root";
  }

  cJSON* v = cJSON_GetObjectItemCaseSensitive(doc, field.c_str());
  // Check if the required field exists
  if (required && v == nullptr) return "Missing required field: " + field;
  // Skip type checking if the field is not present and not required
  if (v == nullptr || cJSON_IsNull(v)) return "";

  auto isInteger = [](double value) { return std::floor(value) == value; };

  switch (type) {
    case FT_String: {
      if (!cJSON_IsString(v) || v->valuestring == nullptr)
        return "Invalid type for field: " + field;
    } break;
    case FT_HexString: {
      if (!cJSON_IsString(v) || v->valuestring == nullptr)
        return "Invalid type for field: " + field;
      const std::string hexStr = v->valuestring;
      if (!hexStr.empty()) {
        if (hexStr.length() % 2 != 0)
          return "Invalid hex string length: " + field;
        for (char c : hexStr) {
          if (!isxdigit((unsigned char)c)) {
            return "Invalid hex string character for field: " + field;
          }
        }
      }
    } break;
    case FT_Bool: {
      if (!cJSON_IsBool(v)) return "Invalid type for field: " + field;
    } break;
    case FT_Int: {
      if (!cJSON_IsNumber(v) || !isInteger(v->valuedouble))
        return "Invalid type for field: " + field;
      if (v->valuedouble < std::numeric_limits<int>::min() ||
          v->valuedouble > std::numeric_limits<int>::max())
        return "Out of range for field: " + field;
    } break;
    case FT_Float: {
      if (!cJSON_IsNumber(v)) return "Invalid type for field: " + field;
    } break;
    case FT_Uint8T: {
      if (!cJSON_IsNumber(v) || !isInteger(v->valuedouble))
        return "Invalid type for field: " + field;
      if (v->valuedouble < 0 || v->valuedouble > 0xFF)
        return "Out of range for field: " + field;
    } break;
    case FT_Uint32T: {
      if (!cJSON_IsNumber(v) || !isInteger(v->valuedouble))
        return "Invalid type for field: " + field;
      if (v->valuedouble < 0 || v->valuedouble > UINT32_MAX)
        return "Out of range for field: " + field;
    } break;
    case FT_SizeT: {
      if (!cJSON_IsNumber(v) || !isInteger(v->valuedouble))
        return "Invalid type for field: " + field;
      if (v->valuedouble < 0 ||
          v->valuedouble >
              static_cast<double>(std::numeric_limits<size_t>::max()))
        return "Out of range for field: " + field;
    } break;
    case FT_DataType: {
      if (!cJSON_IsString(v) || v->valuestring == nullptr ||
          ebus::stringToDataType(v->valuestring) == ebus::DataType::error)
        return "Invalid datatype for field: " + field;
    } break;
    case FT_KeyValueMap: {
      if (!cJSON_IsObject(v)) return "Invalid type for field: " + field;
      return isKeyValueMapValid(v);
    }
  }

  return "";
}

const std::string Command::isKeyValueMapValid(const cJSON* ha_key_value_map) {
  if (!cJSON_IsObject(ha_key_value_map)) return "Invalid value type in map";

  for (cJSON* kv = ha_key_value_map->child; kv != nullptr; kv = kv->next) {
    if (kv->string == nullptr) return "Invalid key in map";

    // Check if the key can be converted to an integer
    char* endptr = nullptr;
    errno = 0;
    long long keyValue = std::strtoll(kv->string, &endptr, 10);
    if (endptr == kv->string || *endptr != '\0' || errno == ERANGE) {
      return "Invalid key: " + std::string(kv->string);
    }
    if (keyValue < std::numeric_limits<int>::min() ||
        keyValue > std::numeric_limits<int>::max()) {
      return "Key out of range: " + std::string(kv->string);
    }

    if (!cJSON_IsString(kv) || kv->valuestring == nullptr)
      return "Invalid value type in map";
  }
  // Passed key-value map evaluation checks
  return "";
}

const ebus::DataTypeInfo* Command::getMetaCached() const {
  if (!_cachedMeta.has_value()) {
    _cachedMeta = ebus::getMeta(datatype);
  }
  return _cachedMeta.has_value() ? &_cachedMeta.value() : nullptr;
}

ebus::Sequence Command::getVectorFromDouble(double value) const {
  double scaledValue = ebus::roundDigits(value * divider, digits);

  ebus::DataValue dv;
  dv = static_cast<float>(scaledValue);

  return ebus::encode(datatype, dv);
}

ebus::Sequence Command::getVectorFromString(const std::string& value) const {
  const auto meta = getMetaCached();
  if (!meta) return ebus::Sequence{};

  ebus::DataValue dv;
  if (std::string(meta->name).find("HEX") == 0) {
    dv = ebus::byteToChar(ebus::toVector(value));
  } else {
    dv = value.substr(0, length);
  }

  return ebus::encode(datatype, dv);
}

#endif
