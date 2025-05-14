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
  std::string unit;              // unit of the interested part
  bool active;                   // active sending of command
  uint32_t interval;  // minimum interval between two commands in seconds
  uint32_t last;      // last time of the successful command (INTERNAL)
  std::vector<uint8_t> data;  // received raw data (INTERNAL)
  bool master;                // value of interest is in master or slave part
  size_t position;            // starting position in the interested part
  ebus::Datatype datatype;    // ebus datatype
  size_t length;              // length of interested part (INTERNAL)
  bool numeric;               // indicate numeric types (INTERNAL)
  float divider;              // divider for value conversion
  uint8_t digits;             // deciaml digits of value
  std::string topic;          // mqtt topic below "values/"
  bool ha;                    // home assistant support for auto discovery
  std::string ha_class;       // home assistant device_class
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

  static JsonDocument getCommandJson(const Command *command);
  const std::string getCommandsJson() const;

  const std::vector<Command *> getCommands();

  const size_t getActiveCommands() const;
  const size_t getPassiveCommands() const;

  const bool active() const;

  Command *nextActiveCommand();
  std::vector<Command *> findPassiveCommands(
      const std::vector<uint8_t> &master);

  std::vector<Command *> updateData(Command *command,
                                    const std::vector<uint8_t> &master,
                                    const std::vector<uint8_t> &slave);

  static JsonDocument getValueJson(const Command *command);

  const std::string getValuesJson() const;

 private:
  std::vector<Command> allCommands;

  size_t activeCommands = 0;
  size_t passiveCommands = 0;

  void countCommands();

  const std::string serializeCommands() const;
  void deserializeCommands(const char *payload);

  static const double getValueDouble(const Command *command);
  static const std::string getValueString(const Command *command);
};

extern Store store;
