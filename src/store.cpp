#include "store.hpp"

#include <Preferences.h>
#include <Sequence.h>

#include "mqtt.hpp"

Store store;

void Store::insertCommands(const JsonArray &commands) {
  for (JsonVariant command : commands)
    newCommands.push_back(createCommand(command));
}

void Store::removeCommands(const JsonArray &keys) {
  for (JsonVariant key : keys) remCommands.push_back(key);
}

void Store::publishCommands() {
  for (const Command &command : allCommands) pubCommands.push_back(&command);

  if (allCommands.size() == 0) mqtt.publish("commands", 0, false, "");
}

void Store::loadCommands() {
  Preferences commands;
  commands.begin("commands", true);

  size_t bytes = commands.getBytesLength("ebus");
  if (bytes > 2) {  // 2 = empty json array "[]"
    std::vector<char> buffer(bytes);
    bytes = commands.getBytes("ebus", buffer.data(), bytes);
    if (bytes > 2) {
      std::string payload(buffer.begin(), buffer.end());
      deserializeCommands(payload.c_str());
      publishResponse("load", "successful", bytes);
    } else {
      publishResponse("load", "failed");
    }
  } else {
    publishResponse("load", "no data");
  }

  commands.end();
}

void Store::saveCommands() const {
  Preferences commands;
  commands.begin("commands", false);

  size_t bytes = strlen(serializeCommands().c_str());
  if (bytes > 2) {  // 2 = empty json array "[]"
    bytes = commands.putBytes("ebus", serializeCommands().c_str(), bytes);
    if (bytes > 2)
      publishResponse("save", "successful", bytes);
    else
      publishResponse("save", "failed");
  } else {
    publishResponse("save", "no data");
  }

  commands.end();
}

void Store::wipeCommands() {
  Preferences commands;
  commands.begin("commands", false);

  size_t bytes = commands.getBytesLength("ebus");
  if (bytes > 0) {
    if (commands.remove("ebus"))
      publishResponse("wipe", "successful", bytes);
    else
      publishResponse("wipe", "failed");
  } else {
    publishResponse("wipe", "no data");
  }

  commands.end();
}

const std::string Store::getCommands() const {
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

void Store::doLoop() {
  if (millis() > 2 * 1000) {
    checkInsertCommands();
    checkRemoveCommands();
    checkPublishCommands();
  }
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

void Store::checkInsertCommands() {
  if (newCommands.size() > 0) {
    if (millis() > lastInsert + distanceInsert) {
      Command command = newCommands.front();
      newCommands.pop_front();
      insertCommand(command);
    }
  }
}

void Store::checkRemoveCommands() {
  if (remCommands.size() > 0) {
    if (millis() > lastRemove + distanceRemove) {
      std::string payload = remCommands.front();
      remCommands.pop_front();
      removeCommand(payload);
    }
  }
}

void Store::checkPublishCommands() {
  if (pubCommands.size() > 0) {
    if (millis() > lastPublish + distancePublish) {
      publishCommand(pubCommands.front(), false);
      pubCommands.pop_front();
    }
  }
}

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
  const std::vector<Command>::const_iterator it =
      std::find_if(allCommands.begin(), allCommands.end(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (it != allCommands.end()) allCommands.erase(it);

  allCommands.push_back(command);
  countCommands();
  publishCommand(&allCommands.back(), false);
  if (command.ha) publishHomeAssistant(&allCommands.back(), false);

  lastInsert = millis();
  publishResponse("insert", "key '" + key + "' inserted");
}

void Store::removeCommand(const std::string &key) {
  const std::vector<Command>::const_iterator it =
      std::find_if(allCommands.begin(), allCommands.end(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (it != allCommands.end()) {
    publishCommand(&(*it), true);
    if (it->ha) publishHomeAssistant(&(*it), true);
    allCommands.erase(it);
    countCommands();
    lastRemove = millis();
    publishResponse("remove", "key '" + key + "' removed");
  } else {
    publishResponse("remove", "key '" + key + "' not found");
  }
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

  if (error) {
    std::string errorPayload;
    JsonDocument errorDoc;
    errorDoc["error"] = error.c_str();
    serializeJson(errorDoc, errorPayload);
    mqtt.publish("response", 0, false, errorPayload.c_str());
  } else {
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

      newCommands.push_back(createCommand(tmpDoc));
    }
  }
}

void Store::publishResponse(const std::string &id, const std::string &status,
                            const size_t &bytes) {
  std::string payload;
  JsonDocument doc;
  doc["id"] = id;
  doc["status"] = status;
  if (bytes > 0) doc["bytes"] = bytes;
  serializeJson(doc, payload);
  mqtt.publish("response", 0, false, payload.c_str());
}

void Store::publishCommand(const Command *command, const bool remove) {
  std::string topic = "commands/" + command->key;

  std::string payload;

  if (!remove) {
    JsonDocument doc;

    doc["key"] = command->key;
    doc["command"] = ebus::Sequence::to_string(command->command);
    doc["unit"] = command->unit;
    doc["active"] = command->active;
    doc["interval"] = command->interval;
    doc["master"] = command->master;
    doc["position"] = command->position;
    doc["datatype"] = ebus::datatype2string(command->datatype);
    doc["topic"] = command->topic;
    doc["ha"] = command->ha;
    doc["ha_class"] = command->ha_class;

    serializeJson(doc, payload);
  }

  mqtt.publish(topic.c_str(), 0, false, payload.c_str());

  if (remove) {
    topic = "values/" + command->topic;
    mqtt.publish(topic.c_str(), 0, false, "");
  }
}

void Store::publishHomeAssistant(const Command *command, const bool remove) {
  std::string stateTopic = command->topic;
  std::transform(stateTopic.begin(), stateTopic.end(), stateTopic.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  std::string subTopic = stateTopic;
  std::replace(subTopic.begin(), subTopic.end(), '/', '_');

  std::string topic = "homeassistant/sensor/ebus" + mqtt.getUniqueId() + '/' +
                      subTopic + "/config";

  std::string payload;

  if (!remove) {
    JsonDocument doc;

    std::string name = command->topic;
    std::replace(name.begin(), name.end(), '/', ' ');
    std::replace(name.begin(), name.end(), '_', ' ');

    doc["name"] = name;
    if (command->ha_class.compare("null") != 0 &&
        command->ha_class.length() > 0)
      doc["device_class"] = command->ha_class;
    doc["state_topic"] =
        mqtt.getRootTopic() + std::string("values/") + stateTopic;
    if (command->unit.compare("null") != 0 && command->unit.length() > 0)
      doc["unit_of_measurement"] = command->unit;
    doc["unique_id"] = "ebus" + mqtt.getUniqueId() + '_' + command->key;
    doc["value_template"] = "{{value_json.value}}";

    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"] = "ebus" + mqtt.getUniqueId();
    device["name"] = "esp-ebus";
    device["manufacturer"] = "";  // TODO(yuhu-): fill with thing data
    device["model"] = "";         // TODO(yuhu-): fill with thing data
    device["model_id"] = "";      // TODO(yuhu-): fill with thing data
    device["serial_number"] = mqtt.getUniqueId();
    device["hw_version"] = "";  // TODO(yuhu-): fill with thing data
    device["sw_version"] = "";  // TODO(yuhu-): fill with thing data
    device["configuration_url"] = "http://esp-ebus.local";

    serializeJson(doc, payload);
  }

  mqtt.publish(topic.c_str(), 0, true, payload.c_str(), false);
}
