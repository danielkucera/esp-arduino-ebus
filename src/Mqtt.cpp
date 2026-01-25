#if defined(EBUS_INTERNAL)
#include "Mqtt.hpp"

#include <functional>

#include "MqttHA.hpp"
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
  willTopic = mqtt.rootTopic + "available";
  client.setWill(willTopic.c_str(), 0, true, "{ \"value\": \"offline\" }");
}

const std::string& Mqtt::getUniqueId() const { return uniqueId; }

const std::string& Mqtt::getRootTopic() const { return rootTopic; }

const std::string& Mqtt::getWillTopic() const { return willTopic; }

void Mqtt::setServer(const char* host, uint16_t port) {
  client.setServer(host, port);
}

void Mqtt::setCredentials(const char* username, const char* password) {
  client.setCredentials(username, password);
}

void Mqtt::setEnabled(const bool enable) { enabled = enable; }

const bool Mqtt::isEnabled() const { return enabled; }

void Mqtt::connect() { client.connect(); }

const bool Mqtt::connected() const { return client.connected(); }

void Mqtt::disconnect() { client.disconnect(); }

uint16_t Mqtt::publish(const char* topic, uint8_t qos, bool retain,
                       const char* payload, bool prefix) {
  if (!enabled) return 0;

  std::string mqttTopic = prefix ? rootTopic + topic : topic;
  return client.publish(mqttTopic.c_str(), qos, retain, payload);
}

void Mqtt::enqueueOutgoing(const OutgoingAction& action) {
  if (!mqtt.enabled) return;

  mqtt.outgoingQueue.push(action);
}

void Mqtt::publishData(const std::string& id,
                       const std::vector<uint8_t>& master,
                       const std::vector<uint8_t>& slave) {
  if (!mqtt.enabled) return;

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
  if (!mqtt.enabled) return;

  std::string payload;
  serializeJson(doc, payload);

  std::string name = command->name;
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  std::string topic = "values/" + name;

  mqtt.publish(topic.c_str(), 0, false, payload.c_str());
}

void Mqtt::doLoop() {
  checkIncomingQueue();
  checkOutgoingQueue();
}

uint16_t Mqtt::subscribe(const char* topic, uint8_t qos) {
  return client.subscribe(topic, qos);
}

void Mqtt::onConnect(bool sessionPresent) {
  std::string topicRequest = mqtt.rootTopic + "request";
  mqtt.subscribe(topicRequest.c_str(), 0);

  mqtt.publish(mqtt.willTopic.c_str(), 0, true, "{ \"value\": \"online\" }",
               false);

  if (mqttha.isEnabled()) mqttha.publishDeviceInfo();
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
    errorDoc.shrinkToFit();
    serializeJson(errorDoc, errorPayload);
    mqtt.publish("response", 0, false, errorPayload.c_str());
    return;
  }

  std::string id = doc["id"].as<std::string>();
  auto it = mqtt.commandHandlers.find(id);
  if (it != mqtt.commandHandlers.end()) {
    it->second(doc);  // Call the handler
  } else {
    // Unknown command error handling
    std::string errorPayload;
    JsonDocument errorDoc;
    errorDoc["error"] = "command '" + id + "' not found";
    errorDoc.shrinkToFit();
    serializeJson(errorDoc, errorPayload);
    mqtt.publish("response", 0, false, errorPayload.c_str());
  }
}

void Mqtt::handleRestart(const JsonDocument& doc) { restart(); }

void Mqtt::handleInsert(const JsonDocument& doc) {
  JsonArrayConst commands = doc["commands"].as<JsonArrayConst>();
  if (!commands.isNull()) {
    for (JsonVariantConst command : commands) {
      std::string evalError = store.evaluateCommand(command);
      if (evalError.empty()) {
        incomingQueue.push(IncomingAction(store.createCommand(command)));
      } else {
        std::string errorPayload;
        JsonDocument errorDoc;
        errorDoc["error"] = evalError;
        errorDoc.shrinkToFit();
        serializeJson(errorDoc, errorPayload);
        mqtt.publish("response", 0, false, errorPayload.c_str());
      }
    }
  }
}

