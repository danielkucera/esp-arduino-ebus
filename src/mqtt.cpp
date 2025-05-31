#include "mqtt.hpp"

#include <ArduinoJson.h>

#include "main.hpp"

Mqtt mqtt;

Mqtt::Mqtt() {
  client.onConnect(onConnect);
  client.onDisconnect(onDisconnect);
  client.onSubscribe(onSubscribe);
  client.onUnsubscribe(onUnsubscribe);
  client.onMessage(onMessage);
  client.onPublish(onPublish);
}

void Mqtt::setServer(const char *host, uint16_t port) {
  client.setServer(host, port);
}

void Mqtt::setCredentials(const char *username, const char *password) {
  client.setCredentials(username, password);
}

void Mqtt::setUniqueId(const char *id) {
  uniqueId = id;
  rootTopic = "ebus/" + uniqueId + "/";
}

void Mqtt::setHASupport(const bool enable) { haSupport = enable; }

void Mqtt::connect() { client.connect(); }

bool Mqtt::connected() const { return client.connected(); }

void Mqtt::doLoop() {
  checkInsertCommands();
  checkRemoveCommands();
  checkPublishCommands();
  checkPublishHASensors();
  checkPublishParticipants();
}

uint16_t Mqtt::publish(const char *topic, uint8_t qos, bool retain,
                       const char *payload, bool prefix) {
  std::string mqttTopic = topic;
  if (prefix) mqttTopic = rootTopic + topic;
  return client.publish(mqttTopic.c_str(), qos, retain, payload);
}

void Mqtt::publishResponse(const std::string &id, const std::string &status,
                           const size_t &bytes) {
  std::string payload;
  JsonDocument doc;
  doc["id"] = id;
  doc["status"] = status;
  if (bytes > 0) doc["bytes"] = bytes;
  doc.shrinkToFit();
  serializeJson(doc, payload);
  publish("response", 0, false, payload.c_str());
}

void Mqtt::publishHA() const {
  mqtt.publishHADiagnostic("Uptime", !haSupport,
                           "{{timedelta(seconds=((value|float)/1000)|int)}}",
                           true);

  mqtt.publishHAConfigButton("Restart", !haSupport);
}

void Mqtt::publishCommands() {
  for (Command *command : store.getCommands()) pubCommands.push_back(command);
}

void Mqtt::publishHASensors(const bool remove) {
  for (Command *command : store.getCommands())
    pubHASensors.push_back(std::make_tuple(command, remove));
}

void Mqtt::publishParticipants() {
  for (Participant *participant : schedule.getParticipants())
    pubParticipants.push_back(participant);
}

void Mqtt::publishData(const std::string &id,
                       const std::vector<uint8_t> &master,
                       const std::vector<uint8_t> &slave) {
  std::string payload;
  JsonDocument doc;
  doc["id"] = id;
  doc["master"] = ebus::to_string(master);
  doc["slave"] = ebus::to_string(slave);
  doc.shrinkToFit();
  serializeJson(doc, payload);
  mqtt.publish("response", 0, false, payload.c_str());
}

void Mqtt::publishValue(Command *command, const JsonDocument &doc) {
  command->last = millis();

  std::string payload;
  serializeJson(doc, payload);

  std::string name = command->topic;
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  std::string topic = "values/" + name;

  mqtt.publish(topic.c_str(), 0, false, payload.c_str());
}

void Mqtt::setWill(const char *topic, uint8_t qos, bool retain,
                   const char *payload, size_t length) {
  client.setWill(topic, qos, retain, payload, length);
}

uint16_t Mqtt::subscribe(const char *topic, uint8_t qos) {
  return client.subscribe(topic, qos);
}

void Mqtt::onConnect(bool sessionPresent) {
  std::string topicRequest = mqtt.rootTopic + "request";
  mqtt.subscribe(topicRequest.c_str(), 0);

  std::string topicWill = mqtt.rootTopic + "state/available";
  mqtt.publish(topicWill.c_str(), 0, true, "online", false);
  mqtt.setWill(topicWill.c_str(), 0, true, "offline");

  mqtt.publishHA();
}

