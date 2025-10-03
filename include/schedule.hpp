#pragma once

#if defined(EBUS_INTERNAL)
#include <ArduinoJson.h>
#include <Ebus.h>
#include <WiFiClient.h>

#include <deque>
#include <map>
#include <string>
#include <vector>

#include "store.hpp"

// Active commands are sent on the eBUS at scheduled intervals, and the received
// data is saved. Passive received messages are compared against defined
// commands, and if they match, the received data is also saved. Furthermore, it
// is possible to send individual commands to the eBUS. The results are returned
// along with the command as raw data. Defined messages (filter function) can be
// forwarded. Scanning of eBUS participants is also available.

constexpr uint8_t VENDOR_VAILLANT = 0xb5;

struct Participant {
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

  void start(ebus::Request *request, ebus::Handler *handler);

  void stop();

  void setDistance(const uint8_t distance);

  void handleScanFull();
  void handleScan();
  void handleScanAddresses(const JsonArray &addresses);
  void handleScanVendor();

  void handleSend(const JsonArray &commands);

  void toggleForward(const bool enable);
  void handleForwardFilter(const JsonArray &filters);

  void setPublishCounter(const bool enable);
  void resetCounter();
  void fetchCounter();
  const std::string getCounterJson();

  void setPublishTiming(const bool enable);
  void resetTiming();
  void fetchTiming();
  const std::string getTimingJson();

  static JsonDocument getParticipantJson(const Participant *participant);
  const std::string getParticipantsJson() const;

  const std::vector<Participant *> getParticipants();

 private:
  ebus::Request *ebusRequest = nullptr;
  ebus::Handler *ebusHandler = nullptr;

  volatile bool stopRunner = false;

  bool sendInquiryOfExistence = true;

  enum class Mode { scan, send, normal };
  Mode mode = Mode::normal;

  Command *scheduleCommand = nullptr;

  uint32_t distanceCommands = 0;     // in milliseconds
  uint32_t lastCommand = 10 * 1000;  // 10 seconds after start

  uint32_t distanceScans = 11 * 1000;  // 11 seconds after start
  uint32_t lastScan = 0;               // in milliseconds
  uint8_t maxScans = 5;                // maximum number of scans
  uint8_t currentScan = 0;             // current scan count

  bool fullScan = false;
  uint8_t scanIndex = 0;

  std::map<uint8_t, Participant> allParticipants;

  std::deque<std::vector<uint8_t>> scanCommands;
  std::deque<std::vector<uint8_t>> sendCommands;

  bool forward = false;
  std::vector<std::vector<uint8_t>> forwardfilters;

  bool publishCounter = false;
  bool publishTiming = false;

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

  ebus::Queue<CallbackEvent *> eventQueue{8};

  TaskHandle_t scheduleTaskHandle;

  static void taskFunc(void *arg);

  void handleEvents();

  void nextCommand();

  void nextScanCommand();

  static void reactiveMasterSlaveCallback(const std::vector<uint8_t> &master,
                                          std::vector<uint8_t> *const slave);

  void processActive(const Mode &mode, const std::vector<uint8_t> &master,
                     const std::vector<uint8_t> &slave);

  void processPassive(const std::vector<uint8_t> &master,
                      const std::vector<uint8_t> &slave);

  void processScan(const std::vector<uint8_t> &master,
                   const std::vector<uint8_t> &slave);
};

extern Schedule schedule;
#endif
