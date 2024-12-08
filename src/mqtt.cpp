#include "mqtt.hpp"

#include "main.hpp"
#include "schedule.hpp"

AsyncMqttClient mqttClient;

void onMqttConnect(bool sessionPresent) {
  mqttClient.subscribe("ebus/config/commands/#", 0);
  mqttClient.subscribe("ebus/config/list", 0);
  mqttClient.subscribe("ebus/config/raw", 0);
  mqttClient.subscribe("ebus/config/filter", 0);
  mqttClient.subscribe("ebus/config/reset", 0);
  mqttClient.subscribe("ebus/config/load", 0);
  mqttClient.subscribe("ebus/config/save", 0);
  mqttClient.subscribe("ebus/config/wipe", 0);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {}

void onMqttUnsubscribe(uint16_t packetId) {}

void onMqttMessage(char *topic, char *payload,
                   AsyncMqttClientMessageProperties properties, size_t len,
                   size_t index, size_t total) {
  String tmp = String(topic);
  if (tmp.startsWith("ebus/config/commands/")) {
    if (String(payload).length() > 0)
      schedule.enqueCommand(payload);
    else
      schedule.removeCommand(topic);
  } else if (tmp.equals("ebus/config/list")) {
    schedule.publishCommands();
  } else if (tmp.equals("ebus/config/raw")) {
    schedule.publishRaw(payload);
  } else if (tmp.equals("ebus/config/filter")) {
    if (String(payload).length() > 0) schedule.handleFilter(payload);
  } else if (tmp.equals("ebus/config/reset")) {
    if (String(payload).equalsIgnoreCase("true")) reset();
  } else if (tmp.equals("ebus/config/load")) {
    if (String(payload).equalsIgnoreCase("true")) loadCommands();
  } else if (tmp.equals("ebus/config/save")) {
    if (String(payload).equalsIgnoreCase("true")) saveCommands();
  } else if (tmp.equals("ebus/config/wipe")) {
    if (String(payload).equalsIgnoreCase("true")) wipeCommands();
  }
}

void onMqttPublish(uint16_t packetId) {}
