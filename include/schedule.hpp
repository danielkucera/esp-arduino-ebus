#pragma once

#include <ArduinoJson.h>
#include <Datatypes.h>
#include <EbusHandler.h>
#include <WiFiClient.h>

#include <deque>
#include <string>
#include <vector>

#include "store.hpp"

// Active commands are sent on the eBUS at scheduled intervals, and the received
// data is saved. Passive received messages are compared against defined
// commands, and if they match, the received data is also saved. Furthermore, it
// is possible to send individual commands to the eBUS. The results are returned
// along with the command as raw data. Defined messages (filter function) can be
// forwarded.

class Schedule {
 public:
  Schedule();

  void setAddress(const uint8_t source);
  void setDistance(const uint8_t distance);

  void handleSend(const JsonArray &commands);

  void toggleForward(const bool enable);
  void handleForwadFilter(const JsonArray &filters);

  void nextCommand();
  void processData(const uint8_t byte);

  void resetCounters();
  void publishCounters();

 private:
  ebus::EbusHandler ebusHandler;

  Command *scheduleCommand = nullptr;

  uint32_t distanceCommands = 0;
  uint32_t lastCommand = 0;

  bool send = false;
  std::deque<std::vector<uint8_t>> sendCommands;
  std::vector<uint8_t> sendCommand;

  bool forward = false;
  std::vector<std::vector<uint8_t>> forwardfilters;

  static void onWriteCallback(const uint8_t byte);
  static int isDataAvailableCallback();

  static void onTelegramCallback(const ebus::Message &message,
                                 const ebus::Type &type,
                                 const std::vector<uint8_t> &master,
                                 std::vector<uint8_t> *const slave);

  static void onErrorCallback(const std::string &str);

  void processActive(const std::vector<uint8_t> &master,
                     const std::vector<uint8_t> &slave);

  void processPassive(const std::vector<uint8_t> &master,
                      const std::vector<uint8_t> &slave);
};

extern Schedule schedule;
