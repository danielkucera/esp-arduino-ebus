#if defined(EBUS_INTERNAL)
#include "Command.hpp"

#include <Ebus.h>

#include <cmath>
#include <limits>
#include <regex>
#include <stdexcept>

const uint32_t& Command::getLast() const { return last; }

void Command::setLast(const uint32_t time) { last = time; }

const std::vector<uint8_t>& Command::getData() const { return data; }

void Command::setData(const std::vector<uint8_t>& data) { this->data = data; }

const size_t& Command::getLength() const { return length; }

const bool& Command::getNumeric() const { return numeric; }

const std::string& Command::getKey() const { return key; }

const std::string& Command::getName() const { return name; }

const std::vector<uint8_t>& Command::getReadCmd() const { return read_cmd; }

const std::vector<uint8_t>& Command::getWriteCmd() const { return write_cmd; }

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

const std::string Command::getValueJson() const {
  cJSON* doc = cJSON_CreateObject();
  if (numeric)
    cJSON_AddNumberToObject(doc, "value", getDoubleFromVector());
  else
    cJSON_AddStringToObject(doc, "value", getStringFromVector().c_str());

  char* printed = cJSON_PrintUnformatted(doc);
  std::string payload = printed != nullptr ? printed : "{}";
  if (printed != nullptr) cJSON_free(printed);
  cJSON_Delete(doc);
  return payload;
}

const std::vector<uint8_t> Command::getVectorFromJson(const cJSON* doc) const {
  std::vector<uint8_t> result;

  if (doc == nullptr) return result;
  cJSON* valueNode = cJSON_GetObjectItemCaseSensitive(doc, "value");
  if (valueNode == nullptr) return result;

  if (numeric && cJSON_IsNumber(valueNode)) {
    double value = valueNode->valuedouble;
    if ((value >= min) && (value <= max)) result = getVectorFromDouble(value);
  } else if (!numeric && cJSON_IsString(valueNode) &&
             valueNode->valuestring != nullptr) {
    result = getVectorFromString(valueNode->valuestring);
  }

  return result;
}

