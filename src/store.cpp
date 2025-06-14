#include "store.hpp"

#include <Preferences.h>

Store store;

Command Store::createCommand(const JsonDocument &doc) {
  Command command;
  // TODO(yuhu-): check incoming data for completeness
  command.key = doc["key"].as<std::string>();
  command.command = ebus::to_vector(doc["command"].as<std::string>());
  command.unit = doc["unit"].as<std::string>();
  command.active = doc["active"].as<bool>();
  command.interval =
      doc["interval"].isNull() ? 60 : doc["interval"].as<uint32_t>();
  command.last = 0;
  command.data = std::vector<uint8_t>();
  command.master = doc["master"].as<bool>();
  command.position = doc["position"].as<size_t>();
  command.datatype =
      ebus::string_2_datatype(doc["datatype"].as<const char *>());
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

void Store::insertCommand(const Command &command) {
  std::string key = command.key;
  const std::vector<Command>::iterator it =
      std::find_if(allCommands.begin(), allCommands.end(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (it != allCommands.end()) {
    *it = command;
  } else {
    allCommands.push_back(command);
    countCommands();
  }
}

void Store::removeCommand(const std::string &key) {
  const std::vector<Command>::const_iterator it =
      std::find_if(allCommands.cbegin(), allCommands.cend(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (it != allCommands.end()) {
    allCommands.erase(it);
    countCommands();
  }
}

const Command *Store::findCommand(const std::string &key) {
  const std::vector<Command>::const_iterator it =
      std::find_if(allCommands.cbegin(), allCommands.cend(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (it != allCommands.end())
    return &(*it);
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

JsonDocument Store::getCommandJson(const Command *command) {
  JsonDocument doc;

  doc["key"] = command->key;
  doc["command"] = ebus::to_string(command->command);
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

  if (allCommands.size() > 0) {
    for (const Command &command : allCommands)
      doc.add(getCommandJson(&command));
  }

  if (doc.isNull()) doc.to<JsonArray>();

  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload;
}

const std::vector<Command *> Store::getCommands() {
  std::vector<Command *> commands;
  for (Command &command : allCommands) commands.push_back(&(command));
  return commands;
}

const size_t Store::getActiveCommands() const { return activeCommands; }
const size_t Store::getPassiveCommands() const { return passiveCommands; }

const bool Store::active() const { return activeCommands > 0; }

Command *Store::nextActiveCommand() {
  Command *next = nullptr;
  bool init = false;

  for (Command &cmd : allCommands) {
    if (cmd.active) {
      if (cmd.last == 0) {
        next = &cmd;
        init = true;
        break;
      }

      if (next == nullptr) {
        next = &cmd;
      } else {
        if (cmd.last + cmd.interval * 1000 < next->last + next->interval * 1000)
          next = &cmd;
      }
    }
  }

  if (!init && millis() < next->last + next->interval * 1000) next = nullptr;

  return next;
}

std::vector<Command *> Store::findPassiveCommands(
    const std::vector<uint8_t> &master) {
  std::vector<Command *> commands;

  for (Command &command : allCommands) {
    if (!command.active && ebus::contains(master, command.command))
      commands.push_back(&(command));
  }

  return commands;
}

std::vector<Command *> Store::updateData(Command *command,
                                         const std::vector<uint8_t> &master,
                                         const std::vector<uint8_t> &slave) {
  std::vector<Command *> commands;

  if (command == nullptr)
    commands = findPassiveCommands(master);
  else
    commands.push_back(command);

  for (Command *cmd : commands) {
    cmd->last = millis();

    if (cmd->master)
      cmd->data = ebus::range(master, 4 + cmd->position, cmd->length);
    else
      cmd->data = ebus::range(slave, cmd->position, cmd->length);
  }

  return commands;
}

JsonDocument Store::getValueJson(const Command *command) {
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

  if (allCommands.size() > 0) {
    size_t index = 0;
    uint32_t now = millis();

    for (const Command &command : allCommands) {
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

void Store::countCommands() {
  activeCommands = std::count_if(allCommands.begin(), allCommands.end(),
                                 [](const Command &cmd) { return cmd.active; });

  passiveCommands = allCommands.size() - activeCommands;
}

const std::string Store::serializeCommands() const {
  std::string payload;
  JsonDocument doc;

  if (allCommands.size() > 0) {
    for (const Command &command : allCommands) {
      JsonArray array = doc.add<JsonArray>();

      array.add(command.key);
      array.add(ebus::to_string(command.command));
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

void Store::deserializeCommands(const char *payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (!error) {
    JsonArray array = doc.as<JsonArray>();
    for (JsonVariant variant : array) {
      JsonDocument tmpDoc;

      tmpDoc["key"] = variant[0];
      tmpDoc["command"] = variant[1];
      tmpDoc["unit"] = variant[2];
      tmpDoc["active"] = variant[3];
      tmpDoc["interval"] = variant[4];
      tmpDoc["master"] = variant[5];
      tmpDoc["position"] = variant[6];
      tmpDoc["datatype"] = variant[7];
      tmpDoc["divider"] = variant[8];
      tmpDoc["digits"] = variant[9];
      tmpDoc["topic"] = variant[10];
      tmpDoc["ha"] = variant[11];
      tmpDoc["ha_class"] = variant[12];

      insertCommand(createCommand(tmpDoc));
    }
  }
}

const double Store::getValueDouble(const Command *command) {
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

const std::string Store::getValueString(const Command *command) {
  return ebus::byte_2_string(command->data);
}
