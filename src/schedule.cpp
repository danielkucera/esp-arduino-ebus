#if defined(EBUS_INTERNAL)
#include "schedule.hpp"

#include <set>

#include "bus.hpp"
#include "mqtt.hpp"
#include "track.hpp"

constexpr uint8_t SCAN_VENDOR_VAILLANT = 0xb5;

const std::vector<uint8_t> SCAN_070400 = {0x07, 0x04, 0x00};
const std::vector<uint8_t> SCAN_b50901 = {0xb5, 0x09, 0x01};
const std::vector<uint8_t> SCAN_b5090124 = {0xb5, 0x09, 0x01, 0x24};
const std::vector<uint8_t> SCAN_b5090125 = {0xb5, 0x09, 0x01, 0x25};
const std::vector<uint8_t> SCAN_b5090126 = {0xb5, 0x09, 0x01, 0x26};
const std::vector<uint8_t> SCAN_b5090127 = {0xb5, 0x09, 0x01, 0x27};

volatile bool runnerShouldStop = false;

// ebus/<unique_id>/state/addresses
std::map<uint8_t, uint32_t> seenMasters;
std::map<uint8_t, uint32_t> seenSlaves;

#define TRACK_U32(NAME, PATH) Track<uint32_t> NAME("state/" PATH, 10);

#define ASSIGN_COUNTER(NAME) NAME = counters.NAME;

// Messages
TRACK_U32(messagesTotal, "messages")
TRACK_U32(messagesPassiveMasterSlave, "messages/passiveMasterSlave")
TRACK_U32(messagesPassiveMasterMaster, "messages/passiveMasterMaster")
TRACK_U32(messagesPassiveBroadcast, "messages/passiveBroadcast")
TRACK_U32(messagesReactiveMasterSlave, "messages/reactiveMasterSlave")
TRACK_U32(messagesReactiveMasterMaster, "messages/reactiveMasterMaster")
TRACK_U32(messagesActiveMasterSlave, "messages/activeMasterSlave")
TRACK_U32(messagesActiveMasterMaster, "messages/activeMasterMaster")
TRACK_U32(messagesActiveBroadcast, "messages/activeBroadcast")

// Requests
TRACK_U32(requestsTotal, "requests")
TRACK_U32(requestsWon1, "requests/won1")
TRACK_U32(requestsWon2, "requests/won2")
TRACK_U32(requestsLost1, "requests/lost1")
TRACK_U32(requestsLost2, "requests/lost2")
TRACK_U32(requestsError1, "requests/error1")
TRACK_U32(requestsError2, "requests/error2")
TRACK_U32(requestsErrorRetry, "requests/errorRetry")

// Resets
TRACK_U32(resetsTotal, "resets")
TRACK_U32(resetsPassive00, "resets/passive00")
TRACK_U32(resetsPassive0704, "resets/passive0704")
TRACK_U32(resetsPassive, "resets/passive")
TRACK_U32(resetsActive, "resets/active")

// Errors
TRACK_U32(errorsTotal, "errors")
TRACK_U32(errorsPassive, "errors/passive")
TRACK_U32(errorsPassiveMaster, "errors/passive/master")
TRACK_U32(errorsPassiveMasterACK, "errors/passive/masterACK")
TRACK_U32(errorsPassiveSlave, "errors/passive/slave")
TRACK_U32(errorsPassiveSlaveACK, "errors/passive/slaveACK")
TRACK_U32(errorsReactive, "errors/reactive")
TRACK_U32(errorsReactiveMaster, "errors/reactive/master")
TRACK_U32(errorsReactiveMasterACK, "errors/reactive/masterACK")
TRACK_U32(errorsReactiveSlave, "errors/reactive/slave")
TRACK_U32(errorsReactiveSlaveACK, "errors/reactive/slaveACK")
TRACK_U32(errorsActive, "errors/active")
TRACK_U32(errorsActiveMaster, "errors/active/master")
TRACK_U32(errorsActiveMasterACK, "errors/active/masterACK")
TRACK_U32(errorsActiveSlave, "errors/active/slave")
TRACK_U32(errorsActiveSlaveACK, "errors/active/slaveACK")

