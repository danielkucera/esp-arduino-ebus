#if defined(EBUS_INTERNAL)
#include "Mqtt.hpp"

#include <esp_timer.h>

#include <functional>

#include "Logger.hpp"
#include "MqttHA.hpp"
#include "Store.hpp"
#include "ebus/status.hpp"
#include "ebus_accessor.hpp"
#include "main.hpp"

Mqtt mqtt;

namespace {
std::string printJson(cJSON* node, const char* fallback = "{}") {
  char* printed = cJSON_PrintUnformatted(node);
  std::string out = printed != nullptr ? printed : fallback;
  if (printed != nullptr) cJSON_free(printed);
  return out;
}

std::string errorPayload(const std::string& message) {
  cJSON* doc = cJSON_CreateObject();
  cJSON_AddStringToObject(doc, "error", message.c_str());
  std::string payload = printJson(doc);
  cJSON_Delete(doc);
  return payload;
}

std::vector<std::string> getStringArray(cJSON* doc, const char* key) {
  std::vector<std::string> out;
  cJSON* arr = cJSON_GetObjectItemCaseSensitive(doc, key);
  if (!cJSON_IsArray(arr)) return out;

  cJSON* item = nullptr;
  cJSON_ArrayForEach(item, arr) {
    if (cJSON_IsString(item) && item->valuestring != nullptr)
      out.emplace_back(item->valuestring);
  }
  return out;
}
}  // namespace

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
  xTaskCreate(&Mqtt::taskFunc, "mqtt_loop", 4096, this, 1, &task_handle_); // Increased stack size to 4096 words (16KB)
}

void Mqtt::stopTask() {
  if (task_handle_ != nullptr) {
    vTaskDelete(task_handle_);
    task_handle_ = nullptr;
  }
}

