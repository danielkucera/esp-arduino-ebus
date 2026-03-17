#if defined(EBUS_INTERNAL)
#include "Mqtt.hpp"

#include <esp_timer.h>

#include <functional>

#include "DeviceManager.hpp"
#include "Logger.hpp"
#include "MqttHA.hpp"
#include "Schedule.hpp"
#include "Store.hpp"
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
  if (enabled) {
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client,
                                   (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                   &Mqtt::eventHandler, this);
    esp_mqtt_client_start(client);
  }
}

void Mqtt::change() {
  if (connected) esp_mqtt_client_stop(client);
  start();
}

void Mqtt::startTask() {
  if (taskHandle != nullptr) return;
  xTaskCreate(&Mqtt::taskFunc, "mqtt_loop", 6144, this, 1, &taskHandle);
}

void Mqtt::stopTask() {
  if (taskHandle != nullptr) {
    vTaskDelete(taskHandle);
    taskHandle = nullptr;
  }
}

void Mqtt::setStatusProvider(const std::function<std::string()>& provider) {
  statusProvider = provider;
}

void Mqtt::setup(const char* id) {
  uniqueId = id;
  clientId = "ebus-" + uniqueId;
  rootTopic = "ebus/" + uniqueId + "/";
  willTopic = mqtt.rootTopic + "available";
  requestTopic = mqtt.rootTopic + "request";

  mqtt_cfg.credentials.client_id = clientId.c_str();
  // Last Will
  mqtt_cfg.session.last_will.topic = willTopic.c_str();
  mqtt_cfg.session.last_will.msg = "{ \"value\": \"offline\" }";
  mqtt_cfg.session.last_will.msg_len = 0;
  mqtt_cfg.session.last_will.qos = 1;
  mqtt_cfg.session.last_will.retain = 1;
  // Keep-alive interval in seconds
  mqtt_cfg.session.keepalive = 60;
}

void Mqtt::setServer(const char* host, uint16_t port) {
  std::string hostname;
  for (size_t i = 0; host[i] != '\0'; ++i)
    if (!std::isspace(host[i])) hostname += host[i];

  uri = "mqtt://" + hostname;
  if (port > 0) uri += ":" + std::to_string(port);

  mqtt_cfg.broker.address.uri = uri.c_str();
}

void Mqtt::setCredentials(const char* username, const char* password) {
  mqtt_cfg.credentials.username = username;
  mqtt_cfg.credentials.authentication.password = password;
}

void Mqtt::setRootTopic(const std::string& topic) {
  rootTopic = topic;
  // Ensure proper formatting with trailing slash
  if (!rootTopic.empty() && rootTopic.back() != '/') {
    rootTopic += '/';
  }
  willTopic = rootTopic + "available";
  requestTopic = rootTopic + "request";
}

void Mqtt::setEnabled(const bool enable) { enabled = enable; }

bool Mqtt::isEnabled() const { return enabled; }

bool Mqtt::isConnected() const { return connected; }

const std::string& Mqtt::getUniqueId() const { return uniqueId; }

const std::string& Mqtt::getRootTopic() const { return rootTopic; }

const std::string& Mqtt::getWillTopic() const { return willTopic; }

void Mqtt::publish(const char* topic, uint8_t qos, bool retain,
                   const char* payload, bool prefix) {
  if (!enabled) return;

  std::string mqttTopic = prefix ? rootTopic + topic : topic;
  esp_mqtt_client_publish(client, mqttTopic.c_str(), payload, 0, qos, retain);
}

void Mqtt::enqueueOutgoing(const OutgoingAction& action) {
  if (!mqtt.enabled) return;
  mqtt.outgoingQueue.push(action);
}

void Mqtt::publishData(const std::string& id,
                       const std::vector<uint8_t>& master,
                       const std::vector<uint8_t>& slave) {
  if (!mqtt.enabled) return;

  cJSON* doc = cJSON_CreateObject();
  cJSON_AddStringToObject(doc, "id", id.c_str());
  cJSON_AddStringToObject(doc, "master", ebus::to_string(master).c_str());
  cJSON_AddStringToObject(doc, "slave", ebus::to_string(slave).c_str());

  std::string payload = printJson(doc);
  cJSON_Delete(doc);

  mqtt.publish("response", 0, false, payload.c_str());
}

