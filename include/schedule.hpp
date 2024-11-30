#ifndef _SCHEDULE_H_
#define _SCHEDULE_H_

#include <WiFiClient.h>

#include <string>
#include <vector>

#include "Datatypes.h"
#include "EbusHandler.h"

// Implementation of the perodic sending of predefined commands.

struct Command {
  std::string key;               // ebus command as string
  std::vector<uint8_t> command;  // ebus command as vector ZZ PB SB NN DBx
  std::string unit;              // unit of the received data
  bool active;                   // active sending of command
  uint32_t interval;        // minimum interval between two commands in seconds
  uint32_t last;            // last time of the successful command
  bool master;              // true..master false..slave
  size_t position;          // starting byte in payload (DBx)
  ebus::Datatype datatype;  // ebus datatype
  std::string topic;        // mqtt topic
  bool ha;                  // home assistant support for auto discovery
  std::string ha_class;     // home assistant device_class
};

class Schedule {
 public:
  Schedule();

  void setAddress(const uint8_t source);
  void setDistance(const uint8_t distance);

  void insertCommand(const char *payload);
  void removeCommand(const char *topic);

  void publishCommands() const;
  const char *printCommands() const;

  void publishRaw(const char *payload);
  void handleFilter(const char*payload);

  bool needTX();

  void processSend();
  bool processReceive(bool enhanced, WiFiClient *client, const uint8_t byte);

  void resetCounters();
  void publishCounters();

 private:
  uint8_t address = 0xff;

  std::vector<Command> activeCommands;
  std::vector<Command> passiveCommands;

  WiFiClient *dummyClient = new WiFiClient();
  ebus::EbusHandler ebusHandler;

  bool initCounters = true;
  ebus::Counter lastCounters;

  Command *actCommand = nullptr;

  uint32_t distanceCommands = 0;
  uint32_t lastCommand = 0;

  bool initDone = false;

  bool raw = false;
  std::vector<std::vector<uint8_t>> rawFilters;

  void publishCommand(const std::vector<Command> *commands,
                      const std::string key, bool remove) const;
  void publishHomeAssistant(const Command *commands, bool remove) const;

  const std::vector<uint8_t> nextActiveCommand();

  static bool busReadyCallback();
  static void busWriteCallback(const uint8_t byte);
  static void responseCallback(const std::vector<uint8_t> slave);
  static void telegramCallback(const std::vector<uint8_t> master,
                               const std::vector<uint8_t> slave);

  void processResponse(const std::vector<uint8_t> slave);
  void processTelegram(const std::vector<uint8_t> master,
                       const std::vector<uint8_t> slave);

  void publishValue(Command *command, const std::vector<uint8_t> value);
};

extern Schedule schedule;

#endif  // _SCHEDULE_H_
