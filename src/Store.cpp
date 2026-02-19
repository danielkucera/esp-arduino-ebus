#if defined(EBUS_INTERNAL)
#include "Store.hpp"

#include <Preferences.h>

Store store;

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
  Preferences preferences;
  preferences.begin("commands", true);

  int64_t bytes = preferences.getBytesLength("ebus");
  if (bytes > 2) {  // 2 = empty json array "[]"
    std::vector<char> buffer(bytes);
    bytes = preferences.getBytes("ebus", buffer.data(), bytes);
    if (bytes > 0) {
      std::string payload(buffer.begin(), buffer.end());
      deserializeCommands(payload.c_str());
    } else {
      bytes = -1;
    }
  } else {
    bytes = 0;
  }

  preferences.end();
  return bytes;
}

int64_t Store::saveCommands() const {
  Preferences preferences;
  preferences.begin("commands", false);

  std::string payload = serializeCommands();
  int64_t bytes = payload.size();
  if (bytes > 2) {  // 2 = empty json array "[]"
    bytes = preferences.putBytes("ebus", payload.data(), bytes);
    if (bytes == 0) bytes = -1;
  } else {
    bytes = 0;
  }

  preferences.end();
  return bytes;
}

int64_t Store::wipeCommands() {
  Preferences preferences;
  preferences.begin("commands", false);

  int64_t bytes = preferences.getBytesLength("ebus");
  if (bytes > 0) {
    if (!preferences.remove("ebus")) bytes = -1;
  }

  preferences.end();
  return bytes;
}

const JsonDocument Store::getCommandsJsonDoc() const {
  JsonDocument doc;

  std::vector<std::pair<std::string, Command>> orderedCommands(commands.begin(),
                                                               commands.end());

  std::sort(orderedCommands.begin(), orderedCommands.end(),
            [](const std::pair<std::string, Command>& a,
               const std::pair<std::string, Command>& b) {
              return a.first < b.first;  // Compare based on keys
            });

  for (const auto& kv : orderedCommands) doc.add(kv.second.toJson());

  if (doc.isNull()) doc.to<JsonArray>();

  doc.shrinkToFit();
  return doc;
}

const std::string Store::getCommandsJson() const {
  std::string payload;
  JsonDocument doc = getCommandsJsonDoc();
  serializeJson(doc, payload);
  return payload;
}

const std::vector<Command*> Store::getCommands() {
  std::vector<Command*> result;
  for (auto& kv : commands) result.push_back(&(kv.second));
  return result;
}

const size_t Store::getActiveCommands() const {
  size_t count = 0;
  for (const auto& kv : commands) {
    if (kv.second.getActive()) count++;
  }
  return count;
}

const size_t Store::getPassiveCommands() const {
  size_t count = 0;
  for (const auto& kv : commands) {
    if (!kv.second.getActive()) count++;
  }
  return count;
}

const bool Store::active() const {
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
    if (!cmd->getActive()) continue;  // Only consider active commands
    if (cmd->getLast() == 0) {
      next = cmd;
      init = true;
      break;
    }
    if (next == nullptr || (cmd->getLast() + cmd->getInterval() * 1000 <
                            next->getLast() + next->getInterval() * 1000))
      next = cmd;
  }

  if (!init && next && millis() < next->getLast() + next->getInterval() * 1000)
    next = nullptr;

  return next;
}