#define TRACK_TIMING(NAME, PATH)                                    \
  Track<int64_t> NAME##Last("state/timings/" PATH "/last", 10);     \
  Track<int64_t> NAME##Mean("state/timings/" PATH "/mean", 10);     \
  Track<int64_t> NAME##StdDev("state/timings/" PATH "/stddev", 10); \
  Track<uint64_t> NAME##Count("state/timings/" PATH "/count", 10);

#define ASSIGN_TIMING(NAME)            \
  NAME##Last = timings.NAME##Last;     \
  NAME##Mean = timings.NAME##Mean;     \
  NAME##StdDev = timings.NAME##StdDev; \
  NAME##Count = timings.NAME##Count;

#define ASSIGN_STATE_TIMING(NAME, STATE_ENUM)                     \
  NAME##Last = stats.states[ebus::FsmState::STATE_ENUM].last;     \
  NAME##Mean = stats.states[ebus::FsmState::STATE_ENUM].mean;     \
  NAME##StdDev = stats.states[ebus::FsmState::STATE_ENUM].stddev; \
  NAME##Count = stats.states[ebus::FsmState::STATE_ENUM].count;

// General timings
TRACK_TIMING(sync, "sync")
TRACK_TIMING(write, "write");
TRACK_TIMING(passiveFirst, "passive/first")
TRACK_TIMING(passiveData, "passive/data")
TRACK_TIMING(activeFirst, "active/first")
TRACK_TIMING(activeData, "active/data")
TRACK_TIMING(callbackReactive, "callback/reactive");
TRACK_TIMING(callbackTelegram, "callback/telegram");
TRACK_TIMING(callbackError, "callback/error");

// FSM state timings
TRACK_TIMING(passiveReceiveMaster, "fsmstate/passiveReceiveMaster")
TRACK_TIMING(passiveReceiveMasterAcknowledge,
             "fsmstate/passiveReceiveMasterAcknowledge")
TRACK_TIMING(passiveReceiveSlave, "fsmstate/passiveReceiveSlave")
TRACK_TIMING(passiveReceiveSlaveAcknowledge,
             "fsmstate/passiveReceiveSlaveAcknowledge")
TRACK_TIMING(reactiveSendMasterPositiveAcknowledge,
             "fsmstate/reactiveSendMasterPositiveAcknowledge")
TRACK_TIMING(reactiveSendMasterNegativeAcknowledge,
             "fsmstate/reactiveSendMasterNegativeAcknowledge")
TRACK_TIMING(reactiveSendSlave, "fsmstate/reactiveSendSlave")
TRACK_TIMING(reactiveReceiveSlaveAcknowledge,
             "fsmstate/reactiveReceiveSlaveAcknowledge")
TRACK_TIMING(requestBus, "fsmstate/requestBus")
TRACK_TIMING(activeSendMaster, "fsmstate/activeSendMaster")
TRACK_TIMING(activeReceiveMasterAcknowledge,
             "fsmstate/activeReceiveMasterAcknowledge")
TRACK_TIMING(activeReceiveSlave, "fsmstate/activeReceiveSlave")
TRACK_TIMING(activeSendSlavePositiveAcknowledge,
             "fsmstate/activeSendSlavePositiveAcknowledge")
TRACK_TIMING(activeSendSlaveNegativeAcknowledge,
             "fsmstate/activeSendSlaveNegativeAcknowledge")
TRACK_TIMING(releaseBus, "fsmstate/releaseBus");

Schedule schedule;

