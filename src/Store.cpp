#if defined(EBUS_INTERNAL)
#include "Store.hpp"

#include <cJSON.h>
#include <esp_littlefs.h>
#include <esp_timer.h>
#include <sys/stat.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ebus/detail/json_writer.hpp>
#include <ebus/detail/protocol_limits.hpp>

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
    return -1;
  }

  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return -1;
  }

  long size = std::ftell(file);
  if (size <= 2) {
    std::fclose(file);
    return 0;
  }

  if (size <= 0 || std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    return -1;
  }

  std::string payload;
  payload.resize(static_cast<size_t>(size));
  size_t bytesRead = std::fread(payload.data(), 1, payload.size(), file);
  std::fclose(file);
  if (bytesRead != payload.size()) return -1;

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
  // Header row
  writer.startArray();
  static const char* fields[] = {"key",
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
  for (const auto& f : fields) writer.writeValue(f);
  writer.endArray();

  // Data rows
  for (const auto& kv : commands_) {
    kv.second.writePersistenceRow(writer);
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
    writer.startObject(); // Manual construction requires startObject
    writer.writeField("key", cmd->getKey());
    writer.writeField("name", cmd->getName());

    // Optimized Value fetching
    auto decoded = ebus::decode(cmd->getDatatype(), cmd->getData());
    writer.appendKey("value");
    if (!decoded || ebus::isNull(*decoded)) {
      writer.writeRaw("null");
    } else {
      if (cmd->getNumeric())
        writer.writeValueFloat(static_cast<float>(cmd->getDoubleFromVector()));
      else
        writer.writeValue(cmd->getStringFromVector());
    }

    writer.writeField("unit", cmd->getUnit());
    writer.writeField("age",
                      (cmd->getLast() > 0) ? (now - cmd->getLast()) / 1000 : 0);
    writer.writeField("write", !cmd->getWriteCmd().empty());
    writer.writeField("active", cmd->getActive());
    writer.endObject();
  }
  writer.endArray();
}

void Store::deserializeCommands(const char* payload) {
  cJSON* doc = cJSON_Parse(payload);
  if (!cJSON_IsArray(doc)) {
    if (doc) cJSON_Delete(doc);
    return;
  }

  int arraySize = cJSON_GetArraySize(doc);
  if (arraySize < 2) {
    cJSON_Delete(doc);
    return;
  }

  // Read header
  cJSON* header = cJSON_GetArrayItem(doc, 0);
  if (!cJSON_IsArray(header)) {
    cJSON_Delete(doc);
    return;
  }

  std::vector<std::string> fields;
  int headerSize = cJSON_GetArraySize(header);
  for (int i = 0; i < headerSize; ++i) {
    cJSON* name = cJSON_GetArrayItem(header, i);
    if (cJSON_IsString(name) && name->valuestring != nullptr)
      fields.emplace_back(name->valuestring);
    else
      fields.emplace_back();
  }

  // Read each command
  for (int i = 1; i < arraySize; ++i) {
    cJSON* values = cJSON_GetArrayItem(doc, i);
    if (!cJSON_IsArray(values)) continue;

    cJSON* tmpDoc = cJSON_CreateObject();
    int valueSize = cJSON_GetArraySize(values);
    int limit = std::min(static_cast<int>(fields.size()), valueSize);

    for (int j = 0; j < limit; ++j) {
      cJSON* valueItem = cJSON_GetArrayItem(values, j);
      if (fields[j].empty() || valueItem == nullptr) continue;
      // Special handling for 'ha_key_value_map'
      cJSON_AddItemToObject(tmpDoc, fields[j].c_str(),
                            cJSON_Duplicate(valueItem, 1));
    }

    std::string evalError = Command::evaluate(tmpDoc);
    if (evalError.empty()) insertCommand(Command::fromJson(tmpDoc));

    cJSON_Delete(tmpDoc);
  }

  cJSON_Delete(doc);
}

#endif