void Mqtt::setStatusProvider(const std::function<std::string()>& provider) {
  status_provider_ = provider;
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
  mqtt_cfg_.buffer.size = 2048;
  mqtt_cfg_.buffer.out_size = 2048;
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

void Mqtt::enqueueOutgoing(const OutgoingAction& action) {
  if (!mqtt.enabled_) return;
  std::lock_guard<std::mutex> lock(mqtt.outgoing_queue_mutex_);
  mqtt.outgoing_queue_.push(action);
}

void Mqtt::publishData(const std::string& id,
                       const std::vector<uint8_t>& master,
                       const std::vector<uint8_t>& slave) {
  if (!mqtt.enabled_) return;

  cJSON* doc = cJSON_CreateObject();
  cJSON_AddStringToObject(doc, "id", id.c_str());
  cJSON_AddStringToObject(doc, "master", ebus::toString(master).c_str());
  cJSON_AddStringToObject(doc, "slave", ebus::toString(slave).c_str());

  std::string payload = printJson(doc);
  cJSON_Delete(doc);

  mqtt.publish("response", 0, false, payload.c_str());
}

void Mqtt::publishError(const ebus::ErrorInfo& info) {
  if (!mqtt.enabled_) return;

  // Convert ebus::ErrorInfo to JSON string using the library's utility
  std::string payload = ebus::toJson(info, 512);
  mqtt.publish("errors", 0, false, payload.c_str());
}

void Mqtt::publishValue(const std::string& name, const std::string& valueJson) {
  if (!mqtt.enabled_) return;

  // Memory optimization: Avoid multiple std::string allocations and
  // transformations
  char topicBuf[128];
  int written = snprintf(topicBuf, sizeof(topicBuf), "values/%s", name.c_str());
  if (written > 0 && (size_t)written < sizeof(topicBuf)) {
    for (int i = 7; i < written;
         ++i) {  // Transform only the sub-topic part to lowercase
      topicBuf[i] = (char)tolower((unsigned char)topicBuf[i]);
    }
    mqtt.publish(topicBuf, 0, false, valueJson.c_str());
  }
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
  checkIncomingQueue();
  checkOutgoingQueue();
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
          case 0:  // Phase 1: Basic App State
            if (self->status_provider_) {
              const std::string payload = self->status_provider_();
              self->publish("state", 0, false, payload.c_str());
            }
            tele_phase = 1;
            break;

          case 1: {  // Phase 2: App Resources (Thread stacks and Queue depths)
            char* appRes = getAppResourcesJson();
            self->publish("resources/app", 0, false, appRes);
            cJSON_free(appRes);
            tele_phase = 2;
            break;
          }

          case 2: {  // Phase 3: Library Resources (Internal protocol metrics)
            getEbusController().fetchSystemResources([self](const ebus::SystemResources& res) {
              std::string libRes = ebus::toJson(res, 2048);
              self->publish("resources/lib", 0, false, libRes.c_str());
            });
            tele_phase = 0;
            break;
          }
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

      // Memory optimization: Parse directly from event buffer without creating
      // a temporary std::string
      cJSON* doc = cJSON_ParseWithLength(event->data, event->data_len);
      if (!cJSON_IsObject(doc)) {
        self->publish("response", 0, false,
                      errorPayload("invalid json payload").c_str());
        if (doc) cJSON_Delete(doc);
        return;
      }

      cJSON* idNode = cJSON_GetObjectItemCaseSensitive(doc, "id");
      std::string id =
          (cJSON_IsString(idNode) && idNode->valuestring != nullptr)
              ? idNode->valuestring
              : "";

      auto it = self->command_handlers_.find(id);
      if (it != self->command_handlers_.end()) {
        it->second(doc);
      } else {
        self->publish("response", 0, false,
                      errorPayload("command '" + id + "' not found").c_str());
      }

      cJSON_Delete(doc);
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

void Mqtt::handleRestart(const cJSON* doc) { restart(); }

void Mqtt::handleInsert(const cJSON* doc) {
  cJSON* commands =
      cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(doc), "commands");
  if (!cJSON_IsArray(commands)) return;

  cJSON* command = nullptr;
  cJSON_ArrayForEach(command, commands) {
    std::string evalError = Command::evaluate(command);
    if (evalError.empty()) {
      {
        std::lock_guard<std::mutex> lock(incoming_queue_mutex_);
        incoming_queue_.push(IncomingAction(Command::fromJson(command)));
      }
    } else {
      mqtt.publish("response", 0, false, errorPayload(evalError).c_str());
    }
  }
}

void Mqtt::handleRemove(const cJSON* doc) {
  std::vector<std::string> keys =
      getStringArray(const_cast<cJSON*>(doc), "keys");

  {
    std::lock_guard<std::mutex> lock(mqtt.incoming_queue_mutex_);
    if (!keys.empty()) {
      for (const std::string& key : keys)
        mqtt.incoming_queue_.push(IncomingAction(key));
    } else {
      for (const Command* command : store.getCommands())
        mqtt.incoming_queue_.push(IncomingAction(command->getKey()));
    }
  }
}

void Mqtt::handlePublish(const cJSON* doc) {
  for (const Command* command : store.getCommands())
    enqueueOutgoing(OutgoingAction(command));
}

void Mqtt::handleLoad(const cJSON* doc) {
  int64_t bytes = store.loadCommands();
  if (bytes > 0)
    mqtt.publishResponse("load", "successful", bytes);
  else if (bytes < 0)
    mqtt.publishResponse("load", "failed");
  else
    mqtt.publishResponse("load", "no data");

  if (mqttha.isEnabled()) mqttha.publishComponents();
}

void Mqtt::handleSave(const cJSON* doc) {
  int64_t bytes = store.saveCommands();
  if (bytes > 0)
    mqtt.publishResponse("save", "successful", bytes);
  else if (bytes < 0)
    mqtt.publishResponse("save", "failed");
  else
    mqtt.publishResponse("save", "no data");
}

void Mqtt::handleWipe(const cJSON* doc) {
  int64_t bytes = store.wipeCommands();
  if (bytes > 0)
    mqtt.publishResponse("wipe", "successful", bytes);
  else if (bytes < 0)
    mqtt.publishResponse("wipe", "failed");
  else
    mqtt.publishResponse("wipe", "no data");
}

void Mqtt::handleScan(const cJSON* doc) {
  cJSON* fullNode =
      cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(doc), "full");
  bool full = cJSON_IsTrue(fullNode);

  std::vector<std::string> addresses =
      getStringArray(const_cast<cJSON*>(doc), "addresses");

  if (full) {
    getEbusController().initFullScan(true);
  } else if (addresses.empty()) {
    getEbusController().scanObservedDevices();
  } else {
    std::vector<uint8_t> addrVec;
    for (const auto& a : addresses) {
      addrVec.push_back(
          static_cast<uint8_t>(std::strtoul(a.c_str(), nullptr, 16)));
    }
    getEbusController().scanAddresses(addrVec);
  }

  mqtt.publishResponse("scan", "initiated");
}

void Mqtt::handleDevices(const cJSON* doc) {
  getEbusController().fetchDeviceInfo([](const ebus::DeviceInfo& device) {
    enqueueOutgoing(OutgoingAction(device));
  });
}

void Mqtt::handleSend(const cJSON* doc) {
  std::vector<std::string> commands =
      getStringArray(const_cast<cJSON*>(doc), "commands");
  if (commands.empty()) {
    mqtt.publishResponse("send", "commands array invalid");
  } else {
    for (const auto& cmdStr : commands) {
      getEbusController().enqueue(PRIO_SEND, ebus::toVector(cmdStr));
    }
  }
}

void Mqtt::handleForward(const cJSON* doc) {
  // std::vector<std::string> filters =
  //     getStringArray(const_cast<cJSON*>(doc), "filters");
  // if (!filters.empty()) schedule.handleForwardFilter(filters);

  // cJSON* enableNode =
  //     cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(doc), "enable");
  // schedule.toggleForward(cJSON_IsTrue(enableNode));
}

