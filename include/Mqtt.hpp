#pragma once

#if defined(EBUS_INTERNAL)
#include <cJSON.h>
#include <mqtt_client.h>

#include <functional>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Command.hpp"
#include "Device.hpp"

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

using CommandHandler = std::function<void(const cJSON*)>;

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
  bool isEnabled() const;

  bool isConnected() const;

  const std::string& getUniqueId() const;
  const std::string& getRootTopic() const;
  const std::string& getWillTopic() const;

  void publish(const char* topic, uint8_t qos, bool retain,
               const char* payload = nullptr, bool prefix = true);

  static void enqueueOutgoing(const OutgoingAction& action);

  static void publishData(const std::string& id,
                          const std::vector<uint8_t>& master,
                          const std::vector<uint8_t>& slave);

  static void publishValue(const std::string& name,
                           const std::string& valueJson);

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
      {"restart", [this](const cJSON* doc) { handleRestart(doc); }},
      {"insert", [this](const cJSON* doc) { handleInsert(doc); }},
      {"remove", [this](const cJSON* doc) { handleRemove(doc); }},
      {"publish", [this](const cJSON* doc) { handlePublish(doc); }},

      {"load", [this](const cJSON* doc) { handleLoad(doc); }},
      {"save", [this](const cJSON* doc) { handleSave(doc); }},
      {"wipe", [this](const cJSON* doc) { handleWipe(doc); }},

      {"scan", [this](const cJSON* doc) { handleScan(doc); }},
      {"devices", [this](const cJSON* doc) { handleDevices(doc); }},

      {"send", [this](const cJSON* doc) { handleSend(doc); }},
      {"forward", [this](const cJSON* doc) { handleForward(doc); }},

      {"reset", [this](const cJSON* doc) { handleReset(doc); }},

      {"read", [this](const cJSON* doc) { handleRead(doc); }},
      {"write", [this](const cJSON* doc) { handleWrite(doc); }},
  };

  static void eventHandler(void* handler_args, esp_event_base_t base,
                           int32_t event_id, void* event_data);

  // Command handlers
  static void handleRestart(const cJSON* doc);
  void handleInsert(const cJSON* doc);
  void handleRemove(const cJSON* doc);
  static void handlePublish(const cJSON* doc);

  static void handleLoad(const cJSON* doc);
  static void handleSave(const cJSON* doc);
  static void handleWipe(const cJSON* doc);

  void handleScan(const cJSON* doc);
  static void handleDevices(const cJSON* doc);

  void handleSend(const cJSON* doc);
  void handleForward(const cJSON* doc);

  static void handleReset(const cJSON* doc);

  void handleRead(const cJSON* doc);
  void handleWrite(const cJSON* doc);

  void checkIncomingQueue();
  void checkOutgoingQueue();

  void publishResponse(const std::string& id, const std::string& status,
                       const size_t& bytes = 0);

  void publishCommand(const Command* command);

  void publishDevice(const Device* device);
};

extern Mqtt mqtt;
#endif