void Mqtt::onMessage(const char *topic, const char *payload,
                     AsyncMqttClientMessageProperties properties, size_t len,
                     size_t index, size_t total) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    std::string errorPayload;
    JsonDocument errorDoc;
    errorDoc["error"] = error.c_str();
    doc.shrinkToFit();
    serializeJson(errorDoc, errorPayload);
    mqtt.publish("response", 0, false, errorPayload.c_str());
  } else {
    std::string id = doc["id"].as<std::string>();
    if (id.compare("restart") == 0) {
      boolean value = doc["value"].as<boolean>();
      if (value) restart();
    }
#ifdef EBUS_INTERNAL
    else if (id.compare("insert") == 0) {  // NOLINT
      JsonArray commands = doc["commands"].as<JsonArray>();
      if (commands != nullptr) mqtt.insertCommands(commands);
    } else if (id.compare("remove") == 0) {
      JsonArray keys = doc["keys"].as<JsonArray>();
      if (keys != nullptr) mqtt.removeCommands(keys);
    } else if (id.compare("publish") == 0) {
      boolean value = doc["value"].as<boolean>();
      if (value) mqtt.publishCommands();
    } else if (id.compare("load") == 0) {
      boolean value = doc["value"].as<boolean>();
      if (value) loadCommands();
    } else if (id.compare("save") == 0) {
      boolean value = doc["value"].as<boolean>();
      if (value) saveCommands();
    } else if (id.compare("wipe") == 0) {
      boolean value = doc["value"].as<boolean>();
      if (value) wipeCommands();
    } else if (id.compare("scan") == 0) {
      boolean full = doc["full"].as<boolean>();
      JsonArray addresses = doc["addresses"].as<JsonArray>();
      mqtt.initScan(full, addresses);
    } else if (id.compare("participants") == 0) {
      boolean value = doc["value"].as<boolean>();
      if (value) mqtt.publishParticipants();
    } else if (id.compare("send") == 0) {
      JsonArray commands = doc["commands"].as<JsonArray>();
      if (commands.isNull() || commands.size() == 0)
        mqtt.publishResponse("send", "commands array invalid");
      else
        schedule.handleSend(commands);
    } else if (id.compare("forward") == 0) {
      JsonArray filters = doc["filters"].as<JsonArray>();
      if (!filters.isNull()) schedule.handleForwadFilter(filters);
      boolean value = doc["value"].as<boolean>();
      schedule.toggleForward(value);
    }
#endif
    else {  // NOLINT
      std::string errorPayload;
      JsonDocument errorDoc;
      errorDoc["error"] = "command '" + id + "' not found";
      doc.shrinkToFit();
      serializeJson(errorDoc, errorPayload);
      mqtt.publish("response", 0, false, errorPayload.c_str());
    }
  }
}

void Mqtt::insertCommands(const JsonArray &commands) {
  for (JsonVariant command : commands)
    insCommands.push_back(store.createCommand(command));
}

void Mqtt::removeCommands(const JsonArray &keys) {
  for (JsonVariant key : keys) remCommands.push_back(key);
}

void Mqtt::checkInsertCommands() {
  if (insCommands.size() > 0) {
    if (millis() > lastInsert + distanceInsert) {
      lastInsert = millis();
      Command command = insCommands.front();
      insCommands.pop_front();
      store.insertCommand(command);
      if (haSupport) mqtt.publishHASensor(&command, false);
      mqtt.publishResponse("insert", "key '" + command.key + "' inserted");
    }
  }
}

void Mqtt::checkRemoveCommands() {
  if (remCommands.size() > 0) {
    if (millis() > lastRemove + distanceRemove) {
      lastRemove = millis();
      std::string key = remCommands.front();
      remCommands.pop_front();
      const Command *command = store.findCommand(key);
      if (command != nullptr) {
        if (haSupport) mqtt.publishHASensor(command, true);
        store.removeCommand(key);
        mqtt.publishResponse("remove", "key '" + key + "' removed");
      } else {
        mqtt.publishResponse("remove", "key '" + key + "' not found");
      }
    }
  }
}

void Mqtt::checkPublishCommands() {
  if (pubCommands.size() > 0) {
    if (millis() > lastPublish + distancePublish) {
      lastPublish = millis();
      mqtt.publishCommand(pubCommands.front());
      pubCommands.pop_front();
    }
  }
}

void Mqtt::checkPublishHASensors() {
  if (pubHASensors.size() > 0) {
    if (millis() > lastHASensors + distanceHASensors) {
      lastHASensors = millis();
      mqtt.publishHASensor(std::get<0>(pubHASensors.front()),
                           std::get<1>(pubHASensors.front()));
      pubHASensors.pop_front();
    }
  }
}

void Mqtt::checkPublishParticipants() {
  if (pubParticipants.size() > 0) {
    if (millis() > lastParticipants + distanceParticipants) {
      lastParticipants = millis();
      mqtt.publishParticipant(pubParticipants.front());
      pubParticipants.pop_front();
    }
  }
}

void Mqtt::loadCommands() {
  int64_t bytes = store.loadCommands();
  if (bytes > 0)
    mqtt.publishResponse("load", "successful", bytes);
  else if (bytes < 0)
    mqtt.publishResponse("load", "failed");
  else
    mqtt.publishResponse("load", "no data");
}

void Mqtt::saveCommands() {
  int64_t bytes = store.saveCommands();
  if (bytes > 0)
    mqtt.publishResponse("save", "successful", bytes);
  else if (bytes < 0)
    mqtt.publishResponse("save", "failed");
  else
    mqtt.publishResponse("save", "no data");
}

