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

struct Participant {
  uint8_t slave;
  std::vector<uint8_t> scan070400;
  std::vector<uint8_t> scanb5090124;
  std::vector<uint8_t> scanb5090125;
  std::vector<uint8_t> scanb5090126;
  std::vector<uint8_t> scanb5090127;
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

  void handleSend(const JsonArray &commands);

  void toggleForward(const bool enable);
  void handleForwadFilter(const JsonArray &filters);

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

  Command *scheduleCommand = nullptr;

  uint32_t distanceCommands = 0;
  uint32_t lastCommand = 0;

  std::map<uint8_t, Participant> allParticipants;

  enum class Mode { scan, send, normal };
  Mode mode = Mode::normal;

  bool fullScan = false;
  uint8_t scanIndex = 0;

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
