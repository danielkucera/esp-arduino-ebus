#include "schedule.hpp"

#include <ArduinoJson.h>

#include "bus.hpp"
#include "mqtt.hpp"

Track<uint32_t> total("ebus/messages/total", 10);

Track<uint32_t> success("ebus/messages/success", 10);
Track<float> successPercent("ebus/messages/success/percent", 10);
Track<uint32_t> successMS("ebus/messages/success/master_slave", 10);
Track<uint32_t> successMM("ebus/messages/success/master_master", 10);
Track<uint32_t> successBC("ebus/messages/success/broadcast", 10);

Track<uint32_t> failure("ebus/messages/failure", 10);
Track<float> failurePercent("ebus/messages/failure/percent", 10);

Track<uint32_t> failureMasterEmpty("ebus/messages/failure/master/empty", 10);
Track<uint32_t> failureMasterOk("ebus/messages/failure/master/ok", 10);
Track<uint32_t> failureMasterShort("ebus/messages/failure/master/short", 10);
Track<uint32_t> failureMasterLong("ebus/messages/failure/master/long", 10);
Track<uint32_t> failureMasterNN("ebus/messages/failure/master/nn", 10);
Track<uint32_t> failureMasterCRC("ebus/messages/failure/master/crc", 10);
Track<uint32_t> failureMasterACK("ebus/messages/failure/master/ack", 10);
Track<uint32_t> failureMasterQQ("ebus/messages/failure/master/qq", 10);
Track<uint32_t> failureMasterZZ("ebus/messages/failure/master/zz", 10);
Track<uint32_t> failureMasterAckMiss("ebus/messages/failure/master/ack_miss",
                                     10);
Track<uint32_t> failureMasterInvalid("ebus/messages/failure/master/invalid",
                                     10);

Track<uint32_t> failureSlaveEmpty("ebus/messages/failure/slave/empty", 10);
Track<uint32_t> failureSlaveOk("ebus/messages/failure/slave/ok", 10);
Track<uint32_t> failureSlaveShort("ebus/messages/failure/slave/short", 10);
Track<uint32_t> failureSlaveLong("ebus/messages/failure/slave/long", 10);
Track<uint32_t> failureSlaveNN("ebus/messages/failure/slave/nn", 10);
Track<uint32_t> failureSlaveCRC("ebus/messages/failure/slave/crc", 10);
Track<uint32_t> failureSlaveACK("ebus/messages/failure/slave/ack", 10);
Track<uint32_t> failureSlaveQQ("ebus/messages/failure/slave/qq", 10);
Track<uint32_t> failureSlaveZZ("ebus/messages/failure/slave/zz", 10);
Track<uint32_t> failureSlaveAckMiss("ebus/messages/failure/slave/ack_miss", 10);
Track<uint32_t> failureSlaveInvalid("ebus/messages/failure/slave/invalid", 10);

Track<uint32_t> special00("ebus/messages/special/00", 10);
Track<uint32_t> special0704Success("ebus/messages/special/0704Success", 10);
Track<uint32_t> special0704Failure("ebus/messages/special/0704Failure", 10);

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

void Schedule::enqueCommand(const char *payload) {
  newCommands.push_back(std::string(payload));
}

void Schedule::enqueCommands(const char *payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    std::string err = "DeserializationError ";
    err += error.c_str();
    mqttClient.publish("ebus/config/error", 0, false, err.c_str());
  } else {
    JsonArray array = doc.as<JsonArray>();
    for (JsonVariant variant : array)
      newCommands.push_back(variant.as<std::string>());
  }
}