void Mqtt::wipeCommands() {
  int64_t bytes = store.wipeCommands();
  if (bytes > 0)
    mqtt.publishResponse("wipe", "successful", bytes);
  else if (bytes < 0)
    mqtt.publishResponse("wipe", "failed");
  else
    mqtt.publishResponse("wipe", "no data");
}

void Mqtt::initScan(const bool full, const JsonArray &addresses) {
  if (full)
    schedule.handleScanFull();
  else if (addresses.isNull() || addresses.size() == 0)
    schedule.handleScanSeen();
  else
    schedule.handleScanAddresses(addresses);

  mqtt.publishResponse("scan", "initiated");
}

void Mqtt::publishCommand(const Command *command) {
  std::string topic = "commands/" + command->key;
  std::string payload;
  serializeJson(store.getCommandJson(command), payload);
  publish(topic.c_str(), 0, false, payload.c_str());
}

void Mqtt::publishHADiagnostic(const char *name, const bool remove,
                               const char *value_template, const bool full) {
  std::string lowerName = name;
  std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  std::string payload;

  if (haSupport) {
    JsonDocument doc;

    doc["name"] = name;
    doc["entity_category"] = "diagnostic";
    doc["unique_id"] = "ebus" + uniqueId + '_' + lowerName;
    doc["state_topic"] = rootTopic + std::string("state/") + lowerName;
    doc["value_template"] = value_template;

    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"] = "ebus" + uniqueId;
    if (full) {
      device["name"] = "esp-ebus";
      device["manufacturer"] = "";  // TODO(yuhu-): fill with thing data
      device["model"] = "";         // TODO(yuhu-): fill with thing data
      device["model_id"] = "";      // TODO(yuhu-): fill with thing data
      device["serial_number"] = uniqueId;
      device["hw_version"] = "";  // TODO(yuhu-): fill with thing data
      device["sw_version"] = "";  // TODO(yuhu-): fill with thing data
      device["configuration_url"] = "http://esp-ebus.local";
    }

    doc.shrinkToFit();
    serializeJson(doc, payload);
  }

  if (remove || haSupport) {
    std::string topic =
        "homeassistant/sensor/ebus" + uniqueId + '/' + lowerName + "/config";
    publish(topic.c_str(), 0, true, payload.c_str(), false);
  }
}

void Mqtt::publishHAConfigButton(const char *name, const bool remove) {
  std::string lowerName = name;
  std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  std::string payload;

  if (haSupport) {
    JsonDocument doc;

    doc["name"] = name;
    doc["entity_category"] = "config";
    doc["unique_id"] = "ebus" + uniqueId + '_' + lowerName;
    doc["availability_topic"] = rootTopic + std::string("state/available");
    doc["command_topic"] = rootTopic + "request";
    doc["payload_press"] = "{\"id\":\"" + lowerName + "\",\"value\":true}";
    doc["qos"] = 0;
    doc["retain"] = false;

    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"] = "ebus" + uniqueId;

    doc.shrinkToFit();
    serializeJson(doc, payload);
  }

  if (remove || haSupport) {
    std::string topic =
        "homeassistant/button/ebus" + uniqueId + '/' + lowerName + "/config";
    publish(topic.c_str(), 0, true, payload.c_str(), false);
  }
}

void Mqtt::publishHASensor(const Command *command, const bool remove) {
  std::string stateTopic = command->topic;
  std::transform(stateTopic.begin(), stateTopic.end(), stateTopic.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  std::string subTopic = stateTopic;
  std::replace(subTopic.begin(), subTopic.end(), '/', '_');

  std::string topic =
      "homeassistant/sensor/ebus" + uniqueId + '/' + subTopic + "/config";

  std::string payload;

  if (command->ha && haSupport && !remove) {
    JsonDocument doc;

    std::string name = command->topic;
    std::replace(name.begin(), name.end(), '/', ' ');
    std::replace(name.begin(), name.end(), '_', ' ');

    doc["name"] = name;
    if (command->ha_class.compare("null") != 0 &&
        command->ha_class.length() > 0)
      doc["device_class"] = command->ha_class;
    doc["state_topic"] = rootTopic + std::string("values/") + stateTopic;
    if (command->unit.compare("null") != 0 && command->unit.length() > 0)
      doc["unit_of_measurement"] = command->unit;
    doc["unique_id"] = "ebus" + uniqueId + '_' + command->key;
    doc["value_template"] = "{{value_json.value}}";

    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"] = "ebus" + uniqueId;

    doc.shrinkToFit();
    serializeJson(doc, payload);
  }

  if (remove || haSupport)
    publish(topic.c_str(), 0, true, payload.c_str(), false);
}

void Mqtt::publishParticipant(const Participant *participant) {
  std::string topic = "participants/" + ebus::to_string(participant->slave);
  std::string payload;
  serializeJson(schedule.getParticipantJson(participant), payload);
  publish(topic.c_str(), 0, false, payload.c_str());
}
