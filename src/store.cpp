#include "store.hpp"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "Sequence.h"
#include "mqtt.hpp"

Store store;

void Store::enqueCommand(const char *payload) {
  newCommands.push_back(std::string(payload));
}

// payload - optional: unit, ha_class
// {
//   "command": "08b509030d0600",
//   "unit": "°C",
//   "active": true,
//   "interval": 60,
//   "master": false,
//   "position": 1,
//   "datatype": "DATA2c",
//   "topic": "Aussentemperatur",
//   "ha": true,
//   "ha_class": "temperature"
// }
void Store::insertCommand(const char *payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    std::string err = "DeserializationError ";
    err += error.c_str();
    mqttClient.publish("ebus/config/error", 0, false, err.c_str());
  } else {
    Command command;

    std::string key = doc["command"].as<std::string>();

    command.key = key;
    command.command = ebus::Sequence::to_vector(key);
    command.unit = doc["unit"].as<std::string>();
    command.active = doc["active"].as<bool>();
    command.interval = doc["interval"].as<uint32_t>();
    command.last = 0;
    command.master = doc["master"].as<bool>();
    command.position = doc["position"].as<size_t>();
    command.datatype =
        ebus::string2datatype(doc["datatype"].as<const char *>());
    command.topic = doc["topic"].as<std::string>();
    command.ha = doc["ha"].as<bool>();
    command.ha_class = doc["ha_class"].as<std::string>();

    std::vector<Command> *usedCommands = nullptr;
    if (command.active)
      usedCommands = &activeCommands;
    else
      usedCommands = &passiveCommands;

    const std::vector<Command>::const_iterator it =
        std::find_if(usedCommands->begin(), usedCommands->end(),
                     [&key](const Command &cmd) { return cmd.key == key; });

    if (it != usedCommands->end()) usedCommands->erase(it);

    usedCommands->push_back(command);
    publishCommand(usedCommands, command.key, false);

    init = true;
    lastInsert = millis();
  }
}

