#include "mqtt.hpp"

#include <ArduinoJson.h>

#include "main.hpp"
#include "schedule.hpp"
#include "store.hpp"

Mqtt mqtt;

Mqtt::Mqtt() {
  client.onConnect(onConnect);
  client.onDisconnect(onDisconnect);
  client.onSubscribe(onSubscribe);
  client.onUnsubscribe(onUnsubscribe);
  client.onMessage(onMessage);
  client.onPublish(onPublish);
}

void Mqtt::setUniqueId(const char *id) {
  uniqueId = id;
  rootTopic = "ebus/" + uniqueId + "/";
}

const std::string &Mqtt::getUniqueId() const { return uniqueId; }

const std::string &Mqtt::getRootTopic() const { return rootTopic; }

void Mqtt::setServer(const char *host, uint16_t port) {
  client.setServer(host, port);
}

void Mqtt::setCredentials(const char *username, const char *password) {
  client.setCredentials(username, password);
}

void Mqtt::connect() { client.connect(); }

bool Mqtt::connected() const { return client.connected(); }

uint16_t Mqtt::publish(const char *topic, uint8_t qos, bool retain,
                       const char *payload, bool prefix) {
  std::string mqttTopic = topic;
  if (prefix) mqttTopic = rootTopic + topic;
  return client.publish(mqttTopic.c_str(), qos, retain, payload);
}

uint16_t Mqtt::subscribe(const char *topic, uint8_t qos) {
  return client.subscribe(topic, qos);
}

void Mqtt::onConnect(bool sessionPresent) {
  std::string topic = mqtt.getRootTopic() + "request";
  mqtt.subscribe(topic.c_str(), 0);
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
    serializeJson(errorDoc, errorPayload);
    mqtt.publish("response", 0, false, errorPayload.c_str());
  } else {
    std::string id = doc["id"].as<std::string>();
    if (id.compare("restart") == 0) {
      boolean value = doc["value"].as<boolean>();
      if (value) reset();
    }
#ifdef EBUS_INTERNAL
    if (id.compare("insert") == 0) {
      JsonArray commands = doc["commands"].as<JsonArray>();
      if (commands != nullptr) store.insertCommands(commands);
    } else if (id.compare("remove") == 0) {
      JsonArray keys = doc["keys"].as<JsonArray>();
      if (keys != nullptr) store.removeCommands(keys);
    } else if (id.compare("list") == 0) {
      boolean value = doc["value"].as<boolean>();
      if (value) store.publishCommands();
    } else if (id.compare("load") == 0) {
      boolean value = doc["value"].as<boolean>();
      if (value) store.loadCommands();
    } else if (id.compare("save") == 0) {
      boolean value = doc["value"].as<boolean>();
      if (value) store.saveCommands();
    } else if (id.compare("wipe") == 0) {
      boolean value = doc["value"].as<boolean>();
      if (value) store.wipeCommands();
    } else if (id.compare("send") == 0) {
      JsonArray commands = doc["commands"].as<JsonArray>();
      if (commands != nullptr) schedule.handleSend(commands);
    } else if (id.compare("forward") == 0) {
      JsonArray filters = doc["filters"].as<JsonArray>();
      if (filters != nullptr) schedule.handleForwadFilter(filters);
      boolean value = doc["value"].as<boolean>();
      schedule.toggleForward(value);
    }
#endif
  }
}
