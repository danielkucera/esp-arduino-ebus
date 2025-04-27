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

void Mqtt::setWill(const char *topic, uint8_t qos, bool retain,
                   const char *payload, size_t length) {
  client.setWill(topic, qos, retain, payload, length);
}

void Mqtt::connect() { client.connect(); }

bool Mqtt::connected() const { return client.connected(); }

uint16_t Mqtt::publish(const char *topic, uint8_t qos, bool retain,
                       const char *payload, bool prefix) {
  std::string mqttTopic = topic;
  if (prefix) mqttTopic = rootTopic + topic;
  return client.publish(mqttTopic.c_str(), qos, retain, payload);
}

void Mqtt::setHASupport(const bool enable) { haSupport = enable; }

const bool Mqtt::getHASupport() const { return haSupport; }

void Mqtt::publisHA(const bool remove) {
  mqtt.publishHADiagnostic("Uptime", remove,
                           "{{timedelta(seconds=((value|float)/1000)|int)}}",
                           true);

  mqtt.publishHAConfigButton("Restart", remove);
}

uint16_t Mqtt::subscribe(const char *topic, uint8_t qos) {
  return client.subscribe(topic, qos);
}

void Mqtt::onConnect(bool sessionPresent) {
  std::string topicRequest = mqtt.getRootTopic() + "request";
  mqtt.subscribe(topicRequest.c_str(), 0);

  std::string topicWill = mqtt.getRootTopic() + "state/available";
  mqtt.publish(topicWill.c_str(), 0, true, "online", false);
  mqtt.setWill(topicWill.c_str(), 0, true, "offline");

  mqtt.publisHA(false);
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
      if (value) restart();
    }
#ifdef EBUS_INTERNAL
    else if (id.compare("insert") == 0) {  // NOLINT
      JsonArray commands = doc["commands"].as<JsonArray>();
      if (commands != nullptr) store.insertCommands(commands);
    } else if (id.compare("remove") == 0) {
      JsonArray keys = doc["keys"].as<JsonArray>();
      if (keys != nullptr) store.removeCommands(keys);
    } else if (id.compare("publish") == 0) {
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
    else {  // NOLINT
      std::string errorPayload;
      JsonDocument errorDoc;
      errorDoc["error"] = "command '" + id + "' not found";
      serializeJson(errorDoc, errorPayload);
      mqtt.publish("response", 0, false, errorPayload.c_str());
    }
  }
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

    serializeJson(doc, payload);
  }

  if (remove || haSupport) {
    std::string topic =
        "homeassistant/sensor/ebus" + uniqueId + '/' + lowerName + "/config";
    mqtt.publish(topic.c_str(), 0, true, payload.c_str(), false);
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

    serializeJson(doc, payload);
  }

  if (remove || haSupport) {
    std::string topic =
        "homeassistant/button/ebus" + uniqueId + '/' + lowerName + "/config";
    mqtt.publish(topic.c_str(), 0, true, payload.c_str(), false);
  }
}
