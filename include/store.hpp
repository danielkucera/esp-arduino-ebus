#pragma once

#include <ArduinoJson.h>
#include <Datatypes.h>

#include <string>
#include <vector>

// This Store class stores both active and passive eBUS commands. For permanent
// storage (NVS), functions for saving, loading, and deleting commands are
// available. Permanently stored commands are automatically loaded when the
// device is restarted.

struct Command {
  std::string key;               // unique key of command
  std::vector<uint8_t> command;  // ebus command as vector of "ZZPBSBNNDBx"
  std::string unit;              // unit of the received data
  bool active;                   // active sending of command
  uint32_t interval;        // minimum interval between two commands in seconds
  uint32_t last;            // last time of the successful command
  bool master;              // value of interest is in master or slave part
  size_t position;          // starting byte in interested part
  ebus::Datatype datatype;  // ebus datatype
  std::string topic;        // mqtt topic below "values/"
  bool ha;                  // home assistant support for auto discovery
  std::string ha_class;     // home assistant device_class
};

class Store {
 public:
  Store() = default;

  Command createCommand(const JsonDocument &doc);

  void insertCommand(const Command &command);
  void removeCommand(const std::string &key);
  const Command *findCommand(const std::string &key);

  int64_t loadCommands();
  int64_t saveCommands() const;
  static int64_t wipeCommands();

  const std::string getCommandsJson() const;
  const std::vector<Command *> getCommands();

  const size_t getActiveCommands() const;
  const size_t getPassiveCommands() const;

  const bool active() const;

  Command *nextActiveCommand();
  std::vector<Command *> findPassiveCommands(
      const std::vector<uint8_t> &master);

 private:
  std::vector<Command> allCommands;

  size_t activeCommands = 0;
  size_t passiveCommands = 0;

  void countCommands();

  const std::string serializeCommands() const;
  void deserializeCommands(const char *payload);
};

extern Store store;
