#pragma once

#if defined(EBUS_INTERNAL)
#include <ArduinoJson.h>
#include <Ebus.h>

#include <map>
#include <string>
#include <vector>

// This class represents a command configuration and its associated data

class Command {
 public:
  // Internal fields accessors
  const uint32_t& getLast() const;
  void setLast(const uint32_t time);

  const std::vector<uint8_t>& getData() const;
  void setData(const std::vector<uint8_t>& data);

  const size_t& getLength() const;

  const bool& getNumeric() const;

  // Command field accessors
  const std::string& getKey() const;
  const std::string& getName() const;
  const std::vector<uint8_t>& getReadCmd() const;
  const std::vector<uint8_t>& getWriteCmd() const;
  const bool& getActive() const;
  const uint32_t& getInterval() const;

  // Data field accessors
  const bool& getMaster() const;
  const size_t& getPosition() const;
  const ebus::DataType& getDatatype() const;
  const float& getDivider() const;
  const float& getMin() const;
  const float& getMax() const;
  const uint8_t& getDigits() const;
  const std::string& getUnit() const;

  // Home Assistant field accessors
  const bool& getHA() const;
  const std::string& getHAComponent() const;
  const std::string& getHADeviceClass() const;
  const std::string& getHAEntityCategory() const;
  const std::string& getHAMode() const;
  const std::map<int, std::string>& getHAKeyValueMap() const;
  const int& getHADefaultKey() const;
  const uint8_t& getHAPayloadOn() const;
  const uint8_t& getHAPayloadOff() const;
  const std::string& getHAStateClass() const;
  const float& getHAStep() const;

  // Data conversion
  const JsonDocument getValueJsonDoc() const;
  const std::vector<uint8_t> getVectorFromJson(const JsonDocument& doc);

  // Serialization / Deserialization
  JsonDocument toJson() const;
  static Command fromJson(const JsonDocument& doc);

  static const std::string evaluate(const JsonDocument& doc);

 private:
  // Internal fields
  // last time of the successful command
  uint32_t last = 0;
  // received raw data
  std::vector<uint8_t> data = {};
  // length of datatype
  size_t length = 1;
  // indicates numeric datatype
  bool numeric = false;

  // Command fields
  // unique key of command
  std::string key = "";
  // name of the command used as mqtt topic below "values/"
  std::string name = "";
  // read command as vector of "ZZPBSBNNDBx"
  std::vector<uint8_t> read_cmd = {};
  // write command as vector of "ZZPBSBNNDBx" (OPTIONAL)
  std::vector<uint8_t> write_cmd = {};
  // active sending of command
  bool active = false;
  // minimum interval between two commands in seconds (OPTIONAL)
  uint32_t interval = 60;

  // Data fields
  // value of interest is in master or slave part
  bool master = false;
  // starting position within the data bytes, beginning with 1
  size_t position = 1;
  // ebus data type
  ebus::DataType datatype = ebus::DataType::HEX1;
  // divider for value conversion (OPTIONAL)
  float divider = 1;
  // minimum value (OPTIONAL)
  float min = 1;
  // maximum value (OPTIONAL)
  float max = 100;
  // decimal digits of value (OPTIONAL)
  uint8_t digits = 2;
  // unit (OPTIONAL)
  std::string unit = "";

  // Home Assistant
  // support for auto discovery (OPTIONAL)
  bool ha = false;
  // component type (OPTIONAL)
  std::string ha_component = "";
  // device class (OPTIONAL)
  std::string ha_device_class = "";
  // entity category (OPTIONAL)
  std::string ha_entity_category = "";
  // mode (OPTIONAL)
  std::string ha_mode = "auto";
  // options as pairs of "key":"value" (OPTIONAL)
  std::map<int, std::string> ha_key_value_map = {};
  // options default key (OPTIONAL)
  int ha_default_key = 0;
  // payload for ON state (OPTIONAL)
  uint8_t ha_payload_on = 1;
  // payload for OFF state (OPTIONAL)
  uint8_t ha_payload_off = 0;
  // state class (OPTIONAL)
  std::string ha_state_class = "";
  // step value (OPTIONAL)
  float ha_step = 1;

  // Field types for evaluation
  enum FieldType {
    FT_String,
    FT_HexString,
    FT_Bool,
    FT_Int,
    FT_Float,
    FT_Uint8T,
    FT_Uint32T,
    FT_SizeT,
    FT_DataType,
    FT_KeyValueMap
  };

  // Structure for field evaluation
  struct FieldEvaluation {
    const char* name;
    bool required;
    FieldType type;
  };

  static const std::string isFieldValid(const JsonDocument& doc,
                                        const std::string& field, bool required,
                                        FieldType type);

  static const std::string isKeyValueMapValid(
      const JsonObjectConst ha_key_value_map);

  const double getDoubleFromVector() const;
  const std::string getStringFromVector() const;

  const std::vector<uint8_t> getVectorFromDouble(double value) const;
  const std::vector<uint8_t> getVectorFromString(
      const std::string& value) const;
};

#endif