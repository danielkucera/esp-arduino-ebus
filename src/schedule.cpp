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

    const std::vector<Command>::const_iterator it =
        std::find_if(commands.begin(), commands.end(),
                     [&key](const Command &cmd) { return cmd.key == key; });

    if (it != commands.end()) commands.erase(it);

    commands.push_back(command);

    publishCommand(command.key, false);

    if (command.ha) publishHomeAssistant(command.key, false);
  }
}

void Schedule::removeCommand(const char *topic) {
  std::string tmp = topic;
  std::string key(tmp.substr(tmp.rfind("/") + 1));

  const std::vector<Command>::const_iterator it =
      std::find_if(commands.begin(), commands.end(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (it != commands.end()) {
    publishCommand(key, true);

    if (it->ha) publishHomeAssistant(key, true);

    commands.erase(it);
    publishCommands();
  } else {
    std::string err = key + " not found";
    mqttClient.publish("ebus/config/error", 0, false, err.c_str());
  }
}

void Schedule::publishCommands() const {
  if (commands.size() > 0) {
    for (const Command &command : commands)
      publishCommand(command.key, false);
  } else {
    mqttClient.publish("ebus/config/installed", 0, false, "");
  }
}

const char *Schedule::printCommands() const {
  std::string payload;

  if (commands.size() > 0) {
    JsonDocument doc;

    for (const Command &command : commands) {
      JsonObject obj = doc.add<JsonObject>();
      obj["command"] = command.key;
      obj["unit"] = command.unit;
      obj["active"] = command.active;
      obj["interval"] = command.interval;
      obj["master"] = command.master;
      obj["position"] = command.position;
      obj["datatype"] = ebus::datatype2string(command.datatype);
      obj["topic"] = command.topic;
      obj["ha"] = command.ha;
      obj["ha_class"] = command.ha_class;
    }

    doc.shrinkToFit();
    serializeJsonPretty(doc, payload);
  }

  return payload.c_str();
}

bool Schedule::needTX() { return commands.size() > 0; }

void Schedule::processSend() {
  if (commands.size() == 0) return;

  if (ebusHandler.getState() == ebus::State::MonitorBus) {
    if (millis() > lastCommand + distanceCommands) {
      lastCommand = millis();

      if (ebusHandler.enque(nextCommand())) {
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

  if (commands.size() == 0) return false;

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

void Schedule::publishCommand(const std::string key, bool remove) const {
  const std::vector<Command>::const_iterator it =
      std::find_if(commands.begin(), commands.end(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (it != commands.end()) {
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

void Schedule::publishHomeAssistant(const std::string key, bool remove) const {
  const std::vector<Command>::const_iterator it =
      std::find_if(commands.begin(), commands.end(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (it != commands.end()) {
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

const std::vector<uint8_t> Schedule::nextCommand() {
  if (!initDone) {
    size_t count = std::count_if(
        commands.begin(), commands.end(),
        [](const Command &cmd) { return cmd.active && cmd.last == 0; });

    if (count == 0) {
      initDone = true;
    } else {
      actCommand = &(*std::find_if(
          commands.begin(), commands.end(),
          [](const Command &cmd) { return cmd.active && cmd.last == 0; }));
    }
  } else {
    actCommand = &(*std::min_element(commands.begin(), commands.end(),
                                     [](const Command &i, const Command &j) {
                                       return i.active && j.active &&
                                              (i.last + i.interval * 1000) <
                                                  (j.last + j.interval * 1000);
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
  publishValue(actCommand, slave, actCommand->position);
}

void Schedule::processTelegram(const std::vector<uint8_t> master,
                               const std::vector<uint8_t> slave) {
  // Command *curCommand = &(*std::find_if(
  //     commands.begin(), commands.end(), [&master](const Command &cmd) {
  //       return !cmd.active && ebus::Sequence::contains(master, cmd.command);
  //     }));

  // if (curCommand != nullptr) {
  //   if (curCommand->master)
  //     publishValue(curCommand, master, curCommand->position + 4);
  //   else
  //     publishValue(curCommand, slave, curCommand->position);
  // }
}

void Schedule::publishValue(Command *command, const std::vector<uint8_t> value,
                            const size_t position) {
  command->last = millis();

  JsonDocument doc;

  switch (command->datatype) {
    case ebus::Datatype::BCD:
      doc["value"] =
          ebus::byte_2_bcd(ebus::Sequence::range(value, position, 1));
      break;
    case ebus::Datatype::UINT8:
      doc["value"] =
          ebus::byte_2_uint8(ebus::Sequence::range(value, position, 1));
      break;
    case ebus::Datatype::INT8:
      doc["value"] =
          ebus::byte_2_int8(ebus::Sequence::range(value, position, 1));
      break;
    case ebus::Datatype::UINT16:
      doc["value"] =
          ebus::byte_2_uint16(ebus::Sequence::range(value, position, 2));
      break;
    case ebus::Datatype::INT16:
      doc["value"] =
          ebus::byte_2_int16(ebus::Sequence::range(value, position, 2));
      break;
    case ebus::Datatype::UINT32:
      doc["value"] =
          ebus::byte_2_uint32(ebus::Sequence::range(value, position, 4));
      break;
    case ebus::Datatype::INT32:
      doc["value"] =
          ebus::byte_2_int32(ebus::Sequence::range(value, position, 4));
      break;
    case ebus::Datatype::DATA1b:
      doc["value"] =
          ebus::byte_2_data1b(ebus::Sequence::range(value, position, 1));
      break;
    case ebus::Datatype::DATA1c:
      doc["value"] =
          ebus::byte_2_data1c(ebus::Sequence::range(value, position, 1));
      break;
    case ebus::Datatype::DATA2b:
      doc["value"] =
          ebus::byte_2_data2b(ebus::Sequence::range(value, position, 2));
      break;
    case ebus::Datatype::DATA2c:
      doc["value"] =
          ebus::byte_2_data2c(ebus::Sequence::range(value, position, 2));
      break;
    case ebus::Datatype::FLOAT:
      doc["value"] =
          ebus::byte_2_float(ebus::Sequence::range(value, position, 2));
      break;
    default:
      break;
  }

  std::string payload;
  serializeJson(doc, payload);

  std::string topic = "ebus/values/" + command->topic;

  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}
