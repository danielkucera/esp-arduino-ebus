#include "schedule.hpp"

#include "bus.hpp"
#include "mqtt.hpp"
#include "track.hpp"

// ebus/<unique_id>/state/internal/messages
Track<uint32_t> messagesTotal("state/internal/messages", 10);

Track<uint32_t> messagesPassiveMasterSlave(
    "state/internal/messages/passiveMasterSlave", 10);
Track<uint32_t> messagesPassiveMasterMaster(
    "state/internal/messages/passiveMasterMaster", 10);

Track<uint32_t> messagesReactiveMasterSlave(
    "state/internal/messages/reactiveMasterSlave", 10);
Track<uint32_t> messagesReactiveMasterMaster(
    "state/internal/messages/reactiveMasterMaster", 10);
Track<uint32_t> messagesReactiveBroadcast(
    "state/internal/messages/reactiveBroadcast", 10);

Track<uint32_t> messagesActiveMasterSlave(
    "state/internal/messages/activeMasterSlave", 10);
Track<uint32_t> messagesActiveMasterMaster(
    "state/internal/messages/activeMasterMaster", 10);
Track<uint32_t> messagesActiveBroadcast(
    "state/internal/messages/activeBroadcast", 10);

// ebus/<unique_id>/state/internal/errors
Track<uint32_t> errorsTotal("state/internal/errors", 10);

Track<uint32_t> errorsPassive("state/internal/errors/passive", 10);
Track<uint32_t> errorsPassiveMaster("state/internal/errors/passive/master", 10);
Track<uint32_t> errorsPassiveMasterACK(
    "state/internal/errors/passive/masterACK", 10);
Track<uint32_t> errorsPassiveSlave("state/internal/errors/passive/slave", 10);
Track<uint32_t> errorsPassiveSlaveACK("state/internal/errors/passive/slaveACK",
                                      10);

Track<uint32_t> errorsReactive("state/internal/errors/reactive", 10);
Track<uint32_t> errorsReactiveMaster("state/internal/errors/reactive/master",
                                     10);
Track<uint32_t> errorsReactiveMasterACK(
    "state/internal/errors/reactive/masterACK", 10);
Track<uint32_t> errorsReactiveSlave("state/internal/errors/reactive/slave", 10);
Track<uint32_t> errorsReactiveSlaveACK(
    "state/internal/errors/reactive/slaveACK", 10);

Track<uint32_t> errorsActive("state/internal/errors/active", 10);
Track<uint32_t> errorsActiveMaster("state/internal/errors/active/master", 10);
Track<uint32_t> errorsActiveMasterACK("state/internal/errors/active/masterACK",
                                      10);
Track<uint32_t> errorsActiveSlave("state/internal/errors/active/slave", 10);
Track<uint32_t> errorsActiveSlaveACK("state/internal/errors/active/slaveACK",
                                     10);

// ebus/<unique_id>/state/internal/resets
Track<uint32_t> resetsTotal("state/internal/resets", 10);
Track<uint32_t> resetsPassive00("state/internal/resets/passive00", 10);
Track<uint32_t> resetsPassive0704("state/internal/resets/passive0704", 10);
Track<uint32_t> resetsPassive("state/internal/resets/passive", 10);
Track<uint32_t> resetsActive("state/internal/resets/active", 10);

// ebus/<unique_id>/state/internal/requests
Track<uint32_t> requestsTotal("state/internal/requests", 10);
Track<uint32_t> requestsWon("state/internal/requests/won", 10);
Track<uint32_t> requestsLost("state/internal/requests/lost", 10);
Track<uint32_t> requestsRetry("state/internal/requests/retry", 10);
Track<uint32_t> requestsError("state/internal/requests/error", 10);

Schedule schedule;

Schedule::Schedule() : ebusHandler(0xff) {
  ebusHandler.onWrite(onWriteCallback);
  ebusHandler.isDataAvailable(isDataAvailableCallback);
  ebusHandler.onTelegram(onTelegramCallback);
  ebusHandler.onError(onErrorCallback);
}

void Schedule::setAddress(const uint8_t source) {
  ebusHandler.setAddress(source);
}

void Schedule::setDistance(const uint8_t distance) {
  distanceCommands = distance * 1000;
}

void Schedule::handleSend(const JsonArray &commands) {
  sendCommands.clear();
  for (JsonVariant command : commands)
    sendCommands.push_back(ebus::Sequence::to_vector(command));
}