void Schedule::setHandler(ebus::Handler *handler) {
  ebusHandler = handler;
  if (ebusHandler) {
    ebusHandler->setReactiveMasterSlaveCallback(reactiveMasterSlaveCallback);

    ebusHandler->setTelegramCallback(
        [this](const ebus::MessageType &messageType,
               const ebus::TelegramType &telegramType,
               const std::vector<uint8_t> &master,
               const std::vector<uint8_t> &slave) {
          auto *event = new CallbackEvent();
          event->type = CallbackType::telegram;
          event->mode = mode;
          event->data.messageType = messageType;
          event->data.telegramType = telegramType;
          event->data.master = master;
          event->data.slave = slave;
          eventQueue.try_push(event);
        });

    ebusHandler->setErrorCallback([this](const std::string &error,
                                         const std::vector<uint8_t> &master,
                                         const std::vector<uint8_t> &slave) {
      auto *event = new CallbackEvent();
      event->type = CallbackType::error;
      event->data.error = error;
      event->data.master = master;
      event->data.slave = slave;
      eventQueue.try_push(event);
    });

    // Start the scheduleRunner task
    xTaskCreate(
        [](void *arg) {
          auto *sched = static_cast<Schedule *>(arg);
          for (;;) {
            if (runnerShouldStop) vTaskDelete(NULL);
            sched->handleEvents();
            sched->nextCommand();
            vTaskDelay(pdMS_TO_TICKS(10));  // adjust delay as needed
          }
        },
        "scheduleRunner", 4096, this, 2, &scheduleTask);
  }
}

void Schedule::setDistance(const uint8_t distance) {
  distanceCommands = distance * 1000;
}

void Schedule::stopRunner() { runnerShouldStop = true; }

void Schedule::handleScanFull() {
  scanCommands.clear();
  fullScan = true;
  nextScanCommand();
}

