#if defined(EBUS_INTERNAL)
#include "mqtt.hpp"

#include <esp_timer.h>

#include <functional>

#include "command_manager.hpp"
#include "ebus/detail/json_reader.hpp"
#include "ebus/detail/json_writer.hpp"  // Include for JsonWriter
#include "ebus/status.hpp"
#include "ebus_accessor.hpp"
#include "logger.hpp"
#include "main.hpp"
#include "mqtt_ha.hpp"

Mqtt mqtt;

void Mqtt::start() {
  std::lock_guard<std::recursive_mutex> lock(mqtt_mutex_);
  if (enabled_) {
    // Important: destroy previous client to free resources and close sockets
    if (client_ != nullptr) {
      esp_mqtt_client_destroy(client_);
      client_ = nullptr;
    }
    client_ = esp_mqtt_client_init(&mqtt_cfg_);
    esp_mqtt_client_register_event(client_,
                                   (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                   &Mqtt::eventHandler, this);
    esp_mqtt_client_start(client_);
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
  xTaskCreate(&Mqtt::taskFunc, "mqtt", 7168, this, 3, &task_handle_);
}

void Mqtt::stopTask() {
  if (task_handle_ != nullptr) {
    // Signal task to stop
    task_should_run_ = false;

    // Small delay to allow loop to exit if blocked in xQueueReceive
    vTaskDelay(pdMS_TO_TICKS(50));
    if (outgoing_queue_ != nullptr) {
      vQueueDelete(outgoing_queue_);
      outgoing_queue_ = nullptr;
    }
    task_handle_ = nullptr;
  }
}

void Mqtt::setStatusProvider(
    std::function<void(const ebus::JsonChunkVisitor&)> provider) {
  status_provider_ = std::move(provider);
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
  std::string hostname;
  for (size_t i = 0; host[i] != '\0'; ++i)
    if (!std::isspace(host[i])) hostname += host[i];

  uri_ = "mqtt://" + hostname;
  if (port > 0) uri_ += ":" + std::to_string(port);

  mqtt_cfg_.broker.address.uri = uri_.c_str();
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

const std::string& Mqtt::getUniqueId() const { return unique_id_; }

const std::string& Mqtt::getRootTopic() const { return root_topic_; }

const std::string& Mqtt::getWillTopic() const { return will_topic_; }

void Mqtt::internalPublish(const char* topic, uint8_t qos, bool retain,
                           const char* payload, bool prefix) {
  std::lock_guard<std::recursive_mutex> lock(mqtt_mutex_);
  if (!enabled_ || client_ == nullptr || payload == nullptr) return;

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
    logger.warn("[MQTT] Publish failed (buffer full or slow link)");
  }
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

void Mqtt::publishDiscovery() {
  if (!mqtt.enabled_) return;
  enqueueOutgoing(OutgoingAction(OutgoingActionType::Discovery, ""));
}

void Mqtt::publishComponentDiscovery() {
  if (!mqtt.enabled_) return;
  enqueueOutgoing(OutgoingAction(OutgoingActionType::Components, ""));
}

void Mqtt::enqueueOutgoing(const OutgoingAction& action) {
  if (!mqtt.enabled_ || mqtt.outgoing_queue_ == nullptr) return;

  if (xQueueSend(mqtt.outgoing_queue_, &action, 0) != pdPASS) {
    // Mimic CircularBuffer behavior: drop oldest to make room for new
    OutgoingAction dummy;
    if (xQueueReceive(mqtt.outgoing_queue_, &dummy, 0) == pdTRUE) {
      xQueueSend(mqtt.outgoing_queue_, &action, 0);
    }
    logger.warn("[MQTT] Outgoing queue full, dropped oldest message");
  }

  size_t current = uxQueueMessagesWaiting(mqtt.outgoing_queue_);
  ebus::updateMaxAtomic(mqtt.max_outgoing_, current);
}

void Mqtt::publishData(const std::string& id,
                       const std::vector<uint8_t>& master,
                       const std::vector<uint8_t>& slave) {
  if (!mqtt.enabled_) return;
  enqueueOutgoing(OutgoingAction(id, master, slave));
}

void Mqtt::publishError(const ebus::ProtocolInfo& info) {
  if (!mqtt.enabled_) return;
  enqueueOutgoing(OutgoingAction(info));
}

void Mqtt::publishValue(std::string_view key) {
  if (!mqtt.enabled_) return;
  enqueueOutgoing(
      OutgoingAction(OutgoingActionType::Update, key));  // Pass string_view
}

size_t Mqtt::getOutgoingQueueSize() const {
  if (outgoing_queue_ == nullptr) return 0;
  return uxQueueMessagesWaiting(outgoing_queue_);
}

size_t Mqtt::getOutgoingQueueHighWatermark() const {
  return max_outgoing_.load(std::memory_order_relaxed);
}

void Mqtt::taskFunc(void* arg) {
  Mqtt* self = static_cast<Mqtt*>(arg);
  self->outgoing_queue_ =
      xQueueCreate(max_outgoing_queue_size, sizeof(OutgoingAction));

  uint8_t tele_phase = 0;

  while (self->task_should_run_) {
    if (self->enabled_) {
      uint32_t currentMillis = (uint32_t)(esp_timer_get_time() / 1000ULL);
      if (self->connected_ &&
          currentMillis >
              self->last_status_publish_ + self->status_publish_interval_ms_) {
        self->last_status_publish_ = currentMillis;

        // Telemetry Rotation: Cycle through different status payloads to spread
        // out heap usage and network traffic, preventing congestion on weak
        // links.
        switch (tele_phase) {
          case 0:
            if (self->status_provider_)
              self->publishStream("state", 0, false, self->status_provider_);
            tele_phase = 1;
            break;

          case 1:
            self->publishStream(
                "resources/app", 0, false,
                [](const ebus::JsonChunkVisitor& v) { fetchAppStatus(v); });
            tele_phase = 2;
            break;

          case 2:
            self->publishStream("resources/lib", 0, false,
                                [](const ebus::JsonChunkVisitor& v) {
                                  getEbusController().fetchStatus(
                                      [&v](const ebus::SystemResources& res) {
                                        ebus::detail::JsonWriter writer(v);
                                        res.toJson(writer);
                                      });
                                });
            tele_phase = 0;
            break;
        }
      }

      // Determine how long to wait before next telemetry or a new item
      uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
      uint32_t next_tele =
          self->last_status_publish_ + self->status_publish_interval_ms_;
      uint32_t wait_ms = (next_tele > now) ? (next_tele - now) : 10;

      OutgoingAction action;
      if (xQueueReceive(self->outgoing_queue_, &action,
                        pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
        switch (action.type) {
          case OutgoingActionType::Component:
            // Iterate over all fields with HA enabled (same as
            // publishComponents)
            if (action.command) {
              for (size_t i = 0; i < action.command->getFieldCount(); ++i) {
                if (action.command->getFieldHA(i)) {
                  mqttha.publishComponent(action.command, i, action.ha_remove);
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
            mqttha.publishDeviceInfo();
            break;
          case OutgoingActionType::Components:
            mqttha.publishComponents();
            break;
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  if (self->outgoing_queue_ != nullptr) {
    vQueueDelete(self->outgoing_queue_);
    self->outgoing_queue_ = nullptr;
  }
  self->task_handle_ = nullptr;
  vTaskDelete(nullptr);
}

void Mqtt::eventHandler(void* handler_args, esp_event_base_t base,
                        int32_t event_id, void* event_data) {
  Mqtt* self = static_cast<Mqtt*>(handler_args);
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_BEFORE_CONNECT: {
      logger.debug("[MQTT] before connect");
    } break;
    case MQTT_EVENT_CONNECTED: {
      logger.debug("[MQTT] connected");
      self->connected_ = true;
      esp_mqtt_client_subscribe(self->client_, self->request_topic_.c_str(), 0);
      // Simplified control topic: ebus/<id>/set/#
      std::string set_topic = self->root_topic_ + "set/#";
      esp_mqtt_client_subscribe(self->client_, set_topic.c_str(), 0);

      self->publishStream(
          self->will_topic_.c_str(), 0, true,
          [](const ebus::JsonChunkVisitor& v) {
            ebus::detail::JsonWriter writer(v);
            auto scope = writer.objectScope();
            writer.writeField("value", "online");
          },
          false);

      if (mqttha.isEnabled()) {
        mqttha.publishDeviceInfo();
        Mqtt::publishComponentDiscovery();
      }
    } break;
    case MQTT_EVENT_DISCONNECTED: {
      logger.debug("[MQTT] disconnected");
      self->connected_ = false;
    } break;
    case MQTT_EVENT_SUBSCRIBED: {
      logger.debug("[MQTT] " + self->request_topic_ + " subscribed");
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
        if (!reader.findKey("id")) return;
        reader.next();
        std::string_view id_view = reader.value();

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
      logger.error("[MQTT] Error occurred");
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
      double val = ebus::toNum<double>(val_view);
      if ((val >= command->getFieldMin(0)) &&
          (val <= command->getFieldMax(0))) {
        valueBytes = command->getVectorFromDouble(val, 0);
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

void Mqtt::handleValueUpdate(std::string_view key) {
  Command* cmd = commandManager.findCommand(key);
  if (!cmd) return;

  // Correlation Optimization: Decode once and reuse for both JSON and logging
  auto decoded = ebus::decode(cmd->getFieldDatatype(0), cmd->getData());

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

  logUpdate(cmd, decoded);
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

void Mqtt::handleDirectWrite(std::string_view key, std::string_view val_view) {
  Command* command = commandManager.findCommand(key);
  if (command == nullptr) return;

  ebus::Sequence valueBytes;
  auto field_dt = command->getFieldDatatype(0);
  if (ebus::isNumeric(field_dt)) {
    double val = ebus::toNum<double>(val_view);
    if ((val >= command->getFieldMin(0)) && (val <= command->getFieldMax(0))) {
      valueBytes = command->getVectorFromDouble(val, 0);
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

void Mqtt::logUpdate(const Command* cmd,
                     const std::optional<ebus::DataValue>& decoded) {
  char logBuf[256];
  char* p = logBuf;
  const char* end_buf = logBuf + sizeof(logBuf);

  // Helper lambda to append a string safely
  auto appendStr = [&](std::string_view s) {
    if (p >= end_buf - 1) return;  // Leave space for null terminator
    size_t n = std::min(s.size(), static_cast<size_t>(end_buf - p - 1));
    std::memcpy(p, s.data(), n);
    p += n;
  };

  // Helper lambda to append hex data safely
  auto appendHex = [&](ebus::ByteView data) {
    static constexpr char hex_chars[] = "0123456789abcdef";
    for (uint8_t b : data) {
      if (p + 2 >= end_buf) break;  // Ensure space for 2 hex chars
      *p++ = hex_chars[b >> 4];
      *p++ = hex_chars[b & 0xf];
    }
  };

  // Build the log message
  appendStr(" '");
  appendHex(cmd->getReadCmd());
  appendStr("' [");
  appendStr(cmd->getName());
  appendStr("] ");
  appendHex(cmd->getData());
  appendStr(" -> ");

  // Handle decoded value
  if (!decoded || ebus::isNull(*decoded)) {
    appendStr("null");
  } else if (ebus::isNumeric(cmd->getFieldDatatype(0))) {
    p = ebus::formatFloat(
        ebus::asFloat(*decoded) / cmd->getFieldDivider(0),
        cmd->getFieldDigits(0), p, end_buf - p,
        ebus::detail::FormattingLimits::float_lower_threshold,
        ebus::detail::FormattingLimits::float_upper_threshold);
  } else {
    appendStr(ebus::asString(*decoded));
  }

  if (!std::string(cmd->getFieldUnit(0)).empty()) {
    appendStr(" ");
    appendStr(cmd->getFieldUnit(0));
  }

  // Null-terminate the string
  *p = '\0';

  // Log the message
  logger.debug(logBuf);
}

#endif
