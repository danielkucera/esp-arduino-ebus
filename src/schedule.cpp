#if defined(EBUS_INTERNAL)
#include "schedule.hpp"

#include <set>

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

#define ASSIGN_REQUEST_COUNTER(NAME) NAME = requestCounter.NAME;
#define ASSIGN_HANDLER_COUNTER(NAME) NAME = handlerCounter.NAME;

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
TRACK_U32(requestsStartBit, "requests/startBit")
TRACK_U32(requestsFirstSyn, "requests/firstSyn")
TRACK_U32(requestsFirstWon, "requests/firstWon")
TRACK_U32(requestsFirstRetry, "requests/firstRetry")
TRACK_U32(requestsFirstLost, "requests/firstLost")
TRACK_U32(requestsFirstError, "requests/firstError")
TRACK_U32(requestsRetrySyn, "requests/retrySyn")
TRACK_U32(requestsRetryError, "requests/retryError")
TRACK_U32(requestsSecondWon, "requests/secondWon")
TRACK_U32(requestsSecondLost, "requests/secondLost")
TRACK_U32(requestsSecondError, "requests/secondError")

// Reset
TRACK_U32(resetTotal, "reset")
TRACK_U32(resetPassive00, "reset/passive00")
TRACK_U32(resetPassive0704, "reset/passive0704")
TRACK_U32(resetPassive, "reset/passive")
TRACK_U32(resetActive, "reset/active")

// Error
TRACK_U32(errorTotal, "error")
TRACK_U32(errorPassive, "error/passive")
TRACK_U32(errorPassiveMaster, "error/passive/master")
TRACK_U32(errorPassiveMasterACK, "error/passive/masterACK")
TRACK_U32(errorPassiveSlave, "error/passive/slave")
TRACK_U32(errorPassiveSlaveACK, "error/passive/slaveACK")
TRACK_U32(errorReactive, "error/reactive")
TRACK_U32(errorReactiveMaster, "error/reactive/master")
TRACK_U32(errorReactiveMasterACK, "error/reactive/masterACK")
TRACK_U32(errorReactiveSlave, "error/reactive/slave")
TRACK_U32(errorReactiveSlaveACK, "error/reactive/slaveACK")
TRACK_U32(errorActive, "error/active")
TRACK_U32(errorActiveMaster, "error/active/master")
TRACK_U32(errorActiveMasterACK, "error/active/masterACK")
TRACK_U32(errorActiveSlave, "error/active/slave")
TRACK_U32(errorActiveSlaveACK, "error/active/slaveACK")

#define TRACK_TIMING(NAME, PATH)                                   \
  Track<int64_t> NAME##Last("state/timing/" PATH "/last", 10);     \
  Track<int64_t> NAME##Mean("state/timing/" PATH "/mean", 10);     \
  Track<int64_t> NAME##StdDev("state/timing/" PATH "/stddev", 10); \
  Track<uint64_t> NAME##Count("state/timing/" PATH "/count", 10);

#define ASSIGN_REQUEST_TIMING(NAME)          \
  NAME##Last = requestTiming.NAME##Last;     \
  NAME##Mean = requestTiming.NAME##Mean;     \
  NAME##StdDev = requestTiming.NAME##StdDev; \
  NAME##Count = requestTiming.NAME##Count;

#define ASSIGN_HANDLER_TIMING(NAME)          \
  NAME##Last = handlerTiming.NAME##Last;     \
  NAME##Mean = handlerTiming.NAME##Mean;     \
  NAME##StdDev = handlerTiming.NAME##StdDev; \
  NAME##Count = handlerTiming.NAME##Count;

#define ASSIGN_HANDLER_STATE_TIMING(NAME, STATE_ENUM)                          \
  NAME##Last = handlerStateTiming.timing[ebus::HandlerState::STATE_ENUM].last; \
  NAME##Mean = handlerStateTiming.timing[ebus::HandlerState::STATE_ENUM].mean; \
  NAME##StdDev =                                                               \
      handlerStateTiming.timing[ebus::HandlerState::STATE_ENUM].stddev;        \
  NAME##Count = handlerStateTiming.timing[ebus::HandlerState::STATE_ENUM].count;

