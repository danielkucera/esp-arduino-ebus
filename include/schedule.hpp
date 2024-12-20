#ifndef INCLUDE_SCHEDULE_HPP_
#define INCLUDE_SCHEDULE_HPP_

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

class Schedule {
 public:
  Schedule();

  void setAddress(const uint8_t source);
  void setDistance(const uint8_t distance);

  void publishRaw(const char *payload);
  void handleFilter(const char *payload);

  void processSend();
  bool processReceive(bool enhanced, const WiFiClient *client,
                      const uint8_t byte);

  void resetCounters();
  void publishCounters();

 private:
  WiFiClient *dummyClient = new WiFiClient();
  ebus::EbusHandler ebusHandler;

  Command *actCommand = nullptr;

  uint32_t distanceCommands = 0;
  uint32_t lastCommand = 0;

  bool raw = false;
  std::vector<std::vector<uint8_t>> rawFilters;

  static bool busReadyCallback();
  static void busWriteCallback(const uint8_t byte);
  static void responseCallback(const std::vector<uint8_t> &slave);
  static void telegramCallback(const std::vector<uint8_t> &master,
                               const std::vector<uint8_t> &slave);

  void processResponse(const std::vector<uint8_t> &slave);
  void processTelegram(const std::vector<uint8_t> &master,
                       const std::vector<uint8_t> &slave);

  static void publishValue(Command *command, const std::vector<uint8_t> &value);
};

extern Schedule schedule;

#endif  // INCLUDE_SCHEDULE_HPP_
