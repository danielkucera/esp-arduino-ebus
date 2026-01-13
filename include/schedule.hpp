#pragma once

#if defined(EBUS_INTERNAL)
#include <ArduinoJson.h>
#include <Ebus.h>
#include <WiFiClient.h>

#include <map>
#include <queue>
#include <string>
#include <vector>

#include "store.hpp"

// Active commands are sent on the eBUS at scheduled intervals, and the received
// data is saved. Passive received messages are compared against defined
// commands, and if they match, the received data is also saved. Furthermore, it
// is possible to send individual commands to the eBUS. The results are returned
// along with the command as raw data. Defined messages (filter function) can be
// forwarded. Scanning of eBUS devices is also available.

constexpr uint8_t VENDOR_VAILLANT = 0xb5;

struct Device {
  uint8_t slave;
  std::vector<uint8_t> vec_070400;
  std::vector<uint8_t> vec_b5090124;
  std::vector<uint8_t> vec_b5090125;
  std::vector<uint8_t> vec_b5090126;
  std::vector<uint8_t> vec_b5090127;

  bool isVaillant() const {
    return (vec_070400.size() > 1 && vec_070400[1] == VENDOR_VAILLANT);
  }

  bool isVaillantValid() const {
    return (vec_b5090124.size() > 0 && vec_b5090125.size() > 0 &&
            vec_b5090126.size() > 0 && vec_b5090127.size() > 0);
  }
};

class Schedule {
 public:
  Schedule() = default;

  void start(ebus::Request* request, ebus::Handler* handler);
  void stop();

  void setSendInquiryOfExistence(const bool enable);
  void setScanOnStartup(const bool enable);
  void setFirstCommandAfterStart(const uint8_t delay);

  void handleScanFull();
  void handleScan();
  void handleScanAddresses(const JsonArrayConst& addresses);
  void handleScanVendor();

  void handleSend(const std::vector<uint8_t>& command);
  void handleSend(const JsonArrayConst& commands);

  void handleWrite(const std::vector<uint8_t>& command);

  void toggleForward(const bool enable);
  void handleForwardFilter(const JsonArrayConst& filters);

  void setPublishCounter(const bool enable);
  void resetCounter();
  void publishCounter();
  const std::string getCounterJson();

  void setPublishTiming(const bool enable);
  void resetTiming();
  void publishTiming();
  const std::string getTimingJson();

  static JsonDocument getDeviceJsonDoc(const Device* device);
  const std::string getDevicesJson() const;

  const std::vector<Device*> getDevices();

 private:
  ebus::Request* ebusRequest = nullptr;
  ebus::Handler* ebusHandler = nullptr;

  volatile bool stopRunner = false;

  bool sendInquiryOfExistence = false;
  bool scanOnStartup = false;

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

  Command* scheduleCommand = nullptr;
  uint32_t scheduleCommandSetTime = 0;  // time when command was scheduled
  uint32_t scheduleCommandTimeout = 1 * 1000;  // 1 second after schedule

  uint32_t distanceScans = 10 * 1000;  // 10 seconds after start
  uint32_t lastScan = 0;               // in milliseconds
  uint8_t maxScans = 5;                // maximum number of scans
  uint8_t currentScan = 0;             // current scan count

  bool fullScan = false;
  uint8_t scanIndex = 0;

  std::map<uint8_t, Device> allDevices;

  bool forward = false;
  std::vector<std::vector<uint8_t>> forwardfilters;

  bool counterEnabled = false;
  bool timingEnabled = false;

  enum class CallbackType { telegram, error };

  struct CallbackEvent {
    CallbackType type;
    Mode mode;
    struct {
      ebus::MessageType messageType;
      ebus::TelegramType telegramType;
      std::vector<uint8_t> master;
      std::vector<uint8_t> slave;
      std::string error;
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

  void processScan(const std::vector<uint8_t>& master,
                   const std::vector<uint8_t>& slave);
};

extern Schedule schedule;
#endif
