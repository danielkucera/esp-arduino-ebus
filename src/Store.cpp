#if defined(EBUS_INTERNAL)
#include "Store.hpp"

#include <Preferences.h>

#include <regex>

const double getDoubleFromVector(const Command* command) {
  double value = 0;

  switch (command->datatype) {
    case ebus::DataType::BCD:
      value = ebus::byte_2_bcd(command->data);
      break;
    case ebus::DataType::UINT8:
      value = ebus::byte_2_uint8(command->data);
      break;
    case ebus::DataType::INT8:
      value = ebus::byte_2_int8(command->data);
      break;
    case ebus::DataType::UINT16:
      value = ebus::byte_2_uint16(command->data);
      break;
    case ebus::DataType::INT16:
      value = ebus::byte_2_int16(command->data);
      break;
    case ebus::DataType::UINT32:
      value = ebus::byte_2_uint32(command->data);
      break;
    case ebus::DataType::INT32:
      value = ebus::byte_2_int32(command->data);
      break;
    case ebus::DataType::DATA1B:
      value = ebus::byte_2_data1b(command->data);
      break;
    case ebus::DataType::DATA1C:
      value = ebus::byte_2_data1c(command->data);
      break;
    case ebus::DataType::DATA2B:
      value = ebus::byte_2_data2b(command->data);
      break;
    case ebus::DataType::DATA2C:
      value = ebus::byte_2_data2c(command->data);
      break;
    case ebus::DataType::FLOAT:
      value = ebus::byte_2_float(command->data);
      break;
    default:
      break;
  }

  value = value / command->divider;
  value = ebus::round_digits(value, command->digits);

  return value;
}