const std::string Command::toJson() const {
  cJSON* doc = cJSON_CreateObject();

  // Command Fields
  cJSON_AddStringToObject(doc, "key", key.c_str());
  cJSON_AddStringToObject(doc, "name", name.c_str());
  cJSON_AddStringToObject(doc, "read_cmd", ebus::to_string(read_cmd).c_str());
  cJSON_AddStringToObject(doc, "write_cmd",
                          ebus::to_string(write_cmd).c_str());
  cJSON_AddBoolToObject(doc, "active", active);
  cJSON_AddNumberToObject(doc, "interval", interval);

  // Data Fields
  cJSON_AddBoolToObject(doc, "master", master);
  cJSON_AddNumberToObject(doc, "position", static_cast<double>(position));
  cJSON_AddStringToObject(doc, "datatype", ebus::datatype_2_string(datatype));
  cJSON_AddNumberToObject(doc, "divider", divider);
  cJSON_AddNumberToObject(doc, "min", min);
  cJSON_AddNumberToObject(doc, "max", max);
  cJSON_AddNumberToObject(doc, "digits", digits);
  cJSON_AddStringToObject(doc, "unit", unit.c_str());

  // Home Assistant
  cJSON_AddBoolToObject(doc, "ha", ha);
  cJSON_AddStringToObject(doc, "ha_component", ha_component.c_str());
  cJSON_AddStringToObject(doc, "ha_device_class", ha_device_class.c_str());
  cJSON_AddStringToObject(doc, "ha_entity_category", ha_entity_category.c_str());
  cJSON_AddStringToObject(doc, "ha_mode", ha_mode.c_str());

  cJSON* haMap = cJSON_AddObjectToObject(doc, "ha_key_value_map");
  for (const auto& kv : ha_key_value_map)
    cJSON_AddStringToObject(haMap, std::to_string(kv.first).c_str(),
                            kv.second.c_str());

  cJSON_AddNumberToObject(doc, "ha_default_key", ha_default_key);
  cJSON_AddNumberToObject(doc, "ha_payload_on", ha_payload_on);
  cJSON_AddNumberToObject(doc, "ha_payload_off", ha_payload_off);
  cJSON_AddStringToObject(doc, "ha_state_class", ha_state_class.c_str());
  cJSON_AddNumberToObject(doc, "ha_step", ha_step);

  char* printed = cJSON_PrintUnformatted(doc);
  std::string payload = printed != nullptr ? printed : "{}";
  if (printed != nullptr) cJSON_free(printed);
  cJSON_Delete(doc);
  return payload;
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

  auto getDouble = [doc](const char* key, double def = 0.0) {
    cJSON* node = cJSON_GetObjectItemCaseSensitive(doc, key);
    if (cJSON_IsNumber(node)) return node->valuedouble;
    return def;
  };

  // Command Fields
  command.key = getString("key");
  command.name = getString("name");
  command.read_cmd = ebus::to_vector(getString("read_cmd"));

  std::string writeCmd = getString("write_cmd");
  if (!writeCmd.empty()) command.write_cmd = ebus::to_vector(writeCmd);

  command.active = getBool("active", false);
  cJSON* intervalNode = cJSON_GetObjectItemCaseSensitive(doc, "interval");
  if (cJSON_IsNumber(intervalNode) && intervalNode->valuedouble >= 0)
    command.interval = static_cast<uint32_t>(intervalNode->valuedouble);
  command.last = 0;
  command.data = std::vector<uint8_t>();

  // Data Fields
  command.master = getBool("master", false);
  cJSON* positionNode = cJSON_GetObjectItemCaseSensitive(doc, "position");
  if (cJSON_IsNumber(positionNode) && positionNode->valuedouble >= 0)
    command.position = static_cast<size_t>(positionNode->valuedouble);

  command.datatype = ebus::string_2_datatype(getString("datatype").c_str());
  command.length = ebus::sizeof_datatype(command.datatype);
  command.numeric = ebus::typeof_datatype(command.datatype);

  cJSON* dividerNode = cJSON_GetObjectItemCaseSensitive(doc, "divider");
  if (cJSON_IsNumber(dividerNode) && dividerNode->valuedouble > 0)
    command.divider = static_cast<float>(dividerNode->valuedouble);

  cJSON* minNode = cJSON_GetObjectItemCaseSensitive(doc, "min");
  if (cJSON_IsNumber(minNode)) command.min = static_cast<float>(minNode->valuedouble);

  cJSON* maxNode = cJSON_GetObjectItemCaseSensitive(doc, "max");
  if (cJSON_IsNumber(maxNode)) command.max = static_cast<float>(maxNode->valuedouble);

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

    cJSON* defaultKeyNode = cJSON_GetObjectItemCaseSensitive(doc, "ha_default_key");
    if (cJSON_IsNumber(defaultKeyNode))
      command.ha_default_key = static_cast<int>(defaultKeyNode->valuedouble);

    cJSON* payloadOnNode = cJSON_GetObjectItemCaseSensitive(doc, "ha_payload_on");
    if (cJSON_IsNumber(payloadOnNode) && payloadOnNode->valuedouble >= 0)
      command.ha_payload_on = static_cast<uint8_t>(payloadOnNode->valuedouble);

    cJSON* payloadOffNode = cJSON_GetObjectItemCaseSensitive(doc, "ha_payload_off");
    if (cJSON_IsNumber(payloadOffNode) && payloadOffNode->valuedouble >= 0)
      command.ha_payload_off = static_cast<uint8_t>(payloadOffNode->valuedouble);

    command.ha_state_class = getString("ha_state_class", command.ha_state_class);

    cJSON* stepNode = cJSON_GetObjectItemCaseSensitive(doc, "ha_step");
    if (cJSON_IsNumber(stepNode) && stepNode->valuedouble > 0)
      command.ha_step = static_cast<float>(stepNode->valuedouble);
  }

  return command;
}

const std::string Command::evaluate(const cJSON* doc) {
  // Define the fields to evaluate
  const FieldEvaluation fields[] = {
      {"key", true, FT_String},
      {"name", true, FT_String},
      {"read_cmd", true, FT_HexString},
      {"write_cmd", false, FT_HexString},
      {"active", true, FT_Bool},
      {"interval", false, FT_Uint32T},
      {"master", true, FT_Bool},
      {"position", true, FT_SizeT},
      {"datatype", true, FT_DataType},
      {"divider", false, FT_Float},
      {"min", false, FT_Float},
      {"max", false, FT_Float},
      {"digits", false, FT_Uint8T},
      {"unit", false, FT_String},
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

  auto isInteger = [](double value) {
    return std::floor(value) == value;
  };

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
        std::regex hexRegex(R"(^([0-9A-Fa-f]{2})+$)");
        if (!std::regex_match(hexStr, hexRegex))
          return "Invalid hex string for field: " + field;
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
          v->valuedouble > static_cast<double>(std::numeric_limits<size_t>::max()))
        return "Out of range for field: " + field;
    } break;
    case FT_DataType: {
      if (!cJSON_IsString(v) || v->valuestring == nullptr ||
          ebus::string_2_datatype(v->valuestring) == ebus::DataType::ERROR)
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
    try {
      std::stoi(kv->string);
    } catch (const std::invalid_argument&) {
      return "Invalid key: " + std::string(kv->string);
    } catch (const std::out_of_range&) {
      return "Key out of range: " + std::string(kv->string);
    }

    if (!cJSON_IsString(kv) || kv->valuestring == nullptr)
      return "Invalid value type in map";
  }
  // Passed key-value map evaluation checks
  return "";
}