void Schedule::toggleForward(const bool enable) { forward = enable; }

void Schedule::handleForwadFilter(const JsonArray &filters) {
  forwardfilters.clear();
  for (JsonVariant filter : filters)
    forwardfilters.push_back(ebus::Sequence::to_vector(filter));
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

  messagesPassiveMasterSlave = counters.messagesPassiveMasterSlave;
  messagesPassiveMasterMaster = counters.messagesPassiveMasterMaster;

  messagesReactiveMasterSlave = counters.messagesReactiveMasterSlave;
  messagesReactiveMasterMaster = counters.messagesReactiveMasterMaster;
  messagesReactiveBroadcast = counters.messagesReactiveBroadcast;

  messagesActiveMasterSlave = counters.messagesActiveMasterSlave;
  messagesActiveMasterMaster = counters.messagesActiveMasterMaster;
  messagesActiveBroadcast = counters.messagesActiveBroadcast;

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
  resetsPassive0704 = counters.resetsPassive0704;
  resetsPassive = counters.resetsPassive;
  resetsActive = counters.resetsActive;

  // requests
  requestsTotal = counters.requestsTotal;
  requestsWon = counters.requestsWon;
  requestsLost = counters.requestsLost;
  requestsRetry = counters.requestsRetry;
  requestsError = counters.requestsError;
}

void Schedule::onWriteCallback(const uint8_t byte) { Bus.write(byte); }

int Schedule::isDataAvailableCallback() { return Bus.available(); }

void Schedule::onTelegramCallback(const ebus::Message &message,
                                  const ebus::Type &type,
                                  const std::vector<uint8_t> &master,
                                  std::vector<uint8_t> *const slave) {
  switch (message) {
    case ebus::Message::active:
      schedule.processActive(std::vector<uint8_t>(master),
                             std::vector<uint8_t>(*slave));
      break;
    case ebus::Message::passive:
      schedule.processPassive(std::vector<uint8_t>(master),
                              std::vector<uint8_t>(*slave));
      break;
    case ebus::Message::reactive:

      switch (type) {
        case ebus::Type::broadcast:
          schedule.processPassive(std::vector<uint8_t>(master),
                                  std::vector<uint8_t>());
          break;
        case ebus::Type::masterMaster:
          schedule.processPassive(std::vector<uint8_t>(master),
                                  std::vector<uint8_t>());
          break;
        case ebus::Type::masterSlave:
          // TODO(yuhu-): fill with thing data
          // handle Identification (Service 07h 04h)
          // hh...Manufacturer (BYTE)
          // gg...Unit_ID_0-5 (ASCII)
          // ss...Software version (BCD)
          // rr...Revision (BCD)
          // vv...Hardware version (BCD)
          // hh...Revision (BCD)
          // std::vector<uint8_t> search = {0x07, 0x04};
          // if (ebus::Sequence::contains(master, search))
          //   *slave = ebus::Sequence::to_vector("0ahhggggggggggssrrhhrr");

          schedule.processPassive(std::vector<uint8_t>(master),
                                  std::vector<uint8_t>(*slave));
          break;
        default:
          break;
      }
  }
}

void Schedule::onErrorCallback(const std::string &str) {
  std::string topic = "state/internal/resets/last";
  std::string payload = str;
  mqtt.publish(topic.c_str(), 0, false, payload.c_str());
}

void Schedule::processActive(const std::vector<uint8_t> &master,
                             const std::vector<uint8_t> &slave) {
  if (send) {
    mqtt.publishData("send", master, slave);
  } else {
    store.updateData(scheduleCommand, master, slave);
    mqtt.publishValue(scheduleCommand, store.getValueJson(scheduleCommand));
  }
}

void Schedule::processPassive(const std::vector<uint8_t> &master,
                              const std::vector<uint8_t> &slave) {
  if (forward) {
    size_t count = std::count_if(forwardfilters.begin(), forwardfilters.end(),
                                 [&master](const std::vector<uint8_t> &vec) {
                                   return ebus::Sequence::contains(master, vec);
                                 });
    if (count > 0 || forwardfilters.size() == 0)
      mqtt.publishData("forward", master, slave);
  }

  std::vector<Command *> pasCommands = store.updateData(nullptr, master, slave);

  for (Command *command : pasCommands) {
    mqtt.publishValue(command, store.getValueJson(command));
  }
}
