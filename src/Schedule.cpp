#if defined(EBUS_INTERNAL)
#include "Schedule.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <set>

#include "Logger.hpp"
#include "Mqtt.hpp"
#include "http.hpp"

// Identification (Service 07h 04h)
const std::vector<uint8_t> VEC_070400 = {0x07, 0x04, 0x00};

// Vaillant identification (Service B5h 09h 01h + 24h-27h)
const std::vector<uint8_t> VEC_b50901 = {0xb5, 0x09, 0x01};
const std::vector<uint8_t> VEC_b5090124 = {0xb5, 0x09, 0x01, 0x24};
const std::vector<uint8_t> VEC_b5090125 = {0xb5, 0x09, 0x01, 0x25};
const std::vector<uint8_t> VEC_b5090126 = {0xb5, 0x09, 0x01, 0x26};
const std::vector<uint8_t> VEC_b5090127 = {0xb5, 0x09, 0x01, 0x27};

// search Inquiry of Existence (Service 07h FEh)
const std::vector<uint8_t> VEC_07fe00 = {0x07, 0xfe, 0x00};

// broadcast Inquiry of Existence (Service 07h FEh)
const std::vector<uint8_t> VEC_fe07fe00 = {0xfe, 0x07, 0xfe, 0x00};

// broadcast Sign of Life (Service 07h FFh)
const std::vector<uint8_t> VEC_fe07ff00 = {0xfe, 0x07, 0xff, 0x00};

static constexpr uint8_t PRIO_INTERNAL = 5;  // highest
static constexpr uint8_t PRIO_SEND = 4;      // manual send
static constexpr uint8_t PRIO_SCHEDULE = 3;  // schedule commands
static constexpr uint8_t PRIO_SCAN = 2;      // manual scan
static constexpr uint8_t PRIO_FULLSCAN = 1;  // manual full scan

// collected addresses
std::map<uint8_t, uint32_t> seenMasters;
std::map<uint8_t, uint32_t> seenSlaves;

portMUX_TYPE commandMux = portMUX_INITIALIZER_UNLOCKED;

Schedule schedule;

void Schedule::start(ebus::Request* request, ebus::Handler* handler) {
  ebusRequest = request;
  ebusHandler = handler;
  if (ebusRequest && ebusHandler) {
    ebusHandler->setBusRequestWonCallback([this]() {
      CallbackEvent* event = new CallbackEvent();
      event->type = CallbackType::won;
      event->data.message = "Bus request won";
      eventQueue.try_push(event);
    });

    ebusHandler->setBusRequestLostCallback([this]() {
      CallbackEvent* event = new CallbackEvent();
      event->type = CallbackType::lost;
      event->data.message = "Bus request lost";
      eventQueue.try_push(event);
    });

    ebusHandler->setReactiveMasterSlaveCallback(reactiveMasterSlaveCallback);

    ebusHandler->setTelegramCallback(
        [this](const ebus::MessageType& messageType,
               const ebus::TelegramType& telegramType,
               const std::vector<uint8_t>& master,
               const std::vector<uint8_t>& slave) {
          CallbackEvent* event = new CallbackEvent();
          event->type = CallbackType::telegram;
          event->mode = mode;
          event->data.messageType = messageType;
          event->data.telegramType = telegramType;
          event->data.master = master;
          event->data.slave = slave;
          eventQueue.try_push(event);
        });

    ebusHandler->setErrorCallback([this](const std::string& error,
                                         const std::vector<uint8_t>& master,
                                         const std::vector<uint8_t>& slave) {
      CallbackEvent* event = new CallbackEvent();
      event->type = CallbackType::error;
      event->data.message = error;
      event->data.master = master;
      event->data.slave = slave;
      eventQueue.try_push(event);
    });

    // Start the scheduleRunner task
    xTaskCreate(&Schedule::taskFunc, "scheduleRunner", 4096, this, 2,
                &scheduleTaskHandle);

    // enqueue Inquiry of Existence at startup to discover all devices
    if (sendInquiryOfExistence)
      enqueueCommand({Mode::internal, PRIO_INTERNAL, VEC_fe07fe00, nullptr});
  }
}

void Schedule::stop() { stopRunner = true; }

void Schedule::setSendInquiryOfExistence(const bool enable) {
  sendInquiryOfExistence = enable;
}

void Schedule::setScanOnStartup(const bool enable) { scanOnStartup = enable; }

void Schedule::setFirstCommandAfterStart(const uint8_t delay) {
  firstCommandAfterStart = delay * 1000;
}