const double Command::getDoubleFromVector() const {
  double value = 0;

  switch (datatype) {
    case ebus::DataType::BCD:
      value = ebus::byte_2_bcd(data);
      break;
    case ebus::DataType::UINT8:
      value = ebus::byte_2_uint8(data);
      break;
    case ebus::DataType::INT8:
      value = ebus::byte_2_int8(data);
      break;
    case ebus::DataType::UINT16:
      value = ebus::byte_2_uint16(data);
      break;
    case ebus::DataType::INT16:
      value = ebus::byte_2_int16(data);
      break;
    case ebus::DataType::UINT32:
      value = ebus::byte_2_uint32(data);
      break;
    case ebus::DataType::INT32:
      value = ebus::byte_2_int32(data);
      break;
    case ebus::DataType::DATA1B:
      value = ebus::byte_2_data1b(data);
      break;
    case ebus::DataType::DATA1C:
      value = ebus::byte_2_data1c(data);
      break;
    case ebus::DataType::DATA2B:
      value = ebus::byte_2_data2b(data);
      break;
    case ebus::DataType::DATA2C:
      value = ebus::byte_2_data2c(data);
      break;
    case ebus::DataType::FLOAT:
      value = ebus::byte_2_float(data);
      break;
    default:
      break;
  }

  value = value / divider;
  value = ebus::round_digits(value, digits);

  return value;
}

const std::string Command::getStringFromVector() const {
  std::string value;

  switch (datatype) {
    case ebus::DataType::CHAR1:
    case ebus::DataType::CHAR2:
    case ebus::DataType::CHAR3:
    case ebus::DataType::CHAR4:
    case ebus::DataType::CHAR5:
    case ebus::DataType::CHAR6:
    case ebus::DataType::CHAR7:
    case ebus::DataType::CHAR8:
      value = ebus::byte_2_char(data);
      break;
    case ebus::DataType::HEX1:
    case ebus::DataType::HEX2:
    case ebus::DataType::HEX3:
    case ebus::DataType::HEX4:
    case ebus::DataType::HEX5:
    case ebus::DataType::HEX6:
    case ebus::DataType::HEX7:
    case ebus::DataType::HEX8:
      value = ebus::byte_2_hex(data);
      break;
    default:
      break;
  }

  return value;
}

const std::vector<uint8_t> Command::getVectorFromDouble(double value) const {
  std::vector<uint8_t> result;

  value = value * divider;
  value = ebus::round_digits(value, digits);

  switch (datatype) {
    case ebus::DataType::BCD:
      result = ebus::bcd_2_byte(value);
      break;
    case ebus::DataType::UINT8:
      result = ebus::uint8_2_byte(value);
      break;
    case ebus::DataType::INT8:
      result = ebus::int8_2_byte(value);
      break;
    case ebus::DataType::UINT16:
      result = ebus::uint16_2_byte(value);
      break;
    case ebus::DataType::INT16:
      result = ebus::int16_2_byte(value);
      break;
    case ebus::DataType::UINT32:
      result = ebus::uint32_2_byte(value);
      break;
    case ebus::DataType::INT32:
      result = ebus::int32_2_byte(value);
      break;
    case ebus::DataType::DATA1B:
      result = ebus::data1b_2_byte(value);
      break;
    case ebus::DataType::DATA1C:
      result = ebus::data1c_2_byte(value);
      break;
    case ebus::DataType::DATA2B:
      result = ebus::data2b_2_byte(value);
      break;
    case ebus::DataType::DATA2C:
      result = ebus::data2c_2_byte(value);
      break;
    case ebus::DataType::FLOAT:
      result = ebus::float_2_byte(value);
      break;
    default:
      break;
  }

  return result;
}

const std::vector<uint8_t> Command::getVectorFromString(
    const std::string& value) const {
  std::vector<uint8_t> result;

  switch (datatype) {
    case ebus::DataType::CHAR1:
    case ebus::DataType::CHAR2:
    case ebus::DataType::CHAR3:
    case ebus::DataType::CHAR4:
    case ebus::DataType::CHAR5:
    case ebus::DataType::CHAR6:
    case ebus::DataType::CHAR7:
    case ebus::DataType::CHAR8:
      result = ebus::char_2_byte(value.substr(0, length));
      break;
    case ebus::DataType::HEX1:
    case ebus::DataType::HEX2:
    case ebus::DataType::HEX3:
    case ebus::DataType::HEX4:
    case ebus::DataType::HEX5:
    case ebus::DataType::HEX6:
    case ebus::DataType::HEX7:
    case ebus::DataType::HEX8:
      result = ebus::hex_2_byte(value.substr(0, length));
      break;
    default:
      break;
  }
  return result;
}

#endif
