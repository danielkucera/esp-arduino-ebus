#include "mqtt.hpp"

#include <functional>

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

void Mqtt::setUniqueId(const char* id) {
  uniqueId = id;
  rootTopic = "ebus/" + uniqueId + "/";
  topicWill = mqtt.rootTopic + "state/available";
  client.setWill(topicWill.c_str(), 0, true, "offline");
}

void Mqtt::setServer(const char* host, uint16_t port) {
  client.setServer(host, port);
}

void Mqtt::setCredentials(const char* username, const char* password) {
  client.setCredentials(username, password);
}

void Mqtt::setHASupport(const bool enable) { haSupport = enable; }

void Mqtt::connect() { client.connect(); }

bool Mqtt::connected() const { return client.connected(); }

void Mqtt::doLoop() {
#if defined(EBUS_INTERNAL)
  checkInsertCommands();
  checkRemoveCommands();
  checkPublishCommands();
  checkPublishHASensors();
  checkPublishParticipants();
#endif
}

uint16_t Mqtt::publish(const char* topic, uint8_t qos, bool retain,
                       const char* payload, bool prefix) {
  std::string mqttTopic = prefix ? rootTopic + topic : topic;
  return client.publish(mqttTopic.c_str(), qos, retain, payload);
}

void Mqtt::publishResponse(const std::string& id, const std::string& status,
                           const size_t& bytes) {
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

#if defined(EBUS_INTERNAL)
void Mqtt::publishHASensors(const bool remove) {
  for (Command* command : store.getCommands())
    pubHASensors.push_back(std::make_tuple(command, remove));
}

void Mqtt::publishParticipants() {
  for (Participant* participant : schedule.getParticipants())
    pubParticipants.push_back(participant);
}

void Mqtt::publishData(const std::string& id,
                       const std::vector<uint8_t>& master,
                       const std::vector<uint8_t>& slave) {
  std::string payload;
  JsonDocument doc;
  doc["id"] = id;
  doc["master"] = ebus::to_string(master);
  doc["slave"] = ebus::to_string(slave);
  doc.shrinkToFit();
  serializeJson(doc, payload);
  mqtt.publish("response", 0, false, payload.c_str());
}

void Mqtt::publishValue(const Command* command, const JsonDocument& doc) {
  std::string payload;
  serializeJson(doc, payload);

  std::string name = command->topic;
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  std::string topic = "values/" + name;

  mqtt.publish(topic.c_str(), 0, false, payload.c_str());
}
#endif

uint16_t Mqtt::subscribe(const char* topic, uint8_t qos) {
  return client.subscribe(topic, qos);
}

void Mqtt::onConnect(bool sessionPresent) {
  std::string topicRequest = mqtt.rootTopic + "request";
  mqtt.subscribe(topicRequest.c_str(), 0);

  mqtt.publish(mqtt.topicWill.c_str(), 0, true, "online", false);

  mqtt.publishHA();
}

void Mqtt::onMessage(const char* topic, const char* payload,
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
    return;
  }

  std::string id = doc["id"].as<std::string>();
  std::unordered_map<std::string, CommandHandler>::const_iterator it =
      mqtt.commandHandlers.find(id);
  if (it != mqtt.commandHandlers.end()) {
    it->second(doc);  // Call the handler
  } else {
    // Unknown command error handling
    std::string errorPayload;
    JsonDocument errorDoc;
    errorDoc["error"] = "command '" + id + "' not found";
    doc.shrinkToFit();
    serializeJson(errorDoc, errorPayload);
    mqtt.publish("response", 0, false, errorPayload.c_str());
  }
}

void Mqtt::handleRestart(const JsonDocument& doc) { restart(); }

#if defined(EBUS_INTERNAL)
void Mqtt::handleInsert(const JsonDocument& doc) {
  JsonArrayConst commands = doc["commands"].as<JsonArrayConst>();
  if (!commands.isNull()) {
    for (JsonVariantConst command : commands)
      insCommands.push_back(store.createCommand(command));
  }
}

void Mqtt::handleRemove(const JsonDocument& doc) {
  JsonArrayConst keys = doc["keys"].as<JsonArrayConst>();
  if (!keys.isNull()) {
    for (JsonVariantConst key : keys)
      remCommands.push_back(key.as<std::string>());
  }
}

void Mqtt::handlePublish(const JsonDocument& doc) {
  for (Command* command : store.getCommands()) pubCommands.push_back(command);
}

