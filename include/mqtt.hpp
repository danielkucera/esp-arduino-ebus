#pragma once

#include <ArduinoJson.h>
#include <AsyncMqttClient.h>

#include <deque>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "schedule.hpp"
#include "store.hpp"

using CommandHandler = std::function<void(const JsonDocument&)>;

// The MQTT class acts as a wrapper for the entire MQTT subsystem. It provides
// some basic methods for supporting Home Assistant.

class Mqtt {
 public:
  Mqtt();

  void setUniqueId(const char* id);

  void setServer(const char* host, uint16_t port);
  void setCredentials(const char* username, const char* password = nullptr);

  void setHASupport(const bool enable);

  void connect();
  bool connected() const;

  void doLoop();

  uint16_t publish(const char* topic, uint8_t qos, bool retain,
                   const char* payload = nullptr, bool prefix = true);

  void publishResponse(const std::string& id, const std::string& status,
                       const size_t& bytes = 0);

  void publishHA() const;

#if defined(EBUS_INTERNAL)
  void publishHASensors(const bool remove);
  void publishParticipants();

  static void publishData(const std::string& id,
                          const std::vector<uint8_t>& master,
                          const std::vector<uint8_t>& slave);

  static void publishValue(const Command* command, const JsonDocument& doc);
#endif

 private:
  AsyncMqttClient client;
  std::string uniqueId;
  std::string rootTopic;
  std::string topicWill;

  bool haSupport = false;

#if defined(EBUS_INTERNAL)
  std::deque<Command> insCommands;
  uint32_t distanceInsert = 300;
  uint32_t lastInsert = 0;

  std::deque<std::string> remCommands;
  uint32_t distanceRemove = 300;
  uint32_t lastRemove = 0;

  std::deque<const Command*> pubCommands;
  uint32_t distancePublish = 200;
  uint32_t lastPublish = 0;

  std::deque<std::tuple<const Command*, bool>> pubHASensors;
  uint32_t distanceHASensors = 200;
  uint32_t lastHASensors = 0;

  std::deque<const Participant*> pubParticipants;
  uint32_t distanceParticipants = 200;
  uint32_t lastParticipants = 0;
#endif

  // Command handlers map
  std::unordered_map<std::string, CommandHandler> commandHandlers = {
      {"restart", [this](const JsonDocument& doc) { handleRestart(doc); }},
#if defined(EBUS_INTERNAL)
      {"insert", [this](const JsonDocument& doc) { handleInsert(doc); }},
      {"remove", [this](const JsonDocument& doc) { handleRemove(doc); }},
      {"publish", [this](const JsonDocument& doc) { handlePublish(doc); }},

      {"load", [this](const JsonDocument& doc) { handleLoad(doc); }},
      {"save", [this](const JsonDocument& doc) { handleSave(doc); }},
      {"wipe", [this](const JsonDocument& doc) { handleWipe(doc); }},

      {"scan", [this](const JsonDocument& doc) { handleScan(doc); }},
      {"participants",
       [this](const JsonDocument& doc) { handleParticipants(doc); }},

      {"send", [this](const JsonDocument& doc) { handleSend(doc); }},
      {"forward", [this](const JsonDocument& doc) { handleForward(doc); }},

      {"reset", [this](const JsonDocument& doc) { handleReset(doc); }},

      {"read", [this](const JsonDocument& doc) { handleRead(doc); }},
      {"write", [this](const JsonDocument& doc) { handleWrite(doc); }},

#endif
  };

  uint16_t subscribe(const char* topic, uint8_t qos);

  static void onConnect(bool sessionPresent);
  static void onDisconnect(AsyncMqttClientDisconnectReason reason) {}

  static void onSubscribe(uint16_t packetId, uint8_t qos) {}
  static void onUnsubscribe(uint16_t packetId) {}

  static void onMessage(const char* topic, const char* payload,
                        AsyncMqttClientMessageProperties properties, size_t len,
                        size_t index, size_t total);

  static void onPublish(uint16_t packetId) {}

  static void handleRestart(const JsonDocument& doc);
#if defined(EBUS_INTERNAL)
  void handleInsert(const JsonDocument& doc);
  void handleRemove(const JsonDocument& doc);
  void handlePublish(const JsonDocument& doc);

  static void handleLoad(const JsonDocument& doc);
  static void handleSave(const JsonDocument& doc);
  static void handleWipe(const JsonDocument& doc);

  void handleScan(const JsonDocument& doc);
  static void handleParticipants(const JsonDocument& doc);

  void handleSend(const JsonDocument& doc);
  void handleForward(const JsonDocument& doc);

  static void handleReset(const JsonDocument& doc);

  void handleRead(const JsonDocument& doc);
  void handleWrite(const JsonDocument& doc);

  void checkInsertCommands();
  void checkRemoveCommands();

  void checkPublishCommands();
  void checkPublishHASensors();
  void checkPublishParticipants();

  void publishCommand(const Command* command);
#endif

  void publishHADiagnostic(const char* name, const bool remove,
                           const char* value_template, const bool full = false);

  void publishHAConfigButton(const char* name, const bool remove);

#if defined(EBUS_INTERNAL)
  void publishHASensor(const Command* command, const bool remove);

  void publishParticipant(const Participant* participant);
#endif
};

extern Mqtt mqtt;
