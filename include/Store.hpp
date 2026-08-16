#pragma once

#if defined(EBUS_INTERNAL)
#include <ebus.hpp>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Command.hpp"

// Forward declaration for custom hash
struct KeyFSHash;

struct KeyFSEqual {
  bool operator()(const command_types::KeyFS& a,
                  const command_types::KeyFS& b) const {
    return std::string_view(a) == std::string_view(b);
  }
};

struct KeyFSHash {
  size_t operator()(const command_types::KeyFS& fs) const {
    return std::hash<std::string_view>{}(std::string_view(fs));
  }
};

// This Store class stores both active and passive eBUS commands. For permanent
// storage (SPIFFS JSON file), functions for saving, loading, and deleting
// commands are
// available. Permanently stored commands are automatically loaded when the
// device is restarted.

using DataUpdatedCallback = std::function<void(std::string_view key)>;
using DataUpdatedLogCallback = std::function<void(std::string_view key)>;

using CommandChangedCallback = std::function<void(Command* command)>;

using MatchingCommands = ebus::StaticVector<Command*, 16>;

class Store {
 public:
  bool initFileSystem();

  void setDataUpdatedCallback(DataUpdatedCallback callback);
  void setDataUpdatedLogCallback(DataUpdatedLogCallback callback);

  void setCommandChangedCallback(CommandChangedCallback callback);
  void setCommandRemovedCallback(CommandChangedCallback callback);

  void insertCommand(const Command& command);
  void removeCommand(std::string_view key);
  Command* findCommand(std::string_view key);
  Command* findCommand(uint32_t poll_id);
  MatchingCommands findAllMatchingCommands(ebus::ByteView master);

  int64_t loadCommands();
  int64_t loadCommandsFrom(const char* path);
  int64_t saveCommands() const;
  int64_t wipeCommands();

  //   const std::string getCommandsJson() const;
  void fetchCommands(const ebus::JsonChunkVisitor& visitor) const;

  const std::vector<Command*> getCommands();

  size_t getActiveCommands() const;
  size_t getPassiveCommands() const;
  size_t getCommandCount() const;

  bool active() const;

  Command* nextActiveCommand();
  MatchingCommands findPassiveCommands(ebus::ByteView master);

  void updateData(Command* command, ebus::ByteView master_view,
                  ebus::ByteView slave_view);

  static const std::string getValueFullJson(const Command* command);

  //   const std::string getValuesJson() const;
  void fetchValues(const ebus::JsonChunkVisitor& visitor) const;

 private:
  // Single unified map for all commands, indexed by key
  mutable std::recursive_mutex mutex_;
  std::unordered_map<command_types::KeyFS, Command, KeyFSHash, KeyFSEqual>
      commands_;

  DataUpdatedCallback data_updated_callback_ = nullptr;
  DataUpdatedLogCallback data_updated_log_callback_ = nullptr;

  CommandChangedCallback command_changed_callback_ = nullptr;
  CommandChangedCallback command_removed_callback_ = nullptr;

  // Flexible serialization/deserialization
  void deserializeCommands(FILE* file);
};

extern Store store;
#endif
