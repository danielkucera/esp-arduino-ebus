#if defined(EBUS_INTERNAL)
#include "Store.hpp"

#include <esp_littlefs.h>
#include <esp_timer.h>
#include <sys/stat.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ebus/detail/protocol_limits.hpp>

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

std::string printJson(cJSON* node, const char* fallback) {
  char* printed = cJSON_PrintUnformatted(node);
  std::string out = printed != nullptr ? printed : fallback;
  if (printed != nullptr) cJSON_free(printed);
  return out;
}

std::string formatDouble(double value, int precision) {
  char buffer[64];
  return ebus::formatFloat(
      value, precision, buffer, sizeof(buffer),
      ebus::detail::FormattingLimits::float_lower_threshold,
      ebus::detail::FormattingLimits::float_upper_threshold);
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

  std::string payload = serializeCommands();
  size_t size = payload.size();
  if (size <= 2) {  // 2 = empty json array "[]"
    return 0;
  }

  FILE* file = std::fopen(kCommandsFilePath, "wb");
  if (file == nullptr) return -1;

  size_t bytesWritten = std::fwrite(payload.data(), 1, size, file);
  std::fclose(file);
  if (bytesWritten != size) return -1;

  return static_cast<int64_t>(size);
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

const std::string Store::getCommandsJson() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  cJSON* root = cJSON_CreateArray();

  std::vector<std::pair<std::string, Command>> orderedCommands(
      commands_.begin(), commands_.end());

  std::sort(orderedCommands.begin(), orderedCommands.end(),
            [](const std::pair<std::string, Command>& a,
               const std::pair<std::string, Command>& b) {
              // Compare based on keys
              return a.first < b.first;
            });

  for (const auto& kv : orderedCommands) {
    cJSON* cmd = cJSON_Parse(kv.second.toJson().c_str());
    if (cmd != nullptr) cJSON_AddItemToArray(root, cmd);
  }

  std::string payload = printJson(root, "[]");
  cJSON_Delete(root);
  return payload;
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
    std::string valueJson = cmd->getValueJson();
    if (data_updated_callback_)
      data_updated_callback_(cmd->getName(), valueJson);

    if (data_updated_log_callback_) {
      std::string valStr =
          cmd->getNumeric()
              ? formatDouble(cmd->getDoubleFromVector(), cmd->getDigits())
              : cmd->getStringFromVector();
      std::string payload = " '" + ebus::toString(cmd->getReadCmd()) + "' [" +
                            cmd->getName() + "] " +
                            ebus::toString(cmd->getData()) + " -> " + valStr +
                            " " + cmd->getUnit();
      data_updated_log_callback_(payload);
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
const std::string Store::getValueFullJson(const Command* command) {
  cJSON* doc = cJSON_CreateObject();

  cJSON_AddStringToObject(doc, "key", command->getKey().c_str());
  cJSON_AddStringToObject(doc, "name", command->getName().c_str());

  cJSON* valueDoc = cJSON_Parse(command->getValueJson().c_str());
  cJSON* valueNode =
      valueDoc ? cJSON_GetObjectItemCaseSensitive(valueDoc, "value") : nullptr;
  if (valueNode) {
    cJSON_AddItemToObject(doc, "value", cJSON_Duplicate(valueNode, 1));
  } else {
    cJSON_AddNullToObject(doc, "value");
  }
  if (valueDoc) cJSON_Delete(valueDoc);

  cJSON_AddStringToObject(doc, "unit", command->getUnit().c_str());
  cJSON_AddNumberToObject(
      doc, "age",
      static_cast<uint32_t>(
          ((uint32_t)(esp_timer_get_time() / 1000ULL) - command->getLast()) /
          1000));
  cJSON_AddBoolToObject(doc, "write", !command->getWriteCmd().empty());
  cJSON_AddBoolToObject(doc, "active", command->getActive());

  std::string payload = printJson(doc, "{}");
  cJSON_Delete(doc);
  return payload;
}

const std::string Store::getValuesJson() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  cJSON* root = cJSON_CreateArray();

  std::vector<std::pair<std::string, Command>> orderedCommands(
      commands_.begin(), commands_.end());

  std::sort(orderedCommands.begin(), orderedCommands.end(),
            [](const std::pair<std::string, Command>& a,
               const std::pair<std::string, Command>& b) {
              return a.first < b.first;
            });

  for (const auto& kv : orderedCommands) {
    cJSON* value = cJSON_Parse(getValueFullJson(&kv.second).c_str());
    if (value != nullptr) cJSON_AddItemToArray(root, value);
  }

  std::string payload = printJson(root, "[]");
  cJSON_Delete(root);
  return payload;
}

const std::string Store::serializeCommands() const {
  cJSON* doc = cJSON_CreateArray();

  // Define field names (order matters)
  std::vector<std::string> fields = {
      // Command Fields
      "key", "name", "read_cmd", "write_cmd", "active", "interval",
      // Data Fields
      "master", "position", "datatype", "divider", "min", "max", "digits",
      "unit",
      // Home Assistant
      "ha", "ha_component", "ha_device_class", "ha_entity_category", "ha_mode",
      "ha_key_value_map", "ha_default_key", "ha_payload_on", "ha_payload_off",
      "ha_state_class", "ha_step"};

  // Add header as first entry
  cJSON* header = cJSON_CreateArray();
  for (const auto& field : fields)
    cJSON_AddItemToArray(header, cJSON_CreateString(field.c_str()));
  cJSON_AddItemToArray(doc, header);

  // Add each command as an array of values in the same order as header
  for (const auto& cmd : commands_) {
    cJSON* cmdDoc = cmd.second.toCJson();
    cJSON* row = cJSON_CreateArray();
    for (const auto& field : fields) {
      cJSON* item = cJSON_GetObjectItemCaseSensitive(cmdDoc, field.c_str());
      if (item)
        cJSON_AddItemToArray(row, cJSON_Duplicate(item, 1));
      else
        cJSON_AddItemToArray(row, cJSON_CreateNull());
    }

    cJSON_AddItemToArray(doc, row);
    cJSON_Delete(cmdDoc);
  }

  std::string payload = printJson(doc, "[]");
  cJSON_Delete(doc);
  return payload;
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