void Schedule::handleScanFull() {
  fullScan = true;
  scanIndex = 0;
  enqueueFullScanCommand();
}

void Schedule::handleScan() {
  std::set<uint8_t> slaves;

  for (const std::pair<uint8_t, uint32_t> master : seenMasters)
    if (master.first != ebusHandler->getSourceAddress())
      slaves.insert(ebus::slaveOf(master.first));

  for (const std::pair<uint8_t, uint32_t> slave : seenSlaves)
    if (slave.first != ebusHandler->getTargetAddress())
      slaves.insert(slave.first);

  for (const uint8_t slave : slaves) {
    std::vector<uint8_t> command = {slave};
    command.insert(command.end(), VEC_070400.begin(), VEC_070400.end());
    enqueueCommand({Mode::scan, PRIO_SCAN, command, nullptr});
  }
}

void Schedule::handleScanAddresses(const JsonArrayConst& addresses) {
  std::set<uint8_t> slaves;

  for (JsonVariantConst address : addresses) {
    uint8_t firstByte = ebus::to_vector(address.as<std::string>())[0];
    if (ebus::isSlave(firstByte) &&
        firstByte != ebusHandler->getTargetAddress())
      slaves.insert(firstByte);
  }

  for (const uint8_t slave : slaves) {
    std::vector<uint8_t> command = {slave};
    command.insert(command.end(), VEC_070400.begin(), VEC_070400.end());
    enqueueCommand({Mode::scan, PRIO_SCAN, command, nullptr});
  }
}

void Schedule::handleScanVendor() {
  for (const std::pair<uint8_t, Device>& device : allDevices) {
    if (device.second.isVaillant()) {
      if (device.second.vec_b5090124.size() == 0) {
        std::vector<uint8_t> command = {device.first};
        command.insert(command.end(), VEC_b5090124.begin(), VEC_b5090124.end());
        enqueueCommand({Mode::scan, PRIO_SCAN, command, nullptr});
      }
      if (device.second.vec_b5090125.size() == 0) {
        std::vector<uint8_t> command = {device.first};
        command.insert(command.end(), VEC_b5090125.begin(), VEC_b5090125.end());
        enqueueCommand({Mode::scan, PRIO_SCAN, command, nullptr});
      }
      if (device.second.vec_b5090126.size() == 0) {
        std::vector<uint8_t> command = {device.first};
        command.insert(command.end(), VEC_b5090126.begin(), VEC_b5090126.end());
        enqueueCommand({Mode::scan, PRIO_SCAN, command, nullptr});
      }
      if (device.second.vec_b5090127.size() == 0) {
        std::vector<uint8_t> command = {device.first};
        command.insert(command.end(), VEC_b5090127.begin(), VEC_b5090127.end());
        enqueueCommand({Mode::scan, PRIO_SCAN, command, nullptr});
      }
    }
  }
}

void Schedule::handleSend(const std::vector<uint8_t>& command) {
  enqueueCommand({Mode::send, PRIO_SEND, command, nullptr});
}

void Schedule::handleSend(const JsonArrayConst& commands) {
  for (JsonVariantConst command : commands)
    handleSend(ebus::to_vector(command));
}

void Schedule::handleWrite(const std::vector<uint8_t>& command) {
  enqueueCommand({Mode::write, PRIO_SEND, command, nullptr});
}

void Schedule::toggleForward(const bool enable) { forward = enable; }

void Schedule::handleForwardFilter(const JsonArrayConst& filters) {
  forwardfilters.clear();
  for (JsonVariantConst filter : filters)
    forwardfilters.push_back(ebus::to_vector(filter));
}

void Schedule::setPublishCounter(const bool enable) { counterEnabled = enable; }

void Schedule::resetCounter() {
  seenMasters.clear();
  seenSlaves.clear();

  if (ebusRequest) ebusRequest->resetCounter();
  if (ebusHandler) ebusHandler->resetCounter();
}

