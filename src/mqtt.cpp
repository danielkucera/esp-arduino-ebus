#include "mqtt.hpp"
// #include "main.hpp"

AsyncMqttClient mqttClient;

void onMqttConnect(bool sessionPresent)
{
    // DEBUG_LOG("Connected to MQTT");
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    // DEBUG_LOG("Disconnected from MQTT");
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos)
{
    // String tmp = "Subscribe acknowledged packetId: " + String(packetId) + " qos: " + String(qos);
    // DEBUG_LOG(tmp.c_str());
}

void onMqttUnsubscribe(uint16_t packetId)
{
    // String tmp = "Unsubscribe acknowledged packetId: " + String(packetId);
    // DEBUG_LOG(tmp.c_str());
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
    // String tmp = "Message received topic: " + String(topic);
    // DEBUG_LOG(tmp.c_str());
}

void onMqttPublish(uint16_t packetId)
{
    // String tmp = "Publish acknowledged packetId: " + String(packetId);
    // DEBUG_LOG(tmp.c_str());
}
