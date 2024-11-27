#include "schedule.hpp"

#include <ArduinoJson.h>

#include "bus.hpp"
#include "mqtt.hpp"

Schedule schedule;

Schedule::Schedule() {
  ebusHandler = ebus::EbusHandler(address, &busReadyCallback, &busWriteCallback,
                                  &responseCallback, &telegramCallback);
}

void Schedule::setAddress(const uint8_t source) {
  address = source;
  ebusHandler.setAddress(source);
}

void Schedule::setDistance(const uint8_t distance) {
  distanceCommands = distance * 1000;
}

// payload - optional parameter: unit, ha_class
//
// example:
// {
//   "command": "08b509030d0600",
//   "unit": "°C",
//   "active": true,
//   "interval": 60,
//   "master": false,
//   "position": 1,
//   "datatype": "DATA2c",
//   "topic": "Aussentemperatur",
//   "ha": true,
//   "ha_class": "temperature"
// }
void Schedule::insertCommand(const char *payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    std::string err = "DeserializationError ";
    err += error.c_str();
    mqttClient.publish("ebus/config/error", 0, false, err.c_str());
  } else {
    Command command;

    std::string key = doc["command"].as<std::string>();

    command.key = key;
    command.command = ebus::Sequence::to_vector(key);
    command.unit = doc["unit"].as<std::string>();
    command.active = doc["active"].as<bool>();
    command.interval = doc["interval"].as<uint32_t>();
    command.last = 0;
    command.master = doc["master"].as<bool>();
    command.position = doc["position"].as<size_t>();
    command.datatype =
        ebus::string2datatype(doc["datatype"].as<const char *>());
    command.topic = doc["topic"].as<std::string>();
    command.ha = doc["ha"].as<bool>();
    command.ha_class = doc["ha_class"].as<std::string>();

    std::vector<Command> *usedCommands = nullptr;
    if (command.active)
      usedCommands = &activeCommands;
    else
      usedCommands = &passiveCommands;

    const std::vector<Command>::const_iterator it =
        std::find_if(usedCommands->begin(), usedCommands->end(),
                     [&key](const Command &cmd) { return cmd.key == key; });

    if (it != usedCommands->end()) usedCommands->erase(it);

    usedCommands->push_back(command);
    publishCommand(usedCommands, command.key, false);

    if (command.ha) publishHomeAssistant(usedCommands, command.key, false);

    initDone = false;
  }
}