void Mqtt::handleLoad(const JsonDocument& doc) {
  int64_t bytes = store.loadCommands();
  if (bytes > 0)
    mqtt.publishResponse("load", "successful", bytes);
  else if (bytes < 0)
    mqtt.publishResponse("load", "failed");
  else
    mqtt.publishResponse("load", "no data");
}

void Mqtt::handleSave(const JsonDocument& doc) {
  int64_t bytes = store.saveCommands();
  if (bytes > 0)
    mqtt.publishResponse("save", "successful", bytes);
  else if (bytes < 0)
    mqtt.publishResponse("save", "failed");
  else
    mqtt.publishResponse("save", "no data");
}

void Mqtt::handleWipe(const JsonDocument& doc) {
  int64_t bytes = store.wipeCommands();
  if (bytes > 0)
    mqtt.publishResponse("wipe", "successful", bytes);
  else if (bytes < 0)
    mqtt.publishResponse("wipe", "failed");
  else
    mqtt.publishResponse("wipe", "no data");
}

void Mqtt::handleScan(const JsonDocument& doc) {
  boolean full = doc["full"].as<boolean>();
  boolean vendor = doc["vendor"].as<boolean>();
  JsonArrayConst addresses = doc["addresses"].as<JsonArrayConst>();

  if (full)
    schedule.handleScanFull();
  else if (vendor)
    schedule.handleScanVendor();
  else if (addresses.isNull() || addresses.size() == 0)
    schedule.handleScan();
  else
    schedule.handleScanAddresses(addresses);

  mqtt.publishResponse("scan", "initiated");
}

void Mqtt::handleParticipants(const JsonDocument& doc) {
  mqtt.publishParticipants();
}

void Mqtt::handleSend(const JsonDocument& doc) {
  JsonArrayConst commands = doc["commands"].as<JsonArrayConst>();
  if (commands.isNull() || commands.size() == 0)
    mqtt.publishResponse("send", "commands array invalid");
  else
    schedule.handleSend(commands);
}

void Mqtt::handleForward(const JsonDocument& doc) {
  JsonArrayConst filters = doc["filters"].as<JsonArrayConst>();
  if (!filters.isNull()) schedule.handleForwardFilter(filters);
  boolean enable = doc["enable"].as<boolean>();
  schedule.toggleForward(enable);
}

void Mqtt::handleReset(const JsonDocument& doc) {
  schedule.resetCounter();
  schedule.resetTiming();
}

void Mqtt::handleRead(const JsonDocument& doc) {
  std::string key = doc["key"].as<std::string>();
  const Command* command = store.findCommand(key);
  if (command != nullptr) {
    String s = "{\"id\":\"read\",";
    s += store.getValueFullJson(command).substr(1).c_str();  // skip opening {
    publish("response", 0, false, s.c_str());
  } else {
    mqtt.publishResponse("read", "key '" + key + "' not found");
  }
}

void Mqtt::handleWrite(const JsonDocument& doc) {
  std::string key = doc["key"].as<std::string>();
  const Command* command = store.findCommand(key);
  if (command != nullptr) {
    std::vector<uint8_t> valueBytes;
    if (command->numeric) {
      double value = doc["value"].as<double>();
      value = value * command->divider;
      valueBytes = getVectorFromDouble(command, value);
    } else {
      std::string value = doc["value"].as<std::string>();
      valueBytes = getVectorFromString(command, value);
    }
    if (valueBytes.size() > 0) {
      std::vector<uint8_t> writeCmd = command->write_cmd;
      writeCmd.insert(writeCmd.end(), valueBytes.begin(), valueBytes.end());
      schedule.handleSend(writeCmd);
    } else {
      mqtt.publishResponse("write", "invalid value for key '" + key + "'");
    }
  } else {
    mqtt.publishResponse("write", "key '" + key + "' not found");
  }
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
      const Command* command = store.findCommand(key);
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

void Mqtt::publishCommand(const Command* command) {
  std::string topic = "commands/" + command->key;
  std::string payload;
  serializeJson(store.getCommandJson(command), payload);
  publish(topic.c_str(), 0, false, payload.c_str());
}
#endif

void Mqtt::publishHADiagnostic(const char* name, const bool remove,
                               const char* value_template, const bool full) {
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

void Mqtt::publishHAConfigButton(const char* name, const bool remove) {
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

#if defined(EBUS_INTERNAL)
void Mqtt::publishHASensor(const Command* command, const bool remove) {
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

void Mqtt::publishParticipant(const Participant* participant) {
  std::string topic = "participants/" + ebus::to_string(participant->slave);
  std::string payload;
  serializeJson(schedule.getParticipantJson(participant), payload);
  publish(topic.c_str(), 0, false, payload.c_str());
}
#endif
