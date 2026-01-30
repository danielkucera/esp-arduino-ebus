#if defined(EBUS_INTERNAL)
#include "Mqtt.hpp"

#include <functional>

#include "Logger.hpp"
#include "MqttHA.hpp"
#include "main.hpp"

Mqtt mqtt;

void Mqtt::start() {
  mqtt_cfg.client_id = clientId.c_str();
  // Last Will
  mqtt_cfg.lwt_topic = willTopic.c_str();
  mqtt_cfg.lwt_msg = "{ \"value\": \"offline\" }";
  mqtt_cfg.lwt_qos = 1;
  mqtt_cfg.lwt_retain = 1;
  // Keep-alive interval in seconds
  mqtt_cfg.keepalive = 60;

  client = esp_mqtt_client_init(&mqtt_cfg);

  esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                 &Mqtt::eventHandler, this);

  esp_mqtt_client_start(client);
}

void Mqtt::setUniqueId(const char* id) {
  uniqueId = id;
  clientId = "ebus-" + uniqueId;
  rootTopic = "ebus/" + uniqueId + "/";
  willTopic = mqtt.rootTopic + "available";
  requestTopic = mqtt.rootTopic + "request";
}

const std::string& Mqtt::getUniqueId() const { return uniqueId; }

const std::string& Mqtt::getRootTopic() const { return rootTopic; }

const std::string& Mqtt::getWillTopic() const { return willTopic; }

void Mqtt::setServer(const char* host, uint16_t port) {
  std::string hostname;
  for (size_t i = 0; host[i] != '\0'; ++i)
    if (!std::isspace(host[i])) hostname += host[i];

  uri = "mqtt://" + hostname;
  if (port > 0) uri += ":" + std::to_string(port);

  mqtt_cfg.uri = uri.c_str();
}

void Mqtt::setCredentials(const char* username, const char* password) {
  mqtt_cfg.username = username;
  mqtt_cfg.password = password;
}

void Mqtt::setEnabled(const bool enable) { enabled = enable; }

const bool Mqtt::isEnabled() const { return enabled; }

void Mqtt::connect() { esp_mqtt_client_reconnect(client); }

const bool Mqtt::connected() const { return online; }

void Mqtt::disconnect() { esp_mqtt_client_disconnect(client); }

void Mqtt::publish(const char* topic, uint8_t qos, bool retain,
                   const char* payload, bool prefix) {
  if (!enabled) return;

  std::string mqttTopic = prefix ? rootTopic + topic : topic;
  esp_mqtt_client_publish(client, mqttTopic.c_str(), payload, 0, qos, retain);
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

void Mqtt::publishValue(const Command* command) {
  if (!mqtt.enabled) return;

  std::string payload;
  serializeJson(command->getValueJsonDoc(), payload);

  std::string name = command->getName();
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  std::string topic = "values/" + name;

  mqtt.publish(topic.c_str(), 0, false, payload.c_str());
}

void Mqtt::doLoop() {
  checkIncomingQueue();
  checkOutgoingQueue();
}

void Mqtt::eventHandler(void* handler_args, esp_event_base_t base,
                        int32_t event_id, void* event_data) {
  Mqtt* self = static_cast<Mqtt*>(handler_args);
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_BEFORE_CONNECT: {
      logger.debug("MQTT before connect");
    } break;
    case MQTT_EVENT_CONNECTED: {
      logger.debug("MQTT connected");
      self->online = true;
      esp_mqtt_client_subscribe(self->client, self->requestTopic.c_str(), 0);

      mqtt.publish(mqtt.willTopic.c_str(), 0, true, "{ \"value\": \"online\" }",
                   false);

      if (mqttha.isEnabled()) mqttha.publishDeviceInfo();
    } break;
    case MQTT_EVENT_DISCONNECTED: {
      logger.debug("MQTT disconnected");
      self->online = false;
    } break;
    case MQTT_EVENT_SUBSCRIBED: {
      logger.debug(String(self->requestTopic.c_str()) + " subscribed");
    } break;
    case MQTT_EVENT_UNSUBSCRIBED: {
    } break;
    case MQTT_EVENT_PUBLISHED: {
    } break;
    case MQTT_EVENT_DATA: {
      logger.debug("MQTT data received");
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, event->data);

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
    } break;
    case MQTT_EVENT_DELETED: {
    } break;
    case MQTT_EVENT_ERROR: {
      logger.error("MQTT Error occured");
    } break;
    default: {
      logger.warn("Unhandled event id:" + event->event_id);
    } break;
  }
}

void Mqtt::handleRestart(const JsonDocument& doc) { restart(); }

void Mqtt::handleInsert(const JsonDocument& doc) {
  JsonArrayConst commands = doc["commands"].as<JsonArrayConst>();
  if (!commands.isNull()) {
    for (JsonVariantConst command : commands) {
      std::string evalError = Command::evaluate(command);
      if (evalError.empty()) {
        incomingQueue.push(IncomingAction(Command::fromJson(command)));
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
      incomingQueue.push(IncomingAction(command->getKey()));
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
    std::vector<uint8_t> valueBytes = command->getVector(doc);
    if (valueBytes.size() > 0) {
      std::vector<uint8_t> writeCmd = command->getWriteCmd();
      writeCmd.insert(writeCmd.end(), valueBytes.begin(), valueBytes.end());
      schedule.handleWrite(writeCmd);
      mqtt.publishResponse("write", "scheduled for key '" + key + "' name '" +
                                        command->getName() + "'");
      command->setLast(0);  // force immediate update
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
        publishResponse("insert",
                        "key '" + action.command.getKey() + "' inserted");
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
  std::string topic = "commands/" + command->getKey();
  std::string payload;
  serializeJson(command->toJson(), payload);
  publish(topic.c_str(), 0, false, payload.c_str());
}

void Mqtt::publishDevice(const Device* device) {
  std::string topic = "devices/" + ebus::to_string(device->slave);
  std::string payload;
  serializeJson(schedule.getDeviceJsonDoc(device), payload);
  publish(topic.c_str(), 0, false, payload.c_str());
}

#endif