void Mqtt::publishValue(const std::string& name, const std::string& valueJson) {
  if (!mqtt.enabled) return;

  std::string subTopic = name;
  std::transform(subTopic.begin(), subTopic.end(), subTopic.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  std::string topic = "values/" + subTopic;
  mqtt.publish(topic.c_str(), 0, false, valueJson.c_str());
}

void Mqtt::doLoop() {
  checkIncomingQueue();
  checkOutgoingQueue();
}

void Mqtt::taskFunc(void* arg) {
  Mqtt* self = static_cast<Mqtt*>(arg);
  for (;;) {
    if (self->enabled && self->connected) {
      uint32_t currentMillis = (uint32_t)(esp_timer_get_time() / 1000ULL);
      if (currentMillis > self->lastStatusPublish + self->statusPublishIntervalMs) {
        self->lastStatusPublish = currentMillis;
        if (self->statusProvider) {
          const std::string payload = self->statusProvider();
          self->publish("state", 0, false, payload.c_str());
        }
        schedule.publishCounter();
        schedule.publishTiming();
      }
      self->doLoop();
    }
    vTaskDelay(1);
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
      self->connected = true;
      esp_mqtt_client_subscribe(self->client, self->requestTopic.c_str(), 0);

      mqtt.publish(mqtt.willTopic.c_str(), 0, true, "{ \"value\": \"online\" }",
                   false);

      if (mqttha.isEnabled()) mqttha.publishDeviceInfo();
    } break;
    case MQTT_EVENT_DISCONNECTED: {
      logger.debug("MQTT disconnected");
      self->connected = false;
    } break;
    case MQTT_EVENT_SUBSCRIBED: {
      logger.debug(self->requestTopic + " subscribed");
    } break;
    case MQTT_EVENT_UNSUBSCRIBED:
    case MQTT_EVENT_PUBLISHED:
      break;
    case MQTT_EVENT_DATA: {
      logger.debug("MQTT data received");

      std::string incoming(event->data, event->data + event->data_len);
      cJSON* doc = cJSON_Parse(incoming.c_str());
      if (!cJSON_IsObject(doc)) {
        mqtt.publish("response", 0, false,
                     errorPayload("invalid json payload").c_str());
        if (doc) cJSON_Delete(doc);
        return;
      }

      cJSON* idNode = cJSON_GetObjectItemCaseSensitive(doc, "id");
      std::string id =
          (cJSON_IsString(idNode) && idNode->valuestring != nullptr)
              ? idNode->valuestring
              : "";

      auto it = mqtt.commandHandlers.find(id);
      if (it != mqtt.commandHandlers.end()) {
        it->second(doc);
      } else {
        // Unknown command error handling
        mqtt.publish("response", 0, false,
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
      logger.warn(std::string("Unhandled event id: ") +
                  std::to_string(event->event_id));
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
      incomingQueue.push(IncomingAction(Command::fromJson(command)));
    } else {
      mqtt.publish("response", 0, false, errorPayload(evalError).c_str());
    }
  }
}

void Mqtt::handleRemove(const cJSON* doc) {
  std::vector<std::string> keys =
      getStringArray(const_cast<cJSON*>(doc), "keys");

  if (!keys.empty()) {
    for (const std::string& key : keys) incomingQueue.push(IncomingAction(key));
  } else {
    for (const Command* command : store.getCommands())
      incomingQueue.push(IncomingAction(command->getKey()));
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
  cJSON* vendorNode =
      cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(doc), "vendor");
  bool full = cJSON_IsTrue(fullNode);
  bool vendor = cJSON_IsTrue(vendorNode);

  std::vector<std::string> addresses =
      getStringArray(const_cast<cJSON*>(doc), "addresses");

  if (full)
    schedule.handleScanFull();
  else if (vendor)
    schedule.handleScanVendor();
  else if (addresses.empty())
    schedule.handleScan();
  else
    schedule.handleScanAddresses(addresses);

  mqtt.publishResponse("scan", "initiated");
}

void Mqtt::handleDevices(const cJSON* doc) {
  for (const Device* device : deviceManager.getDevices())
    enqueueOutgoing(OutgoingAction(device));
}

void Mqtt::handleSend(const cJSON* doc) {
  std::vector<std::string> commands =
      getStringArray(const_cast<cJSON*>(doc), "commands");
  if (commands.empty())
    mqtt.publishResponse("send", "commands array invalid");
  else
    schedule.handleSend(commands);
}

void Mqtt::handleForward(const cJSON* doc) {
  std::vector<std::string> filters =
      getStringArray(const_cast<cJSON*>(doc), "filters");
  if (!filters.empty()) schedule.handleForwardFilter(filters);

  cJSON* enableNode =
      cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(doc), "enable");
  schedule.toggleForward(cJSON_IsTrue(enableNode));
}

void Mqtt::handleReset(const cJSON* doc) {
  deviceManager.resetAddresses();
  schedule.resetCounter();
  schedule.resetTiming();
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
    std::vector<uint8_t> valueBytes = command->getVectorFromJson(doc);
    if (!valueBytes.empty()) {
      std::vector<uint8_t> writeCmd = command->getWriteCmd();
      writeCmd.insert(writeCmd.end(), valueBytes.begin(), valueBytes.end());
      schedule.handleWrite(writeCmd);
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
  if (!incomingQueue.empty() && (uint32_t)(esp_timer_get_time() / 1000ULL) >
                                    lastIncoming + incomingInterval) {
    lastIncoming = (uint32_t)(esp_timer_get_time() / 1000ULL);
    IncomingAction action = incomingQueue.front();
    incomingQueue.pop();

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
  if (!outgoingQueue.empty() && (uint32_t)(esp_timer_get_time() / 1000ULL) >
                                    lastOutgoing + outgoingInterval) {
    lastOutgoing = (uint32_t)(esp_timer_get_time() / 1000ULL);
    OutgoingAction action = outgoingQueue.front();
    outgoingQueue.pop();

    switch (action.type) {
      case OutgoingActionType::Command:
        publishCommand(action.command);
        break;
      case OutgoingActionType::Device:
        publishDevice(action.device);
        break;
      case OutgoingActionType::Component:
        mqttha.publishComponent(action.command, action.haRemove);
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

  publish("response", 0, false, payload.c_str());
}

void Mqtt::publishCommand(const Command* command) {
  std::string topic = "commands/" + command->getKey();
  std::string payload = command->toJson();
  publish(topic.c_str(), 0, false, payload.c_str());
}

void Mqtt::publishDevice(const Device* device) {
  std::string topic = "devices/" + ebus::to_string(device->getSlave());
  std::string payload = device->toJson();
  publish(topic.c_str(), 0, false, payload.c_str());
}

#endif
