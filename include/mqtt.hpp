#pragma once

#include <AsyncMqttClient.h>

#include <deque>
#include <string>
#include <tuple>
#include <vector>

#include "schedule.hpp"
#include "store.hpp"

// The MQTT class acts as a wrapper for the entire MQTT subsystem. It provides
// some basic methods for supporting Home Assistant.

class Mqtt {
 public:
  Mqtt();

  void setServer(const char *host, uint16_t port);
  void setCredentials(const char *username, const char *password = nullptr);

  void setUniqueId(const char *id);
  void setHASupport(const bool enable);

  void connect();
  bool connected() const;

  void doLoop();

  uint16_t publish(const char *topic, uint8_t qos, bool retain,
                   const char *payload = nullptr, bool prefix = true);

  void publishResponse(const std::string &id, const std::string &status,
                       const size_t &bytes = 0);

  void publishHA() const;

  void publishCommands();
  void publishHASensors(const bool remove);
  void publishParticipants();

  static void publishData(const std::string &id,
                          const std::vector<uint8_t> &master,
                          const std::vector<uint8_t> &slave);

  static void publishValue(Command *command, const JsonDocument &doc);

 private:
  AsyncMqttClient client;
  std::string uniqueId;
  std::string rootTopic;

  bool haSupport = false;

  std::deque<Command> insCommands;
  uint32_t distanceInsert = 300;
  uint32_t lastInsert = 0;

  std::deque<std::string> remCommands;
  uint32_t distanceRemove = 300;
  uint32_t lastRemove = 0;

  std::deque<const Command *> pubCommands;
  uint32_t distancePublish = 200;
  uint32_t lastPublish = 0;

  std::deque<std::tuple<const Command *, bool>> pubHASensors;
  uint32_t distanceHASensors = 200;
  uint32_t lastHASensors = 0;

  std::deque<const Participant *> pubParticipants;
  uint32_t distanceParticipants = 200;
  uint32_t lastParticipants = 0;

  void setWill(const char *topic, uint8_t qos, bool retain,
               const char *payload = nullptr, size_t length = 0);

  uint16_t subscribe(const char *topic, uint8_t qos);

  static void onConnect(bool sessionPresent);
  static void onDisconnect(AsyncMqttClientDisconnectReason reason) {}

  static void onSubscribe(uint16_t packetId, uint8_t qos) {}
  static void onUnsubscribe(uint16_t packetId) {}

  static void onMessage(const char *topic, const char *payload,
                        AsyncMqttClientMessageProperties properties, size_t len,
                        size_t index, size_t total);

  static void onPublish(uint16_t packetId) {}

  void insertCommands(const JsonArray &commands);
  void removeCommands(const JsonArray &keys);

  void checkInsertCommands();
  void checkRemoveCommands();

  void checkPublishCommands();
  void checkPublishHASensors();
  void checkPublishParticipants();

  static void loadCommands();
  static void saveCommands();
  static void wipeCommands();

  static void initScan(const bool full, const JsonArray &addresses);

  void publishCommand(const Command *command);

  void publishHADiagnostic(const char *name, const bool remove,
                           const char *value_template, const bool full = false);

  void publishHAConfigButton(const char *name, const bool remove);

  void publishHASensor(const Command *command, const bool remove);

  void publishParticipant(const Participant *participant);
};

extern Mqtt mqtt;
