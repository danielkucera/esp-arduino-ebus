#include "mqtt.hpp"

#include "schedule.hpp"

AsyncMqttClient mqttClient;

void onMqttConnect(bool sessionPresent) {
  mqttClient.subscribe("ebus/config/commands/#", 0);
  mqttClient.subscribe("ebus/config/list", 0);
  mqttClient.subscribe("ebus/config/raw", 0);
  mqttClient.subscribe("ebus/config/filter", 0);
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
      schedule.insertCommand(payload);
    else
      schedule.removeCommand(topic);
  } else if (tmp.equals("ebus/config/list")) {
    schedule.publishCommands();
  } else if (tmp.equals("ebus/config/raw")) {
    schedule.publishRaw(payload);
  } else if (tmp.startsWith("ebus/config/filter")) {
    if (String(payload).length() > 0) schedule.handleFilter(payload);
  }
}

void onMqttPublish(uint16_t packetId) {}