// payload - optional: unit, ha_class
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

    initDone = false;
    lastInsert = millis();
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

    activeCommands.erase(it);
    publishCommands();
  } else {
    const std::vector<Command>::const_iterator it =
        std::find_if(passiveCommands.begin(), passiveCommands.end(),
                     [&key](const Command &cmd) { return cmd.key == key; });

    if (it != passiveCommands.end()) {
      publishCommand(&passiveCommands, key, true);

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

const char *Schedule::getCommands() const {
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

const char *Schedule::serializeCommands() const {
  std::string payload;
  JsonDocument doc;

  if (activeCommands.size() > 0) {
    for (const Command &command : activeCommands) {
      JsonArray arr = doc.add<JsonArray>();
      arr.add(command.key);
      arr.add(command.unit);
      arr.add(true);
      arr.add(command.interval);
      arr.add(command.master);
      arr.add(command.position);
      arr.add(ebus::datatype2string(command.datatype));
      arr.add(command.topic);
      arr.add(command.ha);
      arr.add(command.ha_class);
    }
  }

  if (passiveCommands.size() > 0) {
    for (const Command &command : passiveCommands) {
      JsonArray arr = doc.add<JsonArray>();
      arr.add(command.key);
      arr.add(command.unit);
      arr.add(false);
      arr.add(command.interval);
      arr.add(command.master);
      arr.add(command.position);
      arr.add(ebus::datatype2string(command.datatype));
      arr.add(command.topic);
      arr.add(command.ha);
      arr.add(command.ha_class);
    }
  }

  if (doc.isNull()) {
    doc.to<JsonArray>();
  }

  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload.c_str();
}

void Schedule::deserializeCommands(const char *payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    std::string err = "DeserializationError ";
    err += error.c_str();
    mqttClient.publish("ebus/config/error", 0, false, err.c_str());
  } else {
    JsonArray array = doc.as<JsonArray>();
    for (JsonVariant variant : array) {
      JsonDocument tmpDoc;

      tmpDoc["command"] = variant[0];
      tmpDoc["unit"] = variant[1];
      tmpDoc["active"] = variant[2];
      tmpDoc["interval"] = variant[3];
      tmpDoc["master"] = variant[4];
      tmpDoc["position"] = variant[5];
      tmpDoc["datatype"] = variant[6];
      tmpDoc["topic"] = variant[7];
      tmpDoc["ha"] = variant[8];
      tmpDoc["ha_class"] = variant[9];

      std::string tmpPayload;
      serializeJson(tmpDoc, tmpPayload);

      newCommands.push_back(tmpPayload);
    }
  }
}

void Schedule::publishRaw(const char *payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    std::string err = "DeserializationError ";
    err += error.c_str();
    mqttClient.publish("ebus/config/error", 0, false, err.c_str());
  } else {
    raw = doc.as<bool>();
  }
}

// payload
// [
//   "0700",
//   "b509",
//   "..."
// ]
void Schedule::handleFilter(const char *payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    std::string err = "DeserializationError ";
    err += error.c_str();
    mqttClient.publish("ebus/config/error", 0, false, err.c_str());
  } else {
    rawFilters.clear();

    JsonArray arr = doc.as<JsonArray>();

    for (JsonVariant value : arr)
      rawFilters.push_back(ebus::Sequence::to_vector(value));
  }
}

bool Schedule::needTX() { return activeCommands.size() > 0; }

void Schedule::processSend() {
  checkNewCommands();

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
  if (!enhanced) ebusHandler.feedCounters(byte);

  if (activeCommands.size() == 0) return false;

  if (ebusHandler.getState() == ebus::State::Arbitration) {
    // workaround - master sequence comes twice - waiting for second master
    // sequence without "enhanced" flag
    if (!enhanced && client == dummyClient) ebusHandler.receive(byte);

    // reset - if arbitration last longer than 200ms
    if (millis() > lastCommand + 200) {
      ebusHandler.reset();
      mqttClient.publish("ebus/arbitration/error", 0, false,
                         "arbitration last longer than 200ms");
    }
  } else {
    ebusHandler.receive(byte);
  }

  return true;
}

void Schedule::resetCounters() { ebusHandler.resetCounters(); }

void Schedule::checkNewCommands() {
  if (newCommands.size() > 0) {
    if (millis() > lastInsert + distanceInsert) {
      std::string payload = newCommands.front();
      newCommands.pop_front();
      insertCommand(payload.c_str());
    }
  }
}

