#pragma once

#if defined(EBUS_INTERNAL)
#include <ArduinoJson.h>
#include <Ebus.h>

#include <map>
#include <queue>
#include <string>
#include <vector>

#include "Command.hpp"

// Active commands are sent on the eBUS at scheduled intervals, and the received
// data is saved. Passive received messages are compared against defined
// commands, and if they match, the received data is also saved. Furthermore, it
// is possible to send individual commands to the eBUS. The results are returned
// along with the command as raw data. Defined messages (filter function) can be
// forwarded. Scanning of eBUS devices is also available.

class Schedule {
 public:
  Schedule() = default;

  void start(ebus::Request* request, ebus::Handler* handler);
  void stop();

  void setSendInquiryOfExistence(const bool enable);
  void setFirstCommandAfterStart(const uint8_t delay);

  void handleScanFull();
  void handleScan();
  void handleScanAddresses(const JsonArrayConst& addresses);
  void handleScanVendor();

  void handleSend(const std::vector<uint8_t>& command);
  void handleSend(const JsonArrayConst& commands);

  void handleWrite(const std::vector<uint8_t>& command);

  void toggleForward(bool enable);
  void handleForwardFilter(const JsonArrayConst& filters);

  void setPublishCounter(bool enable);
  const bool getPublishCounter() const;
  void resetCounter();
  void publishCounter();
  const std::string getCounterJson();

  void setPublishTiming(bool enable);
  const bool getPublishTiming() const;
  void resetTiming();
  void publishTiming();
  const std::string getTimingJson();

 private:
  ebus::Request* ebusRequest = nullptr;
  ebus::Handler* ebusHandler = nullptr;

  volatile bool stopRunner = false;

  bool sendInquiryOfExistence = false;

  uint32_t firstCommandAfterStart = 10 * 1000;  // 10 seconds after start

  enum class Mode { schedule, internal, scan, fullscan, send, read, write };
  Mode mode = Mode::schedule;

  struct QueuedCommand {
    Mode mode;
    uint8_t priority;  // higher = higher priority
    std::vector<uint8_t> command;
    Command* scheduleCommand = nullptr;

    QueuedCommand(Mode m, uint8_t p, std::vector<uint8_t> cmd,
                  Command* scheduleCmd)
        : mode(m), priority(p), command(cmd), scheduleCommand(scheduleCmd) {}
  };

  struct CommandComparator {
    bool operator()(const QueuedCommand& lhs, const QueuedCommand& rhs) const {
      return lhs.priority < rhs.priority;
    }
  };

  std::priority_queue<QueuedCommand, std::vector<QueuedCommand>,
                      CommandComparator>
      commandQueue;

  struct ActiveCommand {
    QueuedCommand queuedCommand;
    uint8_t busAttempts;   // number of bus attempts
    uint8_t sendAttempts;  // number of send attempts
    uint32_t setTime;      // time when the command was set
    ActiveCommand(const QueuedCommand& qc, uint8_t busAtt, uint8_t sendAtt,
                  uint32_t st)
        : queuedCommand(qc),
          busAttempts(busAtt),
          sendAttempts(sendAtt),
          setTime(st) {}
  };

  ActiveCommand* activeCommand = nullptr;
  uint32_t activeCommandTimeout = 1 * 1000;  // 1 second after schedule

  uint32_t distanceScans = 10 * 1000;  // 10 seconds after start
  uint32_t lastScan = 0;               // in milliseconds

  uint32_t distanceFullScans = 500;  // 500 milliseconds between 2 scans
  uint32_t lastFullScan = 0;         // in milliseconds

  uint32_t busRequestFailed = 0;
  uint32_t sendingFailed = 0;

  bool forward = false;
  std::vector<std::vector<uint8_t>> forwardfilters;

  bool counterEnabled = false;
  bool timingEnabled = false;

  enum class CallbackType { won, lost, telegram, error };

  struct CallbackEvent {
    CallbackType type;
    Mode mode;
    struct {
      ebus::MessageType messageType;
      ebus::TelegramType telegramType;
      std::vector<uint8_t> master;
      std::vector<uint8_t> slave;
      std::string message;
    } data;
  };

  ebus::Queue<CallbackEvent*> eventQueue{8};

  TaskHandle_t scheduleTaskHandle;

  static void taskFunc(void* arg);

  void handleEventQueue();

  void handleCommandQueue();

  void enqueueCommand(const QueuedCommand& cmd);

  void enqueueScheduleCommand();

  void enqueueStartupScanCommands();

  void enqueueFullScanCommand();

  static void reactiveMasterSlaveCallback(const std::vector<uint8_t>& master,
                                          std::vector<uint8_t>* const slave);

  void processActive(const Mode& mode, const std::vector<uint8_t>& master,
                     const std::vector<uint8_t>& slave);

  void processPassive(const std::vector<uint8_t>& master,
                      const std::vector<uint8_t>& slave);

  void logTelegram(const std::vector<uint8_t>& master,
                   const std::vector<uint8_t>& slave,
                   const Command* cmd = nullptr);
};

extern Schedule schedule;
#endif