void Schedule::handleScan() {
  scanCommands.clear();
  std::set<uint8_t> slaves;

  for (const std::pair<uint8_t, uint32_t> master : seenMasters)
    if (master.first != ebusHandler->getAddress())
      slaves.insert(ebus::slaveOf(master.first));

  for (const std::pair<uint8_t, uint32_t> slave : seenSlaves)
    if (slave.first != ebusHandler->getSlaveAddress())
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
    if (ebus::isSlave(firstByte) && firstByte != ebusHandler->getSlaveAddress())
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

void Schedule::setPublishCounters(const bool enable) {
  publishCounters = enable;
}

void Schedule::resetCounters() {
  seenMasters.clear();
  seenSlaves.clear();

  if (ebusHandler) ebusHandler->resetCounters();
}

void Schedule::fetchCounters() {
  if (!publishCounters) return;

  // Addresses Master
  for (std::pair<const uint8_t, uint32_t> &master : seenMasters) {
    std::string topic =
        "state/addresses/master/" + ebus::to_string(master.first);
    mqtt.publish(topic.c_str(), 0, false, String(master.second).c_str());
  }

  // Addresses Slave
  for (std::pair<const uint8_t, uint32_t> &slave : seenSlaves) {
    std::string topic = "state/addresses/slave/" + ebus::to_string(slave.first);
    mqtt.publish(topic.c_str(), 0, false, String(slave.second).c_str());
  }

  // Counters
  ebus::Counters counters = ebusHandler->getCounters();

  // Messages
  ASSIGN_COUNTER(messagesTotal)
  ASSIGN_COUNTER(messagesPassiveMasterSlave)
  ASSIGN_COUNTER(messagesPassiveMasterMaster)
  ASSIGN_COUNTER(messagesPassiveBroadcast)
  ASSIGN_COUNTER(messagesReactiveMasterSlave)
  ASSIGN_COUNTER(messagesReactiveMasterMaster)
  ASSIGN_COUNTER(messagesActiveMasterSlave)
  ASSIGN_COUNTER(messagesActiveMasterMaster)
  ASSIGN_COUNTER(messagesActiveBroadcast)

  // Requests
  ASSIGN_COUNTER(requestsTotal)
  ASSIGN_COUNTER(requestsWon1)
  ASSIGN_COUNTER(requestsWon2)
  ASSIGN_COUNTER(requestsLost1)
  ASSIGN_COUNTER(requestsLost2)
  ASSIGN_COUNTER(requestsError1)
  ASSIGN_COUNTER(requestsError2)
  ASSIGN_COUNTER(requestsErrorRetry)

  // Resets
  ASSIGN_COUNTER(resetsTotal)
  ASSIGN_COUNTER(resetsPassive00)
  ASSIGN_COUNTER(resetsPassive0704)
  ASSIGN_COUNTER(resetsPassive)
  ASSIGN_COUNTER(resetsActive)

  // Errors
  ASSIGN_COUNTER(errorsTotal)
  ASSIGN_COUNTER(errorsPassive)
  ASSIGN_COUNTER(errorsPassiveMaster)
  ASSIGN_COUNTER(errorsPassiveMasterACK)
  ASSIGN_COUNTER(errorsPassiveSlave)
  ASSIGN_COUNTER(errorsPassiveSlaveACK)
  ASSIGN_COUNTER(errorsReactive)
  ASSIGN_COUNTER(errorsReactiveMaster)
  ASSIGN_COUNTER(errorsReactiveMasterACK)
  ASSIGN_COUNTER(errorsReactiveSlave)
  ASSIGN_COUNTER(errorsReactiveSlaveACK)
  ASSIGN_COUNTER(errorsActive)
  ASSIGN_COUNTER(errorsActiveMaster)
  ASSIGN_COUNTER(errorsActiveMasterACK)
  ASSIGN_COUNTER(errorsActiveSlave)
  ASSIGN_COUNTER(errorsActiveSlaveACK)
}

const std::string Schedule::getCountersJson() {
  std::string payload;
  JsonDocument doc;

  // Addresses Master
  JsonObject Addresses_Master = doc["Addresses"]["Master"].to<JsonObject>();

  for (const std::pair<uint8_t, uint32_t> master : seenMasters)
    Addresses_Master[ebus::to_string(master.first)] = master.second;

  // Addresses Slave
  JsonObject Addresses_Slave = doc["Addresses"]["Slave"].to<JsonObject>();

  for (const std::pair<uint8_t, uint32_t> slave : seenSlaves)
    Addresses_Slave[ebus::to_string(slave.first)] = slave.second;

  // Counters
  ebus::Counters counters = ebusHandler->getCounters();

  // Messages
  JsonObject Messages = doc["Messages"].to<JsonObject>();
  Messages["Total"] = counters.messagesTotal;
  Messages["Passive_Master_Slave"] = counters.messagesPassiveMasterSlave;
  Messages["Passive_Master_Master"] = counters.messagesPassiveMasterMaster;
  Messages["Passive_Broadcast"] = counters.messagesPassiveBroadcast;
  Messages["Reactive_Master_Slave"] = counters.messagesReactiveMasterSlave;
  Messages["Reactive_Master_Master"] = counters.messagesReactiveMasterMaster;
  Messages["Active_Master_Slave"] = counters.messagesActiveMasterSlave;
  Messages["Active_Master_Master"] = counters.messagesActiveMasterMaster;
  Messages["Active_Broadcast"] = counters.messagesActiveBroadcast;

  // Requests
  JsonObject Requests = doc["Requests"].to<JsonObject>();
  Requests["Total"] = counters.requestsTotal;
  Requests["Won1"] = counters.requestsWon1;
  Requests["Won2"] = counters.requestsWon2;
  Requests["Lost1"] = counters.requestsLost1;
  Requests["Lost2"] = counters.requestsLost2;
  Requests["Error1"] = counters.requestsError1;
  Requests["Error2"] = counters.requestsError2;
  Requests["ErrorRetry"] = counters.requestsErrorRetry;

  // Resets
  JsonObject Resets = doc["Resets"].to<JsonObject>();
  Resets["Total"] = counters.resetsTotal;
  Resets["Passive_00"] = counters.resetsPassive00;
  Resets["Passive_0704"] = counters.resetsPassive0704;
  Resets["Passive"] = counters.resetsPassive;
  Resets["Active"] = counters.resetsActive;

  // Errors
  JsonObject Errors = doc["Errors"].to<JsonObject>();
  Errors["Total"] = counters.errorsTotal;

  // Errors Passive
  JsonObject Errors_Passive = doc["Errors"]["Passive"].to<JsonObject>();
  Errors_Passive["Total"] = counters.errorsPassive;
  Errors_Passive["Master"] = counters.errorsPassiveMaster;
  Errors_Passive["Master_ACK"] = counters.errorsPassiveMasterACK;
  Errors_Passive["Slave"] = counters.errorsPassiveSlave;
  Errors_Passive["Slave_ACK"] = counters.errorsPassiveSlaveACK;

  // Erros Reactive
  JsonObject Errors_Reactive = doc["Errors"]["Reactive"].to<JsonObject>();
  Errors_Reactive["Total"] = counters.errorsReactive;
  Errors_Reactive["Master"] = counters.errorsReactiveMaster;
  Errors_Reactive["Master_ACK"] = counters.errorsReactiveMasterACK;
  Errors_Reactive["Slave"] = counters.errorsReactiveSlave;
  Errors_Reactive["Slave_ACK"] = counters.errorsReactiveSlaveACK;

  // Erros Active
  JsonObject Errors_Active = doc["Errors"]["Active"].to<JsonObject>();
  Errors_Active["Total"] = counters.errorsActive;
  Errors_Active["Master"] = counters.errorsActiveMaster;
  Errors_Active["Master_ACK"] = counters.errorsActiveMasterACK;
  Errors_Active["Slave"] = counters.errorsActiveSlave;
  Errors_Active["Slave_ACK"] = counters.errorsActiveSlaveACK;

  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload;
}

void Schedule::setPublishTimings(const bool enable) { publishTimings = enable; }

void Schedule::resetTimings() {
  if (ebusHandler) ebusHandler->resetTimings();
}

void Schedule::fetchTimings() {
  if (!publishTimings) return;

  // Timings
  ebus::Timings timings = ebusHandler->getTimings();

  ASSIGN_TIMING(sync)
  ASSIGN_TIMING(write)
  ASSIGN_TIMING(passiveFirst)
  ASSIGN_TIMING(passiveData)
  ASSIGN_TIMING(activeFirst)
  ASSIGN_TIMING(activeData)
  ASSIGN_TIMING(callbackReactive)
  ASSIGN_TIMING(callbackTelegram)
  ASSIGN_TIMING(callbackError)

  ebus::StateTimingStatsResults stats =
      ebusHandler->getStateTimingStatsResults();

  ASSIGN_STATE_TIMING(passiveReceiveMaster, passiveReceiveMaster)
  ASSIGN_STATE_TIMING(passiveReceiveMasterAcknowledge,
                      passiveReceiveMasterAcknowledge)
  ASSIGN_STATE_TIMING(passiveReceiveSlave, passiveReceiveSlave)
  ASSIGN_STATE_TIMING(passiveReceiveSlaveAcknowledge,
                      passiveReceiveSlaveAcknowledge)
  ASSIGN_STATE_TIMING(reactiveSendMasterPositiveAcknowledge,
                      reactiveSendMasterPositiveAcknowledge)
  ASSIGN_STATE_TIMING(reactiveSendMasterNegativeAcknowledge,
                      reactiveSendMasterNegativeAcknowledge)
  ASSIGN_STATE_TIMING(reactiveSendSlave, reactiveSendSlave)
  ASSIGN_STATE_TIMING(reactiveReceiveSlaveAcknowledge,
                      reactiveReceiveSlaveAcknowledge)
  ASSIGN_STATE_TIMING(requestBus, requestBus)
  ASSIGN_STATE_TIMING(activeSendMaster, activeSendMaster)
  ASSIGN_STATE_TIMING(activeReceiveMasterAcknowledge,
                      activeReceiveMasterAcknowledge)
  ASSIGN_STATE_TIMING(activeReceiveSlave, activeReceiveSlave)
  ASSIGN_STATE_TIMING(activeSendSlavePositiveAcknowledge,
                      activeSendSlavePositiveAcknowledge)
  ASSIGN_STATE_TIMING(activeSendSlaveNegativeAcknowledge,
                      activeSendSlaveNegativeAcknowledge)
  ASSIGN_STATE_TIMING(releaseBus, releaseBus)
}

const std::string Schedule::getTimingsJson() {
  std::string payload;
  JsonDocument doc;

  ebus::Timings timings = ebusHandler->getTimings();

  // Helper lambda to add timing stats to a JsonObject
  auto addTiming = [](JsonObject obj, int64_t last, int64_t mean,
                      int64_t stddev, uint64_t count) {
    obj["Last"] = last;
    obj["Mean"] = mean;
    obj["StdDev"] = stddev;
    obj["Count"] = count;
  };

  addTiming(doc["Sync"].to<JsonObject>(), timings.syncLast, timings.syncMean,
            timings.syncStdDev, timings.syncCount);

  addTiming(doc["Write"].to<JsonObject>(), timings.writeLast, timings.writeMean,
            timings.writeStdDev, timings.writeCount);

  addTiming(doc["Passive"]["First"].to<JsonObject>(), timings.passiveFirstLast,
            timings.passiveFirstMean, timings.passiveFirstStdDev,
            timings.passiveFirstCount);
  addTiming(doc["Passive"]["Data"].to<JsonObject>(), timings.passiveDataLast,
            timings.passiveDataMean, timings.passiveDataStdDev,
            timings.passiveDataCount);

  addTiming(doc["Active"]["First"].to<JsonObject>(), timings.activeFirstLast,
            timings.activeFirstMean, timings.activeFirstStdDev,
            timings.activeFirstCount);
  addTiming(doc["Active"]["Data"].to<JsonObject>(), timings.activeDataLast,
            timings.activeDataMean, timings.activeDataStdDev,
            timings.activeDataCount);

  addTiming(doc["Callback"]["Reactive"].to<JsonObject>(),
            timings.callbackReactiveLast, timings.callbackReactiveMean,
            timings.callbackReactiveStdDev, timings.callbackReactiveCount);

  addTiming(doc["Callback"]["Telegram"].to<JsonObject>(),
            timings.callbackTelegramLast, timings.callbackTelegramMean,
            timings.callbackTelegramStdDev, timings.callbackTelegramCount);

  addTiming(doc["Callback"]["Error"].to<JsonObject>(),
            timings.callbackErrorLast, timings.callbackErrorMean,
            timings.callbackErrorStdDev, timings.callbackErrorCount);

  ebus::StateTimingStatsResults stats =
      ebusHandler->getStateTimingStatsResults();

  // Output FSM state timings
  auto addStateTiming =
      [](JsonObject obj,
         const ebus::StateTimingStatsResults::StateStats &state) {
        obj["Last"] = static_cast<int64_t>(state.last);
        obj["Mean"] = static_cast<int64_t>(state.mean);
        obj["StdDev"] = static_cast<int64_t>(state.stddev);
        obj["Count"] = state.count;
      };

  addStateTiming(doc["FsmState"]["passiveReceiveMaster"].to<JsonObject>(),
                 stats.states.at(ebus::FsmState::passiveReceiveMaster));
  addStateTiming(
      doc["FsmState"]["passiveReceiveMasterAcknowledge"].to<JsonObject>(),
      stats.states.at(ebus::FsmState::passiveReceiveMasterAcknowledge));
  addStateTiming(doc["FsmState"]["passiveReceiveSlave"].to<JsonObject>(),
                 stats.states.at(ebus::FsmState::passiveReceiveSlave));
  addStateTiming(
      doc["FsmState"]["passiveReceiveSlaveAcknowledge"].to<JsonObject>(),
      stats.states.at(ebus::FsmState::passiveReceiveSlaveAcknowledge));
  addStateTiming(
      doc["FsmState"]["reactiveSendMasterPositiveAcknowledge"].to<JsonObject>(),
      stats.states.at(ebus::FsmState::reactiveSendMasterPositiveAcknowledge));
  addStateTiming(
      doc["FsmState"]["reactiveSendMasterNegativeAcknowledge"].to<JsonObject>(),
      stats.states.at(ebus::FsmState::reactiveSendMasterNegativeAcknowledge));
  addStateTiming(doc["FsmState"]["reactiveSendSlave"].to<JsonObject>(),
                 stats.states.at(ebus::FsmState::reactiveSendSlave));
  addStateTiming(
      doc["FsmState"]["reactiveReceiveSlaveAcknowledge"].to<JsonObject>(),
      stats.states.at(ebus::FsmState::reactiveReceiveSlaveAcknowledge));
  addStateTiming(doc["FsmState"]["requestBus"].to<JsonObject>(),
                 stats.states.at(ebus::FsmState::requestBus));
  addStateTiming(doc["FsmState"]["activeSendMaster"].to<JsonObject>(),
                 stats.states.at(ebus::FsmState::activeSendMaster));
  addStateTiming(
      doc["FsmState"]["activeReceiveMasterAcknowledge"].to<JsonObject>(),
      stats.states.at(ebus::FsmState::activeReceiveMasterAcknowledge));
  addStateTiming(doc["FsmState"]["activeReceiveSlave"].to<JsonObject>(),
                 stats.states.at(ebus::FsmState::activeReceiveSlave));
  addStateTiming(
      doc["FsmState"]["activeSendSlavePositiveAcknowledge"].to<JsonObject>(),
      stats.states.at(ebus::FsmState::activeSendSlavePositiveAcknowledge));
  addStateTiming(
      doc["FsmState"]["activeSendSlaveNegativeAcknowledge"].to<JsonObject>(),
      stats.states.at(ebus::FsmState::activeSendSlaveNegativeAcknowledge));
  addStateTiming(doc["FsmState"]["releaseBus"].to<JsonObject>(),
                 stats.states.at(ebus::FsmState::releaseBus));

  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload;
}

JsonDocument Schedule::getParticipantJson(const Participant *participant) {
  JsonDocument doc;

  doc["address"] = ebus::to_string(participant->slave);
  doc["manufacturer"] =
      ebus::to_string(ebus::range(participant->scan070400, 1, 1));
  doc["unitid"] =
      ebus::byte_2_string(ebus::range(participant->scan070400, 2, 5));
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

void Schedule::nextCommand() {
  if (scanCommands.size() > 0 || sendCommands.size() > 0 || store.active()) {
    if (!ebusHandler->isActive()) {
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
          ebusHandler->enque(command);
        }
      }
    }
  }
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
        scanIndex != ebusHandler->getSlaveAddress()) {
      std::vector<uint8_t> command;
      command = {scanIndex};
      command.insert(command.end(), SCAN_070400.begin(), SCAN_070400.end());
      scanCommands.push_back(command);
      break;
    }
  }
}

