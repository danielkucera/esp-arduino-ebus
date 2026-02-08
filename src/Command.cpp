#if defined(EBUS_INTERNAL)
#include "Command.hpp"

#include <ArduinoJson.h>
#include <Ebus.h>

#include <regex>

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

const JsonDocument Command::getValueJsonDoc() const {
  JsonDocument doc;

  if (numeric)
    doc["value"] = getDoubleFromVector();
  else
    doc["value"] = getStringFromVector();

  doc.shrinkToFit();
  return doc;
}

const std::vector<uint8_t> Command::getVectorFromJson(const JsonDocument& doc) {
  std::vector<uint8_t> result;

  if (numeric && doc["value"].is<double>()) {
    double value = doc["value"].as<double>();
    if ((value >= min) && (value <= max)) result = getVectorFromDouble(value);
  } else if (!numeric && doc["value"].is<const char*>()) {
    std::string value = doc["value"].as<const char*>();
    result = getVectorFromString(value);
  }

  return result;
}

JsonDocument Command::toJson() const {
  JsonDocument doc;

  // Command Fields
  doc["key"] = key;
  doc["name"] = name;
  doc["read_cmd"] = ebus::to_string(read_cmd);
  doc["write_cmd"] = ebus::to_string(write_cmd);
  doc["active"] = active;
  doc["interval"] = interval;

  // Data Fields
  doc["master"] = master;
  doc["position"] = position;
  doc["datatype"] = ebus::datatype_2_string(datatype);
  doc["divider"] = divider;
  doc["min"] = min;
  doc["max"] = max;
  doc["digits"] = digits;
  doc["unit"] = unit;

  // Home Assistant
  doc["ha"] = ha;
  doc["ha_component"] = ha_component;
  doc["ha_device_class"] = ha_device_class;
  doc["ha_entity_category"] = ha_entity_category;
  doc["ha_mode"] = ha_mode;

  JsonObject ha_key_value_map_object = doc["ha_key_value_map"].to<JsonObject>();
  for (const auto& kv : ha_key_value_map)
    ha_key_value_map_object[std::to_string(kv.first)] = kv.second;

  doc["ha_default_key"] = ha_default_key;
  doc["ha_payload_on"] = ha_payload_on;
  doc["ha_payload_off"] = ha_payload_off;
  doc["ha_state_class"] = ha_state_class;
  doc["ha_step"] = ha_step;

  doc.shrinkToFit();
  return doc;
}

Command Command::fromJson(const JsonDocument& doc) {
  Command command;

  // Command Fields
  command.key = doc["key"].as<std::string>();
  command.name = doc["name"].as<std::string>();
  command.read_cmd = ebus::to_vector(doc["read_cmd"].as<std::string>());
  if (!doc["write_cmd"].isNull())
    command.write_cmd = ebus::to_vector(doc["write_cmd"].as<std::string>());
  command.active = doc["active"].as<bool>();
  if (!doc["interval"].isNull())
    command.interval = doc["interval"].as<uint32_t>();
  command.last = 0;
  command.data = std::vector<uint8_t>();

  // Data Fields
  command.master = doc["master"].as<bool>();
  command.position = doc["position"].as<size_t>();
  command.datatype = ebus::string_2_datatype(doc["datatype"].as<const char*>());
  command.length = ebus::sizeof_datatype(command.datatype);
  command.numeric = ebus::typeof_datatype(command.datatype);
  if (!doc["divider"].isNull() && doc["divider"].as<float>() > 0)
    command.divider = doc["divider"].as<float>();
  if (!doc["min"].isNull()) command.min = doc["min"].as<float>();
  if (!doc["max"].isNull()) command.max = doc["max"].as<float>();
  if (!doc["digits"].isNull()) command.digits = doc["digits"].as<uint8_t>();
  if (!doc["unit"].isNull()) command.unit = doc["unit"].as<std::string>();

  // Home Assistant
  if (!doc["ha"].isNull()) command.ha = doc["ha"].as<bool>();

  if (command.ha) {
    if (!doc["ha_component"].isNull())
      command.ha_component = doc["ha_component"].as<std::string>();
    if (!doc["ha_device_class"].isNull())
      command.ha_device_class = doc["ha_device_class"].as<std::string>();
    if (!doc["ha_entity_category"].isNull())
      command.ha_entity_category = doc["ha_entity_category"].as<std::string>();
    if (!doc["ha_mode"].isNull())
      command.ha_mode = doc["ha_mode"].as<std::string>();

    if (!doc["ha_key_value_map"].isNull()) {
      JsonObjectConst ha_key_value_map = doc["ha_key_value_map"];
      for (JsonPairConst kv : ha_key_value_map) {
        command.ha_key_value_map[std::stoi(kv.key().c_str())] =
            kv.value().as<std::string>();
      }
    }

    if (!doc["ha_default_key"].isNull())
      command.ha_default_key = doc["ha_default_key"].as<int>();
    if (!doc["ha_payload_on"].isNull())
      command.ha_payload_on = doc["ha_payload_on"].as<uint8_t>();
    if (!doc["ha_payload_off"].isNull())
      command.ha_payload_off = doc["ha_payload_off"].as<uint8_t>();
    if (!doc["ha_state_class"].isNull())
      command.ha_state_class = doc["ha_state_class"].as<std::string>();
    if (!doc["ha_step"].isNull() && doc["ha_step"].as<float>() > 0)
      command.ha_step = doc["ha_step"].as<float>();
  }

  return command;
}

