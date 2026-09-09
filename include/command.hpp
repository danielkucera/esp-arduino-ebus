#pragma once

#if defined(EBUS_INTERNAL)

#include <ebus/data_types.hpp>
#include <ebus/detail/json_reader.hpp>
#include <ebus/static_vector.hpp>
#include <ebus/types.hpp>
#include <limits>
#include <string>
#include <string_view>

#include "data_profile.hpp"
#include "ha_profile.hpp"
#include "string_pool.hpp"

namespace command_types {

#ifndef COMMAND_MAX_FIELDS
inline constexpr size_t max_fields = 4;
#else
inline constexpr size_t max_fields = COMMAND_MAX_FIELDS;
#endif

struct FieldRef {
  uint8_t name_id = 0;
  uint8_t profile_idx = 0;
  uint8_t position = 0;
  // Home Assistant - per-field (0 = no HA profile)
  uint8_t ha_profile_idx = 0;
};

using FieldVector = ebus::StaticVector<FieldRef, max_fields>;
}  // namespace command_types

namespace ebus::detail {
class JsonWriter;
}

using PollSequence =
    ebus::SequenceImpl<ebus::detail::SequenceLimits::poll_capacity>;

class Command {
 public:
  const uint32_t& getSessionId() const;
  void setSessionId(const uint32_t id);

  const uint16_t& getPollId() const;
  void setPollId(const uint16_t id);

  const uint32_t& getLast() const;
  void setLast(const uint32_t time);

  ebus::ByteView getData() const;
  void setData(ebus::ByteView data);

  std::string_view getKey() const;
  uint8_t getKeyId() const;
  std::string_view getName() const;
  ebus::ByteView getReadCmd() const;

  bool hasWriteCmd() const;
  ebus::ByteView getWriteCmd(const class CommandManager& command_manager) const;
  void setWriteCmd(PollSequence&& cmd, class CommandManager& command_manager);

  bool getActive() const;
  const uint16_t& getInterval() const;
  bool getMaster() const;

  const command_types::FieldVector& getFields() const;
  const DataProfile* getFieldProfile(size_t i) const;
  const char* getFieldName(size_t i) const;
  size_t getFieldPosition(size_t i) const;
  ebus::DataType getFieldDatatype(size_t i) const;
  float getFieldDivider(size_t i) const;
  float getFieldMin(size_t i) const;
  float getFieldMax(size_t i) const;
  uint8_t getFieldDigits(size_t i) const;
  const char* getFieldUnit(size_t i) const;
  size_t getFieldCount() const;
  size_t getFieldIndex(std::string_view name) const;

  // Per-field HA accessors
  bool hasFieldHA(size_t i) const;
  const HAProfile* getFieldHAProfile(size_t i) const;
  std::string_view getFieldHAProfileName(size_t i) const;

  bool matches(ebus::ByteView master_view) const;

  void writeFieldValue(ebus::detail::JsonWriter& writer, size_t i) const;
  void writeValuePayload(ebus::detail::JsonWriter& writer) const;

  void getValueJson(ebus::detail::JsonWriter& writer) const;

  ebus::Sequence getVectorFromJson(std::string_view json) const;
  ebus::Sequence getVectorFromValue(std::string_view value_json,
                                    size_t field_idx = 0) const;
  ebus::Sequence getVectorFromDouble(double value, size_t field_idx = 0) const;
  ebus::Sequence getVectorFromString(std::string_view value,
                                     size_t field_idx = 0) const;

  size_t writeLogMessage(char* buf, size_t len) const;

  static Command fromJson(ebus::detail::JsonReader& reader);
  static Command fromTabular(ebus::detail::JsonReader& reader);

  static std::string_view evaluate(ebus::detail::JsonReader& reader);

 private:
  uint32_t session_id_ = 0;
  uint16_t poll_id_ = 0;
  uint32_t last_ = 0;
  PollSequence data_;

  uint8_t key_id_ = 0;
  uint8_t name_id_ = 0;
  PollSequence read_cmd_ = {};
  uint8_t write_cmd_idx_ =
      0;  // 0 = none, 1-based index into CommandMmanager::write_cmds_
  uint16_t interval_ = 60;
  bool master_ = false;

  command_types::FieldVector fields_;
};

#endif