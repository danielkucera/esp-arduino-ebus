#pragma once

#if defined(EBUS_INTERNAL)
#include <ArduinoJson.h>
#include <AsyncMqttClient.h>

#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "schedule.hpp"
#include "store.hpp"

enum class IncomingActionType { Insert, Remove };

struct IncomingAction {
  IncomingActionType type;
  Command command;  // for Insert
  std::string key;  // for Remove

  explicit IncomingAction(const Command& cmd)
      : type(IncomingActionType::Insert), command(cmd), key("") {}

  explicit IncomingAction(const std::string& k)
      : type(IncomingActionType::Remove), command(), key(k) {}
};

enum class OutgoingActionType { Command, Participant, Component };

struct OutgoingAction {
  OutgoingActionType type;
  const Command* command;          // for Command and Component
  const Participant* participant;  // for Participant
  bool haRemove;                   // for Component

  explicit OutgoingAction(const Command* cmd)
      : type(OutgoingActionType::Command),
        command(cmd),
        participant(nullptr),
        haRemove(false) {}

  explicit OutgoingAction(const Participant* part)
      : type(OutgoingActionType::Participant),
        command(nullptr),
        participant(part),
        haRemove(false) {}

  explicit OutgoingAction(const Command* cmd, bool remove)
      : type(OutgoingActionType::Component),
        command(cmd),
        participant(nullptr),
        haRemove(remove) {}
};

using CommandHandler = std::function<void(const JsonDocument&)>;

// The MQTT class acts as a wrapper for the entire MQTT subsystem.

class Mqtt {
 public:
  Mqtt();

  void setUniqueId(const char* id);

  const std::string& getUniqueId() const;
  const std::string& getRootTopic() const;

  void setServer(const char* host, uint16_t port);
  void setCredentials(const char* username, const char* password = nullptr);

  void connect();
  const bool connected() const;

  uint16_t publish(const char* topic, uint8_t qos, bool retain,
                   const char* payload = nullptr, bool prefix = true);

  static void enqueueOutgoing(const OutgoingAction& action);

#if defined(EBUS_INTERNAL)
  static void publishData(const std::string& id,
                          const std::vector<uint8_t>& master,
                          const std::vector<uint8_t>& slave);

  static void publishValue(const Command* command, const JsonDocument& doc);

  void doLoop();
#endif

 private:
  AsyncMqttClient client;
  std::string uniqueId;
  std::string rootTopic;
  std::string topicWill;

#if defined(EBUS_INTERNAL)
  std::queue<IncomingAction> incomingQueue;
  uint32_t lastIncoming = 0;
  uint32_t incomingInterval = 200;  // ms

  std::queue<OutgoingAction> outgoingQueue;
  uint32_t lastOutgoing = 0;
  uint32_t outgoingInterval = 200;  // ms
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

  // Command handlers
  static void handleRestart(const JsonDocument& doc);
#if defined(EBUS_INTERNAL)
  void handleInsert(const JsonDocument& doc);
  void handleRemove(const JsonDocument& doc);
  static void handlePublish(const JsonDocument& doc);

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

  void checkIncomingQueue();
  void checkOutgoingQueue();

  void publishResponse(const std::string& id, const std::string& status,
                       const size_t& bytes = 0);

  void publishCommand(const Command* command);

  void publishParticipant(const Participant* participant);

#endif
};

extern Mqtt mqtt;
#endif
