#if defined(EBUS_INTERNAL)
#include "Mqtt.hpp"

#include <cJSON.h>
#include <esp_timer.h>

#include <functional>

#include "Logger.hpp"
#include "MqttHA.hpp"
#include "Store.hpp"
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

  mqtt_cfg_.credentials.client_id = client_id_.c_str();
  // Last Will
  mqtt_cfg_.session.last_will.topic = will_topic_.c_str();
  mqtt_cfg_.session.last_will.msg = "{ \"value\": \"offline\" }";
  mqtt_cfg_.session.last_will.msg_len = 0;
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

void Mqtt::publish(const char* topic, uint8_t qos, bool retain,
                   const char* payload, bool prefix) {
  if (!enabled_ || client_ == nullptr || payload == nullptr) return;

  if (prefix) {
    // Memory optimization: Use stack buffer for combined topic to avoid
    // std::string concatenation
    char fullTopic[256];
    int n = snprintf(fullTopic, sizeof(fullTopic), "%s%s", root_topic_.c_str(),
                     topic);
    if (n > 0 && (size_t)n < sizeof(fullTopic)) {
      if (esp_mqtt_client_publish(client_, fullTopic, payload, 0, qos, retain) <
          0) {
        // Use static strings for error logging to avoid heap churn during link
        // congestion
        logger.warn("MQTT: Publish failed (buffer full or slow link)");
      }
      return;
    }
  }

  if (esp_mqtt_client_publish(client_, topic, payload, 0, qos, retain) < 0) {
    logger.warn("MQTT: Publish failed");
  }
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
  publish(topic, qos, retain, publish_buffer_.c_str(), prefix);
}

void Mqtt::enqueueOutgoing(const OutgoingAction& action) {
  if (!mqtt.enabled_) return;
  std::lock_guard<std::mutex> lock(mqtt.outgoing_queue_mutex_);
  if (mqtt.outgoing_queue_.push_back(action)) {
    logger.warn("MQTT: Outgoing queue full, dropping oldest message");
  }
}

void Mqtt::publishData(const std::string& id,
                       const std::vector<uint8_t>& master,
                       const std::vector<uint8_t>& slave) {
  if (!mqtt.enabled_) return;
  enqueueOutgoing(OutgoingAction(id, master, slave));
}

void Mqtt::publishError(const ebus::ErrorInfo& info) {
  if (!mqtt.enabled_) return;
  enqueueOutgoing(OutgoingAction(info));
}

void Mqtt::publishValue(const std::string& key) {
  if (!mqtt.enabled_) return;
  enqueueOutgoing(OutgoingAction(OutgoingActionType::Update, key));
}

size_t Mqtt::getIncomingQueueSize() const {
  std::lock_guard<std::mutex> lock(incoming_queue_mutex_);
  return incoming_queue_.size();
}

