#if defined(EBUS_INTERNAL)
#include "Store.hpp"

#include <esp_littlefs.h>
#include <esp_timer.h>
#include <sys/stat.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>
#include <ebus/detail/protocol_limits.hpp>

#include "Logger.hpp"
#include "Mqtt.hpp"

Store store;

namespace {
constexpr const char* kLittlefsBasePath = "/littlefs";
constexpr const char* kLittlefsPartitionLabel = "littlefs";
constexpr const char* kCommandsFilePath = "/littlefs/commands.json";

bool ensureLittlefsMounted() {
  static bool mounted = false;
  if (mounted) return true;

  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = kLittlefsBasePath;
  conf.partition_label = kLittlefsPartitionLabel;
  conf.partition = nullptr;
  conf.format_if_mount_failed = true;
  conf.read_only = false;
  conf.dont_mount = false;
  conf.grow_on_mount = false;

  esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
    mounted = true;
    return true;
  }

  return false;
}

}  // namespace

bool Store::initFileSystem() { return ensureLittlefsMounted(); }

void Store::setDataUpdatedCallback(DataUpdatedCallback callback) {
  data_updated_callback_ = std::move(callback);
}

void Store::setDataUpdatedLogCallback(DataUpdatedLogCallback callback) {
  data_updated_log_callback_ = std::move(callback);
}

void Store::setCommandChangedCallback(CommandChangedCallback callback) {
  command_changed_callback_ = std::move(callback);
}

void Store::setCommandRemovedCallback(CommandChangedCallback callback) {
  command_removed_callback_ = std::move(callback);
}

void Store::insertCommand(const Command& command) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  // Insert or update in commands map
  auto it = commands_.find(command.getKey());
  if (it != commands_.end()) {
    it->second = command;
  } else {
    it = commands_.insert(std::make_pair(command.getKey(), command)).first;
  }

  if (command_changed_callback_) command_changed_callback_(&it->second);
}

void Store::removeCommand(const std::string& key) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it = commands_.find(key);
  if (it != commands_.end()) {
    if (command_removed_callback_) command_removed_callback_(&it->second);
    commands_.erase(it);
  }
}

Command* Store::findCommand(const std::string& key) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it = commands_.find(key);
  if (it != commands_.end())
    return &(it->second);
  else
    return nullptr;
}

Command* Store::findCommand(uint32_t poll_id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (auto& kv : commands_) {
    Command* cmd = &kv.second;
    if (cmd->getPollId() == poll_id) {
      return cmd;
    }
  }
  return nullptr;
}

std::vector<Command*> Store::findAllMatchingCommands(ebus::ByteView master) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::vector<Command*> result;
  for (auto& kv : commands_) {
    Command* cmd = &kv.second;
    if (cmd->matches(master)) {
      result.push_back(cmd);
    }
  }
  return result;
}

int64_t Store::loadCommands() {
  if (!ensureLittlefsMounted()) return -1;

  FILE* file = std::fopen(kCommandsFilePath, "rb");
  if (file == nullptr) {
    if (errno == ENOENT) return 0;
    logger.error("Store: Failed to open commands file: " +
                 std::to_string(errno));
    return -1;
  }

  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    logger.error("Store: Failed to seek end of commands file");
    return -1;
  }

  long size = std::ftell(file);
  if (size <= 2) {
    std::fclose(file);
    return 0;
  }

  if (size <= 0 || std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    logger.error("Store: Failed to seek start of commands file");
    return -1;
  }

  std::string payload;
  payload.resize(static_cast<size_t>(size));
  size_t bytesRead = std::fread(payload.data(), 1, payload.size(), file);
  std::fclose(file);
  if (bytesRead != payload.size()) return -1;

  logger.info("Store: Loading commands from LittleFS (" + std::to_string(size) +
              " bytes)");
  deserializeCommands(payload.c_str());
  return static_cast<int64_t>(payload.size());
}

