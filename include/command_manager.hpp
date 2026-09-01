#pragma once

#if defined(EBUS_INTERNAL)
#include <ebus.hpp>
#include <ebus/types.hpp>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "command.hpp"

#ifndef COMMAND_CAPACITY
inline constexpr size_t command_capacity = 64;
#else
inline constexpr size_t command_capacity = COMMAND_CAPACITY;
#endif

#ifndef WRITE_CMD_CAPACITY
inline constexpr size_t write_cmd_capacity = 16;
#else
inline constexpr size_t write_cmd_capacity = WRITE_CMD_CAPACITY;
#endif

#ifndef OVERRIDE_FIELD_CAPACITY
inline constexpr size_t override_field_capacity = 16;
#else
inline constexpr size_t override_field_capacity = OVERRIDE_FIELD_CAPACITY;
#endif

using DataUpdatedCallback = std::function<void(std::string_view key)>;
using DataUpdatedLogCallback = std::function<void(std::string_view key)>;

using CommandChangedCallback = std::function<void(Command* command)>;

using MatchingCommands = ebus::StaticVector<Command*, 16>;

struct FieldOverride {
  uint8_t key_id = 0;
  uint8_t field_idx = 0;
  float min_override = std::numeric_limits<float>::quiet_NaN();
  float max_override = std::numeric_limits<float>::quiet_NaN();
};

class CommandManager {
 public:
  static bool initFileSystem();

  void setDataUpdatedCallback(DataUpdatedCallback callback);
  void setDataUpdatedLogCallback(DataUpdatedLogCallback callback);

  void setCommandChangedCallback(CommandChangedCallback callback);
  void setCommandRemovedCallback(CommandChangedCallback callback);

  void insertCommand(Command command);
  void removeCommand(std::string_view key);
  void removeAll();

  Command* findCommand(std::string_view key);
  Command* findCommand(uint16_t poll_id);
  MatchingCommands findPassiveCommands(ebus::ByteView master);

  int64_t loadCommands();
  int64_t loadCommandsFrom(const char* path);
  int64_t saveCommands() const;
  int64_t wipeCommands();

  void fetchCommands(const ebus::JsonChunkVisitor& visitor) const;

  template <typename Func>
  void forEachCommand(Func&& func) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (size_t i = 0; i < commands_.size(); i++) {
      func(&commands_[i]);
    }
  }

  size_t getActiveCommands() const;
  size_t getPassiveCommands() const;
  size_t getCommandCount() const;

  Command* nextActiveCommand();

  void updateData(const ebus::ProtocolInfo& info);

  void fetchValues(const ebus::JsonChunkVisitor& visitor) const;

  // Write command storage (separate from Command to save memory)
  ebus::ByteView getWriteCmd(size_t idx) const;
  bool addWriteCmd(PollSequence&& cmd);
  size_t getWriteCmdCount() const;

  // Min/Max override storage (separate from Command to save memory)
  float getFieldMinOverride(uint8_t key_id, size_t field_idx) const;
  float getFieldMaxOverride(uint8_t key_id, size_t field_idx) const;
  void setFieldMinOverride(uint8_t key_id, size_t field_idx, float min_val);
  void setFieldMaxOverride(uint8_t key_id, size_t field_idx, float max_val);
  void removeFieldOverrides(uint8_t key_id);
  void clearFieldOverrides();

 private:
  mutable std::recursive_mutex mutex_;

  ebus::StaticVector<Command, command_capacity> commands_;
  ebus::StaticVector<PollSequence, write_cmd_capacity> write_cmds_;
  ebus::StaticVector<FieldOverride, override_field_capacity> field_overrides_;

  DataUpdatedCallback data_updated_callback_ = nullptr;
  DataUpdatedLogCallback data_updated_log_callback_ = nullptr;

  CommandChangedCallback command_changed_callback_ = nullptr;
  CommandChangedCallback command_removed_callback_ = nullptr;

  void deserializeCommands(FILE* file);
};

extern CommandManager commandManager;
#endif