std::vector<Command*> Store::findPassiveCommands(
    const std::vector<uint8_t>& master) {
  std::vector<Command*> result;
  for (auto& kv : commands) {
    Command* cmd = &kv.second;
    if (cmd->getActive()) continue;  // Skip active commands
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
    cmd->setLast(millis());
    if (cmd->getMaster())
      cmd->setData(
          ebus::range(master, 4 + cmd->getPosition(), cmd->getLength()));
    else
      cmd->setData(ebus::range(slave, cmd->getPosition(), cmd->getLength()));

    dataUpdatedCallback(cmd->getName(), cmd->getValueJsonDoc());

    std::string payload = " '" + ebus::to_string(cmd->getReadCmd()) + "' [" +
                          cmd->getName() + "] " +
                          ebus::to_string(cmd->getData()) + " -> " +
                          cmd->getValueJsonDoc()["value"].as<std::string>() +
                          " " + cmd->getUnit();

    dataUpdatedLogCallback(payload.c_str());
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

const JsonDocument Store::getValueFullJsonDoc(const Command* command) {
  JsonDocument doc;

  doc["key"] = command->getKey();
  doc["name"] = command->getName();
  doc["value"] = command->getValueJsonDoc()["value"];
  doc["unit"] = command->getUnit();
  doc["age"] = static_cast<uint32_t>((millis() - command->getLast()) / 1000);
  doc["write"] = !command->getWriteCmd().empty();
  doc["active"] = command->getActive();

  doc.shrinkToFit();
  return doc;
}

const std::string Store::getValueFullJson(const Command* command) {
  std::string payload;
  serializeJson(getValueFullJsonDoc(command), payload);
  return payload;
}

const JsonDocument Store::getValuesJsonDoc() const {
  JsonDocument doc;

  std::vector<std::pair<std::string, Command>> orderedCommands(commands.begin(),
                                                               commands.end());

  std::sort(orderedCommands.begin(), orderedCommands.end(),
            [](const std::pair<std::string, Command>& a,
               const std::pair<std::string, Command>& b) {
              return a.first < b.first;  // Compare based on keys
            });

  for (const auto& kv : orderedCommands)
    doc.add(getValueFullJsonDoc(&kv.second));

  if (doc.isNull()) doc.to<JsonArray>();

  doc.shrinkToFit();
  return doc;
}

const std::string Store::getValuesJson() const {
  std::string payload;
  JsonDocument doc = getValuesJsonDoc();
  serializeJson(doc, payload);
  return payload;
}

const std::string Store::serializeCommands() const {
  std::string payload;
  JsonDocument doc;

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
  JsonArray header = doc.add<JsonArray>();
  for (const auto& field : fields) header.add(field);

  // Add each command as an array of values in the same order as header
  for (const auto& cmd : commands) {
    const Command& command = cmd.second;
    JsonArray array = doc.add<JsonArray>();

    // Command Fields
    array.add(command.getKey());
    array.add(command.getName());
    array.add(ebus::to_string(command.getReadCmd()));
    array.add(ebus::to_string(command.getWriteCmd()));
    array.add(command.getActive());
    array.add(command.getInterval());

    // Data Fields
    array.add(command.getMaster());
    array.add(command.getPosition());
    array.add(ebus::datatype_2_string(command.getDatatype()));
    array.add(command.getDivider());
    array.add(command.getMin());
    array.add(command.getMax());
    array.add(command.getDigits());
    array.add(command.getUnit());

    // Home Assistant
    array.add(command.getHA());
    array.add(command.getHAComponent());
    array.add(command.getHADeviceClass());
    array.add(command.getHAEntityCategory());
    array.add(command.getHAMode());

    JsonObject ha_key_value_map = array.add<JsonObject>();
    for (const auto& kv : command.getHAKeyValueMap())
      ha_key_value_map[std::to_string(kv.first)] = kv.second;

    array.add(command.getHADefaultKey());
    array.add(command.getHAPayloadOn());
    array.add(command.getHAPayloadOff());
    array.add(command.getHAStateClass());
    array.add(command.getHAStep());
  }

  doc.shrinkToFit();
  serializeJson(doc, payload);
  return payload;
}

void Store::deserializeCommands(const char* payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (!error) {
    JsonArray array = doc.as<JsonArray>();
    if (array.size() < 2) return;  // Need at least header + one command

    // Read header
    JsonArray header = array[0];
    std::vector<std::string> fields;
    for (JsonVariant v : header) fields.push_back(v.as<std::string>());

    // Read each command
    for (size_t i = 1; i < array.size(); ++i) {
      JsonArray values = array[i];
      JsonDocument tmpDoc;
      for (size_t j = 0; j < fields.size() && j < values.size(); ++j) {
        // Special handling for 'ha_key_value_map'
        if (fields[j] == "ha_key_value_map") {
          JsonObjectConst kvObject = values[j].as<JsonObject>();
          JsonObject ha_key_value_map =
              tmpDoc["ha_key_value_map"].to<JsonObject>();

          for (JsonPairConst kv : kvObject)
            ha_key_value_map[kv.key()] = kv.value();
        } else {
          tmpDoc[fields[j]] = values[j];
        }
      }
      std::string evalError = Command::evaluate(tmpDoc);
      if (evalError.empty()) insertCommand(Command::fromJson(tmpDoc));
    }
  }
}

#endif
