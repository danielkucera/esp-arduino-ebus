#include "schedule.hpp"

#include <ArduinoJson.h>

#include <sstream>

#include "bus.hpp"
#include "mqtt.hpp"

// messages
Track<uint32_t> messagesTotal("ebus/messages/total", 10);

Track<uint32_t> messagesPassiveMS("ebus/messages/passiveMS", 10);
Track<uint32_t> messagesPassiveMM("ebus/messages/passiveMM", 10);

Track<uint32_t> messagesReactiveMS("ebus/messages/reactiveMS", 10);
Track<uint32_t> messagesReactiveMM("ebus/messages/reactiveMM", 10);
Track<uint32_t> messagesReactiveBC("ebus/messages/reactiveBC", 10);

Track<uint32_t> messagesActiveMS("ebus/messages/activeMS", 10);
Track<uint32_t> messagesActiveMM("ebus/messages/activeMM", 10);
Track<uint32_t> messagesActiveBC("ebus/messages/activeBC", 10);

// errors
Track<uint32_t> errorsTotal("ebus/errors/total", 10);

Track<uint32_t> errorsPassive("ebus/errors/passive", 10);
Track<uint32_t> errorsPassiveMaster("ebus/errors/passive/master", 10);
Track<uint32_t> errorsPassiveMasterACK("ebus/errors/passive/masterACK", 10);
Track<uint32_t> errorsPassiveSlave("ebus/errors/passive/slave", 10);
Track<uint32_t> errorsPassiveSlaveACK("ebus/errors/passive/slaveACK", 10);

Track<uint32_t> errorsReactive("ebus/errors/reactive", 10);
Track<uint32_t> errorsReactiveMaster("ebus/errors/reactive/master", 10);
Track<uint32_t> errorsReactiveMasterACK("ebus/errors/reactive/masterACK", 10);
Track<uint32_t> errorsReactiveSlave("ebus/errors/reactive/slave", 10);
Track<uint32_t> errorsReactiveSlaveACK("ebus/errors/reactive/slaveACK", 10);

Track<uint32_t> errorsActive("ebus/errors/active", 10);
Track<uint32_t> errorsActiveMaster("ebus/errors/active/master", 10);
Track<uint32_t> errorsActiveMasterACK("ebus/errors/active/masterACK", 10);
Track<uint32_t> errorsActiveSlave("ebus/errors/active/slave", 10);
Track<uint32_t> errorsActiveSlaveACK("ebus/errors/active/slaveACK", 10);

// resets
Track<uint32_t> resetsTotal("ebus/resets/total", 10);
Track<uint32_t> resetsPassive00("ebus/resets/passive00", 10);
Track<uint32_t> resetsPassive("ebus/resets/passive", 10);
Track<uint32_t> resetsActive("ebus/resets/active", 10);

// requests
Track<uint32_t> requestsTotal("ebus/requests/total", 10);
Track<uint32_t> requestsWon("ebus/requests/won", 10);
Track<uint32_t> requestsLost("ebus/requests/lost", 10);
Track<uint32_t> requestsRetry("ebus/requests/retry", 10);
Track<uint32_t> requestsError("ebus/requests/error", 10);

Schedule schedule;

Schedule::Schedule()
    : ebusHandler(0xff, &busReadyCallback, &busWriteCallback, &activeCallback,
                  &passiveCallback, &reactiveCallback) {
  ebusHandler.setErrorCallback(errorCallback);
}

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

void Schedule::nextCommand() {
  if (sendCommands.size() > 0 || store.active()) {
    if (!ebusHandler.isActive()) {
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
          scheduleCommand = store.nextActiveCommand();
          if (scheduleCommand != nullptr) command = scheduleCommand->command;
        }

        if (command.size() > 0) {
          ebusHandler.enque(command);
        }
      }
    }
  }
}

void Schedule::processData(const uint8_t byte) { ebusHandler.run(byte); }

void Schedule::resetCounters() { ebusHandler.resetCounters(); }

void Schedule::publishCounters() {
  ebus::Counters counters = ebusHandler.getCounters();

  // messages
  messagesTotal = counters.messagesTotal;

  messagesPassiveMS = counters.messagesPassiveMS;
  messagesPassiveMM = counters.messagesPassiveMM;

  messagesReactiveMS = counters.messagesReactiveMS;
  messagesReactiveMM = counters.messagesReactiveMM;
  messagesReactiveBC = counters.messagesReactiveBC;

  messagesActiveMS = counters.messagesActiveMS;
  messagesActiveMM = counters.messagesActiveMM;
  messagesActiveBC = counters.messagesActiveBC;

  // errors
  errorsTotal = counters.errorsTotal;

  errorsPassive = counters.errorsPassive;
  errorsPassiveMaster = counters.errorsPassiveMaster;
  errorsPassiveMasterACK = counters.errorsPassiveMasterACK;
  errorsPassiveSlave = counters.errorsPassiveSlave;
  errorsPassiveSlaveACK = counters.errorsPassiveSlaveACK;

  errorsReactive = counters.errorsReactive;
  errorsReactiveMaster = counters.errorsReactiveMaster;
  errorsReactiveMasterACK = counters.errorsReactiveMasterACK;
  errorsReactiveSlave = counters.errorsReactiveSlave;
  errorsReactiveSlaveACK = counters.errorsReactiveSlaveACK;

  errorsActive = counters.errorsActive;
  errorsActiveMaster = counters.errorsActiveMaster;
  errorsActiveMasterACK = counters.errorsActiveMasterACK;
  errorsActiveSlave = counters.errorsActiveSlave;
  errorsActiveSlaveACK = counters.errorsActiveSlaveACK;

  // resets
  resetsTotal = counters.resetsTotal;
  resetsPassive00 = counters.resetsPassive00;
  resetsPassive = counters.resetsPassive;
  resetsActive = counters.resetsActive;

  // requests
  requestsTotal = counters.requestsTotal;
  requestsWon = counters.requestsWon;
  requestsLost = counters.requestsLost;
  requestsRetry = counters.requestsRetry;
  requestsError = counters.requestsError;
}

