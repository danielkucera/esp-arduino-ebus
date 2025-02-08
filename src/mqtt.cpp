#include "mqtt.hpp"

#include "main.hpp"
#include "schedule.hpp"
#include "store.hpp"

AsyncMqttClient mqttClient;

void onMqttConnect(bool sessionPresent) {
  mqttClient.subscribe("ebus/config/restart", 0);
  // Restart the device
  // topic  : ebus/config/restart
  // payload: true

#ifdef EBUS_INTERNAL
  mqttClient.subscribe("ebus/config/insert/#", 0);
  // Insert new command
  // topic  : ebus/config/insert/NAME_OF_COMMAND
  // payload: ebus command in form of "ZZPBSBNNDBx" for e.g.
  // {
  //   "command": "fe070009",
  //   "unit": "°C",
  //   "active": false,
  //   "interval": 0,
  //   "master": true,
  //   "position": 1,
  //   "datatype": "DATA2b",
  //   "topic": "outdoor",
  //   "ha": true,
  //   "ha_class": "temperature"
  // }

  mqttClient.subscribe("ebus/config/remove/#", 0);
  // Remove loaded command
  // topic  : ebus/config/remove/NAME_OF_COMMAND
  // payload: true

  mqttClient.subscribe("ebus/config/list", 0);
  // Publish loaded commands
  // topic  : ebus/config/list
  // payload: true

  mqttClient.subscribe("ebus/config/raw", 0);
  // Enable/disable the raw data printout
  // topic  : ebus/config/raw
  // payload: true or false

  mqttClient.subscribe("ebus/config/filter", 0);
  // Insert raw data filter
  // topic  : ebus/config/filter
  // payload: array of sequences for e.g.
  // [
  //   "0700",
  //   "fe"
  // ]

  mqttClient.subscribe("ebus/config/load", 0);
  // Loading saved commands
  // topic  : ebus/config/load
  // payload: true

  mqttClient.subscribe("ebus/config/save", 0);
  // Saving loaded commands
  // topic  : ebus/config/save
  // payload: true

  mqttClient.subscribe("ebus/config/wipe", 0);
  // Wiping saved commands
  // topic  : ebus/config/wipe
  // payload: true

  mqttClient.subscribe("ebus/config/send", 0);
  // Sending of given ebus command(s) once
  // topic  : ebus/config/send
  // payload: array of ebus command(s) in form of "ZZPBSBNNDBx" for e.g.
  // [
  //   "05070400",
  //   "15070400"
  // ]
#endif
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {}

void onMqttUnsubscribe(uint16_t packetId) {}

void onMqttMessage(const char *topic, const char *payload,
                   AsyncMqttClientMessageProperties properties, size_t len,
                   size_t index, size_t total) {
  String tmp = String(topic);
  if (tmp.equals("ebus/config/restart")) {
    if (String(payload).equalsIgnoreCase("true")) reset();
  }
#ifdef EBUS_INTERNAL
  else if (tmp.startsWith("ebus/config/insert/")) {
    if (String(payload).length() > 0) store.enqueCommand(payload);
  } else if (tmp.startsWith("ebus/config/remove/")) {
    if (String(payload).equalsIgnoreCase("true")) store.removeCommand(topic);
  } else if (tmp.equals("ebus/config/list")) {
    if (String(payload).equalsIgnoreCase("true")) store.publishCommands();
  } else if (tmp.equals("ebus/config/raw")) {
    schedule.publishRaw(payload);
  } else if (tmp.equals("ebus/config/filter")) {
    if (String(payload).length() > 0) schedule.handleFilter(payload);
  } else if (tmp.equals("ebus/config/load")) {
    if (String(payload).equalsIgnoreCase("true")) store.loadCommands();
  } else if (tmp.equals("ebus/config/save")) {
    if (String(payload).equalsIgnoreCase("true")) store.saveCommands();
  } else if (tmp.equals("ebus/config/wipe")) {
    if (String(payload).equalsIgnoreCase("true")) store.wipeCommands();
  } else if (tmp.equals("ebus/config/send")) {
    if (String(payload).length() > 0) schedule.handleSend(payload);
  }
#endif
}

void onMqttPublish(uint16_t packetId) {}
