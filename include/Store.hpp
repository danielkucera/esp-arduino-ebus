#pragma once

#if defined(EBUS_INTERNAL)
#include <cJSON.h>

#include <ebus.hpp>
#include <functional>
#include <mutex>
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

using CommandChangedCallback = std::function<void(Command* command)>;

class Store {
 public:
  bool initFileSystem();

  void setDataUpdatedCallback(DataUpdatedCallback callback);
  void setDataUpdatedLogCallback(DataUpdatedLogCallback callback);

  void setCommandChangedCallback(CommandChangedCallback callback);
  void setCommandRemovedCallback(CommandChangedCallback callback);

  void insertCommand(const Command& command);
  void removeCommand(const std::string& key);
  Command* findCommand(const std::string& key);
  std::vector<Command*> findAllMatchingCommands(ebus::ByteView master);

  int64_t loadCommands();
  int64_t saveCommands() const;
  int64_t wipeCommands();

  const std::string getCommandsJson() const;

  const std::vector<Command*> getCommands();

  size_t getActiveCommands() const;
  size_t getPassiveCommands() const;

  bool active() const;

  Command* nextActiveCommand();
  std::vector<Command*> findPassiveCommands(ebus::ByteView master);

  std::vector<Command*> updateData(Command* command, ebus::ByteView master_view,
                                   ebus::ByteView slave_view);

  static const std::string getValueFullJson(const Command* command);

  const std::string getValuesJson() const;

 private:
  // Single unified map for all commands, indexed by key
  mutable std::recursive_mutex mutex_;
  std::unordered_map<std::string, Command> commands_;

  DataUpdatedCallback data_updated_callback_ = nullptr;
  DataUpdatedLogCallback data_updated_log_callback_ = nullptr;

  CommandChangedCallback command_changed_callback_ = nullptr;
  CommandChangedCallback command_removed_callback_ = nullptr;

  // Flexible serialization/deserialization
  const std::string serializeCommands() const;
  void deserializeCommands(const char* payload);
};

extern Store store;
#endif
