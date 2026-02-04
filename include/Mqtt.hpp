#pragma once

#if defined(EBUS_INTERNAL)
#include <ArduinoJson.h>
#include <mqtt_client.h>

#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Device.hpp"
#include "Schedule.hpp"
#include "Store.hpp"

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

enum class OutgoingActionType { Command, Device, Component };

struct OutgoingAction {
  OutgoingActionType type;
  const Command* command;  // for Command and Component
  const Device* device;    // for Device
  bool haRemove;           // for Component

  explicit OutgoingAction(const Command* cmd)
      : type(OutgoingActionType::Command),
        command(cmd),
        device(nullptr),
        haRemove(false) {}

  explicit OutgoingAction(const Device* part)
      : type(OutgoingActionType::Device),
        command(nullptr),
        device(part),
        haRemove(false) {}

  explicit OutgoingAction(const Command* cmd, bool remove)
      : type(OutgoingActionType::Component),
        command(cmd),
        device(nullptr),
        haRemove(remove) {}
};

using CommandHandler = std::function<void(const JsonDocument&)>;

// The MQTT class acts as a wrapper for the entire MQTT subsystem.

class Mqtt {
 public:
  Mqtt() = default;

  void start();
  void change();

  void setup(const char* id);

  void setServer(const char* host, uint16_t port);
  void setCredentials(const char* username, const char* password = nullptr);

  void setEnabled(const bool enable);
  const bool isEnabled() const;

  const bool isConnected() const;

  const std::string& getUniqueId() const;
  const std::string& getRootTopic() const;
  const std::string& getWillTopic() const;

  void publish(const char* topic, uint8_t qos, bool retain,
               const char* payload = nullptr, bool prefix = true);

  static void enqueueOutgoing(const OutgoingAction& action);

  static void publishData(const std::string& id,
                          const std::vector<uint8_t>& master,
                          const std::vector<uint8_t>& slave);

  static void publishValue(const Command* command);

  void doLoop();

 private:
  esp_mqtt_client_handle_t client = nullptr;
  esp_mqtt_client_config_t mqtt_cfg = {};

  std::string uniqueId;
  std::string clientId;
  std::string rootTopic;
  std::string willTopic;
  std::string requestTopic;

  std::string uri;

  bool enabled = false;
  bool connected = false;

  std::queue<IncomingAction> incomingQueue;
  uint32_t lastIncoming = 0;
  uint32_t incomingInterval = 25;  // ms

  std::queue<OutgoingAction> outgoingQueue;
  uint32_t lastOutgoing = 0;
  uint32_t outgoingInterval = 25;  // ms

  // Command handlers map
  std::unordered_map<std::string, CommandHandler> commandHandlers = {
      {"restart", [this](const JsonDocument& doc) { handleRestart(doc); }},
      {"insert", [this](const JsonDocument& doc) { handleInsert(doc); }},
      {"remove", [this](const JsonDocument& doc) { handleRemove(doc); }},
      {"publish", [this](const JsonDocument& doc) { handlePublish(doc); }},

      {"load", [this](const JsonDocument& doc) { handleLoad(doc); }},
      {"save", [this](const JsonDocument& doc) { handleSave(doc); }},
      {"wipe", [this](const JsonDocument& doc) { handleWipe(doc); }},

      {"scan", [this](const JsonDocument& doc) { handleScan(doc); }},
      {"devices", [this](const JsonDocument& doc) { handleDevices(doc); }},

      {"send", [this](const JsonDocument& doc) { handleSend(doc); }},
      {"forward", [this](const JsonDocument& doc) { handleForward(doc); }},

      {"reset", [this](const JsonDocument& doc) { handleReset(doc); }},

      {"read", [this](const JsonDocument& doc) { handleRead(doc); }},
      {"write", [this](const JsonDocument& doc) { handleWrite(doc); }},
  };

  static void eventHandler(void* handler_args, esp_event_base_t base,
                           int32_t event_id, void* event_data);

  // Command handlers
  static void handleRestart(const JsonDocument& doc);
  void handleInsert(const JsonDocument& doc);
  void handleRemove(const JsonDocument& doc);
  static void handlePublish(const JsonDocument& doc);

  static void handleLoad(const JsonDocument& doc);
  static void handleSave(const JsonDocument& doc);
  static void handleWipe(const JsonDocument& doc);

  void handleScan(const JsonDocument& doc);
  static void handleDevices(const JsonDocument& doc);

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

  void publishDevice(const Device* device);
};

extern Mqtt mqtt;
#endif