void Store::removeCommand(const char *topic) {
  std::string tmp = topic;
  std::string key(tmp.substr(tmp.rfind("/") + 1));

  const std::vector<Command>::const_iterator actIt =
      std::find_if(activeCommands.begin(), activeCommands.end(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (actIt != activeCommands.end()) {
    publishCommand(&activeCommands, key, true);

    activeCommands.erase(actIt);
  } else {
    const std::vector<Command>::const_iterator pasIt =
        std::find_if(passiveCommands.begin(), passiveCommands.end(),
                     [&key](const Command &cmd) { return cmd.key == key; });

    if (pasIt != passiveCommands.end()) {
      publishCommand(&passiveCommands, key, true);

      passiveCommands.erase(pasIt);
    } else {
      std::string err = key + " not found";
      mqttClient.publish("ebus/config/error", 0, false, err.c_str());
    }
  }
}

void Store::publishCommands() {
  for (const Command &command : activeCommands)
    pubCommands.push_back(command.key);

  for (const Command &command : passiveCommands)
    pubCommands.push_back(command.key);

  if (activeCommands.size() + passiveCommands.size() == 0)
    mqttClient.publish("ebus/commands", 0, false, "");
}

const std::string Store::getCommands() const {
  std::string payload;
  JsonDocument doc;

  if (activeCommands.size() > 0) {
    for (const Command &command : activeCommands) {
      JsonObject obj = doc.add<JsonObject>();
      obj["command"] = command.key;
      obj["unit"] = command.unit;
      obj["active"] = true;
      obj["interval"] = command.interval;
      obj["master"] = command.master;
      obj["position"] = command.position;
      obj["datatype"] = ebus::datatype2string(command.datatype);
      obj["topic"] = command.topic;
      obj["ha"] = command.ha;
      obj["ha_class"] = command.ha_class;
    }
  }

  if (passiveCommands.size() > 0) {
    for (const Command &command : passiveCommands) {
      JsonObject obj = doc.add<JsonObject>();
      obj["command"] = command.key;
      obj["unit"] = command.unit;
      obj["active"] = false;
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
    checkNewCommands();
    checkPubCommands();
  }
}

const bool Store::active() const { return activeCommands.size() > 0; }

Command *Store::nextActiveCommand() {
  Command *command = nullptr;

  if (init) {
    size_t count =
        std::count_if(activeCommands.begin(), activeCommands.end(),
                      [](const Command &cmd) { return cmd.last == 0; });

    if (count == 0) {
      init = false;
    } else {
      command =
          &(*std::find_if(activeCommands.begin(), activeCommands.end(),
                          [](const Command &cmd) { return cmd.last == 0; }));
    }
  } else {
    command = &(*std::min_element(activeCommands.begin(), activeCommands.end(),
                                  [](const Command &lhs, const Command &rhs) {
                                    return (lhs.last + lhs.interval * 1000) <
                                           (rhs.last + rhs.interval * 1000);
                                  }));

    if (millis() < command->last + command->interval * 1000) command = nullptr;
  }

  return command;
}

Command *Store::findPassiveCommand(const std::vector<uint8_t> &master) {
  Command *command = nullptr;

  size_t count =
      std::count_if(passiveCommands.begin(), passiveCommands.end(),
                    [&master](const Command &cmd) {
                      return ebus::Sequence::contains(master, cmd.command);
                    });

  if (count > 0) {
    command =
        &(*std::find_if(passiveCommands.begin(), passiveCommands.end(),
                        [&master](const Command &cmd) {
                          return ebus::Sequence::contains(master, cmd.command);
                        }));
  }

  return command;
}

void Store::loadCommands() {
  Preferences commands;
  commands.begin("commands", true);

  size_t bytes = commands.getBytesLength("ebus");
  if (bytes > 2) {  // 2 = empty json array "[]"
    std::vector<char> buffer(bytes);
    bytes = commands.getBytes("ebus", buffer.data(), bytes);
    if (bytes > 2) {  // loading was successful
      std::string payload(buffer.begin(), buffer.end());

      deserializeCommands(payload.c_str());
      mqttClient.publish("ebus/config/loading", 0, false,
                         String(bytes).c_str());
    } else {
      mqttClient.publish("ebus/config/loading", 0, false, "failed");
    }
  } else {
    mqttClient.publish("ebus/config/loading", 0, false, "no data");
  }

  commands.end();
}

void Store::saveCommands() const {
  Preferences commands;
  commands.begin("commands", false);

  size_t bytes = strlen(serializeCommands().c_str());
  if (bytes > 2) {  // 2 = empty json array "[]"
    bytes = commands.putBytes("ebus", serializeCommands().c_str(), bytes);
    if (bytes > 2)  // saving was successful
      mqttClient.publish("ebus/config/saving", 0, false, String(bytes).c_str());
    else
      mqttClient.publish("ebus/config/saving", 0, false, "failed");
  } else {
    mqttClient.publish("ebus/config/saving", 0, false, "no data");
  }

  commands.end();
}

void Store::wipeCommands() {
  Preferences commands;
  commands.begin("commands", false);

  size_t bytes = commands.getBytesLength("ebus");
  if (bytes > 0) {
    if (commands.remove("ebus"))  // wiping was successful
      mqttClient.publish("ebus/config/wiping", 0, false, String(bytes).c_str());
    else
      mqttClient.publish("ebus/config/wiping", 0, false, "failed");
  } else {
    mqttClient.publish("ebus/config/wiping", 0, false, "no data");
  }

  commands.end();
}

void Store::checkNewCommands() {
  if (newCommands.size() > 0) {
    if (millis() > lastInsert + distanceInsert) {
      std::string payload = newCommands.front();
      newCommands.pop_front();
      insertCommand(payload.c_str());
    }
  }
}

void Store::checkPubCommands() {
  if (pubCommands.size() > 0) {
    if (millis() > lastPublish + distancePublish) {
      std::string payload = pubCommands.front();
      pubCommands.pop_front();
      publishCommand(&activeCommands, payload, false);
      publishCommand(&passiveCommands, payload, false);
    }
  }
}

const std::string Store::serializeCommands() const {
  std::string payload;
  JsonDocument doc;

  if (activeCommands.size() > 0) {
    for (const Command &command : activeCommands) {
      JsonArray arr = doc.add<JsonArray>();
      arr.add(command.key);
      arr.add(command.unit);
      arr.add(true);
      arr.add(command.interval);
      arr.add(command.master);
      arr.add(command.position);
      arr.add(ebus::datatype2string(command.datatype));
      arr.add(command.topic);
      arr.add(command.ha);
      arr.add(command.ha_class);
    }
  }

  if (passiveCommands.size() > 0) {
    for (const Command &command : passiveCommands) {
      JsonArray arr = doc.add<JsonArray>();
      arr.add(command.key);
      arr.add(command.unit);
      arr.add(false);
      arr.add(command.interval);
      arr.add(command.master);
      arr.add(command.position);
      arr.add(ebus::datatype2string(command.datatype));
      arr.add(command.topic);
      arr.add(command.ha);
      arr.add(command.ha_class);
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
    std::string err = "DeserializationError ";
    err += error.c_str();
    mqttClient.publish("ebus/config/error", 0, false, err.c_str());
  } else {
    JsonArray array = doc.as<JsonArray>();
    for (JsonVariant variant : array) {
      JsonDocument tmpDoc;

      tmpDoc["command"] = variant[0];
      tmpDoc["unit"] = variant[1];
      tmpDoc["active"] = variant[2];
      tmpDoc["interval"] = variant[3];
      tmpDoc["master"] = variant[4];
      tmpDoc["position"] = variant[5];
      tmpDoc["datatype"] = variant[6];
      tmpDoc["topic"] = variant[7];
      tmpDoc["ha"] = variant[8];
      tmpDoc["ha_class"] = variant[9];

      std::string tmpPayload;
      serializeJson(tmpDoc, tmpPayload);

      newCommands.push_back(tmpPayload);
    }
  }
}

void Store::publishCommand(const std::vector<Command> *commands,
                           const std::string &key, bool remove) {
  const std::vector<Command>::const_iterator it =
      std::find_if(commands->begin(), commands->end(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (it != commands->end()) {
    std::string topic = "ebus/commands/" + it->key;

    std::string payload;

    if (!remove) {
      JsonDocument doc;

      doc["command"] = it->key;
      doc["unit"] = it->unit;
      doc["active"] = it->active;
      doc["interval"] = it->interval;
      doc["master"] = it->master;
      doc["position"] = it->position;
      doc["datatype"] = ebus::datatype2string(it->datatype);
      doc["topic"] = it->topic;
      doc["ha"] = it->ha;
      doc["ha_class"] = it->ha_class;

      serializeJson(doc, payload);
    }

    mqttClient.publish(topic.c_str(), 0, false, payload.c_str());

    if (remove) {
      topic = "ebus/values/" + it->topic;
      mqttClient.publish(topic.c_str(), 0, false, "");

      publishHomeAssistant(&(*it), true);
    } else {
      publishHomeAssistant(&(*it), !it->ha);
    }
  }
}

void Store::publishHomeAssistant(const Command *command, bool remove) {
  std::string name = command->topic;
  std::replace(name.begin(), name.end(), '/', '_');

  std::string topic = "homeassistant/sensor/ebus/" + name + "/config";

  std::string payload;

  if (!remove) {
    JsonDocument doc;

    doc["name"] = name;
    if (command->ha_class.compare("null") != 0 &&
        command->ha_class.length() > 0)
      doc["device_class"] = command->ha_class;
    doc["state_topic"] = "ebus/values/" + command->topic;
    if (command->unit.compare("null") != 0 && command->unit.length() > 0)
      doc["unit_of_measurement"] = command->unit;
    doc["unique_id"] = command->key;
    doc["value_template"] = "{{value_json.value}}";

    serializeJson(doc, payload);
  }

  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}
