#pragma once

#include <WiFiClient.h>

#include <deque>
#include <string>
#include <vector>

#include "Datatypes.h"
#include "EbusHandler.h"
#include "store.hpp"

// This class sends time-controlled active commands to the ebus. All valid
// received messages are compared with passively defined commands and evaluated
// if they match. Raw data (including filter function) can also be output.
// Individual commands are transmitted and the results reported back. The scan
// results are also published.

class Schedule {
 public:
  Schedule();

  void setAddress(const uint8_t source);
  void setDistance(const uint8_t distance);

  void publishRaw(const char *payload);
  void handleFilter(const char *payload);

  void handleSend(const char *payload);

  void nextCommand();
  bool processData(const WiFiClient *client, const uint8_t byte);

  void resetCounters();
  void publishCounters();

 private:
  WiFiClient *dummyClient = new WiFiClient();
  ebus::EbusHandler ebusHandler;

  Command *scheduleCommand = nullptr;

  uint32_t distanceCommands = 0;
  uint32_t lastCommand = 0;

  bool raw = false;
  std::vector<std::vector<uint8_t>> rawFilters;

  bool send = false;
  std::deque<std::vector<uint8_t>> sendCommands;
  std::vector<uint8_t> sendCommand;

  static bool busReadyCallback();
  static void busWriteCallback(const uint8_t byte);

  static void activeCallback(const std::vector<uint8_t> &master,
                             const std::vector<uint8_t> &slave);
  static void passiveCallback(const std::vector<uint8_t> &master,
                              const std::vector<uint8_t> &slave);
  static void reactiveCallback(const std::vector<uint8_t> &master,
                               std::vector<uint8_t> *const slave);

  void processActive(const std::vector<uint8_t>(master),
                     const std::vector<uint8_t> &slave);
  void processPassive(const std::vector<uint8_t> &master,
                      const std::vector<uint8_t> &slave);

  static void publishSend(const std::vector<uint8_t> &master,
                          const std::vector<uint8_t> &slave);

  static void publishValue(Command *command, const std::vector<uint8_t> &value);
};

extern Schedule schedule;