int64_t Store::saveCommands() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!ensureLittlefsMounted()) return -1;
  if (commands_.empty()) return 0;

  FILE* file = std::fopen(kCommandsFilePath, "wb");
  if (file == nullptr) return -1;

  size_t bytes_written = 0;
  ebus::detail::JsonWriter writer([file, &bytes_written](std::string_view s) {
    bytes_written += std::fwrite(s.data(), 1, s.size(), file);
  });

  writer.startArray();

  // Header row for compressed format
  writer.startArray();
  static const char* header[] = {"key",
                                 "name",
                                 "read_cmd",
                                 "write_cmd",
                                 "active",
                                 "interval",
                                 "master",
                                 "position",
                                 "datatype",
                                 "divider",
                                 "min",
                                 "max",
                                 "digits",
                                 "unit",
                                 "ha",
                                 "ha_component",
                                 "ha_device_class",
                                 "ha_entity_category",
                                 "ha_mode",
                                 "ha_key_value_map",
                                 "ha_default_key",
                                 "ha_payload_on",
                                 "ha_payload_off",
                                 "ha_state_class",
                                 "ha_step"};
  for (const char* h : header) writer.writeValue(h);
  writer.endArray();

  // Data rows in tabular format
  for (const auto& kv : commands_) {
    const Command& c = kv.second;
    writer.startArray();
    writer.writeValue(c.getKey());
    writer.writeValue(c.getName());
    writer.writeHexValue(c.getReadCmd());
    writer.writeHexValue(c.getWriteCmd());
    writer.writeValue(c.getActive());
    writer.writeValue(c.getInterval());
    writer.writeValue(c.getMaster());
    writer.writeValue(c.getPosition());
    writer.writeValue(ebus::dataTypeToString(c.getDatatype()));
    writer.writeValueFloat(c.getDivider());
    writer.writeValueFloat(c.getMin());
    writer.writeValueFloat(c.getMax());
    writer.writeValue(c.getDigits());
    writer.writeValue(c.getUnit());
    writer.writeValue(c.getHA());
    writer.writeValue(c.getHAComponent());
    writer.writeValue(c.getHADeviceClass());
    writer.writeValue(c.getHAEntityCategory());
    writer.writeValue(c.getHAMode());

    // Map as object
    writer.startObject();
    for (const auto& kvm : c.getHAKeyValueMap()) {
      char keyBuf[12];
      auto [ptr, ec] =
          std::to_chars(keyBuf, keyBuf + sizeof(keyBuf), kvm.first);
      if (ec == std::errc{}) {
        writer.writeField(std::string_view(keyBuf, ptr - keyBuf), kvm.second);
      }
    }
    writer.endObject();

    writer.writeValue(c.getHADefaultKey());
    writer.writeValue(c.getHAPayloadOn());
    writer.writeValue(c.getHAPayloadOff());
    writer.writeValue(c.getHAStateClass());
    writer.writeValueFloat(c.getHAStep());
    writer.endArray();
  }
  writer.endArray();
  writer.flush();
  std::fclose(file);

  return static_cast<int64_t>(bytes_written);
}

int64_t Store::wipeCommands() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  commands_.clear();
  if (!ensureLittlefsMounted()) return -1;

  struct stat fileStat{};
  if (stat(kCommandsFilePath, &fileStat) != 0) {
    if (errno == ENOENT) return 0;
    return -1;
  }

  if (std::remove(kCommandsFilePath) != 0) {
    if (errno == ENOENT) return 0;
    return -1;
  }

  if (fileStat.st_size <= 0) {
    return 0;
  }

  return static_cast<int64_t>(fileStat.st_size);
}

void Store::fetchCommandsJson(const ebus::JsonChunkVisitor& visitor) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ebus::detail::JsonWriter writer(visitor);
  writer.startArray();

  std::vector<const Command*> ordered;
  for (const auto& kv : commands_) ordered.push_back(&kv.second);
  std::sort(ordered.begin(), ordered.end(),
            [](const Command* a, const Command* b) {
              return a->getKey() < b->getKey();
            });

  for (const Command* cmd : ordered) {
    writer.writeValue(*cmd);
  }
  writer.endArray();
}

const std::vector<Command*> Store::getCommands() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::vector<Command*> result;
  for (auto& kv : commands_) result.push_back(&(kv.second));
  return result;
}

size_t Store::getActiveCommands() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  size_t count = 0;
  for (const auto& kv : commands_) {
    if (kv.second.getActive()) count++;
  }
  return count;
}

size_t Store::getPassiveCommands() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  size_t count = 0;
  for (const auto& kv : commands_) {
    if (!kv.second.getActive()) count++;
  }
  return count;
}

bool Store::active() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (const auto& kv : commands_) {
    if (kv.second.getActive()) return true;
  }
  return false;
}

