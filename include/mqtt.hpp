#ifndef INCLUDE_MQTT_HPP_
#define INCLUDE_MQTT_HPP_

#include <AsyncMqttClient.h>

// The onMqtt callback functions are the interface to the mqtt sub-system for
// the user-defined commands.

extern AsyncMqttClient mqttClient;

void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttSubscribe(uint16_t packetId, uint8_t qos);
void onMqttUnsubscribe(uint16_t packetId);
void onMqttMessage(const char *topic, const char *payload,
                   AsyncMqttClientMessageProperties properties, size_t len,
                   size_t index, size_t total);
void onMqttPublish(uint16_t packetId);

// This class can sum all kinds of primitive number types. After a minimum time
// in seconds, the summed data is published under the specified mqtt topic.
// After a maximum period of time, a publication is always carried out for an
// order.
template <class T>
class Track {
 public:
  Track(const char *topic, const uint16_t seconds, const uint16_t maxage = 60)
      : m_topic(topic), m_seconds(seconds), m_maxage(maxage) {}

  // OK xxx = 1;
  const Track &operator=(const T &value) {
    if (m_value != value) {
      m_value = value;
      publish(false);
    } else {
      if (millis() > m_last + m_maxage * 1000) publish(true);
    }
    return *this;
  }

  // OK xxx += 1;
  const Track &operator+=(const T &value) {
    m_value += value;
    publish(false);
    return *this;
  }

  // OK xxx += xxx;
  const Track &operator+=(const Track &rhs) {
    m_value += rhs.m_value;
    publish(false);
    return *this;
  }

  // OK xxx = xxx + xxx;
  friend Track operator+(Track lhs, const Track &rhs) {
    lhs += rhs;
    return lhs;
  }

  // OK ++xxx;
  const Track &operator++() {
    m_value++;
    publish(false);
    return *this;
  }

  // OK xxx++;
  const Track operator++(int) {
    Track old = *this;
    operator++();
    return old;
  }

  const T &value() const { return m_value; }

  void publish() { publish(true); }

 private:
  T m_value;
  const char *m_topic;
  const uint16_t m_seconds = 0;
  const uint16_t m_maxage = 0;
  uint32_t m_last = 0;

  inline void publish(boolean force) {
    if (force || millis() > m_last + m_seconds * 1000) {
      mqttClient.publish(m_topic, 0, true, String(m_value).c_str());
      m_last = millis();
    }
  }
};

#endif  // INCLUDE_MQTT_HPP_
