#if defined(EBUS_INTERNAL)
#include "store.hpp"

#include <Preferences.h>

Store store;

Command Store::createCommand(const JsonDocument& doc) {
  Command command;
  // TODO(yuhu-): check incoming data for completeness
  command.key = doc["key"].as<std::string>();
  command.read_cmd = ebus::to_vector(doc["read_cmd"].as<std::string>());
  command.write_cmd = ebus::to_vector(doc["write_cmd"].as<std::string>());
  command.unit = doc["unit"].as<std::string>();
  command.active = doc["active"].as<bool>();
  command.interval =
      doc["interval"].isNull() ? 60 : doc["interval"].as<uint32_t>();
  command.last = 0;
  command.data = std::vector<uint8_t>();
  command.master = doc["master"].as<bool>();
  command.position = doc["position"].as<size_t>();
  command.datatype = ebus::string_2_datatype(doc["datatype"].as<const char*>());
  command.length = ebus::sizeof_datatype(command.datatype);
  command.numeric = ebus::typeof_datatype(command.datatype);
  command.divider =
      doc["divider"].isNull()
          ? 1
          : (doc["divider"].as<float>() > 0 ? doc["divider"].as<float>() : 1);
  command.digits = doc["digits"].isNull() ? 2 : doc["digits"].as<uint8_t>();
  command.topic = doc["topic"].as<std::string>();
  command.ha = doc["ha"].isNull() ? false : doc["ha"].as<bool>();
  command.ha_class =
      command.ha
          ? (doc["ha_class"].isNull() ? "" : doc["ha_class"].as<std::string>())
          : "";

  return command;
}

void Store::insertCommand(const Command& command) {
  // Insert or update in allCommandsByKey
  std::unordered_map<std::string, Command>::iterator it =
      allCommandsByKey.find(command.key);
  if (it != allCommandsByKey.end())
    it->second = command;
  else
    allCommandsByKey.insert(std::make_pair(command.key, command));

  // Remove from previous index if exists
  for (std::unordered_map<std::vector<uint8_t>, Command*, VectorHash>::iterator
           itp = passiveCommands.begin();
       itp != passiveCommands.end();) {
    if (itp->second->key == command.key)
      itp = passiveCommands.erase(itp);
    else
      ++itp;
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
    passiveCommands[command.read_cmd] = cmdPtr;
}

void Store::removeCommand(const std::string& key) {
  std::unordered_map<std::string, Command>::iterator it =
      allCommandsByKey.find(key);
  if (it != allCommandsByKey.end()) {
    // Remove from indexes
    for (std::unordered_map<std::vector<uint8_t>, Command*,
                            VectorHash>::iterator itp = passiveCommands.begin();
         itp != passiveCommands.end();) {
      if (itp->second->key == key)
        itp = passiveCommands.erase(itp);
      else
        ++itp;
    }
    activeCommands.erase(
        std::remove_if(activeCommands.begin(), activeCommands.end(),
                       [&](const Command* cmd) { return cmd->key == key; }),
        activeCommands.end());
    allCommandsByKey.erase(it);
  }
}

const Command* Store::findCommand(const std::string& key) {
  std::unordered_map<std::string, Command>::iterator it =
      allCommandsByKey.find(key);
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

  int64_t bytes = strlen(serializeCommands().c_str());
  if (bytes > 2) {  // 2 = empty json array "[]"
    bytes = preferences.putBytes("ebus", serializeCommands().c_str(), bytes);
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

  doc["key"] = command->key;
  doc["read_cmd"] = ebus::to_string(command->read_cmd);
  doc["write_cmd"] = ebus::to_string(command->write_cmd);
  doc["unit"] = command->unit;
  doc["active"] = command->active;
  doc["interval"] = command->interval;
  doc["master"] = command->master;
  doc["position"] = command->position;
  doc["datatype"] = ebus::datatype_2_string(command->datatype);
  doc["divider"] = command->divider;
  doc["digits"] = command->digits;
  doc["topic"] = command->topic;
  doc["ha"] = command->ha;
  doc["ha_class"] = command->ha_class;

  doc.shrinkToFit();
  return doc;
}

const std::string Store::getCommandsJson() const {
  std::string payload;
  JsonDocument doc;

  if (!allCommandsByKey.empty()) {
    for (const std::pair<const std::string, Command>& kv : allCommandsByKey)
      doc.add(getCommandJson(&kv.second));
  }

  if (doc.isNull()) doc.to<JsonArray>();

  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload;
}

const std::vector<Command*> Store::getCommands() {
  std::vector<Command*> commands;
  for (std::pair<const std::string, Command>& kv : allCommandsByKey)
    commands.push_back(&(kv.second));
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
  std::unordered_map<std::vector<uint8_t>, Command*, VectorHash>::iterator it =
      passiveCommands.find(master);
  if (it != passiveCommands.end()) {
    commands.push_back(it->second);
  } else {
    // fallback: scan for all that match (if needed)
    for (const std::pair<const std::vector<uint8_t>, Command*>& kv :
         passiveCommands) {
      if (ebus::contains(master, kv.first)) commands.push_back(kv.second);
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
    doc["value"] = store.getValueDouble(command);
  else
    doc["value"] = store.getValueString(command);

  doc.shrinkToFit();
  return doc;
}

const std::string Store::getValuesJson() const {
  std::string payload;
  JsonDocument doc;

  JsonArray results = doc["results"].to<JsonArray>();

  if (!allCommandsByKey.empty()) {
    size_t index = 0;
    uint32_t now = millis();

    for (const std::pair<const std::string, Command>& kv : allCommandsByKey) {
      const Command& command = kv.second;
      JsonArray array = results[index][command.key].to<JsonArray>();
      if (command.numeric)
        array.add(store.getValueDouble(&command));
      else
        array.add(store.getValueString(&command));

      array.add(command.unit);
      array.add(command.topic);
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

  if (!allCommandsByKey.empty()) {
    for (const std::pair<const std::string, Command>& kv : allCommandsByKey) {
      const Command& command = kv.second;
      JsonArray array = doc.add<JsonArray>();

      array.add(command.key);
      array.add(ebus::to_string(command.read_cmd));
      array.add(ebus::to_string(command.write_cmd));
      array.add(command.unit);
      array.add(command.active);
      array.add(command.interval);
      array.add(command.master);
      array.add(command.position);
      array.add(ebus::datatype_2_string(command.datatype));
      array.add(command.divider);
      array.add(command.digits);
      array.add(command.topic);
      array.add(command.ha);
      array.add(command.ha_class);
    }
  }

  if (doc.isNull()) {
    doc.to<JsonArray>();
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
    for (JsonVariant variant : array) {
      JsonDocument tmpDoc;

      tmpDoc["key"] = variant[0];
      tmpDoc["read_cmd"] = variant[1];
      tmpDoc["write_cmd"] = variant[2];
      tmpDoc["unit"] = variant[3];
      tmpDoc["active"] = variant[4];
      tmpDoc["interval"] = variant[5];
      tmpDoc["master"] = variant[6];
      tmpDoc["position"] = variant[7];
      tmpDoc["datatype"] = variant[8];
      tmpDoc["divider"] = variant[9];
      tmpDoc["digits"] = variant[10];
      tmpDoc["topic"] = variant[11];
      tmpDoc["ha"] = variant[12];
      tmpDoc["ha_class"] = variant[13];

      insertCommand(createCommand(tmpDoc));
    }
  }
}

const double Store::getValueDouble(const Command* command) {
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

const std::string Store::getValueString(const Command* command) {
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
#endif