Command* Store::nextActiveCommand() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  Command* next = nullptr;
  bool init = false;
  for (auto& kv : commands_) {
    Command* cmd = &kv.second;
    // Only consider active commands
    if (!cmd->getActive()) continue;
    if (cmd->getLast() == 0) {
      next = cmd;
      init = true;
      break;
    }
    if (next == nullptr || (cmd->getLast() + cmd->getInterval() * 1000 <
                            next->getLast() + next->getInterval() * 1000))
      next = cmd;
  }

  if (!init && next &&
      (uint32_t)(esp_timer_get_time() / 1000ULL) <
          next->getLast() + next->getInterval() * 1000)
    next = nullptr;

  return next;
}

std::vector<Command*> Store::findPassiveCommands(ebus::ByteView master) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::vector<Command*> result;
  for (auto& kv : commands_) {
    Command* cmd = &kv.second;
    // Skip active commands
    if (cmd->getActive()) continue;
    if (cmd->matches(master)) {
      result.push_back(cmd);
    }
  }
  return result;
}

std::vector<Command*> Store::updateData(Command* command,
                                        ebus::ByteView master_view,
                                        ebus::ByteView slave_view) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto update = [this](Command* cmd, ebus::ByteView master_view,
                       ebus::ByteView slave_view) {
    cmd->setLast((uint32_t)(esp_timer_get_time() / 1000ULL));
    if (cmd->getMaster()) {
      cmd->setData(
          ebus::range(master_view, 4 + cmd->getPosition(), cmd->getLength()));
    } else {
      cmd->setData(
          ebus::range(slave_view, cmd->getPosition(), cmd->getLength()));
    }

    // Offload heavy JSON and string work to background task via key-only
    // callback
    if (data_updated_callback_) {
      data_updated_callback_(cmd->getKey());
    }

    if (data_updated_log_callback_) {
      data_updated_log_callback_(cmd->getKey());
    }
  };

  if (command) {
    update(command, master_view, slave_view);
    // Return a vector with just this command, but avoid heap allocation
    return {command};
  }

  // Find all matching commands (both active and passive)
  std::vector<Command*> matchingCommands = findAllMatchingCommands(master_view);
  for (Command* cmd : matchingCommands) update(cmd, master_view, slave_view);

  return matchingCommands;
}

void Store::fetchValuesJson(const ebus::JsonChunkVisitor& visitor) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ebus::detail::JsonWriter writer(visitor);
  writer.startArray();

  std::vector<const Command*> ordered;
  for (const auto& kv : commands_) ordered.push_back(&kv.second);
  std::sort(ordered.begin(), ordered.end(),
            [](const Command* a, const Command* b) {
              return a->getKey() < b->getKey();
            });

  uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  for (const Command* cmd : ordered) {
    auto scope = writer.objectScope();
    writer.writeField("key", cmd->getKey());
    writer.writeField("name", cmd->getName());

    writer.appendKey("value");
    cmd->getValueJson(writer);

    writer.writeField("unit", cmd->getUnit());
    writer.writeField("age",
                      (cmd->getLast() > 0) ? (now - cmd->getLast()) / 1000 : 0);
    writer.writeField("write", !cmd->getWriteCmd().empty());
    writer.writeField("active", cmd->getActive());
  }
  writer.endArray();
}

void Store::deserializeCommands(const char* payload) {
  ebus::detail::JsonReader reader(payload);
  if (reader.next() != ebus::detail::JsonReader::Token::ArrayStart) {
    logger.warn("Store: Payload does not start with a JSON array");
    return;
  }

  size_t loaded_count = 0;
  bool header_seen = false;

  // Read each row individually
  while (true) {
    std::string_view row_sv = reader.rawValue();
    if (row_sv.empty()) break;

    ebus::detail::JsonReader row_reader(row_sv);
    auto token = row_reader.next();

    if (token == ebus::detail::JsonReader::Token::ArrayStart) {
      if (!header_seen) {
        header_seen = true;
        continue;  // Skip header row
      }
      row_reader.reset();
      insertCommand(Command::fromTabular(row_reader));
      loaded_count++;
    } else if (token == ebus::detail::JsonReader::Token::ObjectStart) {
      row_reader.reset();
      std::string evalError = Command::evaluate(row_reader);
      if (evalError.empty()) {
        row_reader.reset();
        insertCommand(Command::fromJson(row_reader));
        loaded_count++;
      } else {
        logger.error("Store: Command validation failed: " + evalError);
      }
    }
  }
  logger.info("Store: Deserialized " + std::to_string(loaded_count) +
              " commands.");
}

#endif
