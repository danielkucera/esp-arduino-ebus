#pragma once

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
  Schedule();

  void setAddress(const uint8_t source);
  void setDistance(const uint8_t distance);

  void handleScanFull();
  void handleScan();
  void handleScanAddresses(const JsonArray &addresses);

  void handleSend(const JsonArray &commands);

  void toggleForward(const bool enable);
  void handleForwadFilter(const JsonArray &filters);

  void nextCommand();
  void processData(const uint8_t byte);

  void setPublishCounters(const bool enable);
  void resetCounters();
  void fetchCounters();
  const std::string getCountersJson();

  void setPublishTimings(const bool enable);
  void resetTimings();
  void fetchTimings();
  const std::string getTimingsJson();

  static JsonDocument getParticipantJson(const Participant *participant);
  const std::string getParticipantsJson() const;

  const std::vector<Participant *> getParticipants();

 private:
  ebus::Handler ebusHandler;

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

  bool publishCounters = false;
  bool publishTimings = false;

  static void onWriteCallback(const uint8_t byte);
  static int isDataAvailableCallback();

  static void onTelegramCallback(const ebus::MessageType &messageType,
                                 const ebus::TelegramType &telegramType,
                                 const std::vector<uint8_t> &master,
                                 std::vector<uint8_t> *const slave);

  static void onErrorCallback(const std::string &str);

  void processActive(const std::vector<uint8_t> &master,
                     const std::vector<uint8_t> &slave);

  void processPassive(const std::vector<uint8_t> &master,
                      const std::vector<uint8_t> &slave);

  void processScan(const std::vector<uint8_t> &master,
                   const std::vector<uint8_t> &slave);

  void nextScanCommand();
};

extern Schedule schedule;
