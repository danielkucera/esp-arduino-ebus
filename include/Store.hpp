#pragma once

#if defined(EBUS_INTERNAL)
#include <ArduinoJson.h>
#include <Ebus.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Command.hpp"

// This Store class stores both active and passive eBUS commands. For permanent
// storage (NVS), functions for saving, loading, and deleting commands are
// available. Permanently stored commands are automatically loaded when the
// device is restarted.

class Store {
 public:
  void insertCommand(const Command& command);
  void removeCommand(const std::string& key);
  Command* findCommand(const std::string& key);

  int64_t loadCommands();
  int64_t saveCommands() const;
  static int64_t wipeCommands();

  const JsonDocument getCommandsJsonDoc() const;
  const std::string getCommandsJson() const;

  const std::vector<Command*> getCommands();

  const size_t getActiveCommands() const;
  const size_t getPassiveCommands() const;

  const bool active() const;

  Command* nextActiveCommand();
  std::vector<Command*> findPassiveCommands(const std::vector<uint8_t>& master);

  std::vector<Command*> updateData(Command* command,
                                   const std::vector<uint8_t>& master,
                                   const std::vector<uint8_t>& slave);

  static const JsonDocument getValueFullJsonDoc(const Command* command);
  static const std::string getValueFullJson(const Command* command);

  const JsonDocument getValuesJsonDoc() const;
  const std::string getValuesJson() const;

 private:
  // Single unified map for all commands, indexed by key
  std::unordered_map<std::string, Command> commands;

  // Flexible serialization/deserialization
  const std::string serializeCommands() const;
  void deserializeCommands(const char* payload);
};

extern Store store;
#endif
