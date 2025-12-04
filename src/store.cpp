#if defined(EBUS_INTERNAL)
#include "store.hpp"

#include <Preferences.h>

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

Command Store::createCommand(const JsonDocument& doc) {
  // TODO(yuhu-): Validation of required fields ?

  Command command;

  // Command Fields
  command.key = doc["key"].as<std::string>();
  command.name = doc["name"].as<std::string>();
  command.read_cmd = ebus::to_vector(doc["read_cmd"].as<std::string>());
  command.write_cmd = ebus::to_vector(doc["write_cmd"].as<std::string>());
  command.active = doc["active"].as<bool>();
  command.interval =
      doc["interval"].isNull() ? 60 : doc["interval"].as<uint32_t>();
  command.last = 0;
  command.data = std::vector<uint8_t>();

  // Data Fields
  command.master = doc["master"].as<bool>();
  command.position = doc["position"].as<size_t>();
  command.datatype = ebus::string_2_datatype(doc["datatype"].as<const char*>());
  command.length = ebus::sizeof_datatype(command.datatype);
  command.numeric = ebus::typeof_datatype(command.datatype);
  command.divider =
      doc["divider"].isNull()
          ? 1
          : (doc["divider"].as<float>() > 0 ? doc["divider"].as<float>() : 1);
  command.min = doc["min"].isNull() ? 1 : doc["min"].as<float>();
  command.max = doc["max"].isNull() ? 100 : doc["max"].as<float>();
  command.digits = doc["digits"].isNull() ? 2 : doc["digits"].as<uint8_t>();
  command.unit = doc["unit"].isNull() ? "" : doc["unit"].as<std::string>();

  // Home Assistant (OPTIONAL)
  command.ha = doc["ha"].isNull() ? false : doc["ha"].as<bool>();

  if (command.ha) {
    command.ha_component =
        (doc["ha_component"].isNull() ? "sensor"
                                      : doc["ha_component"].as<std::string>());
    command.ha_device_class = (doc["ha_device_class"].isNull()
                                   ? ""
                                   : doc["ha_device_class"].as<std::string>());
    command.ha_entity_category =
        (doc["ha_entity_category"].isNull()
             ? ""
             : doc["ha_entity_category"].as<std::string>());
    command.ha_mode =
        (doc["ha_mode"].isNull() ? "auto" : doc["ha_mode"].as<std::string>());
    command.ha_options_list = (doc["ha_options_list"].isNull()
                                   ? ""
                                   : doc["ha_options_list"].as<std::string>());
    command.ha_options_default =
        (doc["ha_options_default"].isNull()
             ? ""
             : doc["ha_options_default"].as<std::string>());
    command.ha_payload_on =
        doc["ha_payload_on"].isNull() ? 1 : doc["ha_payload_on"].as<uint8_t>();
    command.ha_payload_off = doc["ha_payload_off"].isNull()
                                 ? 0
                                 : doc["ha_payload_off"].as<uint8_t>();
    command.ha_state_class = (doc["ha_state_class"].isNull()
                                  ? ""
                                  : doc["ha_state_class"].as<std::string>());
    command.ha_step =
        (doc["ha_step"].isNull()
             ? 1
             : (doc["ha_step"].as<float>() > 0 ? doc["ha_step"].as<float>()
                                               : 1));
  } else {
    command.ha_component = "";
    command.ha_device_class = "";
    command.ha_entity_category = "";
    command.ha_mode = "";
    command.ha_options_list = "";
    command.ha_options_default = "";
    command.ha_payload_on = 1;
    command.ha_payload_off = 0;
    command.ha_state_class = "";
    command.ha_step = 1;
  }

  return command;
}

void Store::insertCommand(const Command& command) {
  // TODO(yuhu-): Validation of required fields ?

  // Insert or update in allCommandsByKey
  auto it = allCommandsByKey.find(command.key);
  if (it != allCommandsByKey.end())
    it->second = command;
  else
    allCommandsByKey.insert(std::make_pair(command.key, command));

  // Remove from previous index if exists
  for (auto itp = passiveCommands.begin(); itp != passiveCommands.end();
       ++itp) {
    itp->second.erase(std::remove_if(itp->second.begin(), itp->second.end(),
                                     [&](const Command* cmd) {
                                       return cmd->key == command.key;
                                     }),
                      itp->second.end());
  }

  activeCommands.erase(
      std::remove_if(
          activeCommands.begin(), activeCommands.end(),
          [&](const Command* cmd) { return cmd->key == command.key; }),
      activeCommands.end());

  // Add to passive or active index
  Command* cmdPtr = &allCommandsByKey[command.key];
  if (command.active)
    activeCommands.push_back(cmdPtr);
  else
    passiveCommands[command.read_cmd].push_back(cmdPtr);
}

void Store::removeCommand(const std::string& key) {
  auto it = allCommandsByKey.find(key);
  if (it != allCommandsByKey.end()) {
    // Remove from passiveCommands vectors (only matching key)
    for (auto& kv : passiveCommands) {
      kv.second.erase(
          std::remove_if(kv.second.begin(), kv.second.end(),
                         [&](const Command* cmd) { return cmd->key == key; }),
          kv.second.end());
    }

    // Remove from activeCommands
    activeCommands.erase(
        std::remove_if(activeCommands.begin(), activeCommands.end(),
                       [&](const Command* cmd) { return cmd->key == key; }),
        activeCommands.end());

    // Remove from allCommandsByKey
    allCommandsByKey.erase(it);
  }
}

