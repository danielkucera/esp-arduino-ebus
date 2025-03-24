#include "mqtt.hpp"

#include "main.hpp"
#include "schedule.hpp"
#include "store.hpp"

Mqtt mqtt;

Mqtt::Mqtt() {
  client.onConnect(onConnect);
  client.onDisconnect(onDisconnect);
  client.onSubscribe(onSubscribe);
  client.onUnsubscribe(onUnsubscribe);
  client.onMessage(onMessage);
  client.onPublish(onPublish);
}

void Mqtt::setUniqueId(const char* id) {
  uniqueId = id;
  rootTopic = "ebus/" + uniqueId + "/";
}

const std::string &Mqtt::getUniqueId() const { return uniqueId; }

const std::string &Mqtt::getRootTopic() const { return rootTopic; }

void Mqtt::setServer(const char *host, uint16_t port) {
  client.setServer(host, port);
}

void Mqtt::setCredentials(const char *username, const char *password) {
  client.setCredentials(username, password);
}

void Mqtt::connect() { client.connect(); }

bool Mqtt::connected() const { return client.connected(); }

uint16_t Mqtt::publish(const char *topic, uint8_t qos, bool retain,
                       const char *payload, bool prefix) {
  std::string mqttTopic = topic;
  if (prefix) mqttTopic = rootTopic + topic;
  return client.publish(mqttTopic.c_str(), qos, retain, payload);
}

uint16_t Mqtt::subscribe(const char *topic, uint8_t qos) {
  return client.subscribe(topic, qos);
}

void Mqtt::onConnect(bool sessionPresent) {
  std::string topic = mqtt.getRootTopic() + "cmd/restart";
  mqtt.subscribe(topic.c_str(), 0);
  // Restarting of the device
  // payload: true

#ifdef EBUS_INTERNAL
  topic = mqtt.getRootTopic() + "cmd/insert";
  mqtt.subscribe(topic.c_str(), 0);
  // Inserting (Installing) a new command
  // payload: ebus command in form of "ZZPBSBNNDBx" with a UNIQUE_KEY for e.g.
  // {
  //   "key": "UNIQUE_KEY",
  //   "command": "fe070009",
  //   "unit": "°C",
  //   "active": false,
  //   "interval": 0,
  //   "master": true,
  //   "position": 1,
  //   "datatype": "DATA2b",
  //   "topic": "outdoor/temperature",
  //   "ha": true,
  //   "ha_class": "temperature"
  // }

  topic = mqtt.getRootTopic() + "cmd/remove";
  mqtt.subscribe(topic.c_str(), 0);
  // Removing an installed command
  // payload: UNIQUE_KEY of ebus command
  // {
  //   "key": "UNIQUE_KEY"
  // }

  topic = mqtt.getRootTopic() + "cmd/list";
  mqtt.subscribe(topic.c_str(), 0);
  // List all installed commands
  // payload: true

  topic = mqtt.getRootTopic() + "cmd/load";
  mqtt.subscribe(topic.c_str(), 0);
  // Loading (install) of saved commands
  // payload: true

  topic = mqtt.getRootTopic() + "cmd/save";
  mqtt.subscribe(topic.c_str(), 0);
  // Saving of current installed commands
  // payload: true

  topic = mqtt.getRootTopic() + "cmd/wipe";
  mqtt.subscribe(topic.c_str(), 0);
  // Wiping of saved commands
  // payload: true

  topic = mqtt.getRootTopic() + "cmd/send";
  mqtt.subscribe(topic.c_str(), 0);
  // Sending of given ebus command(s) once
  // payload: array of ebus command(s) in form of "ZZPBSBNNDBx" for e.g.
  // [
  //   "05070400",
  //   "15070400"
  // ]

  topic = mqtt.getRootTopic() + "cmd/raw";
  mqtt.subscribe(topic.c_str(), 0);
  // Toggling of the raw data printout
  // payload: true | false

  topic = mqtt.getRootTopic() + "cmd/filter";
  mqtt.subscribe(topic.c_str(), 0);
  // Adding filter(s) for raw data printout
  // payload: array of sequences for e.g.
  // [
  //   "0700",
  //   "fe"
  // ]
#endif
}

void Mqtt::onMessage(const char *topic, const char *payload,
                     AsyncMqttClientMessageProperties properties, size_t len,
                     size_t index, size_t total) {
  std::string tmp = topic;

  if (tmp.rfind("restart") != std::string::npos) {
    if (String(payload).equalsIgnoreCase("true")) reset();
  }
#ifdef EBUS_INTERNAL
  if (tmp.rfind("insert") != std::string::npos) {
    if (String(payload).length() > 0) store.enqueCommand(payload);
  } else if (tmp.rfind("remove") != std::string::npos) {
    if (String(payload).length() > 0) store.removeCommand(payload);
  } else if (tmp.rfind("list") != std::string::npos) {
    if (String(payload).equalsIgnoreCase("true")) store.publishCommands();
  } else if (tmp.rfind("load") != std::string::npos) {
    if (String(payload).equalsIgnoreCase("true")) store.loadCommands();
  } else if (tmp.rfind("save") != std::string::npos) {
    if (String(payload).equalsIgnoreCase("true")) store.saveCommands();
  } else if (tmp.rfind("wipe") != std::string::npos) {
    if (String(payload).equalsIgnoreCase("true")) store.wipeCommands();
  } else if (tmp.rfind("send") != std::string::npos) {
    if (String(payload).length() > 0) schedule.handleSend(payload);
  } else if (tmp.rfind("raw") != std::string::npos) {
    schedule.publishRaw(String(payload).equalsIgnoreCase("true"));
  } else if (tmp.rfind("filter") != std::string::npos) {
    if (String(payload).length() > 0) schedule.handleFilter(payload);
  }
#endif
}
