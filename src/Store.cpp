#if defined(EBUS_INTERNAL)
#include "Store.hpp"

#include <esp_timer.h>
#include <nvs.h>

#include <cmath>
#include <cstdio>

Store store;

namespace {
std::string printJson(cJSON* node, const char* fallback) {
  char* printed = cJSON_PrintUnformatted(node);
  std::string out = printed != nullptr ? printed : fallback;
  if (printed != nullptr) cJSON_free(printed);
  return out;
}

std::string formatDouble(double value, int precision) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
  std::string s(buffer);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s.empty() ? "0" : s;
}

std::string jsonValueToString(cJSON* value) {
  if (cJSON_IsString(value) && value->valuestring != nullptr)
    return value->valuestring;
  if (cJSON_IsBool(value)) return cJSON_IsTrue(value) ? "true" : "false";
  if (cJSON_IsNumber(value)) {
    return formatDouble(value->valuedouble, 6);
  }
  return printJson(value, "null");
}
}  // namespace

void Store::setDataUpdatedCallback(DataUpdatedCallback callback) {
  dataUpdatedCallback = std::move(callback);
}

void Store::setDataUpdatedLogCallback(DataUpdatedLogCallback callback) {
  dataUpdatedLogCallback = std::move(callback);
}

void Store::insertCommand(const Command& command) {
  // Insert or update in commands map
  auto it = commands.find(command.getKey());
  if (it != commands.end())
    it->second = command;
  else
    commands.insert(std::make_pair(command.getKey(), command));
}

void Store::removeCommand(const std::string& key) {
  auto it = commands.find(key);
  if (it != commands.end()) commands.erase(it);
}

Command* Store::findCommand(const std::string& key) {
  auto it = commands.find(key);
  if (it != commands.end())
    return &(it->second);
  else
    return nullptr;
}

int64_t Store::loadCommands() {
  nvs_handle_t handle = 0;
  if (nvs_open("commands", NVS_READONLY, &handle) != ESP_OK) return -1;

  size_t size = 0;
  esp_err_t err = nvs_get_blob(handle, "ebus", nullptr, &size);
  if (err == ESP_ERR_NVS_NOT_FOUND || size <= 2) {
    nvs_close(handle);
    return 0;
  }
  if (err != ESP_OK || size == 0) {
    nvs_close(handle);
    return -1;
  }

  std::vector<char> buffer(size);
  err = nvs_get_blob(handle, "ebus", buffer.data(), &size);
  nvs_close(handle);
  if (err != ESP_OK || size == 0) return -1;

  std::string payload(buffer.begin(), buffer.end());
  deserializeCommands(payload.c_str());
  return static_cast<int64_t>(size);
}

int64_t Store::saveCommands() const {
  nvs_handle_t handle = 0;
  if (nvs_open("commands", NVS_READWRITE, &handle) != ESP_OK) return -1;

  std::string payload = serializeCommands();
  size_t size = payload.size();
  if (size <= 2) {  // 2 = empty json array "[]"
    nvs_close(handle);
    return 0;
  }

  esp_err_t err = nvs_set_blob(handle, "ebus", payload.data(), size);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  if (err != ESP_OK) return -1;
  return static_cast<int64_t>(size);
}

int64_t Store::wipeCommands() {
  nvs_handle_t handle = 0;
  if (nvs_open("commands", NVS_READWRITE, &handle) != ESP_OK) return -1;

  size_t size = 0;
  esp_err_t err = nvs_get_blob(handle, "ebus", nullptr, &size);
  if (err == ESP_ERR_NVS_NOT_FOUND || size == 0) {
    nvs_close(handle);
    return 0;
  }
  if (err != ESP_OK) {
    nvs_close(handle);
    return -1;
  }

  err = nvs_erase_key(handle, "ebus");
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  if (err != ESP_OK) return -1;
  return static_cast<int64_t>(size);
}

const std::string Store::getCommandsJson() const {
  cJSON* root = cJSON_CreateArray();

  std::vector<std::pair<std::string, Command>> orderedCommands(commands.begin(),
                                                               commands.end());

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
  std::vector<Command*> result;
  for (auto& kv : commands) result.push_back(&(kv.second));
  return result;
}

size_t Store::getActiveCommands() const {
  size_t count = 0;
  for (const auto& kv : commands) {
    if (kv.second.getActive()) count++;
  }
  return count;
}

size_t Store::getPassiveCommands() const {
  size_t count = 0;
  for (const auto& kv : commands) {
    if (!kv.second.getActive()) count++;
  }
  return count;
}

bool Store::active() const {
  for (const auto& kv : commands) {
    if (kv.second.getActive()) return true;
  }
  return false;
}

Command* Store::nextActiveCommand() {
  Command* next = nullptr;
  bool init = false;
  for (auto& kv : commands) {
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

std::vector<Command*> Store::findPassiveCommands(
    const std::vector<uint8_t>& master) {
  std::vector<Command*> result;
  for (auto& kv : commands) {
    Command* cmd = &kv.second;
    // Skip active commands
    if (cmd->getActive()) continue;
    if (ebus::contains(master, cmd->getReadCmd())) {
      result.push_back(cmd);
    }
  }
  return result;
}

std::vector<Command*> Store::updateData(Command* command,
                                        const std::vector<uint8_t>& master,
                                        const std::vector<uint8_t>& slave) {
  auto update = [this](Command* cmd, const std::vector<uint8_t>& master,
                       const std::vector<uint8_t>& slave) {
    cmd->setLast((uint32_t)(esp_timer_get_time() / 1000ULL));
    if (cmd->getMaster())
      cmd->setData(
          ebus::range(master, 4 + cmd->getPosition(), cmd->getLength()));
    else
      cmd->setData(ebus::range(slave, cmd->getPosition(), cmd->getLength()));

    std::string valueJson = cmd->getValueJson();
    if (dataUpdatedCallback) dataUpdatedCallback(cmd->getName(), valueJson);

    cJSON* valueDoc = cJSON_Parse(valueJson.c_str());
    cJSON* valueNode = valueDoc
                           ? cJSON_GetObjectItemCaseSensitive(valueDoc, "value")
                           : nullptr;

    std::string payload = " '" + ebus::to_string(cmd->getReadCmd()) + "' [" +
                          cmd->getName() + "] " +
                          ebus::to_string(cmd->getData()) + " -> " +
                          jsonValueToString(valueNode) + " " + cmd->getUnit();

    if (valueDoc) cJSON_Delete(valueDoc);

    if (dataUpdatedLogCallback) dataUpdatedLogCallback(payload);
  };

  if (command) {
    update(command, master, slave);
    // Return a vector with just this command, but avoid heap allocation
    return {command};
  }

  // Passive: potentially multiple matches
  std::vector<Command*> passiveCommands = findPassiveCommands(master);
  for (Command* cmd : passiveCommands) update(cmd, master, slave);

  return passiveCommands;
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
  cJSON* root = cJSON_CreateArray();

  std::vector<std::pair<std::string, Command>> orderedCommands(commands.begin(),
                                                               commands.end());

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
  for (const auto& cmd : commands) {
    cJSON* cmdDoc = cJSON_Parse(cmd.second.toJson().c_str());
    if (!cJSON_IsObject(cmdDoc)) {
      if (cmdDoc) cJSON_Delete(cmdDoc);
      continue;
    }

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
