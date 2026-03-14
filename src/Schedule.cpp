#if defined(EBUS_INTERNAL)
#include "Schedule.hpp"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <algorithm>

#include "DeviceManager.hpp"
#include "Logger.hpp"
#include "Mqtt.hpp"
#include "Store.hpp"

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

portMUX_TYPE commandMux = portMUX_INITIALIZER_UNLOCKED;

Schedule schedule;

void Schedule::start(ebus::Bus* bus, ebus::Request* request,
                     ebus::Handler* handler) {
  ebusBus = bus;
  ebusRequest = request;
  ebusHandler = handler;

  if (ebusRequest && ebusHandler) {
    ebusHandler->setBusRequestWonCallback([this]() {
      CallbackEvent* event = new CallbackEvent();
      event->type = CallbackType::won;
      eventQueue.try_push(event);
    });

    ebusHandler->setBusRequestLostCallback([this]() {
      CallbackEvent* event = new CallbackEvent();
      event->type = CallbackType::lost;
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

void Schedule::setFirstCommandAfterStart(const uint8_t delay) {
  firstCommandAfterStart = delay * 1000;
}

void Schedule::handleScanFull() {
  deviceManager.setFullScan(true);
  deviceManager.resetFullScan();
  enqueueFullScanCommand();
}

void Schedule::handleScan() {
  for (const std::vector<uint8_t>& command : deviceManager.scanCommands())
    enqueueCommand({Mode::scan, PRIO_SCAN, command, nullptr});
}

void Schedule::handleScanAddresses(const std::vector<std::string>& addresses) {
  for (const auto& command : deviceManager.addressesScanCommands(addresses))
    enqueueCommand({Mode::scan, PRIO_SCAN, command, nullptr});
}

void Schedule::handleScanVendor() {
  for (const std::vector<uint8_t>& command : deviceManager.vendorScanCommands())
    enqueueCommand({Mode::scan, PRIO_SCAN, command, nullptr});
}

void Schedule::handleSend(const std::vector<uint8_t>& command) {
  enqueueCommand({Mode::send, PRIO_SEND, command, nullptr});
}

void Schedule::handleSend(const std::vector<std::string>& commands) {
  for (const std::string& command : commands)
    handleSend(ebus::to_vector(command));
}

void Schedule::handleWrite(const std::vector<uint8_t>& command) {
  enqueueCommand({Mode::write, PRIO_SEND, command, nullptr});
}

void Schedule::toggleForward(bool enable) { forward = enable; }

void Schedule::handleForwardFilter(const std::vector<std::string>& filters) {
  forwardfilters.clear();
  for (const std::string& filter : filters)
    forwardfilters.push_back(ebus::to_vector(filter));
}

void Schedule::setPublishCounter(bool enable) { counterEnabled = enable; }

bool Schedule::getPublishCounter() const { return counterEnabled; }

void Schedule::resetCounter() {
  deviceManager.resetAddresses();

  busRequestFailed = 0;
  sendingFailed = 0;

  if (ebusRequest) ebusRequest->resetCounter();
  if (ebusHandler) ebusHandler->resetCounter();
}

void Schedule::publishCounter() {
  if (!counterEnabled) return;

  std::string payload = getCounterJson();
  mqtt.publish("state/counter", 0, false, payload.c_str());
}

const std::string Schedule::getCounterJson() {
  cJSON* doc = cJSON_CreateObject();

  // Addresses Master
  cJSON* addresses = cJSON_AddObjectToObject(doc, "Addresses");
  cJSON* addressesMaster = cJSON_AddObjectToObject(addresses, "Master");
  deviceManager.populateMasterAddresses(addressesMaster);

  // Addresses Slave
  cJSON* addressesSlave = cJSON_AddObjectToObject(addresses, "Slave");
  deviceManager.populateSlaveAddresses(addressesSlave);

  // Failed
  cJSON* failed = cJSON_AddObjectToObject(doc, "Failed");
  cJSON_AddNumberToObject(failed, "BusRequest", busRequestFailed);
  cJSON_AddNumberToObject(failed, "Sending", sendingFailed);

  // Counter
  ebus::Bus::Counter busCounter = ebusBus->getCounter();
  ebus::Request::Counter requestCounter = ebusRequest->getCounter();
  ebus::Handler::Counter handlerCounter = ebusHandler->getCounter();

  // Messages
  cJSON* messages = cJSON_AddObjectToObject(doc, "Messages");
  cJSON_AddNumberToObject(messages, "Total", handlerCounter.messagesTotal);
  cJSON_AddNumberToObject(messages, "Passive_Master_Slave",
                          handlerCounter.messagesPassiveMasterSlave);
  cJSON_AddNumberToObject(messages, "Passive_Master_Master",
                          handlerCounter.messagesPassiveMasterMaster);
  cJSON_AddNumberToObject(messages, "Passive_Broadcast",
                          handlerCounter.messagesPassiveBroadcast);
  cJSON_AddNumberToObject(messages, "Reactive_Master_Slave",
                          handlerCounter.messagesReactiveMasterSlave);
  cJSON_AddNumberToObject(messages, "Reactive_Master_Master",
                          handlerCounter.messagesReactiveMasterMaster);
  cJSON_AddNumberToObject(messages, "Active_Master_Slave",
                          handlerCounter.messagesActiveMasterSlave);
  cJSON_AddNumberToObject(messages, "Active_Master_Master",
                          handlerCounter.messagesActiveMasterMaster);
  cJSON_AddNumberToObject(messages, "Active_Broadcast",
                          handlerCounter.messagesActiveBroadcast);

  // Bus
  cJSON* bus = cJSON_AddObjectToObject(doc, "Bus");
  cJSON_AddNumberToObject(bus, "StartBit", busCounter.busStartBit);

  // Requests
  cJSON* requests = cJSON_AddObjectToObject(doc, "Requests");
  cJSON_AddNumberToObject(requests, "FirstSyn",
                          requestCounter.requestsFirstSyn);
  cJSON_AddNumberToObject(requests, "FirstWon",
                          requestCounter.requestsFirstWon);
  cJSON_AddNumberToObject(requests, "FirstRetry",
                          requestCounter.requestsFirstRetry);
  cJSON_AddNumberToObject(requests, "FirstLost",
                          requestCounter.requestsFirstLost);
  cJSON_AddNumberToObject(requests, "FirstError",
                          requestCounter.requestsFirstError);
  cJSON_AddNumberToObject(requests, "RetrySyn",
                          requestCounter.requestsRetrySyn);
  cJSON_AddNumberToObject(requests, "RetryError",
                          requestCounter.requestsRetryError);
  cJSON_AddNumberToObject(requests, "SecondWon",
                          requestCounter.requestsSecondWon);
  cJSON_AddNumberToObject(requests, "SecondLost",
                          requestCounter.requestsSecondLost);
  cJSON_AddNumberToObject(requests, "SecondError",
                          requestCounter.requestsSecondError);

  // Reset
  cJSON* reset = cJSON_AddObjectToObject(doc, "Reset");
  cJSON_AddNumberToObject(reset, "Total", handlerCounter.resetTotal);
  cJSON_AddNumberToObject(reset, "Passive_00", handlerCounter.resetPassive00);
  cJSON_AddNumberToObject(reset, "Passive_0704",
                          handlerCounter.resetPassive0704);
  cJSON_AddNumberToObject(reset, "Passive", handlerCounter.resetPassive);
  cJSON_AddNumberToObject(reset, "Active_00", handlerCounter.resetActive00);
  cJSON_AddNumberToObject(reset, "Active_0704", handlerCounter.resetActive0704);
  cJSON_AddNumberToObject(reset, "Active", handlerCounter.resetActive);

  // Error
  cJSON* error = cJSON_AddObjectToObject(doc, "Error");
  cJSON_AddNumberToObject(error, "Total", handlerCounter.errorTotal);

  // Error Passive
  cJSON* errorPassive = cJSON_AddObjectToObject(error, "Passive");
  cJSON_AddNumberToObject(errorPassive, "Total", handlerCounter.errorPassive);
  cJSON_AddNumberToObject(errorPassive, "Master",
                          handlerCounter.errorPassiveMaster);
  cJSON_AddNumberToObject(errorPassive, "Master_ACK",
                          handlerCounter.errorPassiveMasterACK);
  cJSON_AddNumberToObject(errorPassive, "Slave",
                          handlerCounter.errorPassiveSlave);
  cJSON_AddNumberToObject(errorPassive, "Slave_ACK",
                          handlerCounter.errorPassiveSlaveACK);

  // Error Reactive
  cJSON* errorReactive = cJSON_AddObjectToObject(error, "Reactive");
  cJSON_AddNumberToObject(errorReactive, "Total", handlerCounter.errorReactive);
  cJSON_AddNumberToObject(errorReactive, "Master",
                          handlerCounter.errorReactiveMaster);
  cJSON_AddNumberToObject(errorReactive, "Master_ACK",
                          handlerCounter.errorReactiveMasterACK);
  cJSON_AddNumberToObject(errorReactive, "Slave",
                          handlerCounter.errorReactiveSlave);
  cJSON_AddNumberToObject(errorReactive, "Slave_ACK",
                          handlerCounter.errorReactiveSlaveACK);

  // Error Active
  cJSON* errorActive = cJSON_AddObjectToObject(error, "Active");
  cJSON_AddNumberToObject(errorActive, "Total", handlerCounter.errorActive);
  cJSON_AddNumberToObject(errorActive, "Master",
                          handlerCounter.errorActiveMaster);
  cJSON_AddNumberToObject(errorActive, "Master_ACK",
                          handlerCounter.errorActiveMasterACK);
  cJSON_AddNumberToObject(errorActive, "Slave",
                          handlerCounter.errorActiveSlave);
  cJSON_AddNumberToObject(errorActive, "Slave_ACK",
                          handlerCounter.errorActiveSlaveACK);

  char* printed = cJSON_PrintUnformatted(doc);
  std::string payload = printed != nullptr ? printed : "{}";
  if (printed != nullptr) cJSON_free(printed);
  cJSON_Delete(doc);

  return payload;
}

void Schedule::setPublishTiming(bool enable) { timingEnabled = enable; }

bool Schedule::getPublishTiming() const { return timingEnabled; }

void Schedule::resetTiming() {
  if (ebusBus) ebusBus->resetTiming();
  if (ebusHandler) ebusHandler->resetTiming();
}

void Schedule::publishTiming() {
  if (!timingEnabled) return;

  std::string payload = getTimingJson();
  mqtt.publish("state/timing", 0, false, payload.c_str());
}

const std::string Schedule::getTimingJson() {
  cJSON* doc = cJSON_CreateObject();

  // Timing
  ebus::Bus::Timing busTiming = ebusBus->getTiming();
  ebus::Handler::Timing handlerTiming = ebusHandler->getTiming();

  // Helper lambda to add timing stats to a JsonObject
  auto addTiming = [](cJSON* obj, int64_t last, int64_t mean, int64_t stddev,
                      uint64_t count) {
    cJSON_AddNumberToObject(obj, "Last", static_cast<double>(last));
    cJSON_AddNumberToObject(obj, "Mean", static_cast<double>(mean));
    cJSON_AddNumberToObject(obj, "StdDev", static_cast<double>(stddev));
    cJSON_AddNumberToObject(obj, "Count", static_cast<double>(count));
  };

  cJSON* bus = cJSON_AddObjectToObject(doc, "BusIsr");
  cJSON* busDelay = cJSON_AddObjectToObject(bus, "Delay");
  cJSON* busWindow = cJSON_AddObjectToObject(bus, "Window");
  cJSON* write = cJSON_AddObjectToObject(doc, "Write");
  cJSON* active = cJSON_AddObjectToObject(doc, "Active");
  cJSON* activeFirst = cJSON_AddObjectToObject(active, "First");
  cJSON* activeData = cJSON_AddObjectToObject(active, "Data");
  cJSON* passive = cJSON_AddObjectToObject(doc, "Passive");
  cJSON* passiveFirst = cJSON_AddObjectToObject(passive, "First");
  cJSON* passiveData = cJSON_AddObjectToObject(passive, "Data");
  cJSON* sync = cJSON_AddObjectToObject(doc, "Sync");
  cJSON* callback = cJSON_AddObjectToObject(doc, "Callback");
  cJSON* callbackWon = cJSON_AddObjectToObject(callback, "Won");
  cJSON* callbackLost = cJSON_AddObjectToObject(callback, "Lost");
  cJSON* callbackReactive = cJSON_AddObjectToObject(callback, "Reactive");
  cJSON* callbackTelegram = cJSON_AddObjectToObject(callback, "Telegram");
  cJSON* callbackError = cJSON_AddObjectToObject(callback, "Error");

  addTiming(busDelay, busTiming.busDelay_Last, busTiming.busDelay_Mean,
            busTiming.busDelay_StdDev, busTiming.busDelay_Count);

  addTiming(busWindow, busTiming.busWindow_Last, busTiming.busWindow_Mean,
            busTiming.busWindow_StdDev, busTiming.busWindow_Count);

  addTiming(write, handlerTiming.write_Last, handlerTiming.write_Mean,
            handlerTiming.write_StdDev, handlerTiming.write_Count);

  addTiming(activeFirst, handlerTiming.activeFirst_Last,
            handlerTiming.activeFirst_Mean, handlerTiming.activeFirst_StdDev,
            handlerTiming.activeFirst_Count);

  addTiming(activeData, handlerTiming.activeData_Last,
            handlerTiming.activeData_Mean, handlerTiming.activeData_StdDev,
            handlerTiming.activeData_Count);

  addTiming(passiveFirst, handlerTiming.passiveFirst_Last,
            handlerTiming.passiveFirst_Mean, handlerTiming.passiveFirst_StdDev,
            handlerTiming.passiveFirst_Count);

  addTiming(passiveData, handlerTiming.passiveData_Last,
            handlerTiming.passiveData_Mean, handlerTiming.passiveData_StdDev,
            handlerTiming.passiveData_Count);

  addTiming(sync, handlerTiming.sync_Last, handlerTiming.sync_Mean,
            handlerTiming.sync_StdDev, handlerTiming.sync_Count);

  addTiming(callbackWon, handlerTiming.callbackWon_Last,
            handlerTiming.callbackWon_Mean, handlerTiming.callbackWon_StdDev,
            handlerTiming.callbackWon_Count);

  addTiming(callbackLost, handlerTiming.callbackLost_Last,
            handlerTiming.callbackLost_Mean, handlerTiming.callbackLost_StdDev,
            handlerTiming.callbackLost_Count);

  addTiming(callbackReactive, handlerTiming.callbackReactive_Last,
            handlerTiming.callbackReactive_Mean,
            handlerTiming.callbackReactive_StdDev,
            handlerTiming.callbackReactive_Count);

  addTiming(callbackTelegram, handlerTiming.callbackTelegram_Last,
            handlerTiming.callbackTelegram_Mean,
            handlerTiming.callbackTelegram_StdDev,
            handlerTiming.callbackTelegram_Count);

  addTiming(callbackError, handlerTiming.callbackError_Last,
            handlerTiming.callbackError_Mean, handlerTiming.callbackError_StdDev,
            handlerTiming.callbackError_Count);

  ebus::Handler::StateTiming stateTiming = ebusHandler->getStateTiming();

  // Output handler state timing
  auto addStateTiming = [](cJSON* obj,
                           const ebus::Handler::StateTiming::Timing& timing) {
    cJSON_AddNumberToObject(obj, "Last", static_cast<int64_t>(timing.last));
    cJSON_AddNumberToObject(obj, "Mean", static_cast<int64_t>(timing.mean));
    cJSON_AddNumberToObject(obj, "StdDev", static_cast<int64_t>(timing.stddev));
    cJSON_AddNumberToObject(obj, "Count", timing.count);
  };

  cJSON* handlerState = cJSON_AddObjectToObject(doc, "HandlerState");
  auto addState = [&handlerState, &stateTiming, &addStateTiming](
                      const char* name, ebus::HandlerState state) {
    cJSON* node = cJSON_AddObjectToObject(handlerState, name);
    addStateTiming(node, stateTiming.timing.at(state));
  };

  addState("passiveReceiveMaster", ebus::HandlerState::passiveReceiveMaster);
  addState("passiveReceiveMasterAcknowledge",
           ebus::HandlerState::passiveReceiveMasterAcknowledge);
  addState("passiveReceiveSlave", ebus::HandlerState::passiveReceiveSlave);
  addState("passiveReceiveSlaveAcknowledge",
           ebus::HandlerState::passiveReceiveSlaveAcknowledge);
  addState("reactiveSendMasterPositiveAcknowledge",
           ebus::HandlerState::reactiveSendMasterPositiveAcknowledge);
  addState("reactiveSendMasterNegativeAcknowledge",
           ebus::HandlerState::reactiveSendMasterNegativeAcknowledge);
  addState("reactiveSendSlave", ebus::HandlerState::reactiveSendSlave);
  addState("reactiveReceiveSlaveAcknowledge",
           ebus::HandlerState::reactiveReceiveSlaveAcknowledge);
  addState("requestBus", ebus::HandlerState::requestBus);
  addState("activeSendMaster", ebus::HandlerState::activeSendMaster);
  addState("activeReceiveMasterAcknowledge",
           ebus::HandlerState::activeReceiveMasterAcknowledge);
  addState("activeReceiveSlave", ebus::HandlerState::activeReceiveSlave);
  addState("activeSendSlavePositiveAcknowledge",
           ebus::HandlerState::activeSendSlavePositiveAcknowledge);
  addState("activeSendSlaveNegativeAcknowledge",
           ebus::HandlerState::activeSendSlaveNegativeAcknowledge);
  addState("releaseBus", ebus::HandlerState::releaseBus);

  char* printed = cJSON_PrintUnformatted(doc);
  std::string payload = printed != nullptr ? printed : "{}";
  if (printed != nullptr) cJSON_free(printed);
  cJSON_Delete(doc);

  return payload;
}

void Schedule::taskFunc(void* arg) {
  Schedule* self = static_cast<Schedule*>(arg);
  for (;;) {
    if (self->stopRunner) vTaskDelete(NULL);
    self->handleEventQueue();
    self->handleCommandQueue();
    vTaskDelay(1);  // short delay to yield CPU
  }
}

void Schedule::handleEventQueue() {
  CallbackEvent* event = nullptr;
  while (eventQueue.try_pop(event)) {
    if (event) {
      switch (event->type) {
        case CallbackType::won: {
          logger.debug("Bus request won");
          if (activeCommand) activeCommand->sendAttempts = 1;
        } break;
        case CallbackType::lost: {
          if (activeCommand && activeCommand->busAttempts < 3) {
            activeCommand->busAttempts++;
            activeCommand->queuedCommand.priority = PRIO_INTERNAL;
            enqueueCommand(activeCommand->queuedCommand);
            logger.debug("Bus request retry");
          }
          if (activeCommand && activeCommand->busAttempts >= 3) {
            busRequestFailed++;
            delete activeCommand;
            activeCommand = nullptr;
            logger.warn("Bus request failed");
          }
        } break;
        case CallbackType::telegram: {
          std::string payload = ebus::to_string(event->data.master);
          if (event->data.slave.size() > 0)
            payload += " / " + ebus::to_string(event->data.slave);

          logger.info(payload.c_str());

          deviceManager.collectData(event->data.master, event->data.slave);

          switch (event->data.messageType) {
            case ebus::MessageType::active:
              schedule.processActive(event->mode,
                                     std::vector<uint8_t>(event->data.master),
                                     std::vector<uint8_t>(event->data.slave));
              [[fallthrough]];
              // break;
              // To process fields of active messages with passive definitions.
            case ebus::MessageType::passive:
            case ebus::MessageType::reactive:
              schedule.processPassive(std::vector<uint8_t>(event->data.master),
                                      std::vector<uint8_t>(event->data.slave));
              break;
            case ebus::MessageType::undefined:
            default:
              break;
          }
        } break;
        case CallbackType::error: {
          std::string payload = event->data.message + " : master '" +
                                ebus::to_string(event->data.master) +
                                "' slave '" +
                                ebus::to_string(event->data.slave) + "'";

          logger.warn(payload.c_str());
          // Do not retry fullscan commands on send error
          if (activeCommand &&
              activeCommand->queuedCommand.mode != Mode::fullscan &&
              activeCommand->sendAttempts < 3) {
            activeCommand->sendAttempts++;
            activeCommand->queuedCommand.priority = PRIO_INTERNAL;
            enqueueCommand(activeCommand->queuedCommand);
            logger.debug("Sending retry");
          }
          if (activeCommand &&
              (activeCommand->queuedCommand.mode == Mode::fullscan ||
               activeCommand->sendAttempts >= 3)) {
            sendingFailed++;
            delete activeCommand;
            activeCommand = nullptr;
            logger.warn("Sending failed");
          }
        } break;
      }
      delete event;
    }
  }
}

void Schedule::handleCommandQueue() {
  uint32_t currentMillis = (uint32_t)(esp_timer_get_time() / 1000ULL);

  // Check if activeCommand is stuck
  if (activeCommand && activeCommand->setTime > 0) {
    if ((currentMillis - activeCommand->setTime) >= activeCommandTimeout) {
      delete activeCommand;
      activeCommand = nullptr;
    }
  }

  // Enqueue next schedule command if needed
  if (store.active()) enqueueScheduleCommand();

  // Enqueue startup scan commands if needed
  if (deviceManager.getScanOnStartup()) enqueueStartupScanCommands();

  // Enqueue next full scan command if needed
  if (deviceManager.getFullScan()) enqueueFullScanCommand();

  // Process queue
  if (!ebusHandler->isActiveMessagePending() && !commandQueue.empty() &&
      currentMillis > firstCommandAfterStart && !activeCommand) {
    portENTER_CRITICAL(&commandMux);
    QueuedCommand nextCmd = commandQueue.top();
    commandQueue.pop();
    portEXIT_CRITICAL(&commandMux);

    mode = nextCmd.mode;

    // Track all commands as active
    activeCommand = new ActiveCommand(nextCmd, 1, 1, currentMillis);

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
  if (!cmd || cmd->getReadCmd().empty()) return;

  // Check if this command is already being processed
  if (activeCommand && activeCommand->queuedCommand.mode == Mode::schedule &&
      activeCommand->queuedCommand.scheduleCommand == cmd) {
    return;
  }

  // enqueueCommand will check for duplicates in the queue
  enqueueCommand({Mode::schedule, PRIO_SCHEDULE, cmd->getReadCmd(), cmd});
}

void Schedule::enqueueStartupScanCommands() {
  uint32_t currentMillis = (uint32_t)(esp_timer_get_time() / 1000ULL);
  if (deviceManager.hasNextStartupScan() &&
      currentMillis > lastScan + distanceScans) {
    lastScan = currentMillis;
    distanceScans = 3 * 60 * 1000;
    const auto cmd = deviceManager.nextStartupScanCommand();
    if (!cmd.empty()) enqueueCommand({Mode::scan, PRIO_SCAN, cmd, nullptr});
    handleScanVendor();
  }
}

void Schedule::enqueueFullScanCommand() {
  if ((uint32_t)(esp_timer_get_time() / 1000ULL) >
      lastFullScan + distanceFullScans) {
    lastFullScan = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (deviceManager.hasNextFullScan()) {
      const auto cmd = deviceManager.nextFullScanCommand();
      if (!cmd.empty()) {
        enqueueCommand({Mode::fullscan, PRIO_FULLSCAN, cmd, nullptr});
        return;
      }
    }
    deviceManager.setFullScan(false);
    deviceManager.resetFullScan();
  }
}

void Schedule::reactiveMasterSlaveCallback(const std::vector<uint8_t>& master,
                                           std::vector<uint8_t>* const slave) {
  Device::getIdentification(master, slave);
}

void Schedule::processActive(const Mode& mode,
                             const std::vector<uint8_t>& master,
                             const std::vector<uint8_t>& slave) {
  switch (mode) {
    case Mode::schedule:
      if (activeCommand &&
          activeCommand->queuedCommand.mode == Mode::schedule &&
          activeCommand->queuedCommand.scheduleCommand != nullptr) {
        store.updateData(activeCommand->queuedCommand.scheduleCommand, master,
                         slave);
      }
      break;
    case Mode::internal:
    case Mode::scan:
    case Mode::fullscan:
      // No additional actions needed, just cleanup below
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
  if (activeCommand) {
    delete activeCommand;
    activeCommand = nullptr;
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

  store.updateData(nullptr, master, slave);

  // send Sign of Life in response to an Inquiry of Existence
  if (ebus::contains(master, VEC_07fe00, 2))
    enqueueCommand({Mode::internal, PRIO_INTERNAL, VEC_fe07ff00, nullptr});
}

#endif
