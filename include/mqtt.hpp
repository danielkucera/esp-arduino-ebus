#pragma once

#include <AsyncMqttClient.h>

#include <string>

// The Mqtt class acts as a wrapper for the MQTT subsystem.

class Mqtt {
 public:
  Mqtt();

  void setUniqueId(const char* id);
  const std::string &getUniqueId() const;

  const std::string &getRootTopic() const;

  void setServer(const char *host, uint16_t port);
  void setCredentials(const char *username, const char *password = nullptr);

  void connect();
  bool connected() const;

  uint16_t publish(const char *topic, uint8_t qos, bool retain,
                   const char *payload = nullptr, bool prefix = true);

 private:
  AsyncMqttClient client;
  std::string uniqueId;
  std::string rootTopic;

  uint16_t subscribe(const char *topic, uint8_t qos);

  static void onConnect(bool sessionPresent);
  static void onDisconnect(AsyncMqttClientDisconnectReason reason) {}

  static void onSubscribe(uint16_t packetId, uint8_t qos) {}
  static void onUnsubscribe(uint16_t packetId) {}

  static void onMessage(const char *topic, const char *payload,
                        AsyncMqttClientMessageProperties properties, size_t len,
                        size_t index, size_t total);

  static void onPublish(uint16_t packetId) {}
};

extern Mqtt mqtt;

// The Track class can sum all kinds of primitive number types. After a minimum
// time in seconds, the summed data is published under the specified mqtt topic.
// After a maximum period of time, a publication is always carried out for an
// order.

template <class T>
class Track {
 public:
  Track(const char *topic, const uint16_t minage, const uint16_t maxage = 60)
      : m_topic(topic), m_minage(minage), m_maxage(maxage) {}

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

  void touch() {
    if (millis() > m_last + m_maxage * 1000) publish(true);
  }

 private:
  T m_value;
  const char *m_topic;
  const uint16_t m_minage = 0;
  const uint16_t m_maxage = 0;
  uint32_t m_last = 0;

  inline void publish(boolean force) {
    if (force || millis() > m_last + m_minage * 1000) {
      mqtt.publish(m_topic, 0, false, String(m_value).c_str());
      m_last = millis();
    }
  }
};