void Schedule::publishCounter() {
  if (!counterEnabled) return;

  std::string payload = getCounterJson();
  mqtt.publish("state/counter", 0, false, payload.c_str());
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

void Schedule::setPublishTiming(const bool enable) { timingEnabled = enable; }

void Schedule::resetTiming() {
  if (ebusRequest) ebusRequest->resetTiming();
  if (ebusHandler) ebusHandler->resetTiming();
}

void Schedule::publishTiming() {
  if (!timingEnabled) return;

  std::string payload = getTimingJson();
  mqtt.publish("state/timing", 0, false, payload.c_str());
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

  addTiming(doc["BusIsr"]["Delay"].to<JsonObject>(),
            requestTiming.busIsrDelayLast, requestTiming.busIsrDelayMean,
            requestTiming.busIsrDelayStdDev, requestTiming.busIsrDelayCount);

  addTiming(doc["BusIsr"]["Window"].to<JsonObject>(),
            requestTiming.busIsrWindowLast, requestTiming.busIsrWindowMean,
            requestTiming.busIsrWindowStdDev, requestTiming.busIsrWindowCount);

  addTiming(doc["Write"].to<JsonObject>(), handlerTiming.writeLast,
            handlerTiming.writeMean, handlerTiming.writeStdDev,
            handlerTiming.writeCount);

  addTiming(doc["Active"]["First"].to<JsonObject>(),
            handlerTiming.activeFirstLast, handlerTiming.activeFirstMean,
            handlerTiming.activeFirstStdDev, handlerTiming.activeFirstCount);

  addTiming(doc["Active"]["Data"].to<JsonObject>(),
            handlerTiming.activeDataLast, handlerTiming.activeDataMean,
            handlerTiming.activeDataStdDev, handlerTiming.activeDataCount);

  addTiming(doc["Passive"]["First"].to<JsonObject>(),
            handlerTiming.passiveFirstLast, handlerTiming.passiveFirstMean,
            handlerTiming.passiveFirstStdDev, handlerTiming.passiveFirstCount);

  addTiming(doc["Passive"]["Data"].to<JsonObject>(),
            handlerTiming.passiveDataLast, handlerTiming.passiveDataMean,
            handlerTiming.passiveDataStdDev, handlerTiming.passiveDataCount);

  addTiming(doc["Sync"].to<JsonObject>(), handlerTiming.syncLast,
            handlerTiming.syncMean, handlerTiming.syncStdDev,
            handlerTiming.syncCount);

  addTiming(doc["Callback"]["Won"].to<JsonObject>(),
            handlerTiming.callbackWonLast, handlerTiming.callbackWonMean,
            handlerTiming.callbackWonStdDev, handlerTiming.callbackWonCount);

  addTiming(doc["Callback"]["Lost"].to<JsonObject>(),
            handlerTiming.callbackLostLast, handlerTiming.callbackLostMean,
            handlerTiming.callbackLostStdDev, handlerTiming.callbackLostCount);

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
                           const ebus::Handler::StateTiming::Timing& timing) {
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

JsonDocument Schedule::getDeviceJsonDoc(const Device* device) {
  JsonDocument doc;

  doc["address"] = ebus::to_string(device->slave);
  doc["manufacturer"] = ebus::to_string(ebus::range(device->vec_070400, 1, 1));
  doc["unitid"] = ebus::byte_2_char(ebus::range(device->vec_070400, 2, 5));
  doc["software"] = ebus::to_string(ebus::range(device->vec_070400, 7, 2));
  doc["hardware"] = ebus::to_string(ebus::range(device->vec_070400, 9, 2));

  if (device->isVaillant() && device->isVaillantValid()) {
    std::string serial =
        ebus::byte_2_char(ebus::range(device->vec_b5090124, 2, 8));
    serial += ebus::byte_2_char(ebus::range(device->vec_b5090125, 1, 9));
    serial += ebus::byte_2_char(ebus::range(device->vec_b5090126, 1, 9));
    serial += ebus::byte_2_char(ebus::range(device->vec_b5090127, 1, 2));

    doc["prefix"] = serial.substr(0, 2);
    doc["year"] = serial.substr(2, 2);
    doc["week"] = serial.substr(4, 2);
    doc["product"] = serial.substr(6, 10);
    doc["supplier"] = serial.substr(16, 4);
    doc["counter"] = serial.substr(20, 6);
    doc["suffix"] = serial.substr(26, 2);
  }

  doc.shrinkToFit();
  return doc;
}

const std::string Schedule::getDevicesJson() const {
  std::string payload;
  JsonDocument doc;

  if (allDevices.size() > 0) {
    for (const std::pair<uint8_t, Device>& device : allDevices)
      doc.add(getDeviceJsonDoc(&device.second));
  }

  if (doc.isNull()) doc.to<JsonArray>();

  doc.shrinkToFit();
  serializeJson(doc, payload);

  return payload;
}

const std::vector<Device*> Schedule::getDevices() {
  std::vector<Device*> devices;
  for (std::pair<const uint8_t, Device>& device : allDevices)
    devices.push_back(&(device.second));
  return devices;
}

void Schedule::taskFunc(void* arg) {
  Schedule* self = static_cast<Schedule*>(arg);
  for (;;) {
    if (self->stopRunner) vTaskDelete(NULL);
    self->handleEventQueue();
    self->handleCommandQueue();
    vTaskDelay(pdMS_TO_TICKS(10));  // adjust delay as needed
  }
}

void Schedule::handleEventQueue() {
  CallbackEvent* event = nullptr;
  while (eventQueue.try_pop(event)) {
    if (event) {
      std::string payload;
      switch (event->type) {
        case CallbackType::won: {
          payload = event->data.message;
          logger.debug(payload.c_str());
        } break;
        case CallbackType::lost: {
          payload = event->data.message;
          logger.warn(payload.c_str());
        } break;
        case CallbackType::telegram: {
          payload = ebus::to_string(event->data.master);
          if (event->data.telegramType != ebus::TelegramType::broadcast)
            payload += " / " + ebus::to_string(event->data.slave);

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
            case ebus::MessageType::passive:
            case ebus::MessageType::reactive:
              schedule.processPassive(std::vector<uint8_t>(event->data.master),
                                      std::vector<uint8_t>(event->data.slave));
              break;
          }
          logger.info(payload.c_str());
        } break;
        case CallbackType::error: {
          payload = event->data.message + " : master '" +
                    ebus::to_string(event->data.master) + "' slave '" +
                    ebus::to_string(event->data.slave) + "'";

          logger.warn(payload.c_str());
        } break;
      }
      delete event;
    }
  }
}

void Schedule::handleCommandQueue() {
  uint32_t currentMillis = millis();

  // Check if scheduleCommand is stuck
  if (scheduleCommand != nullptr && scheduleCommandSetTime > 0) {
    if (currentMillis > scheduleCommandSetTime + scheduleCommandTimeout) {
      scheduleCommand = nullptr;  // Clear the stuck command
      scheduleCommandSetTime = 0;
    }
  }

  // Enqueue next schedule command if needed
  if (store.active()) enqueueScheduleCommand();

  // Enqueue startup scan commands if needed
  if (scanOnStartup) enqueueStartupScanCommands();

  // Enqueue next full scan command if needed
  if (fullScan) enqueueFullScanCommand();

  // Process queue
  if (!ebusHandler->isActiveMessagePending() && !commandQueue.empty() &&
      currentMillis > firstCommandAfterStart) {
    portENTER_CRITICAL(&commandMux);
    QueuedCommand nextCmd = commandQueue.top();
    commandQueue.pop();
    portEXIT_CRITICAL(&commandMux);

    mode = nextCmd.mode;

    // Track schedule command
    if (mode == Mode::schedule) {
      scheduleCommand = nextCmd.scheduleCommand;
      scheduleCommandSetTime = currentMillis;
    }

    // Send command
    if (!nextCmd.command.empty()) {
      bool res = ebusHandler->sendActiveMessage(nextCmd.command);
      std::string msg = "Start " +
                        std::string(res ? "success: " : " failed: ") +
                        ebus::to_string(nextCmd.command);
      logger.debug(msg.c_str());
    }
  }
}

void Schedule::enqueueCommand(const QueuedCommand& cmd) {
  portENTER_CRITICAL(&commandMux);

  // Ensure only one schedule command is allowed
  if (cmd.mode == Mode::schedule) {
    auto tmpQueue = commandQueue;  // Create a copy to check
    while (!tmpQueue.empty()) {
      if (tmpQueue.top().mode == Mode::schedule) {
        portEXIT_CRITICAL(&commandMux);
        return;  // A schedule command already exists
      }
      tmpQueue.pop();
    }
  }

  // Ensure only one full scan command is allowed
  if (cmd.mode == Mode::fullscan) {
    auto tmpQueue = commandQueue;  // Create a copy to check
    while (!tmpQueue.empty()) {
      if (tmpQueue.top().mode == Mode::fullscan) {
        portEXIT_CRITICAL(&commandMux);
        return;  // A full scan command already exists
      }
      tmpQueue.pop();
    }
  }

  commandQueue.push(cmd);  // Add the command to the priority queue
  portEXIT_CRITICAL(&commandMux);
}

void Schedule::enqueueScheduleCommand() {
  Command* cmd = store.nextActiveCommand();
  if (cmd && cmd->read_cmd.size() > 0) {
    if (scheduleCommand == cmd) return;  // command is already pending
    enqueueCommand({Mode::schedule, PRIO_SCHEDULE, cmd->read_cmd, cmd});
  }
}

void Schedule::enqueueStartupScanCommands() {
  uint32_t currentMillis = millis();
  if (currentScan < maxScans && currentMillis > lastScan + distanceScans) {
    currentScan++;
    lastScan = currentMillis;
    distanceScans = 3 * 60 * 1000;  // repeat scan in 3 minutes
    handleScan();
    handleScanVendor();
  }
}

void Schedule::enqueueFullScanCommand() {
  if (millis() > lastFullScan + distanceFullScans) {
    lastFullScan = millis();
    while (scanIndex < 0xff) {
      scanIndex++;
      if (ebus::isSlave(scanIndex) &&
          scanIndex != ebusHandler->getTargetAddress()) {
        std::vector<uint8_t> command = {scanIndex};
        command.insert(command.end(), VEC_070400.begin(), VEC_070400.end());
        enqueueCommand({Mode::fullscan, PRIO_FULLSCAN, command, nullptr});
        return;
      }
    }
    fullScan = false;  // reset fullScan to avoid repeated calls
    scanIndex = 0;     // reset scanIndex for next full scan
  }
}

void Schedule::reactiveMasterSlaveCallback(const std::vector<uint8_t>& master,
                                           std::vector<uint8_t>* const slave) {
  // TODO(yuhu-): Implement handling of Identification (Service 07h 04h)
  // Expected data format:
  // hh...Manufacturer (BYTE)
  // gg...Unit_ID_0-5 (ASCII)
  // ss...Software version (BCD)
  // rr...Revision (BCD)
  // vv...Hardware version (BCD)
  // hh...Revision (BCD)
  // Example:
  // if (ebus::contains(master, VEC_070400, 2))
  //   *slave = ebus::to_vector("0ahhggggggggggssrrhhrr");
}

void Schedule::processActive(const Mode& mode,
                             const std::vector<uint8_t>& master,
                             const std::vector<uint8_t>& slave) {
  switch (mode) {
    case Mode::schedule:
      if (scheduleCommand != nullptr) {
        store.updateData(scheduleCommand, master, slave);
        mqtt.publishValue(scheduleCommand,
                          store.getValueJsonDoc(scheduleCommand));
        scheduleCommand = nullptr;
        scheduleCommandSetTime = 0;  // clear after success
      }
      break;
    case Mode::internal:
      break;
    case Mode::scan:
    case Mode::fullscan:
      processScan(master, slave);
      break;
    case Mode::send:
      mqtt.publishData("send", master, slave);
      break;
    case Mode::read:  // not used yet
      mqtt.publishData("read", master, slave);
      break;
    case Mode::write:
      mqtt.publishData("write", master, slave);
      break;
    default:
      break;
  }
}

void Schedule::processPassive(const std::vector<uint8_t>& master,
                              const std::vector<uint8_t>& slave) {
  if (forward) {
    size_t count = std::count_if(forwardfilters.begin(), forwardfilters.end(),
                                 [&master](const std::vector<uint8_t>& vec) {
                                   return ebus::contains(master, vec);
                                 });
    if (count > 0 || forwardfilters.size() == 0)
      mqtt.publishData("forward", master, slave);
  }

  std::vector<Command*> pasCommands = store.updateData(nullptr, master, slave);

  for (const Command* command : pasCommands)
    mqtt.publishValue(command, store.getValueJsonDoc(command));

  processScan(master, slave);

  // send Sign of Life in response to an Inquiry of Existence
  if (ebus::contains(master, VEC_07fe00, 2))
    enqueueCommand({Mode::internal, PRIO_INTERNAL, VEC_fe07ff00, nullptr});
}

void Schedule::processScan(const std::vector<uint8_t>& master,
                           const std::vector<uint8_t>& slave) {
  if (ebus::contains(master, VEC_070400, 2)) {
    allDevices[master[1]].slave = master[1];
    allDevices[master[1]].vec_070400 = slave;
  }

  if (ebus::contains(master, VEC_b5090124, 2))
    allDevices[master[1]].vec_b5090124 = slave;
  if (ebus::contains(master, VEC_b5090125, 2))
    allDevices[master[1]].vec_b5090125 = slave;
  if (ebus::contains(master, VEC_b5090126, 2))
    allDevices[master[1]].vec_b5090126 = slave;
  if (ebus::contains(master, VEC_b5090127, 2))
    allDevices[master[1]].vec_b5090127 = slave;
}
#endif
