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

#include "command.hpp"
#include "ebus/device.hpp"
#include "ebus/types.hpp"
#include "ebus_accessor.hpp"

enum class OutgoingActionType : uint8_t {
  Component,
  Error,
  Data,
  Update,
  Discovery,
  Components,
  HaEnable,
  HaDisable
};

struct OutgoingAction {
  const Command* command;  // for Component
  size_t field_idx;        // for Component (per-field HA)
  OutgoingActionType type;
  bool ha_remove;  // for Component
  union {
    ebus::ProtocolInfo protocol_info;  // for Error
    ebus::FixedString<32> id;          // for Data
    ebus::FixedString<64> key;         // for Update
  };
  ebus::StaticSequence<64> master;  // for Data (bitwise-copy safe)
  ebus::StaticSequence<64> slave;   // for Data (bitwise-copy safe)

  OutgoingAction()
      : command(nullptr),
        field_idx(0),
        type(OutgoingActionType::Component),
        ha_remove(false) {}

  explicit OutgoingAction(const Command* cmd, size_t f_idx, bool remove)
      : command(cmd),
        field_idx(f_idx),
        type(OutgoingActionType::Component),
        ha_remove(remove) {}

  explicit OutgoingAction(const ebus::ProtocolInfo& info)
      : command(nullptr),
        field_idx(0),
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
        field_idx(0),
        type(OutgoingActionType::Data),
        ha_remove(false),
        id(i) {
    master.assign(m.data(), m.size());
    slave.assign(s.data(), s.size());
  }

  // Constructor for OutgoingActionType::Update
  OutgoingAction(OutgoingActionType t, std::string_view k)
      : command(nullptr), field_idx(0), type(t), ha_remove(false), key(k) {}
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

  static void enqueueOutgoing(const OutgoingAction& action);

  void publish(const char* topic, uint8_t qos, bool retain,
               const char* payload = nullptr, bool prefix = true);

  void publishStream(
      const char* topic, uint8_t qos, bool retain,
      const std::function<void(const ebus::JsonChunkVisitor&)>& builder,
      bool prefix = true);

  static void publishData(const std::string& id,
                          const std::vector<uint8_t>& master,
                          const std::vector<uint8_t>& slave);

  static void publishError(const ebus::ProtocolInfo& info);

  static void publishValue(std::string_view key);

  static void publishDiscovery();
  static void publishComponentDiscovery();
  static void publishHaEnable();
  static void publishHaDisable();

  TaskHandle_t getTaskHandle() const { return task_handle_; }
  size_t getOutgoingQueueSize() const;
  static size_t getOutgoingQueueCapacity() { return max_outgoing_queue_size; }
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
  volatile bool task_exited_ = false;
  bool connected_ = false;

  static constexpr size_t max_outgoing_queue_size = 8;

  QueueHandle_t outgoing_queue_ = nullptr;
  std::atomic<size_t> max_outgoing_ = 0;

  TaskHandle_t task_handle_ = nullptr;
  uint32_t last_status_publish_ = 0;
  uint32_t status_publish_interval_ms_ = 10 * 1000;
  std::function<void(const ebus::JsonChunkVisitor&)> status_provider_;

  // Track pending subscriptions for SUBSCRIBED event logging
  struct PendingSub {
    int msg_id = -1;
    char topic[128] = {};
  };
  static constexpr size_t max_pending_subs = 4;
  PendingSub pending_subs_[max_pending_subs] = {};
  size_t pending_subs_count_ = 0;

  mutable std::recursive_mutex mqtt_mutex_;

  static constexpr size_t mqtt_pub_buffer_size = 2048;
  char publish_buffers_[mqtt_pub_buffer_size] = {};

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
};

extern Mqtt mqtt;
#endif