bool Schedule::busReadyCallback() { return Bus.availableForWrite(); }

void Schedule::busWriteCallback(const uint8_t byte) { Bus.write(byte); }

void Schedule::activeCallback(const std::vector<uint8_t> &master,
                              const std::vector<uint8_t> &slave) {
  schedule.processActive(std::vector<uint8_t>(master),
                         std::vector<uint8_t>(slave));
}

void Schedule::passiveCallback(const std::vector<uint8_t> &master,
                               const std::vector<uint8_t> &slave) {
  schedule.processPassive(std::vector<uint8_t>(master),
                          std::vector<uint8_t>(slave));
}

void Schedule::reactiveCallback(const std::vector<uint8_t> &master,
                                std::vector<uint8_t> *const slave) {
  std::vector<uint8_t> search;
  switch (ebus::Telegram::typeOf(master[1])) {
    case ebus::Type::BC:
      schedule.processPassive(std::vector<uint8_t>(master),
                              std::vector<uint8_t>());
      break;
    case ebus::Type::MM:
      schedule.processPassive(std::vector<uint8_t>(master),
                              std::vector<uint8_t>());
      break;
    case ebus::Type::MS:
      // TODO(yuhu-): fill with thing data
      // handle Identification (Service 07h 04h)
      // hh...Manufacturer (BYTE)
      // gg...Unit_ID_0-5 (ASCII)
      // ss...Software version (BCD)
      // rr...Revision (BCD)
      // vv...Hardware version (BCD)
      // hh...Revision (BCD)
      // search = {0x07, 0x04};
      // if (ebus::Sequence::contains(master, search))
      //   *slave = ebus::Sequence::to_vector("0ahhggggggggggssrrhhrr");

      schedule.processPassive(std::vector<uint8_t>(master),
                              std::vector<uint8_t>(*slave));
      break;
    default:
      break;
  }
}

void Schedule::errorCallback(const std::string &str) {
  std::string topic = "ebus/resets/last";
  std::string payload = str;
  mqttClient.publish(topic.c_str(), 0, false, payload.c_str());
}

void Schedule::processActive(const std::vector<uint8_t>(master),
                             const std::vector<uint8_t> &slave) {
  if (send)
    publishSend(sendCommand, slave);
  else
    publishValue(scheduleCommand, slave);
}

void Schedule::processPassive(const std::vector<uint8_t> &master,
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

  if (master[2] == 0x07 && master[3] == 0x00) {
    std::string topic = "ebus/system/time";
    std::string payload = "20";
    payload += ebus::Sequence::to_string(ebus::Sequence::range(master, 13, 1));
    payload += "-";
    payload += ebus::Sequence::to_string(ebus::Sequence::range(master, 11, 1));
    payload += "-";
    payload += ebus::Sequence::to_string(ebus::Sequence::range(master, 10, 1));
    payload += " ";
    payload += ebus::Sequence::to_string(ebus::Sequence::range(master, 9, 1));
    payload += ":";
    payload += ebus::Sequence::to_string(ebus::Sequence::range(master, 8, 1));
    payload += ":";
    payload += ebus::Sequence::to_string(ebus::Sequence::range(master, 7, 1));
    mqttClient.publish(topic.c_str(), 0, true, payload.c_str());

    topic = "ebus/system/outdoor";
    std::ostringstream ostr;
    ostr << ebus::byte_2_data2b(ebus::Sequence::range(master, 5, 2));
    ostr << " °C";
    mqttClient.publish(topic.c_str(), 0, true, ostr.str().c_str());
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
    topic += "nodes/" + ebus::Sequence::to_string(master[1]);
    payload += "MF=";
    payload += ebus::Sequence::to_string(ebus::Sequence::range(slave, 1, 1));
    payload += ";ID=";
    payload += ebus::byte_2_string(ebus::Sequence::range(slave, 2, 5));
    payload += ";SW=";
    payload += ebus::Sequence::to_string(ebus::Sequence::range(slave, 7, 2));
    payload += ";HW=";
    payload += ebus::Sequence::to_string(ebus::Sequence::range(slave, 9, 2));
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
