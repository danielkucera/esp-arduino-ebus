#pragma once

#if defined(EBUS_INTERNAL)
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mqtt_client.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Command.hpp"
#include "ebus/device.hpp"
#include "ebus/types.hpp"
#include "ebus_accessor.hpp"

enum class OutgoingActionType : uint8_t {
  Command,
  Device,
  Component,
  Error,
  Data,
  Update,
  Discovery,
  Components
};

struct OutgoingAction {
  const Command* command;  // for Command and Component
  OutgoingActionType type;
  bool ha_remove;  // for Component
  union {
    ebus::DeviceInfo device;           // for Device
    ebus::ProtocolInfo protocol_info;  // for Error
    ebus::FixedString<32> id;          // for Data
    ebus::FixedString<64> key;         // for Update
  };
  ebus::StaticSequence<64> master;  // for Data (bitwise-copy safe)
  ebus::StaticSequence<64> slave;   // for Data (bitwise-copy safe)

  OutgoingAction()
      : command(nullptr), type(OutgoingActionType::Command), ha_remove(false) {}

  explicit OutgoingAction(const Command* cmd)
      : command(cmd), type(OutgoingActionType::Command), ha_remove(false) {}

  explicit OutgoingAction(const ebus::DeviceInfo& dev)
      : command(nullptr),
        type(OutgoingActionType::Device),
        ha_remove(false),
        device(dev) {}

  explicit OutgoingAction(const Command* cmd, bool remove)
      : command(cmd), type(OutgoingActionType::Component), ha_remove(remove) {}

  explicit OutgoingAction(const ebus::ProtocolInfo& info)
      : command(nullptr),
        type(OutgoingActionType::Error),
        ha_remove(false),
        protocol_info(info) {
    master.assign(info.master_view.data(), info.master_view.size());
    slave.assign(info.slave_view.data(), info.slave_view.size());
    protocol_info.master_view = master;
    protocol_info.slave_view = slave;
  }

  // Constructor for OutgoingActionType::Data
  OutgoingAction(std::string_view i, ebus::ByteView m, ebus::ByteView s)
      : command(nullptr),
        type(OutgoingActionType::Data),
        ha_remove(false),
        id(i) {
    master.assign(m.data(), m.size());
    slave.assign(s.data(), s.size());
  }

  // Constructor for OutgoingActionType::Update
  OutgoingAction(OutgoingActionType t, std::string_view k)
      : command(nullptr), type(t), ha_remove(false), key(k) {}
};

// The MQTT class acts as a wrapper for the entire MQTT subsystem.

class Mqtt {
 public:
  Mqtt() = default;

  void start();
  void change();
  void startTask();
  void stopTask();
  void setStatusProvider(
      std::function<void(const ebus::JsonChunkVisitor&)> provider);

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

  void publishStream(
      const char* topic, uint8_t qos, bool retain,
      const std::function<void(const ebus::JsonChunkVisitor&)>& builder,
      bool prefix = true);

  static void enqueueOutgoing(const OutgoingAction& action);

  static void publishData(const std::string& id,
                          const std::vector<uint8_t>& master,
                          const std::vector<uint8_t>& slave);

  static void publishError(const ebus::ProtocolInfo& info);

  static void publishValue(std::string_view key);

  static void publishDiscovery();
  static void publishComponentDiscovery();

  TaskHandle_t getTaskHandle() const { return task_handle_; }
  size_t getOutgoingQueueSize() const;
  size_t getOutgoingQueueCapacity() const { return kMaxOutgoingQueueSize; }
  size_t getOutgoingQueueHighWatermark() const;

 private:
  esp_mqtt_client_handle_t client_ = nullptr;
  esp_mqtt_client_config_t mqtt_cfg_ = {};

  std::string unique_id_;
  std::string client_id_;
  std::string root_topic_;
  std::string will_topic_;
  std::string request_topic_;
  std::string offline_payload_;
  std::string username_;
  std::string password_;

  std::string uri_;

  bool enabled_ = false;
  volatile bool task_should_run_ = false;
  bool connected_ = false;

  static constexpr size_t kMaxOutgoingQueueSize = 8;

  QueueHandle_t outgoing_queue_ = nullptr;
  std::atomic<size_t> max_outgoing_ = 0;

  TaskHandle_t task_handle_ = nullptr;
  uint32_t last_status_publish_ = 0;
  uint32_t status_publish_interval_ms_ = 10 * 1000;
  std::function<void(const ebus::JsonChunkVisitor&)> status_provider_;

  mutable std::recursive_mutex mqtt_mutex_;

  static constexpr size_t kMqttPubBufferSize = 1024;
  char publish_buffers_[kMqttPubBufferSize];

  void internalPublish(const char* topic, uint8_t qos, bool retain,
                       const char* payload, bool prefix);

  static void taskFunc(void* arg);

  static void eventHandler(void* handler_args, esp_event_base_t base,
                           int32_t event_id, void* event_data);

  void handleRead(std::string_view payload);
  void handleWrite(std::string_view payload);

  void handleDirectWrite(std::string_view key, std::string_view val_view);
  void handleValueUpdate(std::string_view key);

  void publishResponse(std::string_view id, std::string_view status,
                       size_t bytes = 0);

  void logUpdate(const Command* cmd,
                 const std::optional<ebus::DataValue>& decoded);

  void publishCommand(const Command* command);

  void publishDevice(const ebus::DeviceInfo& device);
};

extern Mqtt mqtt;
#endif