// General timing
TRACK_TIMING(sync, "sync")
TRACK_TIMING(write, "write")
TRACK_TIMING(busIsrDelay, "busIsr/delay")
TRACK_TIMING(busIsrWindow, "busIsr/window")
TRACK_TIMING(passiveFirst, "passive/first")
TRACK_TIMING(passiveData, "passive/data")
TRACK_TIMING(activeFirst, "active/first")
TRACK_TIMING(activeData, "active/data")
TRACK_TIMING(callbackReactive, "callback/reactive")
TRACK_TIMING(callbackTelegram, "callback/telegram")
TRACK_TIMING(callbackError, "callback/error")

// Handler state timing
TRACK_TIMING(passiveReceiveMaster, "handlerState/passiveReceiveMaster")
TRACK_TIMING(passiveReceiveMasterAcknowledge,
             "handlerState/passiveReceiveMasterAcknowledge")
TRACK_TIMING(passiveReceiveSlave, "handlerState/passiveReceiveSlave")
TRACK_TIMING(passiveReceiveSlaveAcknowledge,
             "handlerState/passiveReceiveSlaveAcknowledge")
TRACK_TIMING(reactiveSendMasterPositiveAcknowledge,
             "handlerState/reactiveSendMasterPositiveAcknowledge")
TRACK_TIMING(reactiveSendMasterNegativeAcknowledge,
             "handlerState/reactiveSendMasterNegativeAcknowledge")
TRACK_TIMING(reactiveSendSlave, "handlerState/reactiveSendSlave")
TRACK_TIMING(reactiveReceiveSlaveAcknowledge,
             "handlerState/reactiveReceiveSlaveAcknowledge")
TRACK_TIMING(requestBus, "handlerState/requestBus")
TRACK_TIMING(activeSendMaster, "handlerState/activeSendMaster")
TRACK_TIMING(activeReceiveMasterAcknowledge,
             "handlerState/activeReceiveMasterAcknowledge")
TRACK_TIMING(activeReceiveSlave, "handlerState/activeReceiveSlave")
TRACK_TIMING(activeSendSlavePositiveAcknowledge,
             "handlerState/activeSendSlavePositiveAcknowledge")
TRACK_TIMING(activeSendSlaveNegativeAcknowledge,
             "handlerState/activeSendSlaveNegativeAcknowledge")
TRACK_TIMING(releaseBus, "handlerState/releaseBus");

Schedule schedule;

