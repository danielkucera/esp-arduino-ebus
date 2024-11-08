#ifndef _MQTT_H_
#define _MQTT_H_

#include <AsyncMqttClient.h>

extern AsyncMqttClient mqttClient;

void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttSubscribe(uint16_t packetId, uint8_t qos);
void onMqttUnsubscribe(uint16_t packetId);
void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
void onMqttPublish(uint16_t packetId);

template <typename T>
void publishTopic(bool force, const char *topic, T &oldValue, T &newValue)
{
    if (force || oldValue != newValue)
        mqttClient.publish(topic, 0, true, String(newValue).c_str());
}

#endif