#if defined(EBUS_INTERNAL)
#include "app/mqtt.hpp"

#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>

#include "app/app_limits.hpp"
#include "app/command_manager.hpp"
#include "app/ebus_accessor.hpp"
#include "app/mqtt_ha.hpp"
#include "app/mqtt_endpoint.hpp"
#include "ebus/detail/json_reader.hpp"
#include "ebus/detail/json_writer.hpp"
#include "ebus/status.hpp"
#include "main.hpp"
#include "network/wifi_network_manager.hpp"
#include "system/device_status.hpp"
#include "system/logger.hpp"

// Fail the build rather than accidentally ship a WSS build without validation.
#if !CONFIG_MQTT_TRANSPORT_SSL || !CONFIG_MQTT_TRANSPORT_WEBSOCKET || \
    !CONFIG_MQTT_TRANSPORT_WEBSOCKET_SECURE
#error "WSS requires the MQTT SSL, WebSocket and WebSocket Secure transports"
#endif
#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE || !CONFIG_MBEDTLS_HAVE_TIME_DATE
#error "WSS requires the CA bundle and certificate validity-date checks"
#endif
#if CONFIG_ESP_TLS_INSECURE || CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY
#error "Insecure ESP-TLS options must not be enabled in this WSS build"
#endif

namespace {
void logMqttMemory(const char* phase) {
  char message[192];
  snprintf(message, sizeof(message),
           "[MQTT] %s: free=%u, min=%u, largest=%u bytes", phase,
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  logger.info(message);
}
}  // namespace

Mqtt* Mqtt::instance_ = nullptr;

Mqtt::Mqtt() { instance_ = this; }

void Mqtt::setHaHooks(HaHooks hooks) { ha_hooks_ = std::move(hooks); }

bool Mqtt::haEnabled() const {
  return ha_hooks_.is_enabled && ha_hooks_.is_enabled();
}

void Mqtt::haPublishDeviceInfo() const {
  if (ha_hooks_.publish_device_info) ha_hooks_.publish_device_info();
}

void Mqtt::haPublishComponents() const {
  if (ha_hooks_.publish_components) ha_hooks_.publish_components();
}

void Mqtt::haPublishComponent(const Command* command, size_t field_idx,
                              bool remove) const {
  if (ha_hooks_.publish_component)
    ha_hooks_.publish_component(command, field_idx, remove);
}

void Mqtt::haSetEnabled(bool enable) const {
  if (ha_hooks_.set_enabled) ha_hooks_.set_enabled(enable);
}

void Mqtt::haConnected() const {
  if (ha_hooks_.on_connected) ha_hooks_.on_connected();
}

void Mqtt::start() {
  std::lock_guard<std::recursive_mutex> lock(mqtt_mutex_);
  if (enabled_) {
    // Important: destroy previous client to free resources and close sockets
    if (client_ != nullptr) {
      esp_mqtt_client_destroy(client_);
      client_ = nullptr;
    }
    connected_ = false;
    if (!server_valid_) {
      logger.error("[MQTT] Client not started: invalid server/URI configuration");
      return;
    }
    client_ = esp_mqtt_client_init(&mqtt_cfg_);
    if (client_ == nullptr) {
      logger.error("[MQTT] Client initialization failed (configuration or memory)");
      logMqttMemory("init failed");
      return;
    }
    esp_err_t err = esp_mqtt_client_register_event(
        client_, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
        &Mqtt::eventHandler, this);
    if (err == ESP_OK) err = esp_mqtt_client_start(client_);
    if (err != ESP_OK) {
      char message[128];
      snprintf(message, sizeof(message), "[MQTT] Client start failed: %s (0x%x)",
               esp_err_to_name(err), static_cast<unsigned>(err));
      logger.error(message);
      esp_mqtt_client_destroy(client_);
      client_ = nullptr;
    }
  }
}

void Mqtt::change() {
  std::lock_guard<std::recursive_mutex> lock(mqtt_mutex_);
  if (connected_) esp_mqtt_client_stop(client_);
  start();
}

void Mqtt::startTask() {
  if (task_handle_ != nullptr) return;
  task_should_run_ = true;
  xTaskCreate(&Mqtt::taskFunc, "mqtt", app::limits::Task::mqtt_stack, this,
              app::limits::Task::mqtt_priority, &task_handle_);
}

void Mqtt::stopTask() {
  // First, signal task to stop to prevent it from using the MQTT client
  if (task_handle_ != nullptr) {
    task_should_run_ = false;

    // Send a dummy action to unblock xQueueReceive if task is waiting
    // This is safer than vQueueDelete which can cause race conditions
    OutgoingAction dummy;
    dummy.type = OutgoingActionType::Error;
    if (outgoing_queue_ != nullptr) {
      xQueueSend(outgoing_queue_, &dummy, 0);
    }

    // Wait for the task to actually terminate (up to 1 second total)
    // This is critical to prevent the task from accessing MQTT client
    // after it has been destroyed
    // Note: The task resets its own static queue when it exits
    const TickType_t xDelay = pdMS_TO_TICKS(100);
    for (int i = 0; i < 10; ++i) {
      if (task_exited_) {
        break;
      }
      vTaskDelay(xDelay);
    }

    // Task has deleted its own queue and handle
    std::lock_guard<std::recursive_mutex> lock(mqtt_mutex_);
    task_handle_ = nullptr;
    outgoing_queue_ = nullptr;
    task_should_run_ = false;
    task_exited_ = false;
  }

  // Now stop and destroy the MQTT client to prevent reconnections during
  // upgrade This must be done AFTER the task has stopped to avoid race
  // conditions
  {
    std::lock_guard<std::recursive_mutex> lock(mqtt_mutex_);
    if (connected_) {
      esp_mqtt_client_stop(client_);
      connected_ = false;
    }
    if (client_ != nullptr) {
      esp_mqtt_client_destroy(client_);
      client_ = nullptr;
    }
  }
}

void Mqtt::setup(const char* id) {
  unique_id_ = id;
  client_id_ = "ebus-" + unique_id_;
  root_topic_ = "ebus/" + unique_id_ + "/";
  will_topic_ = root_topic_ + "available";
  request_topic_ = root_topic_ + "request";
  offline_payload_ = "{\"value\":\"offline\"}";

  mqtt_cfg_.credentials.client_id = client_id_.c_str();
  // Last Will
  mqtt_cfg_.session.last_will.topic = will_topic_.c_str();
  mqtt_cfg_.session.last_will.msg = offline_payload_.c_str();
  mqtt_cfg_.session.last_will.msg_len =
      static_cast<int>(offline_payload_.size());
  mqtt_cfg_.session.last_will.qos = 1;
  mqtt_cfg_.session.last_will.retain = 1;
  // Keep-alive interval in seconds
  mqtt_cfg_.session.keepalive = 60;
  mqtt_cfg_.buffer.size = 1536;
  mqtt_cfg_.buffer.out_size = 1536;
}

void Mqtt::setServer(const char* host, uint16_t port) {
  std::lock_guard<std::recursive_mutex> lock(mqtt_mutex_);
  const auto endpoint = mqtt_endpoint::parse(host ? host : "", port);
  server_valid_ = static_cast<bool>(endpoint);
  tls_enabled_ = endpoint.tls;
  transport_ = endpoint.transport;
  uri_ = endpoint.uri;
  // Clear previous address/verification settings when switching transport.
  mqtt_cfg_.broker.address = {};
  mqtt_cfg_.broker.verification = {};
  mqtt_cfg_.broker.address.uri = uri_.c_str();
  if (!server_valid_) {
    logger.error(endpoint.error ? endpoint.error : "[MQTT] Invalid endpoint");
    return;
  }
  if (tls_enabled_) {
    mqtt_cfg_.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    mqtt_cfg_.broker.verification.skip_cert_common_name_check = false;
    logger.info("[MQTT] TLS enabled: CA bundle, hostname and expiry verification");
  } else {
    logger.warn("[MQTT] Plaintext transport selected: use only on a trusted network");
  }
  // Retrying never changes the configured transport or disables verification.
  mqtt_cfg_.network.reconnect_timeout_ms = 10000;
  mqtt_cfg_.network.timeout_ms = 15000;
}

void Mqtt::setCredentials(const char* username, const char* password) {
  std::lock_guard<std::recursive_mutex> lock(mqtt_mutex_);
  username_ = username ? username : "";
  password_ = password ? password : "";
  mqtt_cfg_.credentials.username = username_.c_str();
  mqtt_cfg_.credentials.authentication.password = password_.c_str();
}

void Mqtt::setRootTopic(const std::string& topic) {
  std::lock_guard<std::recursive_mutex> lock(mqtt_mutex_);
  root_topic_ = topic;
  // Ensure proper formatting with trailing slash
  if (!root_topic_.empty() && root_topic_.back() != '/') {
    root_topic_ += '/';
  }
  will_topic_ = root_topic_ + "available";
  request_topic_ = root_topic_ + "request";

  // Refresh pointers in config after string modification
  mqtt_cfg_.session.last_will.topic = will_topic_.c_str();
}

void Mqtt::setEnabled(const bool enable) {
  std::lock_guard<std::recursive_mutex> lock(mqtt_mutex_);
  enabled_ = enable;
}

bool Mqtt::isEnabled() const { return enabled_; }

bool Mqtt::isConnected() const { return connected_; }

bool Mqtt::isClockInitialized() {
  // Detect a reset-to-1970 clock; this is not proof of a successful SNTP sync.
  // mbedTLS still checks the real certificate validity interval at handshake.
  return std::time(nullptr) >= static_cast<std::time_t>(1704067200);  // 2024-01-01 UTC
}

const std::string& Mqtt::getUniqueId() const { return unique_id_; }

const std::string& Mqtt::getRootTopic() const { return root_topic_; }

const std::string& Mqtt::getWillTopic() const { return will_topic_; }

void Mqtt::enqueueOutgoing(const OutgoingAction& action) {
  if (!instance_ || !instance_->enabled_ ||
      instance_->outgoing_queue_ == nullptr)
    return;

  // HA-style silent skip: value paths (Data/Update/Error) are high-rate and
  // refresh cyclically, so queuing them while disconnected only produces
  // failed publishes (and evicts queued work). Lifecycle types
  // (Discovery/Components/Ha*) still queue — HaConnected is enqueued from
  // the CONNECTED handler itself.
  if (!instance_->connected_ && (action.type == OutgoingActionType::Data ||
                                 action.type == OutgoingActionType::Update ||
                                 action.type == OutgoingActionType::Error))
    return;

  if (xQueueSend(instance_->outgoing_queue_, &action, 0) != pdPASS) {
    // Mimic CircularBuffer behavior: drop oldest to make room for new
    OutgoingAction dummy;
    if (xQueueReceive(instance_->outgoing_queue_, &dummy, 0) == pdTRUE) {
      xQueueSend(instance_->outgoing_queue_, &action, 0);
    }
    instance_->queue_dropped_.fetch_add(1, std::memory_order_relaxed);
    logger.warn("[MQTT] Outgoing queue full, dropped oldest message");
  }

  size_t current = uxQueueMessagesWaiting(instance_->outgoing_queue_);
  ebus::updateMaxAtomic(instance_->max_outgoing_, current);
}

void Mqtt::publish(const char* topic, uint8_t qos, bool retain,
                   const char* payload, bool prefix) {
  internalPublish(topic, qos, retain, payload, prefix);
}

void Mqtt::publishStream(
    const char* topic, uint8_t qos, bool retain,
    const std::function<void(const ebus::JsonChunkVisitor&)>& builder,
    bool prefix) {
  std::lock_guard<std::recursive_mutex> lock(mqtt_mutex_);
  if (!enabled_ || client_ == nullptr) return;

  // Since we hold the mutex, we can safely use buffer index 0 exclusively.
  char* buf = publish_buffers_;
  size_t len = 0;

  builder([&](std::string_view s) {
    if (len + s.size() < mqtt_pub_buffer_size - 1) {
      std::memcpy(buf + len, s.data(), s.size());
      len += s.size();
      buf[len] = '\0';
    }
  });
  internalPublish(topic, qos, retain, buf, prefix);
}

void Mqtt::publishData(const std::string& id,
                       const std::vector<uint8_t>& master,
                       const std::vector<uint8_t>& slave) {
  if (!instance_ || !instance_->enabled_) return;
  enqueueOutgoing(OutgoingAction(id, master, slave));
}

void Mqtt::publishError(const ebus::ProtocolInfo& info) {
  if (!instance_ || !instance_->enabled_) return;
  enqueueOutgoing(OutgoingAction(info));
}

void Mqtt::publishValue(std::string_view key) {
  if (!instance_ || !instance_->enabled_) return;
  enqueueOutgoing(
      OutgoingAction(OutgoingActionType::Update, key));  // Pass string_view
}

void Mqtt::publishDiscovery() {
  if (!instance_ || !instance_->enabled_ || !instance_->haEnabled()) return;
  enqueueOutgoing(OutgoingAction(OutgoingActionType::Discovery, ""));
}

void Mqtt::publishComponentDiscovery() {
  if (!instance_ || !instance_->enabled_ || !instance_->haEnabled()) return;
  enqueueOutgoing(OutgoingAction(OutgoingActionType::Components, ""));
}

void Mqtt::publishHaEnable() {
  if (!instance_ || !instance_->enabled_) return;
  enqueueOutgoing(OutgoingAction(OutgoingActionType::HaEnable, ""));
}

void Mqtt::publishHaDisable() {
  if (!instance_ || !instance_->enabled_) return;
  enqueueOutgoing(OutgoingAction(OutgoingActionType::HaDisable, ""));
}

size_t Mqtt::getOutgoingQueueSize() const {
  if (outgoing_queue_ == nullptr) return 0;
  return uxQueueMessagesWaiting(outgoing_queue_);
}

size_t Mqtt::getOutgoingQueueHighWatermark() const {
  return max_outgoing_.load(std::memory_order_relaxed);
}

void Mqtt::internalPublish(const char* topic, uint8_t qos, bool retain,
                           const char* payload, bool prefix) {
  std::lock_guard<std::recursive_mutex> lock(mqtt_mutex_);
  if (!enabled_ || client_ == nullptr || payload == nullptr) return;
  // Send-time gate (pairs with the enqueue gate): lifecycle actions
  // (Discovery/Components/Ha*) still queue while disconnected, and the
  // drain pops unconditionally — without this, esp_mqtt_client spams
  // "Losing qos0 data" and every attempt counts as publish_failed.
  // Silent skip, HA-style; discovery re-runs on every (re)connect.
  if (!connected_) return;

  const char* targetTopic = topic;
  char fullTopic[256];

  if (prefix) {
    // Memory optimization: Use stack buffer for combined
    // Check potential length before calling snprintf
    size_t expected_len =
        std::strlen(root_topic_.c_str()) + 1 + std::strlen(topic);
    if (expected_len < sizeof(fullTopic)) {
      int n = snprintf(fullTopic, sizeof(fullTopic), "%s%s",
                       root_topic_.c_str(), topic);
      if (n > 0 && (size_t)n < sizeof(fullTopic)) {
        targetTopic = fullTopic;
      } else {
        // Fallback if construction fails or is too long for buffer
        logger.warn("[MQTT] Failed to construct MQTT topic via snprintf.");
        return;  // Abort publish attempt
      }
    } else {
      logger.warn(
          "[MQTT] Topic concatenation exceeds stack buffer size, skipping "
          "publish.");
      return;
    }
  }

  if (esp_mqtt_client_publish(client_, targetTopic, payload, 0, qos, retain) <
      0) {
    // Use static strings for error logging to avoid heap churn during link
    // congestion
    publish_failed_.fetch_add(1, std::memory_order_relaxed);
    logger.warn("[MQTT] Publish failed (buffer full or slow link)");
  } else {
    published_.fetch_add(1, std::memory_order_relaxed);
  }
}

void Mqtt::taskFunc(void* arg) {
  Mqtt* self = static_cast<Mqtt*>(arg);
  // Static storage: no heap, survives task restarts. Re-initialized on every
  // start so each task run gets a fresh empty queue (never vQueueDelete'd:
  // freeing static storage would corrupt the heap).
  static uint8_t
      outgoing_storage[max_outgoing_queue_size * sizeof(OutgoingAction)];
  static StaticQueue_t outgoing_cb;
  self->outgoing_queue_ =
      xQueueCreateStatic(max_outgoing_queue_size, sizeof(OutgoingAction),
                         outgoing_storage, &outgoing_cb);

  while (self->task_should_run_) {
    if (self->enabled_) {
      uint32_t currentMillis = (uint32_t)(esp_timer_get_time() / 1000ULL);
      if (self->connected_ &&
          currentMillis >
              self->last_status_publish_ + self->status_publish_interval_ms_) {
        self->last_status_publish_ = currentMillis;

        // Check if we should stop before doing telemetry
        // This prevents accessing eBUS resources after shutdown
        if (!self->task_should_run_) {
          continue;
        }

        // Slim telemetry state (single phase): liveness, version, heap
        // gauge, signal and bus health. Heavyweight status/resources
        // payloads moved to HTTP pull on purpose (no dual publication).
        self->publishStream(
            "state", 0, false, [](const ebus::JsonChunkVisitor& v) {
              ebus::detail::JsonWriter writer(v);
              auto scope = writer.objectScope();
              writer.writeField(
                  "uptime",
                  static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL));
              writer.writeField("reset_code", DeviceStatus::resetCode());
              writer.writeField("version", AUTO_VERSION);
              writer.writeField("build", __DATE__ " " __TIME__);
              {
                auto heap = writer.objectScope("heap");
                multi_heap_info_t info;
                heap_caps_get_info(&info,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                writer.writeField("free", info.total_free_bytes);
                writer.writeField("largest", info.largest_free_block);
                writer.writeField("min", info.minimum_free_bytes);
              }
              writer.writeField("rssi", WifiNetworkManager::RSSI());
              uint32_t err_total = 0;
              uint32_t msg_active = 0;
              uint32_t msg_total = 0;
              float err_rate = 0.0f;
              getEbusController().fetchMetrics([&](const ebus::Metrics& m) {
                err_total = m.handler.error_passive + m.handler.error_reactive +
                            m.handler.error_active;
                msg_active = m.handler.messages_active;
                msg_total = m.handler.messages_passive +
                            m.handler.messages_active +
                            m.handler.messages_reactive;
                err_rate = m.handler.errorRate();
              });
              {
                auto bus = writer.objectScope("bus");
                writer.writeFieldFloat("error_rate", err_rate);
                writer.writeField("error_total", err_total);
                writer.writeField("messages_active", msg_active);
                writer.writeField("messages_total", msg_total);
              }
            });
      }

      // Determine how long to wait before next telemetry or a new item
      uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
      uint32_t next_tele =
          self->last_status_publish_ + self->status_publish_interval_ms_;
      uint32_t wait_ms = (next_tele > now) ? (next_tele - now) : 10;

      OutgoingAction action;
      if (xQueueReceive(self->outgoing_queue_, &action,
                        pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
        // Check if we should stop before processing any action
        // This prevents processing actions after shutdown has been initiated
        if (!self->task_should_run_) {
          continue;
        }
        switch (action.type) {
          case OutgoingActionType::Component:
            // Iterate over all fields with HA enabled (same as
            // publishComponents)
            if (action.command) {
              for (size_t i = 0; i < action.command->getFieldCount(); ++i) {
                if (action.command->hasFieldHA(i)) {
                  self->haPublishComponent(action.command, i, action.ha_remove);
                }
              }
            }
            break;
          case OutgoingActionType::Error: {
            // Fix up transient pointers for JSON serialization
            action.protocol_info.master_view = action.master;
            action.protocol_info.slave_view = action.slave;
            self->publishStream("errors", 0, false,
                                [&](const ebus::JsonChunkVisitor& v) {
                                  ebus::detail::JsonWriter writer(v);
                                  action.protocol_info.toJson(writer);
                                });
            break;
          }
          case OutgoingActionType::Data: {
            self->publishStream("response", 0, false,
                                [&](const ebus::JsonChunkVisitor& v) {
                                  ebus::detail::JsonWriter writer(v);
                                  auto scope = writer.objectScope();
                                  writer.writeField("id", action.id);
                                  writer.writeHexField("master", action.master);
                                  writer.writeHexField("slave", action.slave);
                                });
            break;
          }
          case OutgoingActionType::Update:
            self->handleValueUpdate(action.key);
            break;
          case OutgoingActionType::Discovery:
            if (self->haEnabled()) self->haPublishDeviceInfo();
            break;
          case OutgoingActionType::Components:
            if (self->haEnabled()) self->haPublishComponents();
            break;
          case OutgoingActionType::HaEnable:
            self->haSetEnabled(true);
            self->haConnected();
            break;
          case OutgoingActionType::HaDisable:
            self->haConnected();  // publishes removals via !enabled_ in
                                  // publishComponents
            break;
          case OutgoingActionType::HaConnected:
            // Deferred out of IDF's event task: the full discovery storm
            // (JSON build + sync TLS publish per component) overflows its
            // 6KB stack. Runs here on our own task instead.
            self->haConnected();
            break;
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  if (self->outgoing_queue_ != nullptr) {
    xQueueReset(self->outgoing_queue_);
    self->outgoing_queue_ = nullptr;
  }
  self->task_exited_ = true;
  vTaskDelete(nullptr);
}

void Mqtt::eventHandler(void* handler_args, esp_event_base_t base,
                        int32_t event_id, void* event_data) {
  Mqtt* self = static_cast<Mqtt*>(handler_args);
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

  // Guard against stale events after client is destroyed
  if (self->client_ == nullptr || event == nullptr || event->client != self->client_) {
    return;
  }

  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_BEFORE_CONNECT: {
      char message[96];
      snprintf(message, sizeof(message), "[MQTT] Connecting: transport=%s, TLS=%s",
               self->transport_, self->tls_enabled_ ? "verify-required" : "off");
      logger.info(message);
      if (self->tls_enabled_ && !isClockInitialized()) {
        logger.warn("[MQTT] Clock not initialized: enable SNTP and wait for sync. "
                    "TLS may fail until then; verification stays enabled.");
      }
      logMqttMemory("before connect");
    } break;
    case MQTT_EVENT_CONNECTED: {
      char message[96];
      snprintf(message, sizeof(message), "[MQTT] Connected: transport=%s, TLS=%s",
               self->transport_, self->tls_enabled_ ? "verified" : "off");
      logger.info(message);
      logMqttMemory("connected");
      self->connected_ = true;
      self->connects_.fetch_add(1, std::memory_order_relaxed);
      int msg_id1 = esp_mqtt_client_subscribe(self->client_,
                                              self->request_topic_.c_str(), 0);
      if (msg_id1 > 0 && self->pending_subs_count_ < self->max_pending_subs) {
        snprintf(self->pending_subs_[self->pending_subs_count_].topic,
                 sizeof(self->pending_subs_[self->pending_subs_count_].topic),
                 "%s", self->request_topic_.c_str());
        self->pending_subs_[self->pending_subs_count_].msg_id = msg_id1;
        self->pending_subs_count_++;
      }
      // Simplified control topic: ebus/<id>/set/#
      std::string set_topic = self->root_topic_ + "set/#";
      int msg_id2 =
          esp_mqtt_client_subscribe(self->client_, set_topic.c_str(), 0);
      if (msg_id2 > 0 && self->pending_subs_count_ < self->max_pending_subs) {
        snprintf(self->pending_subs_[self->pending_subs_count_].topic,
                 sizeof(self->pending_subs_[self->pending_subs_count_].topic),
                 "%s", set_topic.c_str());
        self->pending_subs_[self->pending_subs_count_].msg_id = msg_id2;
        self->pending_subs_count_++;
      }

      self->publishStream(
          self->will_topic_.c_str(), 0, true,
          [](const ebus::JsonChunkVisitor& v) {
            ebus::detail::JsonWriter writer(v);
            auto scope = writer.objectScope();
            writer.writeField("value", "online");
          },
          false);

      Mqtt::enqueueOutgoing(
          OutgoingAction(OutgoingActionType::HaConnected, ""));
    } break;
    case MQTT_EVENT_DISCONNECTED: {
      logger.warn("[MQTT] Disconnected; will retry the same endpoint");
      self->connected_ = false;
      self->pending_subs_count_ =
          0;  // Clear pending subscriptions for reconnect
    } break;
    case MQTT_EVENT_SUBSCRIBED: {
      for (size_t i = 0; i < self->pending_subs_count_; ++i) {
        if (self->pending_subs_[i].msg_id == event->msg_id) {
          char dbg_buf[128];
          int n = snprintf(dbg_buf, sizeof(dbg_buf), "[MQTT] %s subscribed",
                           self->pending_subs_[i].topic);
          if (n > 0 && (size_t)n < sizeof(dbg_buf)) {
            logger.debug(dbg_buf);
          }
          // Remove from pending by shifting remaining
          for (size_t j = i + 1; j < self->pending_subs_count_; ++j) {
            self->pending_subs_[j - 1] = self->pending_subs_[j];
          }
          self->pending_subs_count_--;
          break;
        }
      }
    } break;
    case MQTT_EVENT_UNSUBSCRIBED:
    case MQTT_EVENT_PUBLISHED:
      break;
    case MQTT_EVENT_DATA: {
      // Use string_view for topic to avoid std::string allocation
      std::string_view topic_sv(event->topic, event->topic_len);
      std::string_view payload(event->data, event->data_len);
      char dbg_buf[128];
      snprintf(dbg_buf, sizeof(dbg_buf), "[MQTT] data received on topic: %.*s",
               (int)topic_sv.size(), topic_sv.data());
      logger.debug(dbg_buf);

      if (topic_sv == self->request_topic_) {
        ebus::detail::JsonReader reader(payload);
        if (reader.next() != ebus::detail::JsonReader::Token::object_start)
          return;

        std::string_view id_view;
        reader.forEachField(
            [&](std::string_view key, ebus::detail::JsonReader& r) {
              if (key == "id") {
                r.next();
                id_view = r.value();
                return false;  // Stop iteration
              }
              return true;
            });

        if (id_view.empty()) return;

        if (id_view == "read")
          self->handleRead(payload);
        else if (id_view == "write")
          self->handleWrite(payload);
        else if (id_view == "restart")
          restart();
      } else if (topic_sv.length() > self->root_topic_.length() + 4 &&
                 topic_sv.compare(0, self->root_topic_.length() + 4,
                                  self->root_topic_ + "set/") == 0) {
        std::string_view key_sv =
            topic_sv.substr(self->root_topic_.length() + 4);
        self->handleDirectWrite(key_sv,
                                payload);  // Pass string_view directly
      } else {
      }
    } break;
    case MQTT_EVENT_DELETED: {
      logger.debug("[MQTT] Message deleted");
    } break;
    case MQTT_EVENT_ERROR: {
      const esp_mqtt_error_codes_t* error = event->error_handle;
      if (error == nullptr) {
        logger.error("[MQTT] Error event without details");
        break;
      }
      char message[320];
      if (error->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        snprintf(message, sizeof(message),
                 "[MQTT] Transport/TLS error: esp=%s (0x%x), tls_stack=%d, "
                 "verify_flags=0x%x, socket_errno=%d (%s)",
                 esp_err_to_name(error->esp_tls_last_esp_err),
                 static_cast<unsigned>(error->esp_tls_last_esp_err),
                 error->esp_tls_stack_err,
                 static_cast<unsigned>(error->esp_tls_cert_verify_flags),
                 error->esp_transport_sock_errno,
                 std::strerror(error->esp_transport_sock_errno));
        logger.error(message);
        if (self->tls_enabled_ && error->esp_tls_cert_verify_flags != 0) {
          logger.warn("[MQTT] Check SNTP time, hostname, and the proxy's complete "
                      "certificate chain. Certificate verification was NOT bypassed.");
        }
      } else if (error->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        snprintf(message, sizeof(message),
                 "[MQTT] Broker refused CONNECT: code=%d (check MQTT credentials/ACL)",
                 static_cast<int>(error->connect_return_code));
        logger.error(message);
      } else {
        snprintf(message, sizeof(message), "[MQTT] Error type=%d",
                 static_cast<int>(error->error_type));
        logger.error(message);
      }
      logMqttMemory("error");
    } break;
    default: {
      logger.warn("[MQTT] Unhandled event " + std::to_string(event_id));
    } break;
  }
}

void Mqtt::handleRead(std::string_view payload) {
  ebus::detail::JsonReader reader(payload);
  if (reader.next() != ebus::detail::JsonReader::Token::object_start) return;

  std::string_view key_view;

  reader.forEachField([&](std::string_view k, ebus::detail::JsonReader& r) {
    if (k == "key") {
      if (r.next() == ebus::detail::JsonReader::Token::string) {
        key_view = r.value();
      }
    }
    return true;
  });

  if (key_view.empty()) return;

  Command* command = commandManager.findCommand(key_view);
  if (command != nullptr) {
    command->setLast(0);  // Trigger immediate physical poll
    handleValueUpdate(key_view);
  } else {  // Use snprintf for error response
    char err_buf[128];
    snprintf(err_buf, sizeof(err_buf), "key '%.*s' not found",
             (int)key_view.size(), key_view.data());
    publishResponse("read", err_buf);
  }
}

void Mqtt::handleWrite(std::string_view payload) {
  ebus::detail::JsonReader reader(payload);
  if (reader.next() != ebus::detail::JsonReader::Token::object_start) return;

  std::string_view key_view;
  std::string_view val_view;

  reader.forEachField([&](std::string_view k, ebus::detail::JsonReader& r) {
    if (k == "key") {
      if (r.next() == ebus::detail::JsonReader::Token::string) {
        key_view = r.value();
      }
    } else if (k == "value") {
      val_view = r.rawValue();
    }
    return true;
  });

  if (key_view.empty()) return;

  Command* command = commandManager.findCommand(key_view);
  if (command != nullptr) {
    if (val_view.empty()) {
      publishStream("response", 0, false,
                    [key_view](const ebus::JsonChunkVisitor& v) {
                      ebus::detail::JsonWriter writer(v);
                      auto scope = writer.objectScope();
                      writer.writeField("id", "write");
                      char status_buf[128];
                      snprintf(status_buf, sizeof(status_buf),
                               "missing value for key '%.*s'",
                               (int)key_view.size(), key_view.data());
                      writer.writeField("status", status_buf);
                    });
      return;
    }

    ebus::Sequence valueBytes;
    auto field_dt = command->getFieldDatatype(0);
    if (ebus::isNumeric(field_dt)) {
      // Strict parse (see command.cpp): garbage must not become 0.
      auto parsed = ebus::toNumStrict<double>(val_view);
      if (parsed) {
        double val = *parsed;
        if ((val >= command->getFieldMin(0)) &&
            (val <= command->getFieldMax(0))) {
          valueBytes = command->getVectorFromDouble(val, 0);
        }
      }
    } else {
      if (val_view.size() >= 2 && val_view.front() == '"' &&
          val_view.back() == '"') {
        val_view.remove_prefix(1);
        val_view.remove_suffix(1);
      }
      valueBytes = command->getVectorFromString(val_view, 0);
    }

    if (!valueBytes.empty()) {
      ebus::Sequence fullWrite =
          ebus::makeSequence(command->getWriteCmd(commandManager));
      fullWrite.append(valueBytes);

      getEbusController().enqueue(prio_send, fullWrite);
      publishStream("response", 0, false,
                    [command, key_view](const ebus::JsonChunkVisitor& v) {
                      ebus::detail::JsonWriter writer(v);
                      auto scope = writer.objectScope();
                      writer.writeField("id", "write");
                      char status_buf[256];
                      snprintf(status_buf, sizeof(status_buf),
                               "scheduled for key '%.*s' name '%.*s'",
                               (int)key_view.size(), key_view.data(),
                               (int)command->getName().size(),
                               command->getName().data());
                      writer.writeField("status", status_buf);
                    });
      command->setLast(0);
    } else {
      // Use snprintf for error response
      publishStream("response", 0, false,
                    [key_view](const ebus::JsonChunkVisitor& v) {
                      ebus::detail::JsonWriter writer(v);
                      auto scope = writer.objectScope();
                      writer.writeField("id", "write");
                      char status_buf[128];
                      snprintf(status_buf, sizeof(status_buf),
                               "invalid value for key '%.*s'",
                               (int)key_view.size(), key_view.data());
                      writer.writeField("status", status_buf);
                    });
    }
  } else {
    publishStream(
        "response", 0, false, [key_view](const ebus::JsonChunkVisitor& v) {
          ebus::detail::JsonWriter writer(v);
          auto scope = writer.objectScope();
          writer.writeField("id", "write");
          char status_buf[128];
          snprintf(status_buf, sizeof(status_buf), "key '%.*s' not found",
                   (int)key_view.size(), key_view.data());
          writer.writeField("status", status_buf);
        });
  }
}

void Mqtt::handleDirectWrite(std::string_view key, std::string_view val_view) {
  Command* command = commandManager.findCommand(key);
  if (command == nullptr) return;

  ebus::Sequence valueBytes;
  auto field_dt = command->getFieldDatatype(0);
  if (ebus::isNumeric(field_dt)) {
    // Strict parse (see command.cpp): garbage must not become 0.
    auto parsed = ebus::toNumStrict<double>(val_view);
    if (parsed) {
      double val = *parsed;
      if ((val >= command->getFieldMin(0)) &&
          (val <= command->getFieldMax(0))) {
        valueBytes = command->getVectorFromDouble(val, 0);
      }
    }
  } else {
    if (val_view.size() >= 2 && val_view.front() == '"' &&
        val_view.back() == '"') {
      val_view.remove_prefix(1);
      val_view.remove_suffix(1);
    }
    valueBytes = command->getVectorFromString(val_view, 0);
  }

  if (!valueBytes.empty()) {
    ebus::Sequence fullWrite =
        ebus::makeSequence(command->getWriteCmd(commandManager));
    fullWrite.append(valueBytes);
    getEbusController().enqueue(prio_send, fullWrite);
    command->setLast(0);
    char buf[128];
    snprintf(buf, sizeof(buf), "[MQTT] Scheduled write for '%.*s'",
             (int)key.size(), key.data());
    logger.info(buf);
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf), "[MQTT] Write failed for '%.*s'",
             (int)key.size(), key.data());
    logger.warn(buf);
  }
}

void Mqtt::handleValueUpdate(std::string_view key) {
  Command* cmd = commandManager.findCommand(key);
  if (!cmd) return;

  std::optional<ebus::DataValue> decoded;
  if (cmd->getFieldCount() > 0) {
    size_t field_pos = cmd->getFieldPosition(0) - 1;
    size_t field_len = ebus::sizeOfDataType(cmd->getFieldDatatype(0));
    if (field_pos + field_len <= cmd->getData().size()) {
      decoded = ebus::decode(cmd->getFieldDatatype(0),
                             ebus::range(cmd->getData(), field_pos, field_len));
    }
  }

  if (connected_) {
    char topicBuf[128];
    int n = snprintf(topicBuf, sizeof(topicBuf), "values/%.*s",
                     (int)cmd->getName().size(), cmd->getName().data());
    if (n > 0 && (size_t)n < sizeof(topicBuf)) {
      for (int i = 7; i < n; ++i)
        topicBuf[i] = (char)tolower((unsigned char)topicBuf[i]);

      publishStream(topicBuf, 0, false, [&](const ebus::JsonChunkVisitor& v) {
        ebus::detail::JsonWriter writer(v);
        auto scope = writer.objectScope();
        cmd->writeValuePayload(writer);
      });
    }
  }
}

void Mqtt::publishResponse(std::string_view id, std::string_view status,
                           size_t bytes) {
  if (!enabled_ || client_ == nullptr) return;
  publishStream("response", 0, false, [&](const ebus::JsonChunkVisitor& v) {
    ebus::detail::JsonWriter writer(v);
    auto scope = writer.objectScope();
    writer.writeField("id", id);
    writer.writeField("status", status);
    if (bytes > 0) writer.writeField("bytes", static_cast<uint32_t>(bytes));
  });
}

void appendMqttStatus(ebus::detail::JsonWriter& writer,
                      const AppConfig::Mqtt& mqtt_config) {
  writer.writeField("enabled",
                    Mqtt::instance_ != nullptr && Mqtt::instance_->isEnabled());
  writer.writeField("server", mqtt_config.server.c_str());
  writer.writeField("user", mqtt_config.user.c_str());
  writer.writeField("connected", Mqtt::instance_ != nullptr &&
                                     Mqtt::instance_->isConnected());
  const Mqtt* client = Mqtt::instance_;
  writer.writeField("server_valid", client != nullptr && client->isServerValid());
  writer.writeField("transport", client != nullptr ? client->getTransport() : "invalid");
  const bool tls = client != nullptr && client->usesTls();
  writer.writeField("tls_enabled", tls);
  writer.writeField("tls_verification", tls ? "ca+hostname+expiry" : "none");
  writer.writeField("clock_initialized", Mqtt::isClockInitialized());
}

#endif