void Schedule::handleEvents() {
  CallbackEvent *event = nullptr;
  while (eventQueue.try_pop(event)) {
    if (event) {
      switch (event->type) {
        case CallbackType::error:
          if (schedule.publishCounters) {
            std::string topic = "state/resets/last";
            std::string payload = event->data.error + " : master '" +
                                  ebus::to_string(event->data.master) +
                                  "' slave '" +
                                  ebus::to_string(event->data.slave) + "'";

            mqtt.publish(topic.c_str(), 0, false, payload.c_str());
          }
          break;
        case CallbackType::telegram:
          if (!event->data.master.empty()) {
            seenMasters[event->data.master[0]] += 1;
            if (event->data.master.size() > 1 &&
                ebus::isSlave(event->data.master[1]))
              seenSlaves[event->data.master[1]] += 1;
          }

          switch (event->data.messageType) {
            case ebus::MessageType::active:
              schedule.processActive(event->mode,
                                     std::vector<uint8_t>(event->data.master),
                                     std::vector<uint8_t>(event->data.slave));
              break;
            case ebus::MessageType::passive:
            case ebus::MessageType::reactive:
              schedule.processPassive(std::vector<uint8_t>(event->data.master),
                                      std::vector<uint8_t>(event->data.slave));
              break;
          }
          break;
      }
      delete event;
    }
  }
}

void Schedule::reactiveMasterSlaveCallback(const std::vector<uint8_t> &master,
                                           std::vector<uint8_t> *const slave) {
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
}

void Schedule::processActive(const Mode &mode,
                             const std::vector<uint8_t> &master,
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
#endif
