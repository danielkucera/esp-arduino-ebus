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

#include "app/command.hpp"
#include "app/ebus_accessor.hpp"
#include "config/app_config.hpp"
#include "ebus/detail/json_writer.hpp"
#include "ebus/device.hpp"
#include "ebus/types.hpp"

enum class OutgoingActionType : uint8_t {
  Component,
  Error,
  Data,
  Update,
  Discovery,
  Components,
  HaEnable,
  HaDisable,
  HaConnected
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
  // Per-side telegram views (CRC/ACK excluded) peak at 21/17 bytes;
  // model_capacity matches ErrorEntry/ProtocolEvent holding the same views.
  ebus::StaticSequence<ebus::detail::SequenceLimits::model_capacity>
      master;  // for Data (bitwise-copy safe)
  ebus::StaticSequence<ebus::detail::SequenceLimits::model_capacity>
      slave;  // for Data (bitwise-copy safe)

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
  Mqtt();

  // Dispatch pointer for the static publish facades below (house pattern:
  // the single instance is owned by App).
  static Mqtt* instance_;

  // Home Assistant hooks, injected by App. Mqtt owns no HA logic; unset
  // hooks silently skip HA work.
  struct HaHooks {
    std::function<bool()> is_enabled;
    std::function<void(bool)> set_enabled;
    std::function<void()> publish_device_info;
    std::function<void()> publish_components;
    std::function<void(const Command*, size_t, bool)> publish_component;
    std::function<void()> on_connected;
  };
  void setHaHooks(HaHooks hooks);

  void start();
  void change();
  void startTask();
  void stopTask();

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
  uint32_t getPublishedCount() const {
    return published_.load(std::memory_order_relaxed);
  }
  uint32_t getPublishFailedCount() const {
    return publish_failed_.load(std::memory_order_relaxed);
  }
  uint32_t getQueueDropCount() const {
    return queue_dropped_.load(std::memory_order_relaxed);
  }
  uint32_t getConnectCount() const {
    return connects_.load(std::memory_order_relaxed);
  }

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
  std::atomic<uint32_t> published_ = 0;
  std::atomic<uint32_t> publish_failed_ = 0;
  std::atomic<uint32_t> queue_dropped_ = 0;
  std::atomic<uint32_t> connects_ = 0;

  TaskHandle_t task_handle_ = nullptr;
  uint32_t last_status_publish_ = 0;
  uint32_t status_publish_interval_ms_ = 10 * 1000;

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

  // Null-safe Home Assistant hook dispatchers (unset hooks skip silently).
  bool haEnabled() const;
  void haPublishDeviceInfo() const;
  void haPublishComponents() const;
  void haPublishComponent(const Command* command, size_t field_idx,
                          bool remove) const;
  void haSetEnabled(bool enable) const;
  void haConnected() const;

 private:
  HaHooks ha_hooks_;
};

// Renders the "mqtt" status section. Colocated here because it reads live
// Mqtt state; the config slice arrives as a parameter (no new DI).
// Writes fields only: the caller opens the named object scope.
void appendMqttStatus(ebus::detail::JsonWriter& writer,
                      const AppConfig::Mqtt& mqtt_config);
#endif
