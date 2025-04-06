#pragma once

#include <AsyncMqttClient.h>

#include <string>

// The Mqtt class acts as a wrapper for the entire MQTT subsystem.

class Mqtt {
 public:
  Mqtt();

  void setUniqueId(const char *id);
  const std::string &getUniqueId() const;

  const std::string &getRootTopic() const;

  void setServer(const char *host, uint16_t port);
  void setCredentials(const char *username, const char *password = nullptr);

  void setWill(const char *topic, uint8_t qos, bool retain,
               const char *payload = nullptr, size_t length = 0);

  void connect();
  bool connected() const;

  uint16_t publish(const char *topic, uint8_t qos, bool retain,
                   const char *payload = nullptr, bool prefix = true);

  void setHASupport(const bool enable);
  const bool getHASupport() const;

  void publisHA(const bool remove) const;

 private:
  AsyncMqttClient client;
  std::string uniqueId;
  std::string rootTopic;

  bool haSupport = false;

  uint16_t subscribe(const char *topic, uint8_t qos);

  static void onConnect(bool sessionPresent);
  static void onDisconnect(AsyncMqttClientDisconnectReason reason) {}

  static void onSubscribe(uint16_t packetId, uint8_t qos) {}
  static void onUnsubscribe(uint16_t packetId) {}

  static void onMessage(const char *topic, const char *payload,
                        AsyncMqttClientMessageProperties properties, size_t len,
                        size_t index, size_t total);

  static void onPublish(uint16_t packetId) {}

  void publishHADiagnostic(const char *name, const bool remove,
                           const char *value_template, const bool full = false);

  void publishHAConfigButton(const char *name, const bool remove);
};

extern Mqtt mqtt;