const std::vector<uint8_t> getVectorFromDouble(const Command* command,
                                               double value) {
  std::vector<uint8_t> result;
  if (!command) return result;

  value = value * command->divider;
  value = ebus::round_digits(value, command->digits);

  switch (command->datatype) {
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

const std::string getStringFromVector(const Command* command) {
  std::string value;

  switch (command->datatype) {
    case ebus::DataType::CHAR1:
    case ebus::DataType::CHAR2:
    case ebus::DataType::CHAR3:
    case ebus::DataType::CHAR4:
    case ebus::DataType::CHAR5:
    case ebus::DataType::CHAR6:
    case ebus::DataType::CHAR7:
    case ebus::DataType::CHAR8:
      value = ebus::byte_2_char(command->data);
      break;
    case ebus::DataType::HEX1:
    case ebus::DataType::HEX2:
    case ebus::DataType::HEX3:
    case ebus::DataType::HEX4:
    case ebus::DataType::HEX5:
    case ebus::DataType::HEX6:
    case ebus::DataType::HEX7:
    case ebus::DataType::HEX8:
      value = ebus::byte_2_hex(command->data);
      break;
    default:
      break;
  }

  return value;
}

const std::vector<uint8_t> getVectorFromString(const Command* command,
                                               const std::string& value) {
  std::vector<uint8_t> result;
  if (!command) return result;

  switch (command->datatype) {
    case ebus::DataType::CHAR1:
    case ebus::DataType::CHAR2:
    case ebus::DataType::CHAR3:
    case ebus::DataType::CHAR4:
    case ebus::DataType::CHAR5:
    case ebus::DataType::CHAR6:
    case ebus::DataType::CHAR7:
    case ebus::DataType::CHAR8:
      result = ebus::char_2_byte(value.substr(0, command->length));
      break;
    case ebus::DataType::HEX1:
    case ebus::DataType::HEX2:
    case ebus::DataType::HEX3:
    case ebus::DataType::HEX4:
    case ebus::DataType::HEX5:
    case ebus::DataType::HEX6:
    case ebus::DataType::HEX7:
    case ebus::DataType::HEX8:
      result = ebus::hex_2_byte(value.substr(0, command->length));
      break;
    default:
      break;
  }
  return result;
}

Store store;

struct FieldEvaluation {
  const char* name;
  bool required;
  FieldType type;
};

const std::string Store::evaluateCommand(const JsonDocument& doc) {
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

Command Store::createCommand(const JsonDocument& doc) {
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

void Store::insertCommand(const Command& command) {
  // Insert or update in commands map
  auto it = commands.find(command.key);
  if (it != commands.end())
    it->second = command;
  else
    commands.insert(std::make_pair(command.key, command));
}

void Store::removeCommand(const std::string& key) {
  auto it = commands.find(key);
  if (it != commands.end()) {
    commands.erase(it);
  }
}

Command* Store::findCommand(const std::string& key) {
  auto it = commands.find(key);
  if (it != commands.end())
    return &(it->second);
  else
    return nullptr;
}

int64_t Store::loadCommands() {
  Preferences preferences;
  preferences.begin("commands", true);

  int64_t bytes = preferences.getBytesLength("ebus");
  if (bytes > 2) {  // 2 = empty json array "[]"
    std::vector<char> buffer(bytes);
    bytes = preferences.getBytes("ebus", buffer.data(), bytes);
    if (bytes > 0) {
      std::string payload(buffer.begin(), buffer.end());
      deserializeCommands(payload.c_str());
    } else {
      bytes = -1;
    }
  } else {
    bytes = 0;
  }

  preferences.end();
  return bytes;
}

int64_t Store::saveCommands() const {
  Preferences preferences;
  preferences.begin("commands", false);

  std::string payload = serializeCommands();
  int64_t bytes = payload.size();
  if (bytes > 2) {  // 2 = empty json array "[]"
    bytes = preferences.putBytes("ebus", payload.data(), bytes);
    if (bytes == 0) bytes = -1;
  } else {
    bytes = 0;
  }

  preferences.end();
  return bytes;
}

int64_t Store::wipeCommands() {
  Preferences preferences;
  preferences.begin("commands", false);

  int64_t bytes = preferences.getBytesLength("ebus");
  if (bytes > 0) {
    if (!preferences.remove("ebus")) bytes = -1;
  }

  preferences.end();
  return bytes;
}

JsonDocument Store::getCommandJsonDoc(const Command* command) {
  JsonDocument doc;

  // Command Fields
  doc["key"] = command->key;
  doc["name"] = command->name;
  doc["read_cmd"] = ebus::to_string(command->read_cmd);
  doc["write_cmd"] = ebus::to_string(command->write_cmd);
  doc["active"] = command->active;
  doc["interval"] = command->interval;

  // Data Fields
  doc["master"] = command->master;
  doc["position"] = command->position;
  doc["datatype"] = ebus::datatype_2_string(command->datatype);
  doc["divider"] = command->divider;
  doc["min"] = command->min;
  doc["max"] = command->max;
  doc["digits"] = command->digits;
  doc["unit"] = command->unit;

  // Home Assistant
  doc["ha"] = command->ha;
  doc["ha_component"] = command->ha_component;
  doc["ha_device_class"] = command->ha_device_class;
  doc["ha_entity_category"] = command->ha_entity_category;
  doc["ha_mode"] = command->ha_mode;

  JsonObject ha_key_value_map = doc["ha_key_value_map"].to<JsonObject>();
  for (const auto& kv : command->ha_key_value_map)
    ha_key_value_map[std::to_string(kv.first)] = kv.second;

  doc["ha_default_key"] = command->ha_default_key;
  doc["ha_payload_on"] = command->ha_payload_on;
  doc["ha_payload_off"] = command->ha_payload_off;
  doc["ha_state_class"] = command->ha_state_class;
  doc["ha_step"] = command->ha_step;

  doc.shrinkToFit();
  return doc;
}

const JsonDocument Store::getCommandsJsonDoc() const {
  JsonDocument doc;

  std::vector<std::pair<std::string, Command>> orderedCommands(
      commands.begin(), commands.end());

  std::sort(orderedCommands.begin(), orderedCommands.end(),
            [](const std::pair<std::string, Command>& a,
               const std::pair<std::string, Command>& b) {
              return a.first < b.first;  // Compare based on keys
            });

  for (const auto& kv : orderedCommands) doc.add(getCommandJsonDoc(&kv.second));

  if (doc.isNull()) doc.to<JsonArray>();

  doc.shrinkToFit();
  return doc;
}

const std::string Store::getCommandsJson() const {
  std::string payload;
  JsonDocument doc = getCommandsJsonDoc();
  serializeJson(doc, payload);
  return payload;
}

const std::vector<Command*> Store::getCommands() {
  std::vector<Command*> result;
  for (auto& kv : commands) result.push_back(&(kv.second));
  return result;
}

const size_t Store::getActiveCommands() const {
  size_t count = 0;
  for (const auto& kv : commands) {
    if (kv.second.active) count++;
  }
  return count;
}

const size_t Store::getPassiveCommands() const {
  size_t count = 0;
  for (const auto& kv : commands) {
    if (!kv.second.active) count++;
  }
  return count;
}

const bool Store::active() const {
  for (const auto& kv : commands) {
    if (kv.second.active) return true;
  }
  return false;
}

Command* Store::nextActiveCommand() {
  Command* next = nullptr;
  bool init = false;
  for (auto& kv : commands) {
    Command* cmd = &kv.second;
    if (!cmd->active) continue;  // Only consider active commands
    if (cmd->last == 0) {
      next = cmd;
      init = true;
      break;
    }
    if (next == nullptr ||
        (cmd->last + cmd->interval * 1000 < next->last + next->interval * 1000))
      next = cmd;
  }

  if (!init && next && millis() < next->last + next->interval * 1000)
    next = nullptr;

  return next;
}

std::vector<Command*> Store::findPassiveCommands(
    const std::vector<uint8_t>& master) {
  std::vector<Command*> result;
  for (auto& kv : commands) {
    Command* cmd = &kv.second;
    if (cmd->active) continue;  // Skip active commands
    if (ebus::contains(master, cmd->read_cmd)) {
      result.push_back(cmd);
    }
  }
  return result;
}

std::vector<Command*> Store::updateData(Command* command,
                                        const std::vector<uint8_t>& master,
                                        const std::vector<uint8_t>& slave) {
  if (command) {
    command->last = millis();
    if (command->master)
      command->data =
          ebus::range(master, 4 + command->position, command->length);
    else
      command->data = ebus::range(slave, command->position, command->length);
    // Return a vector with just this command, but avoid heap allocation
    return {command};
  }

  // Passive: potentially multiple matches
  std::vector<Command*> commands = findPassiveCommands(master);
  for (Command* cmd : commands) {
    cmd->last = millis();
    if (cmd->master)
      cmd->data = ebus::range(master, 4 + cmd->position, cmd->length);
    else
      cmd->data = ebus::range(slave, cmd->position, cmd->length);
  }
  return commands;
}

const JsonDocument Store::getValueJsonDoc(const Command* command) {
  JsonDocument doc;

  if (command->numeric)
    doc["value"] = getDoubleFromVector(command);
  else
    doc["value"] = getStringFromVector(command);

  doc.shrinkToFit();
  return doc;
}

const JsonDocument Store::getValueFullJsonDoc(const Command* command) {
  JsonDocument doc;

  doc["key"] = command->key;
  doc["name"] = command->name;
  doc["value"] = getValueJsonDoc(command)["value"];
  doc["unit"] = command->unit;
  doc["age"] = static_cast<uint32_t>((millis() - command->last) / 1000);
  doc["write"] = !command->write_cmd.empty();
  doc["active"] = command->active;

  doc.shrinkToFit();
  return doc;
}

const std::string Store::getValueFullJson(const Command* command) {
  std::string payload;
  serializeJson(getValueFullJsonDoc(command), payload);
  return payload;
}

const JsonDocument Store::getValuesJsonDoc() const {
  JsonDocument doc;

  std::vector<std::pair<std::string, Command>> orderedCommands(
      commands.begin(), commands.end());

  std::sort(orderedCommands.begin(), orderedCommands.end(),
            [](const std::pair<std::string, Command>& a,
               const std::pair<std::string, Command>& b) {
              return a.first < b.first;  // Compare based on keys
            });

  for (const auto& kv : orderedCommands)
    doc.add(getValueFullJsonDoc(&kv.second));

  if (doc.isNull()) doc.to<JsonArray>();

  doc.shrinkToFit();
  return doc;
}

const std::string Store::getValuesJson() const {
  std::string payload;
  JsonDocument doc = getValuesJsonDoc();
  serializeJson(doc, payload);
  return payload;
}

const std::string Store::isFieldValid(const JsonDocument& doc,
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

const std::string Store::isKeyValueMapValid(
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

const std::string Store::serializeCommands() const {
  std::string payload;
  JsonDocument doc;

  // Define field names (order matters)
  std::vector<std::string> fields = {
      // Command Fields
      "key", "name", "read_cmd", "write_cmd", "active", "interval",

      // Data Fields
      "master", "position", "datatype", "divider", "min", "max", "digits",
      "unit",

      // Home Assistant
      "ha", "ha_component", "ha_device_class", "ha_entity_category", "ha_mode",
      "ha_key_value_map", "ha_default_key", "ha_payload_on", "ha_payload_off",
      "ha_state_class", "ha_step"};

  // Add header as first entry
  JsonArray header = doc.add<JsonArray>();
  for (const auto& field : fields) header.add(field);

  // Add each command as an array of values in the same order as header
  for (const auto& cmd : commands) {
    const Command& command = cmd.second;
    JsonArray array = doc.add<JsonArray>();

    // Command Fields
    array.add(command.key);
    array.add(command.name);
    array.add(ebus::to_string(command.read_cmd));
    array.add(ebus::to_string(command.write_cmd));
    array.add(command.active);
    array.add(command.interval);

    // Data Fields
    array.add(command.master);
    array.add(command.position);
    array.add(ebus::datatype_2_string(command.datatype));
    array.add(command.divider);
    array.add(command.min);
    array.add(command.max);
    array.add(command.digits);
    array.add(command.unit);

    // Home Assistant
    array.add(command.ha);
    array.add(command.ha_component);
    array.add(command.ha_device_class);
    array.add(command.ha_entity_category);
    array.add(command.ha_mode);

    JsonObject ha_key_value_map = array.add<JsonObject>();
    for (const auto& kv : command.ha_key_value_map)
      ha_key_value_map[std::to_string(kv.first)] = kv.second;

    array.add(command.ha_default_key);
    array.add(command.ha_payload_on);
    array.add(command.ha_payload_off);
    array.add(command.ha_state_class);
    array.add(command.ha_step);
  }

  doc.shrinkToFit();
  serializeJson(doc, payload);
  return payload;
}

void Store::deserializeCommands(const char* payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (!error) {
    JsonArray array = doc.as<JsonArray>();
    if (array.size() < 2) return;  // Need at least header + one command

    // Read header
    JsonArray header = array[0];
    std::vector<std::string> fields;
    for (JsonVariant v : header) fields.push_back(v.as<std::string>());

    // Read each command
    for (size_t i = 1; i < array.size(); ++i) {
      JsonArray values = array[i];
      JsonDocument tmpDoc;
      for (size_t j = 0; j < fields.size() && j < values.size(); ++j) {
        // Special handling for 'ha_key_value_map'
        if (fields[j] == "ha_key_value_map") {
          JsonObjectConst kvObject = values[j].as<JsonObject>();
          JsonObject ha_key_value_map =
              tmpDoc["ha_key_value_map"].to<JsonObject>();

          for (JsonPairConst kv : kvObject)
            ha_key_value_map[kv.key()] = kv.value();
        } else {
          tmpDoc[fields[j]] = values[j];
        }
      }
      std::string evalError = store.evaluateCommand(tmpDoc);
      if (evalError.empty()) insertCommand(createCommand(tmpDoc));
    }
  }
}

#endif