void Schedule::setup(ebus::Request *request, ebus::Handler *handler) {
  ebusRequest = request;
  ebusHandler = handler;
  if (ebusRequest && ebusHandler) {
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
    if (master.first != ebusHandler->getSourceAddress())
      slaves.insert(ebus::slaveOf(master.first));

  for (const std::pair<uint8_t, uint32_t> slave : seenSlaves)
    if (slave.first != ebusHandler->getTargetAddress())
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
    if (ebus::isSlave(firstByte) &&
        firstByte != ebusHandler->getTargetAddress())
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

void Schedule::setPublishCounter(const bool enable) { publishCounter = enable; }

void Schedule::resetCounter() {
  seenMasters.clear();
  seenSlaves.clear();

  if (ebusRequest) ebusRequest->resetCounter();
  if (ebusHandler) ebusHandler->resetCounter();
}

void Schedule::fetchCounter() {
  if (!publishCounter) return;

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

  // Counter
  ebus::Request::Counter requestCounter = ebusRequest->getCounter();
  ebus::Handler::Counter handlerCounter = ebusHandler->getCounter();

  // Messages
  ASSIGN_HANDLER_COUNTER(messagesTotal)
  ASSIGN_HANDLER_COUNTER(messagesPassiveMasterSlave)
  ASSIGN_HANDLER_COUNTER(messagesPassiveMasterMaster)
  ASSIGN_HANDLER_COUNTER(messagesPassiveBroadcast)
  ASSIGN_HANDLER_COUNTER(messagesReactiveMasterSlave)
  ASSIGN_HANDLER_COUNTER(messagesReactiveMasterMaster)
  ASSIGN_HANDLER_COUNTER(messagesActiveMasterSlave)
  ASSIGN_HANDLER_COUNTER(messagesActiveMasterMaster)
  ASSIGN_HANDLER_COUNTER(messagesActiveBroadcast)

  // Requests
  ASSIGN_REQUEST_COUNTER(requestsStartBit)
  ASSIGN_REQUEST_COUNTER(requestsFirstSyn)
  ASSIGN_REQUEST_COUNTER(requestsFirstWon)
  ASSIGN_REQUEST_COUNTER(requestsFirstRetry)
  ASSIGN_REQUEST_COUNTER(requestsFirstLost)
  ASSIGN_REQUEST_COUNTER(requestsFirstError)
  ASSIGN_REQUEST_COUNTER(requestsRetrySyn)
  ASSIGN_REQUEST_COUNTER(requestsRetryError)
  ASSIGN_REQUEST_COUNTER(requestsSecondWon)
  ASSIGN_REQUEST_COUNTER(requestsSecondLost)
  ASSIGN_REQUEST_COUNTER(requestsSecondError)

  // Reset
  ASSIGN_HANDLER_COUNTER(resetTotal)
  ASSIGN_HANDLER_COUNTER(resetPassive00)
  ASSIGN_HANDLER_COUNTER(resetPassive0704)
  ASSIGN_HANDLER_COUNTER(resetPassive)
  ASSIGN_HANDLER_COUNTER(resetActive)

  // Error
  ASSIGN_HANDLER_COUNTER(errorTotal)
  ASSIGN_HANDLER_COUNTER(errorPassive)
  ASSIGN_HANDLER_COUNTER(errorPassiveMaster)
  ASSIGN_HANDLER_COUNTER(errorPassiveMasterACK)
  ASSIGN_HANDLER_COUNTER(errorPassiveSlave)
  ASSIGN_HANDLER_COUNTER(errorPassiveSlaveACK)
  ASSIGN_HANDLER_COUNTER(errorReactive)
  ASSIGN_HANDLER_COUNTER(errorReactiveMaster)
  ASSIGN_HANDLER_COUNTER(errorReactiveMasterACK)
  ASSIGN_HANDLER_COUNTER(errorReactiveSlave)
  ASSIGN_HANDLER_COUNTER(errorReactiveSlaveACK)
  ASSIGN_HANDLER_COUNTER(errorActive)
  ASSIGN_HANDLER_COUNTER(errorActiveMaster)
  ASSIGN_HANDLER_COUNTER(errorActiveMasterACK)
  ASSIGN_HANDLER_COUNTER(errorActiveSlave)
  ASSIGN_HANDLER_COUNTER(errorActiveSlaveACK)
}

const std::string Schedule::getCounterJson() {
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

  // Counter
  ebus::Handler::Counter handlerCounter = ebusHandler->getCounter();
  ebus::Request::Counter requestCounter = ebusRequest->getCounter();

  // Messages
  JsonObject Messages = doc["Messages"].to<JsonObject>();
  Messages["Total"] = handlerCounter.messagesTotal;
  Messages["Passive_Master_Slave"] = handlerCounter.messagesPassiveMasterSlave;
  Messages["Passive_Master_Master"] =
      handlerCounter.messagesPassiveMasterMaster;
  Messages["Passive_Broadcast"] = handlerCounter.messagesPassiveBroadcast;
  Messages["Reactive_Master_Slave"] =
      handlerCounter.messagesReactiveMasterSlave;
  Messages["Reactive_Master_Master"] =
      handlerCounter.messagesReactiveMasterMaster;
  Messages["Active_Master_Slave"] = handlerCounter.messagesActiveMasterSlave;
  Messages["Active_Master_Master"] = handlerCounter.messagesActiveMasterMaster;
  Messages["Active_Broadcast"] = handlerCounter.messagesActiveBroadcast;

  // Requests
  JsonObject Requests = doc["Requests"].to<JsonObject>();
  Requests["StartBit"] = requestCounter.requestsStartBit;
  Requests["FirstSyn"] = requestCounter.requestsFirstSyn;
  Requests["FirstWon"] = requestCounter.requestsFirstWon;
  Requests["FirstRetry"] = requestCounter.requestsFirstRetry;
  Requests["FirstLost"] = requestCounter.requestsFirstLost;
  Requests["FirstError"] = requestCounter.requestsFirstError;
  Requests["RetrySyn"] = requestCounter.requestsRetrySyn;
  Requests["RetryError"] = requestCounter.requestsRetryError;
  Requests["SecondWon"] = requestCounter.requestsSecondWon;
  Requests["SecondLost"] = requestCounter.requestsSecondLost;
  Requests["SecondError"] = requestCounter.requestsSecondError;

  // Reset
  JsonObject Reset = doc["Reset"].to<JsonObject>();
  Reset["Total"] = handlerCounter.resetTotal;
  Reset["Passive_00"] = handlerCounter.resetPassive00;
  Reset["Passive_0704"] = handlerCounter.resetPassive0704;
  Reset["Passive"] = handlerCounter.resetPassive;
  Reset["Active"] = handlerCounter.resetActive;

  // Error
  JsonObject Error = doc["Error"].to<JsonObject>();
  Error["Total"] = handlerCounter.errorTotal;

  // Error Passive
  JsonObject Error_Passive = doc["Error"]["Passive"].to<JsonObject>();
  Error_Passive["Total"] = handlerCounter.errorPassive;
  Error_Passive["Master"] = handlerCounter.errorPassiveMaster;
  Error_Passive["Master_ACK"] = handlerCounter.errorPassiveMasterACK;
  Error_Passive["Slave"] = handlerCounter.errorPassiveSlave;
  Error_Passive["Slave_ACK"] = handlerCounter.errorPassiveSlaveACK;

  // Error Reactive
  JsonObject Error_Reactive = doc["Error"]["Reactive"].to<JsonObject>();
  Error_Reactive["Total"] = handlerCounter.errorReactive;
  Error_Reactive["Master"] = handlerCounter.errorReactiveMaster;
  Error_Reactive["Master_ACK"] = handlerCounter.errorReactiveMasterACK;
  Error_Reactive["Slave"] = handlerCounter.errorReactiveSlave;
  Error_Reactive["Slave_ACK"] = handlerCounter.errorReactiveSlaveACK;

  // Error Active
  JsonObject Error_Active = doc["Error"]["Active"].to<JsonObject>();
  Error_Active["Total"] = handlerCounter.errorActive;
  Error_Active["Master"] = handlerCounter.errorActiveMaster;
  Error_Active["Master_ACK"] = handlerCounter.errorActiveMasterACK;
  Error_Active["Slave"] = handlerCounter.errorActiveSlave;
  Error_Active["Slave_ACK"] = handlerCounter.errorActiveSlaveACK;

  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload;
}

void Schedule::setPublishTiming(const bool enable) { publishTiming = enable; }

void Schedule::resetTiming() {
  if (ebusRequest) ebusRequest->resetTiming();
  if (ebusHandler) ebusHandler->resetTiming();
}

void Schedule::fetchTiming() {
  if (!publishTiming) return;

  // Timing
  ebus::Request::Timing requestTiming = ebusRequest->getTiming();
  ebus::Handler::Timing handlerTiming = ebusHandler->getTiming();

  ASSIGN_HANDLER_TIMING(sync)
  ASSIGN_HANDLER_TIMING(write)
  ASSIGN_REQUEST_TIMING(busIsrDelay)
  ASSIGN_REQUEST_TIMING(busIsrWindow)
  ASSIGN_HANDLER_TIMING(passiveFirst)
  ASSIGN_HANDLER_TIMING(passiveData)
  ASSIGN_HANDLER_TIMING(activeFirst)
  ASSIGN_HANDLER_TIMING(activeData)
  ASSIGN_HANDLER_TIMING(callbackReactive)
  ASSIGN_HANDLER_TIMING(callbackTelegram)
  ASSIGN_HANDLER_TIMING(callbackError)

  ebus::Handler::StateTiming handlerStateTiming = ebusHandler->getStateTiming();

  ASSIGN_HANDLER_STATE_TIMING(passiveReceiveMaster, passiveReceiveMaster)
  ASSIGN_HANDLER_STATE_TIMING(passiveReceiveMasterAcknowledge,
                              passiveReceiveMasterAcknowledge)
  ASSIGN_HANDLER_STATE_TIMING(passiveReceiveSlave, passiveReceiveSlave)
  ASSIGN_HANDLER_STATE_TIMING(passiveReceiveSlaveAcknowledge,
                              passiveReceiveSlaveAcknowledge)
  ASSIGN_HANDLER_STATE_TIMING(reactiveSendMasterPositiveAcknowledge,
                              reactiveSendMasterPositiveAcknowledge)
  ASSIGN_HANDLER_STATE_TIMING(reactiveSendMasterNegativeAcknowledge,
                              reactiveSendMasterNegativeAcknowledge)
  ASSIGN_HANDLER_STATE_TIMING(reactiveSendSlave, reactiveSendSlave)
  ASSIGN_HANDLER_STATE_TIMING(reactiveReceiveSlaveAcknowledge,
                              reactiveReceiveSlaveAcknowledge)
  ASSIGN_HANDLER_STATE_TIMING(requestBus, requestBus)
  ASSIGN_HANDLER_STATE_TIMING(activeSendMaster, activeSendMaster)
  ASSIGN_HANDLER_STATE_TIMING(activeReceiveMasterAcknowledge,
                              activeReceiveMasterAcknowledge)
  ASSIGN_HANDLER_STATE_TIMING(activeReceiveSlave, activeReceiveSlave)
  ASSIGN_HANDLER_STATE_TIMING(activeSendSlavePositiveAcknowledge,
                              activeSendSlavePositiveAcknowledge)
  ASSIGN_HANDLER_STATE_TIMING(activeSendSlaveNegativeAcknowledge,
                              activeSendSlaveNegativeAcknowledge)
  ASSIGN_HANDLER_STATE_TIMING(releaseBus, releaseBus)
}

const std::string Schedule::getTimingJson() {
  std::string payload;
  JsonDocument doc;

  // Timing
  ebus::Request::Timing requestTiming = ebusRequest->getTiming();
  ebus::Handler::Timing handlerTiming = ebusHandler->getTiming();

  // Helper lambda to add timing stats to a JsonObject
  auto addTiming = [](JsonObject obj, int64_t last, int64_t mean,
                      int64_t stddev, uint64_t count) {
    obj["Last"] = last;
    obj["Mean"] = mean;
    obj["StdDev"] = stddev;
    obj["Count"] = count;
  };

  addTiming(doc["Sync"].to<JsonObject>(), handlerTiming.syncLast,
            handlerTiming.syncMean, handlerTiming.syncStdDev,
            handlerTiming.syncCount);

  addTiming(doc["Write"].to<JsonObject>(), handlerTiming.writeLast,
            handlerTiming.writeMean, handlerTiming.writeStdDev,
            handlerTiming.writeCount);

  addTiming(doc["BusIsr"]["Delay"].to<JsonObject>(),
            requestTiming.busIsrDelayLast, requestTiming.busIsrDelayMean,
            requestTiming.busIsrDelayStdDev, requestTiming.busIsrDelayCount);

  addTiming(doc["BusIsr"]["Window"].to<JsonObject>(),
            requestTiming.busIsrWindowLast, requestTiming.busIsrWindowMean,
            requestTiming.busIsrWindowStdDev, requestTiming.busIsrWindowCount);

  addTiming(doc["Passive"]["First"].to<JsonObject>(),
            handlerTiming.passiveFirstLast, handlerTiming.passiveFirstMean,
            handlerTiming.passiveFirstStdDev, handlerTiming.passiveFirstCount);

  addTiming(doc["Passive"]["Data"].to<JsonObject>(),
            handlerTiming.passiveDataLast, handlerTiming.passiveDataMean,
            handlerTiming.passiveDataStdDev, handlerTiming.passiveDataCount);

  addTiming(doc["Active"]["First"].to<JsonObject>(),
            handlerTiming.activeFirstLast, handlerTiming.activeFirstMean,
            handlerTiming.activeFirstStdDev, handlerTiming.activeFirstCount);

  addTiming(doc["Active"]["Data"].to<JsonObject>(),
            handlerTiming.activeDataLast, handlerTiming.activeDataMean,
            handlerTiming.activeDataStdDev, handlerTiming.activeDataCount);

  addTiming(doc["Callback"]["Reactive"].to<JsonObject>(),
            handlerTiming.callbackReactiveLast,
            handlerTiming.callbackReactiveMean,
            handlerTiming.callbackReactiveStdDev,
            handlerTiming.callbackReactiveCount);

  addTiming(doc["Callback"]["Telegram"].to<JsonObject>(),
            handlerTiming.callbackTelegramLast,
            handlerTiming.callbackTelegramMean,
            handlerTiming.callbackTelegramStdDev,
            handlerTiming.callbackTelegramCount);

  addTiming(doc["Callback"]["Error"].to<JsonObject>(),
            handlerTiming.callbackErrorLast, handlerTiming.callbackErrorMean,
            handlerTiming.callbackErrorStdDev,
            handlerTiming.callbackErrorCount);

  ebus::Handler::StateTiming stateTiming = ebusHandler->getStateTiming();

  // Output handler state timing
  auto addStateTiming = [](JsonObject obj,
                           const ebus::Handler::StateTiming::Timing &timing) {
    obj["Last"] = static_cast<int64_t>(timing.last);
    obj["Mean"] = static_cast<int64_t>(timing.mean);
    obj["StdDev"] = static_cast<int64_t>(timing.stddev);
    obj["Count"] = timing.count;
  };

  addStateTiming(
      doc["HandlerState"]["passiveReceiveMaster"].to<JsonObject>(),
      stateTiming.timing.at(ebus::HandlerState::passiveReceiveMaster));
  addStateTiming(
      doc["HandlerState"]["passiveReceiveMasterAcknowledge"].to<JsonObject>(),
      stateTiming.timing.at(
          ebus::HandlerState::passiveReceiveMasterAcknowledge));
  addStateTiming(
      doc["HandlerState"]["passiveReceiveSlave"].to<JsonObject>(),
      stateTiming.timing.at(ebus::HandlerState::passiveReceiveSlave));
  addStateTiming(
      doc["HandlerState"]["passiveReceiveSlaveAcknowledge"].to<JsonObject>(),
      stateTiming.timing.at(
          ebus::HandlerState::passiveReceiveSlaveAcknowledge));
  addStateTiming(
      doc["HandlerState"]["reactiveSendMasterPositiveAcknowledge"]
          .to<JsonObject>(),
      stateTiming.timing.at(
          ebus::HandlerState::reactiveSendMasterPositiveAcknowledge));
  addStateTiming(
      doc["HandlerState"]["reactiveSendMasterNegativeAcknowledge"]
          .to<JsonObject>(),
      stateTiming.timing.at(
          ebus::HandlerState::reactiveSendMasterNegativeAcknowledge));
  addStateTiming(doc["HandlerState"]["reactiveSendSlave"].to<JsonObject>(),
                 stateTiming.timing.at(ebus::HandlerState::reactiveSendSlave));
  addStateTiming(
      doc["HandlerState"]["reactiveReceiveSlaveAcknowledge"].to<JsonObject>(),
      stateTiming.timing.at(
          ebus::HandlerState::reactiveReceiveSlaveAcknowledge));
  addStateTiming(doc["HandlerState"]["requestBus"].to<JsonObject>(),
                 stateTiming.timing.at(ebus::HandlerState::requestBus));
  addStateTiming(doc["HandlerState"]["activeSendMaster"].to<JsonObject>(),
                 stateTiming.timing.at(ebus::HandlerState::activeSendMaster));
  addStateTiming(
      doc["HandlerState"]["activeReceiveMasterAcknowledge"].to<JsonObject>(),
      stateTiming.timing.at(
          ebus::HandlerState::activeReceiveMasterAcknowledge));
  addStateTiming(doc["HandlerState"]["activeReceiveSlave"].to<JsonObject>(),
                 stateTiming.timing.at(ebus::HandlerState::activeReceiveSlave));
  addStateTiming(doc["HandlerState"]["activeSendSlavePositiveAcknowledge"]
                     .to<JsonObject>(),
                 stateTiming.timing.at(
                     ebus::HandlerState::activeSendSlavePositiveAcknowledge));
  addStateTiming(doc["HandlerState"]["activeSendSlaveNegativeAcknowledge"]
                     .to<JsonObject>(),
                 stateTiming.timing.at(
                     ebus::HandlerState::activeSendSlaveNegativeAcknowledge));
  addStateTiming(doc["HandlerState"]["releaseBus"].to<JsonObject>(),
                 stateTiming.timing.at(ebus::HandlerState::releaseBus));

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
        ebusHandler->enqueueActiveMessage(command);
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
        scanIndex != ebusHandler->getTargetAddress()) {
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
          if (schedule.publishCounter) {
            std::string topic = "state/reset/last";
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
