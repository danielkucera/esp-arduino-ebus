#pragma once

#include <Datatypes.h>

#include <deque>
#include <string>
#include <vector>

// Implements the storage of active and passive commands. New commands can be
// added and removed via mqtt. It provides functions for saving, loading and
// deleting commands in the nvs memory. Commands stored in the nvs are loaded at
// startup.

struct Command {
  std::string key;               // ebus command as string
  std::vector<uint8_t> command;  // ebus command as vector of "ZZPBSBNNDBx"
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

class Store {
 public:
  Store() = default;

  void enqueCommand(const char *payload);
  void insertCommand(const char *payload);
  void removeCommand(const char *payload);

  void publishCommands();
  const std::string getCommands() const;

  void doLoop();

  const bool active() const;
  Command *nextActiveCommand();
  std::vector<Command *> findPassiveCommands(
      const std::vector<uint8_t> &master);

  void loadCommands();
  void saveCommands() const;
  static void wipeCommands();

 private:
  std::vector<Command> allCommands;

  size_t activeCommands = 0;
  size_t passiveCommands = 0;

  std::deque<std::string> newCommands;
  uint32_t distanceInsert = 300;
  uint32_t lastInsert = 0;

  std::deque<const Command *> pubCommands;
  uint32_t distancePublish = 100;
  uint32_t lastPublish = 0;

  void countCommands();

  void checkNewCommands();
  void checkPubCommands();

  const std::string serializeCommands() const;
  void deserializeCommands(const char *payload);

  static void publishCommand(const Command *command, const bool remove);

  static void publishHomeAssistant(const Command *command, const bool remove);
};

extern Store store;
