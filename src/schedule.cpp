#include "schedule.hpp"

#include <ArduinoJson.h>

#include "bus.hpp"
#include "mqtt.hpp"

Track<uint32_t> total("ebus/messages/total", 10);

// passive + reactive
Track<uint32_t> passive("ebus/messages/passive", 10);
Track<float> passivePercent("ebus/messages/passive/percent", 10);

Track<uint32_t> passiveMS("ebus/messages/passive/passiveMS", 10);
Track<uint32_t> passiveMM("ebus/messages/passive/passiveMM", 10);

Track<uint32_t> reactiveMS("ebus/messages/passive/reactiveMS", 10);
Track<uint32_t> reactiveMM("ebus/messages/passive/reactiveMM", 10);
Track<uint32_t> reactiveBC("ebus/messages/passive/reactiveBC", 10);

// active
Track<uint32_t> active("ebus/messages/active", 10);
Track<float> activePercent("ebus/messages/active/percent", 10);

Track<uint32_t> activeMS("ebus/messages/active/activeMS", 10);
Track<uint32_t> activeMM("ebus/messages/active/activeMM", 10);
Track<uint32_t> activeBC("ebus/messages/active/activeBC", 10);

// error
Track<uint32_t> error("ebus/messages/error", 10);
Track<float> errorPercent("ebus/messages/error/percent", 10);

Track<uint32_t> errorPassive("ebus/messages/error/passive", 10);
Track<float> errorPassivePercent("ebus/messages/error/passive/percent", 10);

Track<uint32_t> errorPassiveMaster("ebus/messages/error/passive/master", 10);
Track<uint32_t> errorPassiveMasterACK("ebus/messages/error/passive/masterACK",
                                      10);
Track<uint32_t> errorPassiveSlaveACK("ebus/messages/error/passive/slaveACK",
                                     10);

Track<uint32_t> errorReactiveSlaveACK(
    "ebus/messages/error/passive/reactiveSlaveACK", 10);

Track<uint32_t> errorActive("ebus/messages/error/active", 10);
Track<float> errorActivePercent("ebus/messages/error/active/percent", 10);

Track<uint32_t> errorActiveMasterACK("ebus/messages/error/active/masterACK",
                                     10);
Track<uint32_t> errorActiveSlaveACK("ebus/messages/error/active/slaveACK", 10);

// reset
Track<uint32_t> resetAll("ebus/messages/reset", 10);

Track<uint32_t> resetPassive("ebus/messages/reset/passive", 10);
Track<uint32_t> resetActive("ebus/messages/reset/active", 10);

// request
Track<uint32_t> requestTotal("ebus/requests/total", 10);

Track<uint32_t> requestWon("ebus/requests/won", 10);
Track<float> requestWonPercent("ebus/requests/won/percent", 10);

Track<uint32_t> requestLost("ebus/requests/lost", 10);
Track<float> requestLostPercent("ebus/requests/lost/percent", 10);

Track<uint32_t> requestRetry("ebus/requests/retry", 10);
Track<uint32_t> requestError("ebus/requests/error", 10);

Schedule schedule;

Schedule::Schedule()
    : ebusHandler(0xff, &busReadyCallback, &busWriteCallback, &activeCallback,
                  &passiveCallback, &reactiveCallback) {
  ebusHandler.setMaxLockCounter(3);
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

        ebusHandler.enque(command);
        // TODO(yuhu-) handle arbitration external ?
        // start arbitration
        // WiFiClient *client = dummyClient;
        // uint8_t address = ebusHandler.getAddress();
        // setArbitrationClient(client, address);
      }
    }
  }
}

bool Schedule::processData(const uint8_t byte) {
  ebusHandler.run(byte);
  return ebusHandler.isActive();
}

void Schedule::resetCounters() { ebusHandler.resetCounters(); }

void Schedule::publishCounters() {
  ebus::Counters counters = ebusHandler.getCounters();
  total = counters.total;

  // passive + reactive
  passive = counters.passive;
  passivePercent = counters.passivePercent;

  passiveMS = counters.passiveMS;
  passiveMM = counters.passiveMM;

  reactiveMS = counters.reactiveMS;
  reactiveMM = counters.reactiveMM;
  reactiveBC = counters.reactiveBC;

  // active
  active = counters.active;
  activePercent = counters.activePercent;

  activeMS = counters.activeMS;
  activeMM = counters.activeMM;
  activeBC = counters.activeBC;

  // error
  error = counters.error;
  errorPercent = counters.errorPercent;

  errorPassive = counters.errorPassive;
  errorPassivePercent = counters.errorPassivePercent;

  errorPassiveMaster = counters.errorPassiveMaster;
  errorPassiveMasterACK = counters.errorPassiveMasterACK;
  errorPassiveSlaveACK = counters.errorPassiveSlaveACK;

  errorReactiveSlaveACK = counters.errorReactiveSlaveACK;

  errorActive = counters.errorActive;
  errorActivePercent = counters.errorActivePercent;

  errorActiveMasterACK = counters.errorActiveMasterACK;
  errorActiveSlaveACK = counters.errorActiveSlaveACK;

  // reset
  resetAll = counters.reset;
  resetPassive = counters.resetPassive;
  resetActive = counters.resetActive;

  // request
  requestTotal = counters.requestTotal;

  requestWon = counters.requestWon;
  requestWonPercent = counters.requestWonPercent;

  requestLost = counters.requestLost;
  requestLostPercent = counters.requestLostPercent;

  requestRetry = counters.requestRetry;
  requestError = counters.requestError;
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
      // TODO(yuhu-) - fill with thing data
      // 0004070400
      search = {0x07, 0x04};
      if (ebus::Sequence::contains(master, search))
        *slave = ebus::Sequence::to_vector("0acc454255533006010602");

      schedule.processPassive(std::vector<uint8_t>(master),
                              std::vector<uint8_t>(*slave));
      break;
    default:
      break;
  }
}

void Schedule::errorCallback(const std::string &str) {
  std::string topic = "ebus/messages/reset/last";
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
