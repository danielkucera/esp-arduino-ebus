#pragma once

#include <ArduinoJson.h>
#include <Datatypes.h>
#include <EbusHandler.h>
#include <WiFiClient.h>

#include <deque>
#include <string>
#include <vector>

#include "store.hpp"

// This class sends time-controlled active commands to the ebus. All valid
// received messages are compared with passively defined commands and evaluated
// if they match.Individual commands are transmitted and the results reported
// back. Raw data (including filter function) can also be output.

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

  static void writeCallback(const uint8_t byte);
  static int readBufferCallback();

  static void publishCallback(const ebus::Message message,
                              const std::vector<uint8_t> &master,
                              std::vector<uint8_t> *const slave);

  static void errorCallback(const std::string &str);

  void processActive(const std::vector<uint8_t> &master,
                     const std::vector<uint8_t> &slave);
  void processPassive(const std::vector<uint8_t> &master,
                      const std::vector<uint8_t> &slave);

  static void publishResponse(const std::string &id,
                              const std::vector<uint8_t> &master,
                              const std::vector<uint8_t> &slave);

  static void publishValue(Command *command, const std::vector<uint8_t> &value);
};

extern Schedule schedule;