Command* Store::findCommand(const std::string& key) {
  auto it = allCommandsByKey.find(key);
  if (it != allCommandsByKey.end())
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

JsonDocument Store::getCommandJson(const Command* command) {
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

  // Home Assistant (OPTIONAL)
  doc["ha"] = command->ha;
  doc["ha_component"] = command->ha_component;
  doc["ha_device_class"] = command->ha_device_class;
  doc["ha_entity_category"] = command->ha_entity_category;
  doc["ha_mode"] = command->ha_mode;
  doc["ha_options_list"] = command->ha_options_list;
  doc["ha_options_default"] = command->ha_options_default;
  doc["ha_payload_on"] = command->ha_payload_on;
  doc["ha_payload_off"] = command->ha_payload_off;
  doc["ha_state_class"] = command->ha_state_class;
  doc["ha_step"] = command->ha_step;

  doc.shrinkToFit();
  return doc;
}

const JsonDocument Store::getCommandsJsonDocument() const {
  JsonDocument doc;

  if (!allCommandsByKey.empty()) {
    for (const auto kv : allCommandsByKey) doc.add(getCommandJson(&kv.second));
  }

  if (doc.isNull()) doc.to<JsonArray>();

  doc.shrinkToFit();
  return doc;
}

const std::string Store::getCommandsJson() const {
  std::string payload;
  JsonDocument doc = getCommandsJsonDocument();
  serializeJson(doc, payload);
  return payload;
}

const std::vector<Command*> Store::getCommands() {
  std::vector<Command*> commands;
  for (auto& kv : allCommandsByKey) commands.push_back(&(kv.second));
  return commands;
}

const size_t Store::getActiveCommands() const { return activeCommands.size(); }

const size_t Store::getPassiveCommands() const {
  return passiveCommands.size();
}

const bool Store::active() const { return !activeCommands.empty(); }

Command* Store::nextActiveCommand() {
  Command* next = nullptr;
  bool init = false;
  for (Command* cmd : activeCommands) {
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
  std::vector<Command*> commands;
  // Fast lookup by command pattern
  auto it = passiveCommands.find(master);
  if (it != passiveCommands.end()) {
    commands = it->second;
  } else {
    // fallback: scan for all that match (if needed)
    for (const auto& kv : passiveCommands) {
      if (ebus::contains(master, kv.first))
        commands.insert(commands.end(), kv.second.begin(), kv.second.end());
    }
  }
  return commands;
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

JsonDocument Store::getValueJson(const Command* command) {
  JsonDocument doc;

  if (command->numeric)
    doc["value"] = getDoubleFromVector(command);
  else
    doc["value"] = getStringFromVector(command);

  doc.shrinkToFit();
  return doc;
}

const std::string Store::getValueFullJson(const Command* command) {
  std::string payload;
  JsonDocument doc;

  doc["key"] = command->key;
  if (command->numeric)
    doc["value"] = getDoubleFromVector(command);
  else
    doc["value"] = getStringFromVector(command);
  doc["unit"] = command->unit;
  doc["name"] = command->name;
  doc["age"] = static_cast<uint32_t>((millis() - command->last) / 1000);
  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload;
}

const std::string Store::getValuesJson() const {
  std::string payload;
  JsonDocument doc;

  JsonArray results = doc["results"].to<JsonArray>();

  if (!allCommandsByKey.empty()) {
    size_t index = 0;
    uint32_t now = millis();

    for (const auto& kv : allCommandsByKey) {
      const Command& command = kv.second;
      JsonArray array = results[index][command.key].to<JsonArray>();
      if (command.numeric)
        array.add(getDoubleFromVector(&command));
      else
        array.add(getStringFromVector(&command));

      array.add(command.unit);
      array.add(command.name);
      array.add(static_cast<uint32_t>((now - command.last) / 1000));
      index++;
    }
  }

  if (doc.isNull()) doc.to<JsonArray>();

  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload;
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
      // Home Assistant (OPTIONAL)
      "ha", "ha_component", "ha_device_class", "ha_entity_category", "ha_mode",
      "ha_options_list", "ha_options_default", "ha_payload_on",
      "ha_payload_off", "ha_state_class", "ha_step"};

  // Add header as first entry
  JsonArray header = doc.add<JsonArray>();
  for (const auto& field : fields) header.add(field);

  // Add each command as an array of values in the same order as header
  for (const auto& kv : allCommandsByKey) {
    const Command& command = kv.second;
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
    // Home Assistant (OPTIONAL)
    array.add(command.ha);
    array.add(command.ha_component);
    array.add(command.ha_device_class);
    array.add(command.ha_entity_category);
    array.add(command.ha_mode);
    array.add(command.ha_options_list);
    array.add(command.ha_options_default);
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
        tmpDoc[fields[j]] = values[j];
      }
      insertCommand(createCommand(tmpDoc));
    }
  }
}

#endif
