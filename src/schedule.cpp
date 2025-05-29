#include "schedule.hpp"

#include <set>

#include "bus.hpp"
#include "mqtt.hpp"
#include "track.hpp"

constexpr uint8_t DEFAULT_EBUS_HANDLER_ADDRESS = 0xff;

constexpr uint8_t SCAN_VENDOR_VAILLANT = 0xb5;

const std::vector<uint8_t> SCAN_070400 = {0x07, 0x04, 0x00};
const std::vector<uint8_t> SCAN_b50901 = {0xb5, 0x09, 0x01};
const std::vector<uint8_t> SCAN_b5090124 = {0xb5, 0x09, 0x01, 0x24};
const std::vector<uint8_t> SCAN_b5090125 = {0xb5, 0x09, 0x01, 0x25};
const std::vector<uint8_t> SCAN_b5090126 = {0xb5, 0x09, 0x01, 0x26};
const std::vector<uint8_t> SCAN_b5090127 = {0xb5, 0x09, 0x01, 0x27};

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

// ebus/<unique_id>/state/internal/addresses
std::map<uint8_t, uint32_t> seenMasters;
std::map<uint8_t, uint32_t> seenSlaves;

Schedule schedule;

Schedule::Schedule() : ebusHandler(DEFAULT_EBUS_HANDLER_ADDRESS) {
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

void Schedule::handleScanFull() {
  scanCommands.clear();
  fullScan = true;
  nextScanCommand();
}

void Schedule::handleScanSeen() {
  scanCommands.clear();
  std::set<uint8_t> slaves;

  for (const std::pair<uint8_t, uint32_t> master : seenMasters)
    if (master.first != ebusHandler.getAddress())
      slaves.insert(ebus::slaveOf(master.first));

  for (const std::pair<uint8_t, uint32_t> slave : seenSlaves)
    if (slave.first != ebusHandler.getSlaveAddress())
      slaves.insert(slave.first);

  for (const uint8_t slave : slaves) {
    std::vector<uint8_t> command;
    command = {slave};
    command.insert(command.end(), SCAN_070400.begin(), SCAN_070400.end());
    scanCommands.push_back(command);
  }
}

void Schedule::handleScanAddresses(const JsonArray &addresses) {
  scanCommands.clear();
  std::set<uint8_t> slaves;

  for (JsonVariant address : addresses) {
    uint8_t firstByte = ebus::to_vector(address.as<std::string>())[0];
    if (ebus::isSlave(firstByte) && firstByte != ebusHandler.getSlaveAddress())
      slaves.insert(firstByte);
  }

  for (const uint8_t slave : slaves) {
    std::vector<uint8_t> command;
    command = {slave};
    command.insert(command.end(), SCAN_070400.begin(), SCAN_070400.end());
    scanCommands.push_back(command);
  }
}

void Schedule::handleSend(const JsonArray &commands) {
  for (JsonVariant command : commands)
    sendCommands.push_back(ebus::to_vector(command));
}

void Schedule::toggleForward(const bool enable) { forward = enable; }

void Schedule::handleForwadFilter(const JsonArray &filters) {
  forwardfilters.clear();
  for (JsonVariant filter : filters)
    forwardfilters.push_back(ebus::to_vector(filter));
}

void Schedule::nextCommand() {
  if (scanCommands.size() > 0 || sendCommands.size() > 0 || store.active()) {
    if (!ebusHandler.isActive()) {
      uint32_t currentMillis = millis();
      if (currentMillis > lastCommand + distanceCommands) {
        lastCommand = currentMillis;

        std::vector<uint8_t> command;
        if (scanCommands.size() > 0) {
          mode = Mode::scan;
          command = scanCommands.front();
          scanCommands.pop_front();
          if (fullScan && scanCommands.size() == 0) nextScanCommand();
        } else if (sendCommands.size() > 0) {
          mode = Mode::send;
          command = sendCommands.front();
          sendCommands.pop_front();
        } else {
          mode = Mode::normal;
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

  // master addresses
  for (std::pair<const uint8_t, uint32_t> &master : seenMasters) {
    std::string topic =
        "state/internal/addresses/master/" + ebus::to_string(master.first);
    mqtt.publish(topic.c_str(), 0, false, String(master.second).c_str());
  }

  // slave addresses
  for (std::pair<const uint8_t, uint32_t> &slave : seenSlaves) {
    std::string topic =
        "state/internal/addresses/slave/" + ebus::to_string(slave.first);
    mqtt.publish(topic.c_str(), 0, false, String(slave.second).c_str());
  }
}

JsonDocument Schedule::getParticipantJson(const Participant *participant) {
  JsonDocument doc;

  doc["address"] = ebus::to_string(participant->slave);
  doc["manufacturer"] = ebus::to_string(ebus::range(participant->scan070400, 1, 1));
  doc["unitid"] = ebus::byte_2_string(ebus::range(participant->scan070400, 2, 5));
  doc["software"] = ebus::to_string(ebus::range(participant->scan070400, 7, 2));
  doc["hardware"] = ebus::to_string(ebus::range(participant->scan070400, 9, 2));

  if (participant->scanb5090124.size() > 0 &&
      participant->scanb5090125.size() > 0 &&
      participant->scanb5090126.size() > 0 &&
      participant->scanb5090127.size() > 0) {
    std::string serial =
        ebus::byte_2_string(ebus::range(participant->scanb5090124, 2, 8));
    serial += ebus::byte_2_string(ebus::range(participant->scanb5090125, 1, 9));
    serial += ebus::byte_2_string(ebus::range(participant->scanb5090126, 1, 9));
    serial += ebus::byte_2_string(ebus::range(participant->scanb5090127, 1, 2));

    doc["serial"] = serial;
    doc["article"] = serial.substr(6, 10);
  }

  doc.shrinkToFit();
  return doc;
}

const std::string Schedule::getParticipantsJson() const {
  std::string payload;
  JsonDocument doc;

  if (allParticipants.size() > 0) {
    for (const std::pair<uint8_t, Participant> &participant : allParticipants)
      doc.add(getParticipantJson(&participant.second));
  }

  if (doc.isNull()) doc.to<JsonArray>();

  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload;
}

const std::vector<Participant *> Schedule::getParticipants() {
  std::vector<Participant *> participants;
  for (std::pair<const uint8_t, Participant> &participant : allParticipants)
    participants.push_back(&(participant.second));
  return participants;
}

void Schedule::onWriteCallback(const uint8_t byte) { Bus.write(byte); }

int Schedule::isDataAvailableCallback() { return Bus.available(); }

void Schedule::onTelegramCallback(const ebus::MessageType &messageType,
                                  const ebus::TelegramType &telegramType,
                                  const std::vector<uint8_t> &master,
                                  std::vector<uint8_t> *const slave) {
  // count master and slave addresses
  seenMasters[master[0]] += 1;
  if (ebus::isSlave(master[1])) seenSlaves[master[1]] += 1;

  switch (messageType) {
    case ebus::MessageType::active:
      schedule.processActive(std::vector<uint8_t>(master),
                             std::vector<uint8_t>(*slave));
      break;
    case ebus::MessageType::passive:
      schedule.processPassive(std::vector<uint8_t>(master),
                              std::vector<uint8_t>(*slave));
      break;
    case ebus::MessageType::reactive:
      switch (telegramType) {
        case ebus::TelegramType::broadcast:
          schedule.processPassive(std::vector<uint8_t>(master),
                                  std::vector<uint8_t>());
          break;
        case ebus::TelegramType::master_master:
          schedule.processPassive(std::vector<uint8_t>(master),
                                  std::vector<uint8_t>());
          break;
        case ebus::TelegramType::master_slave:
          // TODO(yuhu-): Implement handling of Identification (Service 07h 04h)
          // Expected data format:
          // hh...Manufacturer (BYTE)
          // gg...Unit_ID_0-5 (ASCII)
          // ss...Software version (BCD)
          // rr...Revision (BCD)
          // vv...Hardware version (BCD)
          // hh...Revision (BCD)
          // Example:
          // const std::vector<uint8_t> SEARCH_0704 = {0x07, 0x04};
          // if (ebus::Sequence::contains(master, SEARCH_0704))
          //   *slave = ebus::Sequence::to_vector("0ahhggggggggggssrrhhrr");

          schedule.processPassive(std::vector<uint8_t>(master),
                                  std::vector<uint8_t>(*slave));
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}

void Schedule::onErrorCallback(const std::string &str) {
  std::string topic = "state/internal/resets/last";
  std::string payload = str;
  mqtt.publish(topic.c_str(), 0, false, payload.c_str());
}

void Schedule::processActive(const std::vector<uint8_t> &master,
                             const std::vector<uint8_t> &slave) {
  if (mode == Mode::scan) {
    processScan(master, slave);
  } else if (mode == Mode::send) {
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
                                   return ebus::contains(master, vec);
                                 });
    if (count > 0 || forwardfilters.size() == 0)
      mqtt.publishData("forward", master, slave);
  }

  std::vector<Command *> pasCommands = store.updateData(nullptr, master, slave);

  for (Command *command : pasCommands)
    mqtt.publishValue(command, store.getValueJson(command));
}

void Schedule::processScan(const std::vector<uint8_t> &master,
                           const std::vector<uint8_t> &slave) {
  if (ebus::contains(master, SCAN_070400)) {
    allParticipants[master[1]].slave = master[1];
    allParticipants[master[1]].scan070400 = slave;

    if (!fullScan && slave[1] == SCAN_VENDOR_VAILLANT) {
      for (uint8_t pos = 0x27; pos >= 0x24; pos--) {
        std::vector<uint8_t> command;
        command = {master[1]};
        command.insert(command.end(), SCAN_b50901.begin(), SCAN_b50901.end());
        command.push_back(pos);
        scanCommands.push_front(command);
      }
    }
  }

  if (ebus::contains(master, SCAN_b5090124))
    allParticipants[master[1]].scanb5090124 = slave;
  if (ebus::contains(master, SCAN_b5090125))
    allParticipants[master[1]].scanb5090125 = slave;
  if (ebus::contains(master, SCAN_b5090126))
    allParticipants[master[1]].scanb5090126 = slave;
  if (ebus::contains(master, SCAN_b5090127))
    allParticipants[master[1]].scanb5090127 = slave;
}

void Schedule::nextScanCommand() {
  while (scanIndex <= 0xff) {
    scanIndex++;
    if (scanIndex == 0xff) {
      fullScan = false;
      scanIndex = 0;
      break;
    }
    if (ebus::isSlave(scanIndex) &&
        scanIndex != ebusHandler.getSlaveAddress()) {
      std::vector<uint8_t> command;
      command = {scanIndex};
      command.insert(command.end(), SCAN_070400.begin(), SCAN_070400.end());
      scanCommands.push_back(command);
      break;
    }
  }
}