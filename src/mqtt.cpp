#include "mqtt.hpp"
#include "schedule.hpp"

AsyncMqttClient mqttClient;

void onMqttConnect(bool sessionPresent)
{
    mqttClient.subscribe("ebus/config/commands/#", 0);
    mqttClient.subscribe("ebus/config/remove", 0);
    mqttClient.subscribe("ebus/config/list", 0);
}

// void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
// {
// }

// void onMqttSubscribe(uint16_t packetId, uint8_t qos)
// {
// }

// void onMqttUnsubscribe(uint16_t packetId)
// {
// }

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
    String tmp = String(topic);
    if (tmp.startsWith("ebus/config/commands"))
        schedule.insertCommand(payload);
    else if (tmp.startsWith("ebus/config/remove"))
        schedule.removeCommand(payload);
    else if (tmp.startsWith("ebus/config/list"))
        schedule.publishCommands();
}

// void onMqttPublish(uint16_t packetId)
// {
// }
