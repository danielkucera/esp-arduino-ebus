#pragma once

#if defined(EBUS_INTERNAL)

#include <ebus/data_types.hpp>
#include <ebus/detail/json_reader.hpp>
#include <ebus/static_vector.hpp>
#include <ebus/types.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace command_types {

using KeyFS = ebus::FixedString<8>;
using NameFS = ebus::FixedString<48>;
using ProfileFS = ebus::FixedString<24>;
using UnitFS = ebus::FixedString<8>;
}  // namespace command_types

// Forward declaration for JsonWriter
namespace ebus::detail {
class JsonWriter;
}

// This class represents a command configuration and its associated data

class Command {
 public:
  // Internal fields accessors
  const uint32_t& getPollId() const;
  void setPollId(const uint32_t id);

  const uint32_t& getLast() const;
  void setLast(const uint32_t time);

  const ebus::Sequence& getData() const;
  void setData(ebus::ByteView data);

  size_t getLength() const;

  bool getNumeric() const;

  // Command field accessors
  std::string_view getKey() const;
  std::string_view getName() const;
  const ebus::Sequence& getReadCmd() const;
  const ebus::Sequence& getWriteCmd() const;
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
  std::string_view getUnit() const;

  // Home Assistant field accessors
  const bool& getHA() const;
  const ebus::FixedString<24>& getHAProfile() const;

  /**
   * Checks if the master telegram matches this command's read sequence.
   * The match is performed at index 2 (Primary/Secondary bytes).
   * @param master The master part of the telegram.
   */
  bool matches(ebus::ByteView master_view) const;

  // Data conversion
  void getValueJson(ebus::detail::JsonWriter& writer) const;
  ebus::Sequence getVectorFromJson(std::string_view json) const;
  ebus::Sequence getVectorFromValue(std::string_view value_json) const;

  ebus::Sequence getVectorFromDouble(double value) const;
  ebus::Sequence getVectorFromString(std::string_view value) const;

  // Helpers for direct value access to avoid JSON overhead
  double getDoubleFromVector() const;
  const std::string getStringFromVector() const;

  // Serialization / Deserialization
  void toJson(ebus::detail::JsonWriter& writer) const;
  static Command fromJson(ebus::detail::JsonReader& reader);
  static Command fromTabular(ebus::detail::JsonReader& reader);

  static const std::string evaluate(ebus::detail::JsonReader& reader);
  const ebus::DataTypeInfo* getMetaCached() const;

 private:
  // Internal fields
  // polling id
  uint32_t poll_id_ = 0;
  // last time of the successful command
  uint32_t last_ = 0;
  // received raw data
  ebus::Sequence data_;
  // length of datatype
  size_t length_ = 1;
  // indicates numeric datatype
  bool numeric_ = false;

  // Command fields
  // unique key of command
  command_types::KeyFS key_ = {};
  // name of the command used as mqtt topic below "values/"
  command_types::NameFS name_ = {};
  // read command as vector of "ZZPBSBNNDBx"
  ebus::Sequence read_cmd_ = {};
  // write command as vector of "ZZPBSBNNDBx" (OPTIONAL)
  ebus::Sequence write_cmd_ = {};
  // active sending of command
  bool active_ = false;
  // minimum interval between two commands in seconds (OPTIONAL)
  uint32_t interval_ = 60;

  // Data fields
  // value of interest is in master or slave part
  bool master_ = false;
  // starting position within the data bytes, beginning with 1
  size_t position_ = 1;
  // ebus data type
  ebus::DataType datatype_ = ebus::DataType::hex1;
  // divider for value conversion (OPTIONAL)
  float divider_ = 1;
  // minimum value (OPTIONAL)
  float min_ = 1;
  // maximum value (OPTIONAL)
  float max_ = 100;
  // decimal digits of value (OPTIONAL)
  uint8_t digits_ = 2;
  // unit (OPTIONAL)
  command_types::UnitFS unit_ = {};

  // Home Assistant
  // support for auto discovery (OPTIONAL)
  bool ha_ = false;
  // profile name referencing global HA profile registry
  command_types::ProfileFS ha_profile_ = {};

  mutable std::optional<ebus::DataTypeInfo> _cachedMeta;
};

#endif
