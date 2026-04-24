#pragma once

#if defined(EBUS_INTERNAL)
#include <ebus.hpp>
#include <cJSON.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Command.hpp"

// This Store class stores both active and passive eBUS commands. For permanent
// storage (SPIFFS JSON file), functions for saving, loading, and deleting
// commands are
// available. Permanently stored commands are automatically loaded when the
// device is restarted.

using DataUpdatedCallback =
    std::function<void(const std::string& name, const std::string& valueJson)>;

using DataUpdatedLogCallback = std::function<void(const std::string& message)>;

class Store {
 public:
  bool initFileSystem();

  void setDataUpdatedCallback(DataUpdatedCallback callback);
  void setDataUpdatedLogCallback(DataUpdatedLogCallback callback);

  void insertCommand(const Command& command);
  void removeCommand(const std::string& key);
  Command* findCommand(const std::string& key);

  int64_t loadCommands();
  int64_t saveCommands() const;
  static int64_t wipeCommands();

  const std::string getCommandsJson() const;

  const std::vector<Command*> getCommands();

  size_t getActiveCommands() const;
  size_t getPassiveCommands() const;

  bool active() const;

  Command* nextActiveCommand();
  std::vector<Command*> findPassiveCommands(ebus::ByteView master);

  std::vector<Command*> updateData(Command* command,
                                   ebus::ByteView master_view,
                                   ebus::ByteView slave_view);

  static const std::string getValueFullJson(const Command* command);

  const std::string getValuesJson() const;

 private:
  // Single unified map for all commands, indexed by key
  std::unordered_map<std::string, Command> commands;

  DataUpdatedCallback dataUpdatedCallback = nullptr;
  DataUpdatedLogCallback dataUpdatedLogCallback = nullptr;

  // Flexible serialization/deserialization
  const std::string serializeCommands() const;
  void deserializeCommands(const char* payload);
};

extern Store store;
#endif