const std::string Command::evaluate(const JsonDocument& doc) {
  // Define the fields to evaluate
  const FieldEvaluation fields[] = {// Command Fields
                                    {"key", true, FT_String},
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
    if (!error.empty()) return error;  // Return the first error found
  }

  return "";  // No errors found
}

const std::string Command::isFieldValid(const JsonDocument& doc,
                                        const std::string& field, bool required,
                                        FieldType type) {
  JsonObjectConst root = doc.as<JsonObjectConst>();

  // Check if the required field exists
  if (required && !root[field.c_str()].is<JsonVariantConst>())
    return "Missing required field: " + field;

  // Skip type checking if the field is not present and not required
  if (!root[field.c_str()].is<JsonVariantConst>() ||
      root[field.c_str()].isNull())
    return "";  // not present and not required => ok

  JsonVariantConst v = root[field.c_str()];

  switch (type) {
    case FT_String: {
      if (!v.is<std::string>()) return "Invalid type for field: " + field;
    } break;
    case FT_HexString: {
      if (!v.is<std::string>()) return "Invalid type for field: " + field;
      const std::string hexStr = v.as<std::string>();
      if (!hexStr.empty()) {
        std::regex hexRegex(R"(^([0-9A-Fa-f]{2})+$)");
        if (!std::regex_match(hexStr, hexRegex))
          return "Invalid hex string for field: " + field;
      }
    } break;
    case FT_Bool: {
      if (!v.is<bool>()) return "Invalid type for field: " + field;
    } break;
    case FT_Int: {
      if (!v.is<int>()) return "Invalid type for field: " + field;
    } break;
    case FT_Float: {
      if (!v.is<float>() && !v.is<double>() && !v.is<long>())
        return "Invalid type for field: " + field;
    } break;
    case FT_Uint8T: {
      if (!v.is<uint8_t>()) return "Invalid type for field: " + field;
      long val = v.as<long>();
      if (val < 0 || val > 0xFF) return "Out of range for field: " + field;
    } break;
    case FT_Uint32T: {
      if (!v.is<uint32_t>()) return "Invalid type for field: " + field;
      long val = v.as<long>();
      if (val < 0 || val > UINT32_MAX)
        return "Out of range for field: " + field;
    } break;
    case FT_SizeT: {
      if (!v.is<size_t>()) return "Invalid type for field: " + field;
      long val = v.as<long>();
      if (val < 0 || val > SIZE_MAX) return "Out of range for field: " + field;
    } break;
    case FT_DataType: {
      if (!v.is<const char*>() ||
          ebus::string_2_datatype(v.as<const char*>()) == ebus::DataType::ERROR)
        return "Invalid datatype for field: " + field;
    } break;
    case FT_KeyValueMap: {
      if (!v.is<JsonObjectConst>()) return "Invalid type for field: " + field;
      return isKeyValueMapValid(v.as<JsonObjectConst>());
    };
  }

  return "";
}

const std::string Command::isKeyValueMapValid(
    const JsonObjectConst ha_key_value_map) {
  for (JsonPairConst kv : ha_key_value_map) {
    // Check if the key can be converted to an integer
    try {
      std::stoi(kv.key().c_str());
    } catch (const std::invalid_argument& e) {
      return "Invalid key: " + std::string(kv.key().c_str());
    } catch (const std::out_of_range& e) {
      return "Key out of range: " + std::string(kv.key().c_str());
    }
    if (!kv.value().is<std::string>()) return "Invalid value type in map";
  }
  return "";  // Passed key-value map evaluation checks
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