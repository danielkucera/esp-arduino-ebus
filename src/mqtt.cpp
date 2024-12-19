#include "mqtt.hpp"

AsyncMqttClient mqttClient;

void onMqttConnect(bool sessionPresent) {}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {}

void onMqttUnsubscribe(uint16_t packetId) {}

void onMqttMessage(const char *topic, const char *payload,
                   AsyncMqttClientMessageProperties properties, size_t len,
                   size_t index, size_t total) {}

void onMqttPublish(uint16_t packetId) {}
