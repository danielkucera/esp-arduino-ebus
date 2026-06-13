#if defined(EBUS_INTERNAL)
#include "Mqtt.hpp"

#include <esp_timer.h>

#include <functional>

#include "Logger.hpp"
#include "MqttHA.hpp"
#include "Store.hpp"
#include "ebus/detail/json_reader.hpp"
#include "ebus/detail/json_writer.hpp"  // Include for JsonWriter
#include "ebus/status.hpp"
#include "ebus_accessor.hpp"
#include "main.hpp"

Mqtt mqtt;

void Mqtt::start() {
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
  if (connected_) esp_mqtt_client_stop(client_);
  start();
}

void Mqtt::startTask() {
  if (task_handle_ != nullptr) return;
  xTaskCreate(&Mqtt::taskFunc, "mqtt", 4096, this, 2, &task_handle_);
}

void Mqtt::stopTask() {
  if (task_handle_ != nullptr) {
    vTaskDelete(task_handle_);
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

  offline_payload_.clear();
  {
    ebus::detail::JsonWriter writer(
        [this](std::string_view s) { offline_payload_.append(s); });
    auto scope = writer.objectScope();
    writer.writeField("value", "offline");
  }

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
  std::string hostname;
  for (size_t i = 0; host[i] != '\0'; ++i)
    if (!std::isspace(host[i])) hostname += host[i];

  uri_ = "mqtt://" + hostname;
  if (port > 0) uri_ += ":" + std::to_string(port);

  mqtt_cfg_.broker.address.uri = uri_.c_str();
}

void Mqtt::setCredentials(const char* username, const char* password) {
  mqtt_cfg_.credentials.username = username;
  mqtt_cfg_.credentials.authentication.password = password;
}

void Mqtt::setRootTopic(const std::string& topic) {
  root_topic_ = topic;
  // Ensure proper formatting with trailing slash
  if (!root_topic_.empty() && root_topic_.back() != '/') {
    root_topic_ += '/';
  }
  will_topic_ = root_topic_ + "available";
  request_topic_ = root_topic_ + "request";
}

void Mqtt::setEnabled(const bool enable) { enabled_ = enable; }

bool Mqtt::isEnabled() const { return enabled_; }

bool Mqtt::isConnected() const { return connected_; }

const std::string& Mqtt::getUniqueId() const { return unique_id_; }

const std::string& Mqtt::getRootTopic() const { return root_topic_; }

const std::string& Mqtt::getWillTopic() const { return will_topic_; }

void Mqtt::internalPublish(const char* topic, uint8_t qos, bool retain,
                           const char* payload, bool prefix) {
  if (!enabled_ || client_ == nullptr || payload == nullptr) return;

  const char* targetTopic = topic;
  char fullTopic[256];

  if (prefix) {
    // Memory optimization: Use stack buffer for combined topic
    int n = snprintf(fullTopic, sizeof(fullTopic), "%s%s",
                     getRootTopic().c_str(), topic);
    if (n > 0 && (size_t)n < sizeof(fullTopic)) {
      targetTopic = fullTopic;
    }
  }

  if (esp_mqtt_client_publish(client_, targetTopic, payload, 0, qos, retain) <
      0) {
    // Use static strings for error logging to avoid heap churn during link
    // congestion
    logger.warn("MQTT: Publish failed (buffer full or slow link)");
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
  if (!enabled_ || client_ == nullptr) return;
  std::lock_guard<std::mutex> lock(publish_mutex_);
  publish_buffer_.clear();
  publish_buffer_.reserve(1024);
  builder([this](std::string_view s) { publish_buffer_.append(s); });
  internalPublish(topic, qos, retain, publish_buffer_.c_str(), prefix);
}

void Mqtt::enqueueOutgoing(const OutgoingAction& action) {
  if (!mqtt.enabled_) return;
  std::lock_guard<std::mutex> lock(mqtt.outgoing_queue_mutex_);
  if (mqtt.outgoing_queue_.push_back(action)) {
    logger.warn("MQTT: Outgoing queue full, dropping oldest message");
    ebus::updateMaxAtomic(mqtt.max_outgoing_, mqtt.outgoing_queue_.size());
  }
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

size_t Mqtt::getIncomingQueueSize() const {
  std::lock_guard<std::mutex> lock(incoming_queue_mutex_);
  return incoming_queue_.size();
}

size_t Mqtt::getOutgoingQueueSize() const {
  std::lock_guard<std::mutex> lock(outgoing_queue_mutex_);
  return outgoing_queue_.size();
}

size_t Mqtt::getIncomingQueueHighWatermark() const {
  return max_incoming_.load(std::memory_order_relaxed);
}

size_t Mqtt::getOutgoingQueueHighWatermark() const {
  return max_outgoing_.load(std::memory_order_relaxed);
}

void Mqtt::doLoop() {
  // Process up to 10 items per tick to catch up with bursts
  for (int i = 0; i < 10; ++i) {
    bool activity = false;
    if (checkIncomingQueue()) activity = true;
    if (checkOutgoingQueue()) activity = true;
    if (!activity) break;
  }
}

void Mqtt::taskFunc(void* arg) {
  Mqtt* self = static_cast<Mqtt*>(arg);
  uint8_t tele_phase = 0;

  for (;;) {
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
            self->publishStream("resources/app", 0, false,
                                [](const ebus::JsonChunkVisitor& v) {
                                  fetchAppResourcesJson(v);
                                });
            tele_phase = 2;
            break;

          case 2:
            self->publishStream("resources/lib", 0, false,
                                [](const ebus::JsonChunkVisitor& v) {
                                  getEbusController().fetchSystemResources(
                                      [&v](const ebus::SystemResources& res) {
                                        ebus::detail::JsonWriter writer(v);
                                        res.toJson(writer);
                                      });
                                });
            tele_phase = 0;
            break;
        }
      }
      self->doLoop();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void Mqtt::eventHandler(void* handler_args, esp_event_base_t base,
                        int32_t event_id, void* event_data) {
  Mqtt* self = static_cast<Mqtt*>(handler_args);
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_BEFORE_CONNECT: {
      logger.debug("MQTT before connect");
    } break;
    case MQTT_EVENT_CONNECTED: {
      logger.debug("MQTT connected");
      self->connected_ = true;
      esp_mqtt_client_subscribe(self->client_, self->request_topic_.c_str(), 0);

      self->publishStream(
          self->will_topic_.c_str(), 0, true,
          [](const ebus::JsonChunkVisitor& v) {
            ebus::detail::JsonWriter writer(v);
            auto scope = writer.objectScope();
            writer.writeField("value", "online");
          },
          false);

      if (mqttha.isEnabled()) mqttha.publishDeviceInfo();
    } break;
    case MQTT_EVENT_DISCONNECTED: {
      logger.debug("MQTT disconnected");
      self->connected_ = false;
    } break;
    case MQTT_EVENT_SUBSCRIBED: {
      logger.debug(self->request_topic_ + " subscribed");
    } break;
    case MQTT_EVENT_UNSUBSCRIBED:
    case MQTT_EVENT_PUBLISHED:
      break;
    case MQTT_EVENT_DATA: {
      logger.debug("MQTT data received");

      std::string_view payload(event->data, event->data_len);
      ebus::detail::JsonReader reader(payload);
      if (!reader.findKey("id")) return;
      reader.next();
      std::string_view id_view = reader.value();

      if (id_view == "insert") {
        self->handleInsert(payload);
      } else if (id_view == "remove") {
        self->handleRemove(payload);
      } else if (id_view == "scan") {
        self->handleScan(payload);
      } else if (id_view == "send") {
        self->handleSend(payload);
      } else if (id_view == "devices") {
        self->handleDevices(payload);
      } else if (id_view == "load") {
        self->handleLoad(payload);
      } else if (id_view == "save") {
        self->handleSave(payload);
      } else if (id_view == "wipe") {
        self->handleWipe(payload);
      } else if (id_view == "restart") {
        self->handleRestart(payload);
      } else if (id_view == "read") {
        self->handleRead(payload);
      } else if (id_view == "write") {
        self->handleWrite(payload);
      } else if (id_view == "publish") {
        self->handlePublish();
      } else if (id_view == "forward") {
        self->handleForward(payload);
      } else if (id_view == "reset") {
        self->handleReset(payload);
      } else {
        // Unrecognized command
        self->publishStream(
            "response", 0, false, [&](const ebus::JsonChunkVisitor& v) {
              ebus::detail::JsonWriter writer(v);
              auto scope = writer.objectScope();
              writer.writeField("id", "response");
              writer.writeField(
                  "error", "command '" + std::string(id_view) + "' not found");
            });
      }
    } break;
    case MQTT_EVENT_DELETED: {
    } break;
    case MQTT_EVENT_ERROR: {
      logger.error("MQTT Error occured");
    } break;
    default: {
      logger.warn("MQTT: Unhandled event " + std::to_string(event_id));
    } break;
  }
}

void Mqtt::handleRestart(std::string_view payload) { restart(); }

void Mqtt::handleInsert(std::string_view payload) {
  ebus::detail::JsonReader reader(payload);
  if (!reader.findKey("commands")) return;
  if (reader.next() != ebus::detail::JsonReader::Token::array_start) return;

  while (true) {
    std::string_view cmd_sv = reader.rawValue();
    if (cmd_sv.empty()) break;

    ebus::detail::JsonReader cmd_reader(cmd_sv);
    std::string evalError = Command::evaluate(cmd_reader);
    if (evalError.empty()) {
      cmd_reader.reset();
      std::lock_guard<std::mutex> lock(incoming_queue_mutex_);
      incoming_queue_.push_back(IncomingAction(Command::fromJson(cmd_reader)));
      ebus::updateMaxAtomic(max_incoming_, incoming_queue_.size());
    } else {
      publishStream("response", 0, false, [&](const ebus::JsonChunkVisitor& v) {
        ebus::detail::JsonWriter writer(v);
        auto scope = writer.objectScope();
        writer.writeField("id", "insert");
        writer.writeField("error", evalError);
      });
    }
  }
}

void Mqtt::handleRemove(std::string_view payload) {
  ebus::detail::JsonReader reader(payload);
  if (!reader.findKey("keys")) return;
  if (reader.next() != ebus::detail::JsonReader::Token::array_start) return;

  std::lock_guard<std::mutex> lock(incoming_queue_mutex_);
  while (true) {
    auto token = reader.next();
    if (token == ebus::detail::JsonReader::Token::array_end ||
        token == ebus::detail::JsonReader::Token::end)
      break;
    if (token == ebus::detail::JsonReader::Token::string) {
      incoming_queue_.push_back(IncomingAction(std::string(reader.value())));
      ebus::updateMaxAtomic(max_incoming_, incoming_queue_.size());
    }
  }
}

void Mqtt::handlePublish() {
  for (const Command* command : store.getCommands())
    enqueueOutgoing(OutgoingAction(command));
}

void Mqtt::handleLoad(std::string_view payload) {
  int64_t bytes = store.loadCommands();
  if (bytes > 0)
    publishResponse("load", "successful", bytes);
  else if (bytes < 0)
    publishResponse("load", "failed");
  else
    publishResponse("load", "no data");

  if (mqttha.isEnabled()) mqttha.publishComponents();
}

void Mqtt::handleSave(std::string_view payload) {
  int64_t bytes = store.saveCommands();
  if (bytes > 0)
    publishResponse("save", "successful", bytes);
  else if (bytes < 0)
    publishResponse("save", "failed");
  else
    publishResponse("save", "no data");
}

void Mqtt::handleWipe(std::string_view payload) {
  int64_t bytes = store.wipeCommands();
  if (bytes > 0)
    publishResponse("wipe", "successful", bytes);
  else if (bytes < 0)
    publishResponse("wipe", "failed");
  else
    publishResponse("wipe", "no data");
}

void Mqtt::handleScan(std::string_view payload) {
  ebus::detail::JsonReader reader(payload);
  if (reader.get("full") == ebus::detail::JsonReader::Token::boolean &&
      reader.asBool()) {
    getEbusController().initFullScan(true);
  } else {
    reader.reset();
    if (reader.findKey("addresses") &&
        reader.next() == ebus::detail::JsonReader::Token::array_start) {
      std::vector<uint8_t> addrVec;
      while (true) {
        auto t = reader.next();
        if (t == ebus::detail::JsonReader::Token::array_end ||
            t == ebus::detail::JsonReader::Token::end)
          break;
        std::string hex(reader.value());
        addrVec.push_back(
            static_cast<uint8_t>(std::strtoul(hex.c_str(), nullptr, 16)));
      }
      if (!addrVec.empty())
        getEbusController().scanAddresses(addrVec);
      else
        getEbusController().scanObservedDevices();
    } else {
      getEbusController().scanObservedDevices();
    }
  }
  publishResponse("scan", "initiated");
}

void Mqtt::handleDevices(std::string_view payload) {
  getEbusController().fetchDeviceInfo([](const ebus::DeviceInfo& device) {
    mqtt.enqueueOutgoing(OutgoingAction(device));
  });
}

void Mqtt::handleSend(std::string_view payload) {
  ebus::detail::JsonReader reader(payload);
  if (!reader.findKey("commands")) {
    publishResponse("send", "missing commands array");
    return;
  }
  if (reader.next() != ebus::detail::JsonReader::Token::array_start) return;
  while (true) {
    auto token = reader.next();
    if (token == ebus::detail::JsonReader::Token::array_end ||
        token == ebus::detail::JsonReader::Token::end)
      break;
    if (token == ebus::detail::JsonReader::Token::string) {
      getEbusController().enqueue(PRIO_SEND,
                                  ebus::toVector(std::string(reader.value())));
    }
  }
}

void Mqtt::handleForward(std::string_view payload) {
  ebus::detail::JsonReader reader(payload);
  if (reader.get("enable") == ebus::detail::JsonReader::Token::boolean) {
    // bool enabled = reader.asBool();
    // getEbusController().toggleForwarding(enabled);
  }

  reader.reset();
  if (reader.findKey("filters") &&
      reader.next() == ebus::detail::JsonReader::Token::array_start) {
    ebus::StaticVector<std::string_view, 8> filters;
    while (filters.size() < filters.capacity()) {
      auto t = reader.next();
      if (t == ebus::detail::JsonReader::Token::array_end ||
          t == ebus::detail::JsonReader::Token::end)
        break;
      if (t == ebus::detail::JsonReader::Token::string)
        filters.push_back(reader.value());
    }
    // getEbusController().setForwardingFilters(filters);
  }
}

void Mqtt::handleReset(std::string_view payload) {
  getEbusController().resetMetrics();
}

void Mqtt::handleRead(std::string_view payload) {
  ebus::detail::JsonReader reader(payload);
  if (!reader.findKey("key")) return;
  reader.next();
  std::string_view key_view = reader.value();

  const Command* command = store.findCommand(std::string(key_view));
  if (command != nullptr) {
    publishStream("response", 0, false,
                  [command](const ebus::JsonChunkVisitor& v) {
                    ebus::detail::JsonWriter writer(v);
                    auto scope = writer.objectScope();
                    writer.writeField("id", "read");
                    writer.appendKey("value");
                    command->getValueJson(writer);
                  });
  } else {
    publishStream(
        "response", 0, false, [key_view](const ebus::JsonChunkVisitor& v) {
          ebus::detail::JsonWriter writer(v);
          auto scope = writer.objectScope();
          writer.writeField("id", "read");
          writer.writeField("status",
                            "key '" + std::string(key_view) + "' not found");
        });
  }
}

void Mqtt::handleWrite(std::string_view payload) {
  ebus::detail::JsonReader reader(payload);
  if (!reader.findKey("key")) return;
  reader.next();
  std::string_view key_view = reader.value();

  Command* command = store.findCommand(std::string(key_view));
  if (command != nullptr) {
    reader.reset();
    std::string_view val_view;
    if (reader.findKey("value")) {
      val_view = reader.rawValue();
    }

    if (val_view.empty()) {
      publishStream(
          "response", 0, false, [key_view](const ebus::JsonChunkVisitor& v) {
            ebus::detail::JsonWriter writer(v);
            auto scope = writer.objectScope();
            writer.writeField("id", "write");
            writer.writeField("status", "missing value for key '" +
                                            std::string(key_view) + "'");
          });
      return;
    }

    ebus::Sequence valueBytes;
    if (command->getNumeric()) {
      // Use atof for numeric conversion as from_chars doesn't support double in
      // standard ESP-IDF libstdc++
      double val = std::atof(std::string(val_view).c_str());
      if ((val >= command->getMin()) && (val <= command->getMax())) {
        valueBytes = command->getVectorFromDouble(val);
      }
    } else {
      // For strings, strip potential quotes
      if (val_view.size() >= 2 && val_view.front() == '"' &&
          val_view.back() == '"') {
        val_view.remove_prefix(1);
        val_view.remove_suffix(1);
      }
      valueBytes = command->getVectorFromString(std::string(val_view));
    }

    if (!valueBytes.empty()) {
      ebus::Sequence fullWrite = command->getWriteCmd();
      fullWrite.append(valueBytes);

      getEbusController().enqueue(PRIO_SEND, fullWrite);
      publishStream("response", 0, false,
                    [command, key_view](const ebus::JsonChunkVisitor& v) {
                      ebus::detail::JsonWriter writer(v);
                      auto scope = writer.objectScope();
                      writer.writeField("id", "write");
                      writer.writeField("status", "scheduled for key '" +
                                                      std::string(key_view) +
                                                      "' name '" +
                                                      command->getName() + "'");
                    });
      command->setLast(0);
    } else {
      publishStream(
          "response", 0, false, [key_view](const ebus::JsonChunkVisitor& v) {
            ebus::detail::JsonWriter writer(v);
            auto scope = writer.objectScope();
            writer.writeField("id", "write");
            writer.writeField("status", "invalid value for key '" +
                                            std::string(key_view) + "'");
          });
    }
  } else {
    publishStream(
        "response", 0, false, [key_view](const ebus::JsonChunkVisitor& v) {
          ebus::detail::JsonWriter writer(v);
          auto scope = writer.objectScope();
          writer.writeField("id", "write");
          writer.writeField("status",
                            "key '" + std::string(key_view) + "' not found");
        });
  }
}

bool Mqtt::checkIncomingQueue() {
  IncomingAction action(std::string(""));
  bool has_action = false;

  {
    std::lock_guard<std::mutex> lock(incoming_queue_mutex_);
    if (!incoming_queue_.empty() && (uint32_t)(esp_timer_get_time() / 1000ULL) >
                                        last_incoming_ + incoming_interval_) {
      incoming_queue_.tryPop(action);
      has_action = true;
    }
  }

  if (has_action) {
    last_incoming_ = (uint32_t)(esp_timer_get_time() / 1000ULL);

    switch (action.type) {
      case IncomingActionType::Insert:
        store.insertCommand(action.command);
        if (mqttha.isEnabled()) mqttha.publishComponent(&action.command, false);
        publishStream(
            "response", 0, false, [&](const ebus::JsonChunkVisitor& v) {
              ebus::detail::JsonWriter writer(v);
              auto scope = writer.objectScope();
              writer.writeField("id", "insert");
              writer.writeField(
                  "status", "key '" + action.command.getKey() + "' inserted");
            });
        break;
      case IncomingActionType::Remove:
        const Command* cmd = store.findCommand(action.key);
        if (cmd) {
          if (mqttha.isEnabled()) mqttha.publishComponent(cmd, true);
          store.removeCommand(action.key);
          publishStream(
              "response", 0, false, [&](const ebus::JsonChunkVisitor& v) {
                ebus::detail::JsonWriter writer(v);
                auto scope = writer.objectScope();
                writer.writeField("id", "remove");
                writer.writeField("status", "key '" + action.key + "' removed");
              });
        } else {
          publishStream("response", 0, false,
                        [&](const ebus::JsonChunkVisitor& v) {
                          ebus::detail::JsonWriter writer(v);
                          auto scope = writer.objectScope();
                          writer.writeField("id", "remove");
                          writer.writeField(
                              "status", "key '" + action.key + "' not found");
                        });
        }
        break;
    }
    return true;
  }
  return false;
}

bool Mqtt::checkOutgoingQueue() {
  OutgoingAction action(static_cast<const Command*>(nullptr));
  bool has_action = false;

  {
    std::lock_guard<std::mutex> lock(outgoing_queue_mutex_);
    if (!outgoing_queue_.empty() && (uint32_t)(esp_timer_get_time() / 1000ULL) >
                                        last_outgoing_ + outgoing_interval_) {
      outgoing_queue_.tryPop(action);
      has_action = true;
    }
  }

  if (has_action) {
    last_outgoing_ = (uint32_t)(esp_timer_get_time() / 1000ULL);

    switch (action.type) {
      case OutgoingActionType::Command:
        if (action.command) {
          publishCommand(action.command);
        }
        break;
      case OutgoingActionType::Device:
        publishDevice(action.device);
        break;
      case OutgoingActionType::Component:
        mqttha.publishComponent(action.command, action.ha_remove);
        break;
      case OutgoingActionType::Error: {
        publishStream("errors", 0, false, [&](const ebus::JsonChunkVisitor& v) {
          ebus::detail::JsonWriter writer(v);
          action.protocol_info.toJson(writer);
        });
        break;
      }
      case OutgoingActionType::Data: {
        publishStream("response", 0, false,
                      [&](const ebus::JsonChunkVisitor& v) {
                        ebus::detail::JsonWriter writer(v);
                        auto scope = writer.objectScope();
                        writer.writeField("id", action.id);
                        writer.writeHexField("master", action.master);
                        writer.writeHexField("slave", action.slave);
                      });
        break;
      }
      case OutgoingActionType::Update: {
        handleValueUpdate(action.key);
        break;
      }
    }
    return true;
  }
  return false;
}

void Mqtt::handleValueUpdate(std::string_view key) {
  Command* cmd = store.findCommand(std::string(key));
  if (!cmd) return;

  // Correlation Optimization: Decode once and reuse for both JSON and logging
  auto decoded = ebus::decode(cmd->getDatatype(), cmd->getData());

  if (connected_) {
    char topicBuf[128];
    int n = snprintf(topicBuf, sizeof(topicBuf), "values/%s",
                     cmd->getName().c_str());
    if (n > 0 && (size_t)n < sizeof(topicBuf)) {
      for (int i = 7; i < n; ++i)
        topicBuf[i] = (char)tolower((unsigned char)topicBuf[i]);

      publishStream(topicBuf, 0, false, [&](const ebus::JsonChunkVisitor& v) {
        ebus::detail::JsonWriter writer(v);
        auto scope = writer.objectScope();
        writer.appendKey("value");
        cmd->getValueJson(writer);
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

void Mqtt::logUpdate(const Command* cmd,
                     const std::optional<ebus::DataValue>& decoded) {
  char logBuf[512];
  char* p = logBuf;
  const char* end_buf = logBuf + sizeof(logBuf);

  auto appendStr = [&](std::string_view s) {
    if (p >= end_buf - 1) return;
    size_t n = std::min(s.size(), static_cast<size_t>(end_buf - p - 1));
    std::memcpy(p, s.data(), n);
    p += n;
  };

  auto appendHex = [&](ebus::ByteView data) {
    static constexpr char hex_chars[] = "0123456789abcdef";
    for (uint8_t b : data) {
      if (p + 2 >= end_buf) break;
      *p++ = hex_chars[b >> 4];
      *p++ = hex_chars[b & 0xf];
    }
  };

  appendStr(" '");
  appendHex(cmd->getReadCmd());
  appendStr("' [");
  appendStr(cmd->getName());
  appendStr("] ");
  appendHex(cmd->getData());
  appendStr(" -> ");

  if (!decoded || ebus::isNull(*decoded)) {
    appendStr("null");
  } else if (cmd->getNumeric()) {
    p = ebus::formatFloat(
        ebus::asFloat(*decoded) / cmd->getDivider(), cmd->getDigits(), p,
        end_buf - p, ebus::detail::FormattingLimits::float_lower_threshold,
        ebus::detail::FormattingLimits::float_upper_threshold);
  } else {
    appendStr(ebus::asString(*decoded));
  }

  if (!cmd->getUnit().empty()) {
    appendStr(" ");
    appendStr(cmd->getUnit());
  }

  *p = '\0';
  logger.debug(logBuf);
}

void Mqtt::publishCommand(const Command* command) {
  char topicBuf[128];
  snprintf(topicBuf, sizeof(topicBuf), "commands/%s",
           command->getKey().c_str());
  publishStream(topicBuf, 0, false, [&](const ebus::JsonChunkVisitor& v) {
    ebus::detail::JsonWriter writer(v);
    command->toJson(writer);
  });
}

void Mqtt::publishDevice(const ebus::DeviceInfo& device) {
  char topicBuf[64];
  snprintf(topicBuf, sizeof(topicBuf), "devices/%02x", device.slave_address);
  publishStream(topicBuf, 0, false, [&](const ebus::JsonChunkVisitor& v) {
    ebus::detail::JsonWriter writer(v);
    device.toJson(writer);
  });
}

#endif
