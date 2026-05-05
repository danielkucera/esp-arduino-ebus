#pragma once

#if defined(EBUS_INTERNAL)
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mqtt_client.h>

#include <functional>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Command.hpp"
#include "ebus/device.hpp"
#include "ebus_accessor.hpp"

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
  const Command* command;   // for Command and Component
  ebus::DeviceInfo device;  // for Device
  bool ha_remove;           // for Component

  explicit OutgoingAction(const Command* cmd)
      : type(OutgoingActionType::Command),
        command(cmd),
        device(),
        ha_remove(false) {}

  explicit OutgoingAction(const ebus::DeviceInfo& dev)
      : type(OutgoingActionType::Device),
        command(nullptr),
        device(dev),
        ha_remove(false) {}

  explicit OutgoingAction(const Command* cmd, bool remove)
      : type(OutgoingActionType::Component),
        command(cmd),
        device(),
        ha_remove(remove) {}
};

using CommandHandler = std::function<void(const cJSON*)>;

// The MQTT class acts as a wrapper for the entire MQTT subsystem.

class Mqtt {
 public:
  Mqtt() = default;

  void start();
  void change();
  void startTask();
  void stopTask();
  void setStatusProvider(const std::function<std::string()>& provider);

  void setup(const char* id);

  void setServer(const char* host, uint16_t port);
  void setCredentials(const char* username, const char* password = nullptr);
  void setRootTopic(const std::string& topic);

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

  static void publishError(const ebus::ErrorInfo& info);

  static void publishValue(const std::string& name,
                           const std::string& valueJson);

  void doLoop();

 private:
  esp_mqtt_client_handle_t client_ = nullptr;
  esp_mqtt_client_config_t mqtt_cfg_ = {};

  std::string unique_id_;
  std::string client_id_;
  std::string root_topic_;
  std::string will_topic_;
  std::string request_topic_;

  std::string uri_;

  bool enabled_ = false;
  bool connected_ = false;

  std::queue<IncomingAction> incoming_queue_;
  uint32_t last_incoming_ = 0;
  uint32_t incoming_interval_ = 25;  // ms

  std::queue<OutgoingAction> outgoing_queue_;
  uint32_t last_outgoing_ = 0;
  uint32_t outgoing_interval_ = 25;  // ms

  TaskHandle_t task_handle_ = nullptr;
  uint32_t last_status_publish_ = 0;
  uint32_t status_publish_interval_ms_ = 10 * 1000;
  std::function<std::string()> status_provider_;

  static void taskFunc(void* arg);

  // Command handlers map
  std::unordered_map<std::string, CommandHandler> command_handlers_ = {
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

  void publishDevice(const ebus::DeviceInfo& device);
};

extern Mqtt mqtt;
#endif