void Schedule::removeCommand(const char *topic) {
  std::string tmp = topic;
  std::string key(tmp.substr(tmp.rfind("/") + 1));

  const std::vector<Command>::const_iterator it =
      std::find_if(activeCommands.begin(), activeCommands.end(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (it != activeCommands.end()) {
    publishCommand(&activeCommands, key, true);

    if (it->ha) publishHomeAssistant(&activeCommands, key, true);

    activeCommands.erase(it);
    publishCommands();
  } else {
    const std::vector<Command>::const_iterator it =
        std::find_if(passiveCommands.begin(), passiveCommands.end(),
                     [&key](const Command &cmd) { return cmd.key == key; });

    if (it != passiveCommands.end()) {
      publishCommand(&passiveCommands, key, true);

      if (it->ha) publishHomeAssistant(&passiveCommands, key, true);

      passiveCommands.erase(it);
      publishCommands();
    } else {
      std::string err = key + " not found";
      mqttClient.publish("ebus/config/error", 0, false, err.c_str());
    }
  }
}

void Schedule::publishCommands() const {
  for (const Command &command : activeCommands)
    publishCommand(&activeCommands, command.key, false);

  for (const Command &command : passiveCommands)
    publishCommand(&passiveCommands, command.key, false);

  if (activeCommands.size() + passiveCommands.size() == 0)
    mqttClient.publish("ebus/config/installed", 0, false, "");
}

const char *Schedule::printCommands() const {
  std::string payload;
  JsonDocument doc;

  if (activeCommands.size() > 0) {
    for (const Command &command : activeCommands) {
      JsonObject obj = doc.add<JsonObject>();
      obj["command"] = command.key;
      obj["unit"] = command.unit;
      obj["active"] = true;
      obj["interval"] = command.interval;
      obj["master"] = command.master;
      obj["position"] = command.position;
      obj["datatype"] = ebus::datatype2string(command.datatype);
      obj["topic"] = command.topic;
      obj["ha"] = command.ha;
      obj["ha_class"] = command.ha_class;
    }
  }

  if (passiveCommands.size() > 0) {
    for (const Command &command : passiveCommands) {
      JsonObject obj = doc.add<JsonObject>();
      obj["command"] = command.key;
      obj["unit"] = command.unit;
      obj["active"] = false;
      obj["interval"] = command.interval;
      obj["master"] = command.master;
      obj["position"] = command.position;
      obj["datatype"] = ebus::datatype2string(command.datatype);
      obj["topic"] = command.topic;
      obj["ha"] = command.ha;
      obj["ha_class"] = command.ha_class;
    }
  }

  if (doc.isNull()) {
    doc.to<JsonArray>();
  }

  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload.c_str();
}

bool Schedule::needTX() { return activeCommands.size() > 0; }

void Schedule::processSend() {
  if (activeCommands.size() == 0) return;

  if (ebusHandler.getState() == ebus::State::MonitorBus) {
    if (millis() > lastCommand + distanceCommands) {
      lastCommand = millis();

      if (ebusHandler.enque(nextActiveCommand())) {
        // start arbitration
        WiFiClient *client = dummyClient;
        uint8_t address = ebusHandler.getAddress();
        setArbitrationClient(client, address);
      }
    }
  }

  ebusHandler.send();
}

bool Schedule::processReceive(bool enhanced, WiFiClient *client,
                              const uint8_t byte) {
  if (!enhanced) ebusHandler.monitor(byte);

  if (activeCommands.size() == 0) return false;

  if (ebusHandler.getState() == ebus::State::Arbitration) {
    // workaround - master sequence comes twice - waiting for second master
    // sequence without "enhanced" flag
    if (!enhanced && client == dummyClient) ebusHandler.receive(byte);

    // reset - if arbitration went wrong
    if (millis() > lastCommand + 1 * 1000) ebusHandler.reset();
  } else {
    ebusHandler.receive(byte);
  }

  return true;
}

void Schedule::resetCounters() { ebusHandler.resetCounters(); }

void Schedule::publishCounters() {
  ebus::Counter counters = ebusHandler.getCounters();

  publishTopic(initCounters, "ebus/messages/total", lastCounters.total,
               counters.total);

  publishTopic(initCounters, "ebus/messages/success", lastCounters.success,
               counters.success);
  publishTopic(initCounters, "ebus/messages/success/percent",
               lastCounters.successPercent, counters.successPercent);
  publishTopic(initCounters, "ebus/messages/success/master_slave",
               lastCounters.successMS, counters.successMS);
  publishTopic(initCounters, "ebus/messages/success/master_master",
               lastCounters.successMM, counters.successMM);
  publishTopic(initCounters, "ebus/messages/success/broadcast",
               lastCounters.successBC, counters.successBC);

  publishTopic(initCounters, "ebus/messages/failure", lastCounters.failure,
               counters.failure);
  publishTopic(initCounters, "ebus/messages/failure/percent",
               lastCounters.failurePercent, counters.failurePercent);

  publishTopic(initCounters, "ebus/messages/failure/master/empty",
               lastCounters.failureMaster[SEQ_EMPTY],
               counters.failureMaster[SEQ_EMPTY]);
  publishTopic(initCounters, "ebus/messages/failure/master/ok",
               lastCounters.failureMaster[SEQ_OK],
               counters.failureMaster[SEQ_OK]);
  publishTopic(initCounters, "ebus/messages/failure/master/short",
               lastCounters.failureMaster[SEQ_ERR_SHORT],
               counters.failureMaster[SEQ_ERR_SHORT]);
  publishTopic(initCounters, "ebus/messages/failure/master/long",
               lastCounters.failureMaster[SEQ_ERR_LONG],
               counters.failureMaster[SEQ_ERR_LONG]);
  publishTopic(initCounters, "ebus/messages/failure/master/nn",
               lastCounters.failureMaster[SEQ_ERR_NN],
               counters.failureMaster[SEQ_ERR_NN]);
  publishTopic(initCounters, "ebus/messages/failure/master/crc",
               lastCounters.failureMaster[SEQ_ERR_CRC],
               counters.failureMaster[SEQ_ERR_CRC]);
  publishTopic(initCounters, "ebus/messages/failure/master/ack",
               lastCounters.failureMaster[SEQ_ERR_ACK],
               counters.failureMaster[SEQ_ERR_ACK]);
  publishTopic(initCounters, "ebus/messages/failure/master/qq",
               lastCounters.failureMaster[SEQ_ERR_QQ],
               counters.failureMaster[SEQ_ERR_QQ]);
  publishTopic(initCounters, "ebus/messages/failure/master/zz",
               lastCounters.failureMaster[SEQ_ERR_ZZ],
               counters.failureMaster[SEQ_ERR_ZZ]);
  publishTopic(initCounters, "ebus/messages/failure/master/ack_miss",
               lastCounters.failureMaster[SEQ_ERR_ACK_MISS],
               counters.failureMaster[SEQ_ERR_ACK_MISS]);
  publishTopic(initCounters, "ebus/messages/failure/master/invalid",
               lastCounters.failureMaster[SEQ_ERR_INVALID],
               counters.failureMaster[SEQ_ERR_INVALID]);

  publishTopic(initCounters, "ebus/messages/failure/slave/empty",
               lastCounters.failureSlave[SEQ_EMPTY],
               counters.failureSlave[SEQ_EMPTY]);
  publishTopic(initCounters, "ebus/messages/failure/slave/ok",
               lastCounters.failureSlave[SEQ_OK],
               counters.failureSlave[SEQ_OK]);
  publishTopic(initCounters, "ebus/messages/failure/slave/short",
               lastCounters.failureSlave[SEQ_ERR_SHORT],
               counters.failureSlave[SEQ_ERR_SHORT]);
  publishTopic(initCounters, "ebus/messages/failure/slave/long",
               lastCounters.failureSlave[SEQ_ERR_LONG],
               counters.failureSlave[SEQ_ERR_LONG]);
  publishTopic(initCounters, "ebus/messages/failure/slave/nn",
               lastCounters.failureSlave[SEQ_ERR_NN],
               counters.failureSlave[SEQ_ERR_NN]);
  publishTopic(initCounters, "ebus/messages/failure/slave/crc",
               lastCounters.failureSlave[SEQ_ERR_CRC],
               counters.failureSlave[SEQ_ERR_CRC]);
  publishTopic(initCounters, "ebus/messages/failure/slave/ack",
               lastCounters.failureSlave[SEQ_ERR_ACK],
               counters.failureSlave[SEQ_ERR_ACK]);
  publishTopic(initCounters, "ebus/messages/failure/slave/qq",
               lastCounters.failureSlave[SEQ_ERR_QQ],
               counters.failureSlave[SEQ_ERR_QQ]);
  publishTopic(initCounters, "ebus/messages/failure/slave/zz",
               lastCounters.failureSlave[SEQ_ERR_ZZ],
               counters.failureSlave[SEQ_ERR_ZZ]);
  publishTopic(initCounters, "ebus/messages/failure/slave/ack_miss",
               lastCounters.failureSlave[SEQ_ERR_ACK_MISS],
               counters.failureSlave[SEQ_ERR_ACK_MISS]);
  publishTopic(initCounters, "ebus/messages/failure/slave/invalid",
               lastCounters.failureSlave[SEQ_ERR_INVALID],
               counters.failureSlave[SEQ_ERR_INVALID]);

  publishTopic(initCounters, "ebus/messages/special/00", lastCounters.special00,
               counters.special00);
  publishTopic(initCounters, "ebus/messages/special/0704Success",
               lastCounters.special0704Success, counters.special0704Success);
  publishTopic(initCounters, "ebus/messages/special/0704Failure",
               lastCounters.special0704Failure, counters.special0704Failure);

  lastCounters = counters;
  initCounters = false;
}

void Schedule::publishCommand(const std::vector<Command> *command,
                              const std::string key, bool remove) const {
  const std::vector<Command>::const_iterator it =
      std::find_if(command->begin(), command->end(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (it != command->end()) {
    std::string topic = "ebus/config/installed/" + it->key;

    std::string payload;

    if (!remove) {
      JsonDocument doc;

      doc["command"] = it->key;
      doc["unit"] = it->unit;
      doc["active"] = it->active;
      doc["interval"] = it->interval;
      doc["master"] = it->master;
      doc["position"] = it->position;
      doc["datatype"] = ebus::datatype2string(it->datatype);
      doc["topic"] = it->topic;
      doc["ha"] = it->ha;
      doc["ha_class"] = it->ha_class;

      serializeJson(doc, payload);
    }

    mqttClient.publish(topic.c_str(), 0, false, payload.c_str());
  }
}

void Schedule::publishHomeAssistant(const std::vector<Command> *command,
                                    const std::string key, bool remove) const {
  const std::vector<Command>::const_iterator it =
      std::find_if(command->begin(), command->end(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (it != command->end()) {
    std::string name = it->topic;
    std::replace(name.begin(), name.end(), '/', '_');

    std::string topic = "homeassistant/sensor/ebus/" + name + "/config";

    std::string payload;

    if (!remove) {
      JsonDocument doc;

      doc["name"] = name;
      if (it->ha_class.compare("null") != 0 && it->ha_class.length() > 0)
        doc["device_class"] = it->ha_class;
      doc["state_topic"] = "ebus/values/" + it->topic;
      if (it->unit.compare("null") != 0 && it->unit.length() > 0)
        doc["unit_of_measurement"] = it->unit;
      doc["unique_id"] = it->key;
      doc["value_template"] = "{{value_json.value}}";

      serializeJson(doc, payload);
    }

    mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
  }
}

const std::vector<uint8_t> Schedule::nextActiveCommand() {
  if (!initDone) {
    size_t count =
        std::count_if(activeCommands.begin(), activeCommands.end(),
                      [](const Command &cmd) { return cmd.last == 0; });

    if (count == 0) {
      initDone = true;
    } else {
      actCommand =
          &(*std::find_if(activeCommands.begin(), activeCommands.end(),
                          [](const Command &cmd) { return cmd.last == 0; }));
    }
  } else {
    actCommand =
        &(*std::min_element(activeCommands.begin(), activeCommands.end(),
                            [](const Command &lhs, const Command &rhs) {
                              return (lhs.last + lhs.interval * 1000) <
                                     (rhs.last + rhs.interval * 1000);
                            }));

    if (millis() < actCommand->last + actCommand->interval * 1000)
      return ebus::Sequence::to_vector("");
  }

  return actCommand->command;
}

bool Schedule::busReadyCallback() { return Bus.availableForWrite(); }

void Schedule::busWriteCallback(const uint8_t byte) { Bus.write(byte); }

void Schedule::responseCallback(const std::vector<uint8_t> slave) {
  schedule.processResponse(std::vector<uint8_t>(slave));
}

void Schedule::telegramCallback(const std::vector<uint8_t> master,
                                const std::vector<uint8_t> slave) {
  schedule.processTelegram(std::vector<uint8_t>(master),
                           std::vector<uint8_t>(slave));
}

void Schedule::processResponse(const std::vector<uint8_t> slave) {
  publishValue(actCommand, slave);
}

void Schedule::processTelegram(const std::vector<uint8_t> master,
                               const std::vector<uint8_t> slave) {
  size_t count =
      std::count_if(passiveCommands.begin(), passiveCommands.end(),
                    [&master](const Command &cmd) {
                      return ebus::Sequence::contains(master, cmd.command);
                    });

  if (count > 0) {
    Command *pasCommand =
        &(*std::find_if(passiveCommands.begin(), passiveCommands.end(),
                        [&master](const Command &cmd) {
                          return ebus::Sequence::contains(master, cmd.command);
                        }));

    if (pasCommand->master)
      publishValue(pasCommand,
                   ebus::Sequence::range(master, 4, master.size() - 4));
    else
      publishValue(pasCommand, slave);
  }
}

void Schedule::publishValue(Command *command,
                            const std::vector<uint8_t> value) {
  command->last = millis();
  JsonDocument doc;

  switch (command->datatype) {
    case ebus::Datatype::BCD:
      doc["value"] =
          ebus::byte_2_bcd(ebus::Sequence::range(value, command->position, 1));
      break;
    case ebus::Datatype::UINT8:
      doc["value"] = ebus::byte_2_uint8(
          ebus::Sequence::range(value, command->position, 1));
      break;
    case ebus::Datatype::INT8:
      doc["value"] =
          ebus::byte_2_int8(ebus::Sequence::range(value, command->position, 1));
      break;
    case ebus::Datatype::UINT16:
      doc["value"] = ebus::byte_2_uint16(
          ebus::Sequence::range(value, command->position, 2));
      break;
    case ebus::Datatype::INT16:
      doc["value"] = ebus::byte_2_int16(
          ebus::Sequence::range(value, command->position, 2));
      break;
    case ebus::Datatype::UINT32:
      doc["value"] = ebus::byte_2_uint32(
          ebus::Sequence::range(value, command->position, 4));
      break;
    case ebus::Datatype::INT32:
      doc["value"] = ebus::byte_2_int32(
          ebus::Sequence::range(value, command->position, 4));
      break;
    case ebus::Datatype::DATA1b:
      doc["value"] = ebus::byte_2_data1b(
          ebus::Sequence::range(value, command->position, 1));
      break;
    case ebus::Datatype::DATA1c:
      doc["value"] = ebus::byte_2_data1c(
          ebus::Sequence::range(value, command->position, 1));
      break;
    case ebus::Datatype::DATA2b:
      doc["value"] = ebus::byte_2_data2b(
          ebus::Sequence::range(value, command->position, 2));
      break;
    case ebus::Datatype::DATA2c:
      doc["value"] = ebus::byte_2_data2c(
          ebus::Sequence::range(value, command->position, 2));
      break;
    case ebus::Datatype::FLOAT:
      doc["value"] = ebus::byte_2_float(
          ebus::Sequence::range(value, command->position, 2));
      break;
    default:
      break;
  }

  std::string payload;
  serializeJson(doc, payload);

  std::string topic = "ebus/values/" + command->topic;

  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}