void Mqtt::handleRemove(const JsonDocument& doc) {
  JsonArrayConst keys = doc["keys"].as<JsonArrayConst>();
  if (!keys.isNull()) {
    for (JsonVariantConst key : keys)
      incomingQueue.push(IncomingAction(key.as<std::string>()));
  } else {
    for (const Command* command : store.getCommands())
      incomingQueue.push(IncomingAction(command->key));
  }
}

void Mqtt::handlePublish(const JsonDocument& doc) {
  for (const Command* command : store.getCommands())
    enqueueOutgoing(OutgoingAction(command));
}

void Mqtt::handleLoad(const JsonDocument& doc) {
  int64_t bytes = store.loadCommands();
  if (bytes > 0)
    mqtt.publishResponse("load", "successful", bytes);
  else if (bytes < 0)
    mqtt.publishResponse("load", "failed");
  else
    mqtt.publishResponse("load", "no data");

  if (mqttha.isEnabled()) mqttha.publishComponents();
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

void Mqtt::handleDevices(const JsonDocument& doc) {
  for (const Device* device : schedule.getDevices())
    enqueueOutgoing(OutgoingAction(device));
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
  Command* command = store.findCommand(key);
  if (command != nullptr) {
    std::vector<uint8_t> valueBytes;
    if (command->numeric) {
      double value = doc["value"].as<double>();
      valueBytes = getVectorFromDouble(command, value);
    } else {
      std::string value = doc["value"].as<std::string>();
      valueBytes = getVectorFromString(command, value);
    }
    if (valueBytes.size() > 0) {
      std::vector<uint8_t> writeCmd = command->write_cmd;
      writeCmd.insert(writeCmd.end(), valueBytes.begin(), valueBytes.end());
      schedule.handleWrite(writeCmd);
      mqtt.publishResponse("write", "scheduled for key '" + key + "' name '" +
                                        command->name + "'");
      command->last = 0;  // force immediate update
    } else {
      mqtt.publishResponse("write", "invalid value for key '" + key + "'");
    }
  } else {
    mqtt.publishResponse("write", "key '" + key + "' not found");
  }
}

void Mqtt::checkIncomingQueue() {
  if (!incomingQueue.empty() && millis() > lastIncoming + incomingInterval) {
    lastIncoming = millis();
    IncomingAction action = incomingQueue.front();
    incomingQueue.pop();

    switch (action.type) {
      case IncomingActionType::Insert:
        store.insertCommand(action.command);
        if (mqttha.isEnabled()) mqttha.publishComponent(&action.command, false);
        publishResponse("insert", "key '" + action.command.key + "' inserted");
        break;
      case IncomingActionType::Remove:
        const Command* cmd = store.findCommand(action.key);
        if (cmd) {
          if (mqttha.isEnabled()) mqttha.publishComponent(cmd, true);
          store.removeCommand(action.key);
          publishResponse("remove", "key '" + action.key + "' removed");
        } else {
          publishResponse("remove", "key '" + action.key + "' not found");
        }
        break;
    }
  }
}

void Mqtt::checkOutgoingQueue() {
  if (!outgoingQueue.empty() && millis() > lastOutgoing + outgoingInterval) {
    lastOutgoing = millis();
    OutgoingAction action = outgoingQueue.front();
    outgoingQueue.pop();

    switch (action.type) {
      case OutgoingActionType::Command:
        publishCommand(action.command);
        break;
      case OutgoingActionType::Device:
        publishDevice(action.device);
        break;
      case OutgoingActionType::Component:
        mqttha.publishComponent(action.command, action.haRemove);
        break;
    }
  }
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

void Mqtt::publishCommand(const Command* command) {
  std::string topic = "commands/" + command->key;
  std::string payload;
  serializeJson(store.getCommandJsonDoc(command), payload);
  publish(topic.c_str(), 0, false, payload.c_str());
}

void Mqtt::publishDevice(const Device* device) {
  std::string topic = "devices/" + ebus::to_string(device->slave);
  std::string payload;
  serializeJson(schedule.getDeviceJsonDoc(device), payload);
  publish(topic.c_str(), 0, false, payload.c_str());
}

#endif
