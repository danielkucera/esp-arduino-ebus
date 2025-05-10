#include "store.hpp"

#include <Preferences.h>
#include <Sequence.h>

Store store;

Command Store::createCommand(const JsonDocument &doc) {
  Command command;

  command.key = doc["key"].as<std::string>();
  command.command = ebus::Sequence::to_vector(doc["command"].as<std::string>());
  command.unit = doc["unit"].as<std::string>();
  command.active = doc["active"].as<bool>();
  command.interval = doc["interval"].as<uint32_t>();
  command.last = 0;
  command.master = doc["master"].as<bool>();
  command.position = doc["position"].as<size_t>();
  command.datatype = ebus::string2datatype(doc["datatype"].as<const char *>());
  command.topic = doc["topic"].as<std::string>();
  command.ha = doc["ha"].as<bool>();
  command.ha_class = doc["ha_class"].as<std::string>();

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

const std::string Store::getCommandsJson() const {
  std::string payload;
  JsonDocument doc;

  if (allCommands.size() > 0) {
    for (const Command &command : allCommands) {
      JsonObject obj = doc.add<JsonObject>();

      obj["key"] = command.key;
      obj["command"] = ebus::Sequence::to_string(command.command);
      obj["unit"] = command.unit;
      obj["active"] = command.active;
      obj["interval"] = command.interval;
      obj["master"] = command.master;
      obj["position"] = command.position;
      obj["datatype"] = ebus::datatype2string(command.datatype);
      obj["topic"] = command.topic;
      obj["ha"] = command.ha;
      obj["ha_class"] = command.ha_class;
    }
  }

  if (doc.isNull()) {
    doc.to<JsonArray>();
  }

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
    if (!command.active && ebus::Sequence::contains(master, command.command))
      commands.push_back(&(command));
  }

  return commands;
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
      array.add(ebus::Sequence::to_string(command.command));
      array.add(command.unit);
      array.add(command.active);
      array.add(command.interval);
      array.add(command.master);
      array.add(command.position);
      array.add(ebus::datatype2string(command.datatype));
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
      tmpDoc["topic"] = variant[8];
      tmpDoc["ha"] = variant[9];
      tmpDoc["ha_class"] = variant[10];

      insertCommand(createCommand(tmpDoc));
    }
  }
}
