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

Schedule::Schedule()
    : ebusHandler(0xff, &busReadyCallback, &busWriteCallback, &responseCallback,
                  &telegramCallback) {}

void Schedule::setAddress(const uint8_t source) {
  ebusHandler.setAddress(source);
}

void Schedule::setDistance(const uint8_t distance) {
  distanceCommands = distance * 1000;
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
    JsonArray array = doc.as<JsonArray>();
    for (JsonVariant variant : array)
      rawFilters.push_back(ebus::Sequence::to_vector(variant));
  }
}

// payload ZZ PB SB NN Dx
// [
//   "08070400",
//   "..."
// ]
void Schedule::handleSend(const char *payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    std::string err = "DeserializationError ";
    err += error.c_str();
    mqttClient.publish("ebus/config/error", 0, false, err.c_str());
  } else {
    sendCommands.clear();
    JsonArray array = doc.as<JsonArray>();
    for (JsonVariant variant : array)
      sendCommands.push_back(ebus::Sequence::to_vector(variant));
  }
}

void Schedule::processSend() {
  if (!send && sendCommands.size() == 0 && !store.active()) return;

  if (ebusHandler.getState() == ebus::State::MonitorBus) {
    if (millis() > lastCommand + distanceCommands) {
      lastCommand = millis();

      std::vector<uint8_t> command;
      if (sendCommands.size() > 0) {
        send = true;
        command = sendCommands.front();
        sendCommands.pop_front();
        sendCommand = {ebusHandler.getAddress()};
        sendCommand.insert(sendCommand.end(), command.begin(), command.end());
      } else {
        send = false;
        activeCommand = store.nextActiveCommand();
        if (activeCommand != nullptr) command = activeCommand->command;
      }

      if (ebusHandler.enque(command)) {
        // start arbitration
        WiFiClient *client = dummyClient;
        uint8_t address = ebusHandler.getAddress();
        setArbitrationClient(client, address);
      }
    }
  }

  ebusHandler.send();
}

bool Schedule::processReceive(bool enhanced, const WiFiClient *client,
                              const uint8_t byte) {
  if (!enhanced) ebusHandler.feedCounters(byte);

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

bool Schedule::busReadyCallback() { return Bus.availableForWrite(); }

void Schedule::busWriteCallback(const uint8_t byte) { Bus.write(byte); }

void Schedule::responseCallback(const std::vector<uint8_t> &slave) {
  schedule.processResponse(std::vector<uint8_t>(slave));
}

void Schedule::telegramCallback(const std::vector<uint8_t> &master,
                                const std::vector<uint8_t> &slave) {
  schedule.processTelegram(std::vector<uint8_t>(master),
                           std::vector<uint8_t>(slave));
}

void Schedule::processResponse(const std::vector<uint8_t> &slave) {
  if (send)
    publishSend(sendCommand, slave);
  else
    publishValue(activeCommand, slave);
}

void Schedule::processTelegram(const std::vector<uint8_t> &master,
                               const std::vector<uint8_t> &slave) {
  if (raw) {
    size_t count = std::count_if(rawFilters.begin(), rawFilters.end(),
                                 [&master](const std::vector<uint8_t> &vec) {
                                   return ebus::Sequence::contains(master, vec);
                                 });
    if (count > 0 || rawFilters.size() == 0) {
      std::string topic = "ebus/raw/" + ebus::Sequence::to_string(master);
      std::string payload = ebus::Sequence::to_string(slave);
      if (payload.empty()) payload = "-";

      mqttClient.publish(topic.c_str(), 0, false, payload.c_str());
    }
  }

  Command *pasCommand = store.findPassiveCommand(master);
  if (pasCommand != nullptr) {
    if (pasCommand->master)
      publishValue(pasCommand,
                   ebus::Sequence::range(master, 4, master.size() - 4));
    else
      publishValue(pasCommand, slave);
  }
}

void Schedule::publishSend(const std::vector<uint8_t> &master,
                           const std::vector<uint8_t> &slave) {
  std::string topic = "ebus/";
  std::string payload;
  if (master[2] == 0x07 && master[3] == 0x04) {
    topic += "nodes/" + ebus::Sequence::to_string(master);
    std::string t;
    t += "Address: 0x" +
         ebus::Sequence::to_string(ebus::Sequence::range(master, 1, 1));
    t += " Manufacturer: 0x" +
         ebus::Sequence::to_string(ebus::Sequence::range(slave, 1, 1));
    t += " Type: " + ebus::byte_2_string(ebus::Sequence::range(slave, 2, 5));
    t +=
        " SW: " + ebus::Sequence::to_string(ebus::Sequence::range(slave, 7, 1));
    t += "." + ebus::Sequence::to_string(ebus::Sequence::range(slave, 8, 1));
    t +=
        " HW: " + ebus::Sequence::to_string(ebus::Sequence::range(slave, 9, 1));
    t += "." + ebus::Sequence::to_string(ebus::Sequence::range(slave, 10, 1));
    payload = t;
  } else {
    topic += "sent/" + ebus::Sequence::to_string(master);
    payload = ebus::Sequence::to_string(slave);
  }

  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}

void Schedule::publishValue(Command *command,
                            const std::vector<uint8_t> &value) {
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
