#pragma once

#if defined(EBUS_INTERNAL)
#include <ArduinoJson.h>
#include <Ebus.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// This Store class stores both active and passive eBUS commands. For permanent
// storage (NVS), functions for saving, loading, and deleting commands are
// available. Permanently stored commands are automatically loaded when the
// device is restarted.

enum FieldType {
  FT_String,
  FT_Bool,
  FT_Int,
  FT_Float,
  FT_Uint8,
  FT_Uint32,
  FT_SizeT,
  FT_DataType,
  FT_KeyValueMap
};

// Hash for std::vector<uint8_t>
struct VectorHash {
  std::size_t operator()(const std::vector<uint8_t>& vec) const {
    std::size_t hash = vec.size();
    for (const uint8_t& h : vec) {
      hash ^= std::hash<uint8_t>()(h) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    return hash;
  }
};

// clang-format off
struct Command {
  // Internal Fields
  uint32_t last = 0;                                // last time of the successful command 
  std::vector<uint8_t> data = {};                   // received raw data
  size_t length = 1;                                // length of datatype
  bool numeric = false;                             // indicates numeric datatype

  // Command fields
  std::string key = "";                             // unique key of command
  std::string name = "";                            // name of the command used as mqtt topic below "values/"
  std::vector<uint8_t> read_cmd = {};               // read command as vector of "ZZPBSBNNDBx"
  std::vector<uint8_t> write_cmd = {};              // write command as vector of "ZZPBSBNNDBx" (OPTIONAL)
  bool active = false;                              // active sending of command
  uint32_t interval = 60;                           // minimum interval between two commands in seconds (OPTIONAL)

  // Data Fields
  bool master = false;                              // value of interest is in master or slave part
  size_t position = 1;                              // starting position
  ebus::DataType datatype = ebus::DataType::HEX1;   // ebus data type
  float divider = 1;                                // divider for value conversion (OPTIONAL)
  float min = 1;                                    // minimum value (OPTIONAL)
  float max = 100;                                  // maximum value (OPTIONAL)
  uint8_t digits = 2;                               // decimal digits of value (OPTIONAL)
  std::string unit = "";                            // unit (OPTIONAL)
          
  // Home Assistant
  bool ha = false;                                  // support for auto discovery (OPTIONAL)
  std::string ha_component = "";                    // component type (OPTIONAL)
  std::string ha_device_class = "";                 // device class (OPTIONAL)
  std::string ha_entity_category = "";              // entity category (OPTIONAL)
  std::string ha_mode = "auto";                     // mode (OPTIONAL)
  std::map<int, std::string> ha_key_value_map = {}; // options as pairs of "key":"value" (OPTIONAL)
  int ha_default_key = 0;                           // options default key (OPTIONAL)
  uint8_t ha_payload_on = 1;                        // payload for ON state (OPTIONAL)
  uint8_t ha_payload_off = 0;                       // payload for OFF state (OPTIONAL)
  std::string ha_state_class = "";                  // state class (OPTIONAL)
  float ha_step = 1;                                // step value (OPTIONAL)
};
// clang-format on

const double getDoubleFromVector(const Command* command);
const std::vector<uint8_t> getVectorFromDouble(const Command* command,
                                               double value);

const std::string getStringFromVector(const Command* command);
const std::vector<uint8_t> getVectorFromString(const Command* command,
                                               const std::string& value);

class Store {
 public:
  static const std::string evaluateCommand(const JsonDocument& doc);

  Command createCommand(const JsonDocument& doc);

  void insertCommand(const Command& command);
  void removeCommand(const std::string& key);
  Command* findCommand(const std::string& key);

  int64_t loadCommands();
  int64_t saveCommands() const;
  static int64_t wipeCommands();

  static JsonDocument getCommandJson(const Command* command);
  const JsonDocument getCommandsJsonDocument() const;
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

  static JsonDocument getValueJson(const Command* command);
  static const std::string getValueFullJson(const Command* command);
  const std::string getValuesJson() const;

 private:
  // Use unordered_map for fast key lookup
  std::unordered_map<std::string, Command> allCommandsByKey;
  // For passive commands, use command.read_cmd as key for fast lookup
  std::unordered_map<std::vector<uint8_t>, std::vector<Command*>, VectorHash>
      passiveCommands;
  // For active commands, just keep a vector of pointers
  std::vector<Command*> activeCommands;

  static const std::string isFieldValid(const JsonDocument& doc,
                                        const std::string& field, bool required,
                                        FieldType type);

  static const std::string isKeyValueMapValid(
      const JsonObjectConst ha_key_value_map);

  // Flexible serialization/deserialization
  const std::string serializeCommands() const;
  void deserializeCommands(const char* payload);
};

extern Store store;
#endif