size_t Mqtt::getOutgoingQueueSize() const {
  std::lock_guard<std::mutex> lock(outgoing_queue_mutex_);
  return outgoing_queue_.size();
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

      self->publish(self->will_topic_.c_str(), 0, true,
                    "{ \"value\": \"online\" }", false);

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
      std::string_view id_view = ebus::extract(payload, "id");
      if (id_view.size() >= 2 && id_view.front() == '"' &&
          id_view.back() == '"') {
        id_view.remove_prefix(1);
        id_view.remove_suffix(1);
      }

      if (id_view == "insert") {
        self->handleInsert(payload);
        return;
      }

      if (id_view == "remove") {
        self->handleRemove(payload);
        return;
      }

      if (id_view == "scan") {
        self->handleScan(payload);
        return;
      }

      if (id_view == "send") {
        self->handleSend(payload);
        return;
      }

      if (id_view == "devices") {
        self->handleDevices(payload);
        return;
      }

      if (id_view == "load") {
        self->handleLoad(payload);
        return;
      }

      if (id_view == "save") {
        self->handleSave(payload);
        return;
      }

      if (id_view == "wipe") {
        self->handleWipe(payload);
        return;
      }

      if (id_view == "restart") {
        self->handleRestart(payload);
        return;
      }

      if (id_view == "read") {
        self->handleRead(payload);
        return;
      }

      if (id_view == "write") {
        self->handleWrite(payload);
        return;
      }

      if (id_view == "publish") {
        self->handlePublish();
        return;
      }

      if (id_view == "forward") {
        self->handleForward(payload);
        return;
      }

      if (id_view == "reset") {
        self->handleReset(payload);
        return;
      }

      // Unrecognized command
      self->publishStream("response", 0, false,
                          [&](const ebus::JsonChunkVisitor& v) {
                            ebus::detail::JsonWriter writer(v);
                            writer.startObject();
                            writer.appendKey("error");
                            writer.write("\"command '");
                            writer.write(id_view);
                            writer.write("' not found\"");
                            writer.endObject();
                          });
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
  std::string_view commands_part = ebus::extractSub(payload, "commands");
  if (commands_part.empty()) return;

  // Minimal iterative parser to avoid full cJSON DOM of the array
  size_t pos = 0;
  while (pos < commands_part.size()) {
    size_t start_obj = commands_part.find('{', pos);
    if (start_obj == std::string_view::npos) break;

    // Find matching } by balancing braces
    int depth = 0;
    size_t end_obj = std::string_view::npos;
    for (size_t i = start_obj; i < commands_part.size(); ++i) {
      if (commands_part[i] == '{')
        depth++;
      else if (commands_part[i] == '}') {
        depth--;
        if (depth == 0) {
          end_obj = i;
          break;
        }
      }
    }
    if (end_obj == std::string_view::npos) break;

    std::string_view cmd_sv =
        commands_part.substr(start_obj, end_obj - start_obj + 1);

    // Parse only this single command object
    cJSON* cmd_doc = cJSON_ParseWithLength(cmd_sv.data(), cmd_sv.size());
    if (cmd_doc) {
      std::string evalError = Command::evaluate(cmd_doc);
      if (evalError.empty()) {
        std::lock_guard<std::mutex> lock(incoming_queue_mutex_);
        incoming_queue_.push_back(IncomingAction(Command::fromJson(cmd_doc)));
      } else {
        publishStream("response", 0, false,
                      [&](const ebus::JsonChunkVisitor& v) {
                        ebus::detail::JsonWriter writer(v);
                        writer.startObject();
                        writer.writeField("error", evalError);
                        writer.endObject();
                      });
      }
      cJSON_Delete(cmd_doc);
    }
    pos = end_obj + 1;
  }
}