void Mqtt::handleReset(const cJSON* doc) {
  // deviceManager.resetAddresses();
  // schedule.resetCounter();
  // schedule.resetTiming();
}

void Mqtt::handleRead(const cJSON* doc) {
  cJSON* keyNode =
      cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(doc), "key");
  std::string key = (cJSON_IsString(keyNode) && keyNode->valuestring != nullptr)
                        ? keyNode->valuestring
                        : "";

  const Command* command = store.findCommand(key);
  if (command != nullptr) {
    std::string s = "{\"id\":\"read\",";
    s += store.getValueFullJson(command).substr(1);
    publish("response", 0, false, s.c_str());
  } else {
    mqtt.publishResponse("read", "key '" + key + "' not found");
  }
}

void Mqtt::handleWrite(const cJSON* doc) {
  cJSON* keyNode =
      cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(doc), "key");
  std::string key = (cJSON_IsString(keyNode) && keyNode->valuestring != nullptr)
                        ? keyNode->valuestring
                        : "";

  Command* command = store.findCommand(key);
  if (command != nullptr) {
    ebus::Sequence valueBytes = command->getVectorFromJson(doc);
    if (!valueBytes.empty()) {
      ebus::Sequence fullWrite = command->getWriteCmd();
      fullWrite.append(valueBytes);

      getEbusController().enqueue(PRIO_SEND, fullWrite);
      mqtt.publishResponse("write", "scheduled for key '" + key + "' name '" +
                                        command->getName() + "'");
      command->setLast(0);
    } else {
      mqtt.publishResponse("write", "invalid value for key '" + key + "'");
    }
  } else {
    mqtt.publishResponse("write", "key '" + key + "' not found");
  }
}

void Mqtt::checkIncomingQueue() {
  IncomingAction action(std::string(""));
  bool has_action = false;

  {
    std::lock_guard<std::mutex> lock(incoming_queue_mutex_);
    if (!incoming_queue_.empty() && (uint32_t)(esp_timer_get_time() / 1000ULL) >
                                        last_incoming_ + incoming_interval_) {
      action = std::move(incoming_queue_.front());
      incoming_queue_.pop();
      has_action = true;
    }
  }

  if (has_action) {
    last_incoming_ = (uint32_t)(esp_timer_get_time() / 1000ULL);

    switch (action.type) {
      case IncomingActionType::Insert:
        store.insertCommand(action.command);
        if (mqttha.isEnabled()) mqttha.publishComponent(&action.command, false);
        publishResponse("insert",
                        "key '" + action.command.getKey() + "' inserted");
        break;
      case IncomingActionType::Remove:
        const Command* cmd = store.findCommand(action.key);
        if (cmd) {
          if (mqttha.isEnabled()) mqttha.publishComponent(cmd, true);
          store.removeCommand(action.key);
          publishResponse("remove", "key '" + action.key + "' removed");
        } else {
          publishResponse("remove", "key '" + action.key + "' not found");
        }
        break;
    }
  }
}

void Mqtt::checkOutgoingQueue() {
  OutgoingAction action(static_cast<const Command*>(nullptr));
  bool has_action = false;

  {
    std::lock_guard<std::mutex> lock(outgoing_queue_mutex_);
    if (!outgoing_queue_.empty() && (uint32_t)(esp_timer_get_time() / 1000ULL) >
                                        last_outgoing_ + outgoing_interval_) {
      action = std::move(outgoing_queue_.front());
      outgoing_queue_.pop();
      has_action = true;
    }
  }

  if (has_action) {
    last_outgoing_ = (uint32_t)(esp_timer_get_time() / 1000ULL);

    switch (action.type) {
      case OutgoingActionType::Command:
        publishCommand(action.command);
        break;
      case OutgoingActionType::Device:
        publishDevice(action.device);
        break;
      case OutgoingActionType::Component:
        mqttha.publishComponent(action.command, action.ha_remove);
        break;
    }
  }
}

void Mqtt::publishResponse(const std::string& id, const std::string& status,
                           const size_t& bytes) {
  cJSON* doc = cJSON_CreateObject();
  cJSON_AddStringToObject(doc, "id", id.c_str());
  cJSON_AddStringToObject(doc, "status", status.c_str());
  if (bytes > 0) cJSON_AddNumberToObject(doc, "bytes", bytes);

  std::string payload = printJson(doc);
  cJSON_Delete(doc);
  if (client_ != nullptr) {
    publish("response", 0, false, payload.c_str());
  }
}

void Mqtt::publishCommand(const Command* command) {
  std::string topic = "commands/" + command->getKey();
  std::string payload = command->toJson();
  if (client_ != nullptr) {
    publish(topic.c_str(), 0, false, payload.c_str());
  }
}

void Mqtt::publishDevice(const ebus::DeviceInfo& device) {
  std::string topic = "devices/" + ebus::toString(device.slave_address);
  std::string payload = ebus::toJson(device, 256);
  if (client_ != nullptr) {
    publish(topic.c_str(), 0, false, payload.c_str());
  }
}

#endif