void Schedule::publishCounters() {
  ebus::Counter counters = ebusHandler.getCounters();
  total = counters.total;

  success = counters.success;
  successPercent = counters.successPercent;
  successMS = counters.successMS;
  successMM = counters.successMM;
  successBC = counters.successBC;

  failure = counters.failure;
  failurePercent = counters.failurePercent;

  failureMasterEmpty = counters.failureMaster[SEQ_EMPTY];
  failureMasterOk = counters.failureMaster[SEQ_OK];
  failureMasterShort = counters.failureMaster[SEQ_ERR_SHORT];
  failureMasterLong = counters.failureMaster[SEQ_ERR_LONG];
  failureMasterNN = counters.failureMaster[SEQ_ERR_NN];
  failureMasterCRC = counters.failureMaster[SEQ_ERR_CRC];
  failureMasterACK = counters.failureMaster[SEQ_ERR_ACK];
  failureMasterQQ = counters.failureMaster[SEQ_ERR_QQ];
  failureMasterZZ = counters.failureMaster[SEQ_ERR_ZZ];
  failureMasterAckMiss = counters.failureMaster[SEQ_ERR_ACK_MISS];
  failureMasterInvalid = counters.failureMaster[SEQ_ERR_INVALID];

  failureSlaveEmpty = counters.failureSlave[SEQ_EMPTY];
  failureSlaveOk = counters.failureSlave[SEQ_OK];
  failureSlaveShort = counters.failureSlave[SEQ_ERR_SHORT];
  failureSlaveLong = counters.failureSlave[SEQ_ERR_LONG];
  failureSlaveNN = counters.failureSlave[SEQ_ERR_NN];
  failureSlaveCRC = counters.failureSlave[SEQ_ERR_CRC];
  failureSlaveACK = counters.failureSlave[SEQ_ERR_ACK];
  failureSlaveQQ = counters.failureSlave[SEQ_ERR_QQ];
  failureSlaveZZ = counters.failureSlave[SEQ_ERR_ZZ];
  failureSlaveAckMiss = counters.failureSlave[SEQ_ERR_ACK_MISS];
  failureSlaveInvalid = counters.failureSlave[SEQ_ERR_INVALID];

  special00 = counters.special00;
  special0704Success = counters.special0704Success;
  special0704Failure = counters.special0704Failure;
}

void Schedule::publishCommand(const std::vector<Command> *commands,
                              const std::string key, bool remove) const {
  const std::vector<Command>::const_iterator it =
      std::find_if(commands->begin(), commands->end(),
                   [&key](const Command &cmd) { return cmd.key == key; });

  if (it != commands->end()) {
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

    if (remove) {
      topic = "ebus/values/" + it->topic;
      mqttClient.publish(topic.c_str(), 0, false, "");

      publishHomeAssistant(&(*it), true);
    } else {
      publishHomeAssistant(&(*it), !it->ha);
    }
  }
}

void Schedule::publishHomeAssistant(const Command *command, bool remove) const {
  std::string name = command->topic;
  std::replace(name.begin(), name.end(), '/', '_');

  std::string topic = "homeassistant/sensor/ebus/" + name + "/config";

  std::string payload;

  if (!remove) {
    JsonDocument doc;

    doc["name"] = name;
    if (command->ha_class.compare("null") != 0 &&
        command->ha_class.length() > 0)
      doc["device_class"] = command->ha_class;
    doc["state_topic"] = "ebus/values/" + command->topic;
    if (command->unit.compare("null") != 0 && command->unit.length() > 0)
      doc["unit_of_measurement"] = command->unit;
    doc["unique_id"] = command->key;
    doc["value_template"] = "{{value_json.value}}";

    serializeJson(doc, payload);
  }

  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
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
  if (raw) {
    size_t count = std::count_if(rawFilters.begin(), rawFilters.end(),
                                 [&master](const std::vector<uint8_t> &vec) {
                                   return ebus::Sequence::contains(master, vec);
                                 });
    if (count > 0 || rawFilters.size() == 0) {
      std::string topic =
          "ebus/values/raw/" + ebus::Sequence::to_string(master);
      std::string payload = ebus::Sequence::to_string(slave);
      if (payload.empty()) payload = "-";

      mqttClient.publish(topic.c_str(), 0, false, payload.c_str());
    }
  }

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