void Mqtt::handleRemove(std::string_view payload) {
  std::string_view keys_part = ebus::extractSub(payload, "keys");

  {
    std::lock_guard<std::mutex> lock(incoming_queue_mutex_);
    if (keys_part.empty() || keys_part == "[]") {
      // Remove all if keys array is missing or empty
      for (const Command* command : store.getCommands())
        incoming_queue_.push_back(IncomingAction(command->getKey()));
    } else {
      // Minimal iterative parser to avoid full cJSON DOM of the keys array
      size_t pos = 0;
      while (pos < keys_part.size()) {
        size_t start_quote = keys_part.find('"', pos);
        if (start_quote == std::string_view::npos) break;
        size_t end_quote = keys_part.find('"', start_quote + 1);
        if (end_quote == std::string_view::npos) break;

        std::string key(
            keys_part.substr(start_quote + 1, end_quote - start_quote - 1));
        incoming_queue_.push_back(IncomingAction(key));
        pos = end_quote + 1;
      }
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
  std::string_view full_view = ebus::extract(payload, "full");
  if (full_view == "true") {
    getEbusController().initFullScan(true);
  } else {
    std::string_view addr_part = ebus::extractSub(payload, "addresses");
    if (addr_part.empty() || addr_part == "[]") {
      getEbusController().scanObservedDevices();
    } else {
      std::vector<uint8_t> addrVec;
      size_t pos = 0;
      while (pos < addr_part.size()) {
        size_t s = addr_part.find('"', pos);
        if (s == std::string_view::npos) break;
        size_t e = addr_part.find('"', s + 1);
        if (e == std::string_view::npos) break;
        std::string hex(addr_part.substr(s + 1, e - s - 1));
        addrVec.push_back(
            static_cast<uint8_t>(std::strtoul(hex.c_str(), nullptr, 16)));
        pos = e + 1;
      }
      if (!addrVec.empty()) getEbusController().scanAddresses(addrVec);
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
  std::string_view commands_part = ebus::extractSub(payload, "commands");
  if (commands_part.empty() || commands_part == "[]") {
    publishResponse("send", "commands array invalid");
  } else {
    size_t pos = 0;
    while (pos < commands_part.size()) {
      size_t s = commands_part.find('"', pos);
      if (s == std::string_view::npos) break;
      size_t e = commands_part.find('"', s + 1);
      if (e == std::string_view::npos) break;
      std::string cmd_hex(commands_part.substr(s + 1, e - s - 1));
      getEbusController().enqueue(PRIO_SEND, ebus::toVector(cmd_hex));
      pos = e + 1;
    }
  }
}

void Mqtt::handleForward(std::string_view payload) {
  std::string_view enable_view = ebus::extract(payload, "enable");
  bool enabled = (enable_view == "true");
  (void)enabled;
  // getEbusController().toggleForwarding(enabled);

  std::string_view filters_part = ebus::extractSub(payload, "filters");
  if (!filters_part.empty() && filters_part != "[]") {
    // Use StaticVector to avoid heap allocation for the collection of views
    ebus::detail::StaticVector<std::string_view, 8> filters;
    size_t pos = 0;
    while (pos < filters_part.size() && filters.size() < filters.capacity()) {
      size_t s = filters_part.find('"', pos);
      if (s == std::string_view::npos) break;
      size_t e = filters_part.find('"', s + 1);
      if (e == std::string_view::npos) break;

      filters.push_back(filters_part.substr(s + 1, e - s - 1));
      pos = e + 1;
    }
    // getEbusController().setForwardingFilters(filters);
  }
}

void Mqtt::handleReset(std::string_view payload) {
  getEbusController().resetMetrics();
}

void Mqtt::handleRead(std::string_view payload) {
  std::string_view key_view = ebus::extract(payload, "key");
  if (key_view.size() >= 2 && key_view.front() == '"' &&
      key_view.back() == '"') {
    key_view.remove_prefix(1);
    key_view.remove_suffix(1);
  }

  const Command* command = store.findCommand(std::string(key_view));
  if (command != nullptr) {
    publishStream(
        "response", 0, false, [command](const ebus::JsonChunkVisitor& v) {
          ebus::detail::JsonWriter writer(v);
          writer.startObject();
          writer.writeField("id", "read");
          // Now, directly write the value field from the command
          writer.appendKey("value");  // This will add "value":
          auto decoded =
              ebus::decode(command->getDatatype(), command->getData());
          if (!decoded || ebus::isNull(*decoded)) {
            writer.writeRaw("null");
          } else {
            if (command->getNumeric()) {
              writer.writeValueFloat(ebus::roundDigits(
                  ebus::asFloat(*decoded) / command->getDivider(),
                  command->getDigits()));
            } else {
              const auto meta = command->getMetaCached();
              if (meta && std::string_view(meta->name).find("HEX") == 0)
                writer.writeValue(ebus::toHexString(*decoded, 0));
              else
                writer.writeValue(ebus::asString(*decoded));
            }
          }
          writer.endObject();
        });
  } else {
    publishStream("response", 0, false,
                  [key_view](const ebus::JsonChunkVisitor& v) {
                    ebus::detail::JsonWriter writer(v);
                    writer.startObject();
                    writer.writeField("id", "read");
                    writer.appendKey("status");
                    writer.write("\"key '");
                    writer.writeEscaped(key_view);
                    writer.write("' not found\"");
                    writer.endObject();
                  });
  }
}

void Mqtt::handleWrite(std::string_view payload) {
  std::string_view key_view = ebus::extract(payload, "key");
  if (key_view.size() >= 2 && key_view.front() == '"' &&
      key_view.back() == '"') {
    key_view.remove_prefix(1);
    key_view.remove_suffix(1);
  }

  Command* command = store.findCommand(std::string(key_view));
  if (command != nullptr) {
    std::string_view val_view = ebus::extract(payload, "value");
    if (val_view.empty()) {
      publishStream("response", 0, false,
                    [key_view](const ebus::JsonChunkVisitor& v) {
                      ebus::detail::JsonWriter writer(v);
                      writer.startObject();
                      writer.writeField("id", "write");
                      writer.appendKey("status");
                      writer.write("\"missing value for key '");
                      writer.writeEscaped(key_view);
                      writer.write("'\"");
                      writer.endObject();
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
                      writer.startObject();
                      writer.writeField("id", "write");
                      writer.appendKey("status");
                      writer.write("\"scheduled for key '");
                      writer.writeEscaped(key_view);
                      writer.write("' name '");
                      writer.writeEscaped(command->getName());
                      writer.write("'\"");
                      writer.endObject();
                    });
      command->setLast(0);
    } else {
      publishStream("response", 0, false,
                    [key_view](const ebus::JsonChunkVisitor& v) {
                      ebus::detail::JsonWriter writer(v);
                      writer.startObject();
                      writer.writeField("id", "write");
                      writer.appendKey("status");
                      writer.write("\"invalid value for key '");
                      writer.writeEscaped(key_view);
                      writer.write("'\"");
                      writer.endObject();
                    });
    }
  } else {
    publishStream("response", 0, false,
                  [key_view](const ebus::JsonChunkVisitor& v) {
                    ebus::detail::JsonWriter writer(v);
                    writer.startObject();
                    writer.writeField("id", "write");
                    writer.appendKey("status");
                    writer.write("\"key '");
                    writer.writeEscaped(key_view);
                    writer.write("' not found\"");
                    writer.endObject();
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
        publishStream("response", 0, false,
                      [&](const ebus::JsonChunkVisitor& v) {
                        ebus::detail::JsonWriter writer(v);
                        writer.startObject();
                        writer.writeField("id", "insert");
                        writer.appendKey("status");
                        writer.write("\"key '");
                        writer.writeEscaped(action.command.getKey());
                        writer.write("' inserted\"");
                        writer.endObject();
                      });
        break;
      case IncomingActionType::Remove:
        const Command* cmd = store.findCommand(action.key);
        if (cmd) {
          if (mqttha.isEnabled()) mqttha.publishComponent(cmd, true);
          store.removeCommand(action.key);
          publishStream("response", 0, false,
                        [&](const ebus::JsonChunkVisitor& v) {
                          ebus::detail::JsonWriter writer(v);
                          writer.startObject();
                          writer.writeField("id", "remove");
                          writer.appendKey("status");
                          writer.write("\"key '");
                          writer.writeEscaped(action.key);
                          writer.write("' removed\"");
                          writer.endObject();
                        });
        } else {
          publishStream("response", 0, false,
                        [&](const ebus::JsonChunkVisitor& v) {
                          ebus::detail::JsonWriter writer(v);
                          writer.startObject();
                          writer.writeField("id", "remove");
                          writer.appendKey("status");
                          writer.write("\"key '");
                          writer.writeEscaped(action.key);
                          writer.write("' not found\"");
                          writer.endObject();
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
      case OutgoingActionType::Value: {
        char topicBuf[128];
        int written = snprintf(topicBuf, sizeof(topicBuf), "values/%s",
                               action.name.c_str());
        if (written > 0 && (size_t)written < sizeof(topicBuf)) {
          for (int i = 7; i < written; ++i) {
            topicBuf[i] = (char)tolower((unsigned char)topicBuf[i]);
          }
          publish(topicBuf, 0, false, action.payload.c_str());
        }
        break;
      }
      case OutgoingActionType::Error: {
        publishStream("errors", 0, false, [&](const ebus::JsonChunkVisitor& v) {
          ebus::detail::JsonWriter writer(v);
          action.error.toJson(writer);
        });
        break;
      }
      case OutgoingActionType::Data: {
        publishStream("response", 0, false,
                      [&](const ebus::JsonChunkVisitor& v) {
                        ebus::detail::JsonWriter writer(v);
                        writer.startObject();
                        writer.writeField("id", action.id);
                        writer.writeHexField("master", action.master);
                        writer.writeHexField("slave", action.slave);
                        writer.endObject();
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

void Mqtt::handleValueUpdate(const std::string& key) {
  Command* cmd = store.findCommand(key);
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
        writer.startObject();
        writer.appendKey("value");
        if (!decoded || ebus::isNull(*decoded)) {
          writer.writeRaw("null");
        } else if (cmd->getNumeric()) {
          writer.writeValueFloat(ebus::roundDigits(
              ebus::asFloat(*decoded) / cmd->getDivider(), cmd->getDigits()));
        } else {
          const auto meta = cmd->getMetaCached();
          if (meta && std::string_view(meta->name).find("HEX") == 0) {
            const std::string& hexData = ebus::asString(*decoded);
            writer.write("\"");
            ebus::detail::appendHexFieldToWriter(
                writer,
                ebus::ByteView(reinterpret_cast<const uint8_t*>(hexData.data()),
                               hexData.size()));
            writer.write("\"");
            writer.writeRaw(""); // Force first_ = false to ensure next sibling gets a comma
          } else {
            writer.writeValue(ebus::asString(*decoded));
          }
        }
        writer.endObject();
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
    writer.startObject();
    writer.writeField("id", id);
    writer.writeField("status", status);
    if (bytes > 0) writer.writeField("bytes", static_cast<uint32_t>(bytes));
    writer.endObject();
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
