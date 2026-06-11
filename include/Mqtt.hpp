#pragma once

#if defined(EBUS_INTERNAL)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mqtt_client.h>

#include <atomic>
#include <ebus/utils/circular_buffer.hpp>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Command.hpp"
#include "ebus/device.hpp"
#include "ebus/types.hpp"
#include "ebus_accessor.hpp"

enum class IncomingActionType { Insert, Remove };

struct IncomingAction {
  IncomingActionType type;
  Command command;  // for Insert
  std::string key;  // for Remove

  IncomingAction() = default;  // Add default constructor

  explicit IncomingAction(const Command& cmd)
      : type(IncomingActionType::Insert), command(cmd), key("") {}

  explicit IncomingAction(const std::string& k)
      : type(IncomingActionType::Remove), command(), key(k) {}
};

enum class OutgoingActionType {
  Command,
  Device,
  Component,
  Error,
  Data,
  Update
};

struct OutgoingAction {
  OutgoingActionType type;
  const Command* command;            // for Command and Component
  ebus::DeviceInfo device;           // for Device
  ebus::ProtocolInfo protocol_info;  // for Error (contains StaticSequence)
  ebus::FixedString<32> id;          // for Data
  ebus::Sequence master;             // for Data (uses SBO)
  ebus::Sequence slave;              // for Data (uses SBO)
  ebus::FixedString<64> key;         // for Update
  bool ha_remove;                    // for Component

  OutgoingAction() = default;  // Add default constructor

  explicit OutgoingAction(const Command* cmd)
      : type(OutgoingActionType::Command),
        command(cmd),
        device(),
        protocol_info(),
        ha_remove(false) {}

  explicit OutgoingAction(const ebus::DeviceInfo& dev)
      : type(OutgoingActionType::Device),
        command(nullptr),
        device(dev),
        protocol_info(),
        ha_remove(false) {}

  explicit OutgoingAction(const Command* cmd, bool remove)
      : type(OutgoingActionType::Component),
        command(cmd),
        device(),
        protocol_info(),
        ha_remove(remove) {}

  explicit OutgoingAction(const ebus::ProtocolInfo& info)
      : type(OutgoingActionType::Error), protocol_info(info) {
    // Copy transient views from ProtocolInfo into local sequences for safe
    // queuing
    master.assign(info.master_view);
    slave.assign(info.slave_view);
    protocol_info.master_view = master;
    protocol_info.slave_view = slave;
  }

  // Constructor for OutgoingActionType::Data
  OutgoingAction(std::string_view i, ebus::ByteView m, ebus::ByteView s)
      : type(OutgoingActionType::Data),
        id(i),
        master(std::move(m)),
        slave(std::move(s)) {}

  // Constructor for OutgoingActionType::Update
  OutgoingAction(OutgoingActionType t, std::string_view k) : type(t), key(k) {}
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

  void doLoop();

  TaskHandle_t getTaskHandle() const { return task_handle_; }
  size_t getIncomingQueueSize() const;
  size_t getIncomingQueueCapacity() const { return kMaxIncomingQueueSize; }
  size_t getIncomingQueueHighWatermark() const;
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

  std::string uri_;

  bool enabled_ = false;
  bool connected_ = false;

  static constexpr size_t kMaxIncomingQueueSize = 5;

  ebus::detail::CircularBuffer<IncomingAction, kMaxIncomingQueueSize>
      incoming_queue_;
  mutable std::mutex incoming_queue_mutex_;
  uint32_t last_incoming_ = 0;
  uint32_t incoming_interval_ = 10;  // ms
  std::atomic<size_t> max_incoming_ = 0;

  static constexpr size_t kMaxOutgoingQueueSize = 8;

  ebus::detail::CircularBuffer<OutgoingAction, kMaxOutgoingQueueSize>
      outgoing_queue_;
  mutable std::mutex outgoing_queue_mutex_;
  uint32_t last_outgoing_ = 0;
  uint32_t outgoing_interval_ = 10;  // ms
  std::atomic<size_t> max_outgoing_ = 0;

  TaskHandle_t task_handle_ = nullptr;
  uint32_t last_status_publish_ = 0;
  uint32_t status_publish_interval_ms_ = 10 * 1000;
  std::function<void(const ebus::JsonChunkVisitor&)> status_provider_;

  mutable std::mutex publish_mutex_;
  std::string publish_buffer_;

  void internalPublish(const char* topic, uint8_t qos, bool retain,
                       const char* payload, bool prefix);

  static void taskFunc(void* arg);

  static void eventHandler(void* handler_args, esp_event_base_t base,
                           int32_t event_id, void* event_data);

  // Command handlers
  void handleRestart(std::string_view payload);
  void handleInsert(std::string_view payload);
  void handleRemove(std::string_view payload);
  void handlePublish();

  void handleLoad(std::string_view payload);
  void handleSave(std::string_view payload);
  void handleWipe(std::string_view payload);

  void handleScan(std::string_view payload);
  void handleDevices(std::string_view payload);

  void handleSend(std::string_view payload);
  void handleForward(std::string_view payload);

  void handleReset(std::string_view payload);

  void handleRead(std::string_view payload);
  void handleWrite(std::string_view payload);

  bool checkIncomingQueue();
  bool checkOutgoingQueue();
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
