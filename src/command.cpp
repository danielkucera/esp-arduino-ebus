#if defined(EBUS_INTERNAL)
#include "command.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <ebus/detail/json_writer.hpp>
#include <ebus/utils.hpp>
#include <limits>
#include <regex>

#include "data_profile.hpp"
#include "ha_profile.hpp"
#include "logger.hpp"
#include "store.hpp"

const uint16_t& Command::getPollId() const { return poll_id_; }

void Command::setPollId(const uint16_t id) { poll_id_ = id; }

const uint32_t& Command::getLast() const { return last_; }

void Command::setLast(const uint32_t time) { last_ = time; }

ebus::ByteView Command::getData() const {
  return ebus::ByteView(data_.data(), data_.size());
}

void Command::setData(ebus::ByteView data) { this->data_.assign(data); }

const command_types::FieldVector& Command::getFields() const { return fields_; }

const DataProfile* Command::getFieldProfile(size_t i) const {
  if (i >= fields_.size()) return nullptr;
  return getProfileByIndex(fields_[i].profile_idx);
}

const char* Command::getFieldName(size_t i) const {
  if (i >= fields_.size()) return nullptr;
  auto sv = StringPool::instance().lookup(fields_[i].name_id);
  return sv.empty() ? nullptr : sv.data();
}

size_t Command::getFieldPosition(size_t i) const {
  if (i >= fields_.size()) return 1;
  // Bits 0-3: position (1-16)
  return (fields_[i].pos_master & 0x0F) ? (fields_[i].pos_master & 0x0F) : 1;
}

bool Command::getFieldMaster(size_t i) const {
  if (i >= fields_.size()) return false;
  // Bit 7: master flag
  return (fields_[i].pos_master & 0x80) != 0;
}

ebus::DataType Command::getFieldDatatype(size_t i) const {
  auto* p = getFieldProfile(i);
  if (!p) return ebus::DataType::hex1;
  char buf[32];
  size_t len = std::min(std::strlen(p->datatype), sizeof(buf) - 1);
  std::memcpy(buf, p->datatype, len);
  buf[len] = '\0';
  return ebus::stringToDataType(buf);
}

float Command::getFieldDivider(size_t i) const {
  auto* p = getFieldProfile(i);
  return p ? p->divider : 1.0f;
}

float Command::getFieldMin(size_t i) const {
  auto* p = getFieldProfile(i);
  return p ? p->min : 0.0f;
}

float Command::getFieldMax(size_t i) const {
  auto* p = getFieldProfile(i);
  return p ? p->max : 0.0f;
}

uint8_t Command::getFieldDigits(size_t i) const {
  auto* p = getFieldProfile(i);
  return p ? p->digits : 2;
}

const char* Command::getFieldUnit(size_t i) const {
  auto* p = getFieldProfile(i);
  return p ? p->unit : "";
}

size_t Command::getFieldCount() const { return fields_.size(); }

size_t Command::getFieldIndex(std::string_view name) const {
  for (size_t i = 0; i < fields_.size(); i++) {
    if (StringPool::instance().lookup(fields_[i].name_id) == name) return i;
  }
  return 0;
}

std::string_view Command::getKey() const {
  return StringPool::instance().lookup(key_id_);
}

std::string_view Command::getName() const {
  return StringPool::instance().lookup(name_id_);
}

ebus::ByteView Command::getReadCmd() const {
  return ebus::ByteView(read_cmd_.data(), read_cmd_.size());
}

ebus::ByteView Command::getWriteCmd(const Store& store) const {
  if (write_cmd_idx_ == 0) return {};
  return store.getWriteCmd(write_cmd_idx_ - 1);
}

void Command::setWriteCmd(PollSequence&& cmd, Store& store) {
  if (cmd.empty()) {
    write_cmd_idx_ = 0;
    return;
  }
  // Try to find existing matching write_cmd
  for (size_t i = 0; i < store.getWriteCmdCount(); ++i) {
    if (store.getWriteCmd(i) == cmd) {
      write_cmd_idx_ = static_cast<uint8_t>(i + 1);
      return;
    }
  }
  // Add new write_cmd if there's space
  if (store.addWriteCmd(std::move(cmd))) {
    write_cmd_idx_ = static_cast<uint8_t>(store.getWriteCmdCount());
  } else {
    write_cmd_idx_ = 0;  // No space
  }
}

bool Command::hasWriteCmd() const { return write_cmd_idx_ != 0; }

bool Command::getFieldHA(size_t i) const {
  if (i >= fields_.size()) return false;
  return fields_[i].ha_profile_idx != 0;
}

const bool& Command::getActive() const { return active_; }

const uint16_t& Command::getInterval() const { return interval_; }

const HAProfile* Command::getFieldHAProfile(size_t i) const {
  if (i >= fields_.size()) return nullptr;
  return getHAProfileByIndex(fields_[i].ha_profile_idx);
}

std::string_view Command::getFieldHAProfileName(size_t i) const {
  if (i >= fields_.size()) return {};
  const HAProfile* p = getHAProfileByIndex(fields_[i].ha_profile_idx);
  return p ? p->name : std::string_view();
}

bool Command::matches(ebus::ByteView master_view) const {
  return ebus::matches(master_view, read_cmd_, 1);
}

void Command::getValueJson(ebus::detail::JsonWriter& writer) const {
  if (fields_.empty()) {
    writer.writeRaw("null");
    return;
  }

  if (fields_.size() == 1) {
    const auto* profile = getFieldProfile(0);
    if (!profile) {
      writer.writeRaw("null");
      return;
    }
    auto dt = getFieldDatatype(0);
    auto decoded = ebus::decode(dt, ebus::range(data_, 0, data_.size()));
    if (!decoded || ebus::isNull(*decoded)) {
      writer.writeRaw("null");
    } else {
      bool numeric = ebus::isNumeric(dt);
      if (numeric) {
        float val = ebus::roundDigits(
            ebus::asFloat(*decoded) / getFieldDivider(0), getFieldDigits(0));
        writer.writeValueFloat(val);
      } else {
        // Check if datatype is HEX type (no need for cached meta)
        char buf[32];
        size_t len = std::min(std::strlen(profile->datatype), sizeof(buf) - 1);
        std::memcpy(buf, profile->datatype, len);
        buf[len] = '\0';
        if (std::string_view(buf).find("HEX") == 0) {
          writer.writeHexValue(data_);
        } else {
          writer.writeValue(ebus::asString(*decoded));
        }
      }
    }
  } else {
    auto scope = writer.objectScope();
    for (size_t i = 0; i < fields_.size(); i++) {
      writer.appendKey(StringPool::instance().lookup(fields_[i].name_id));
      const auto* profile = getFieldProfile(i);
      if (!profile) {
        writer.writeRaw("null");
        continue;
      }
      auto dt = getFieldDatatype(i);
      size_t field_len = ebus::sizeOfDataType(dt);
      size_t field_pos = getFieldPosition(i) - 1;
      auto field_data = ebus::range(data_, field_pos, field_len);
      auto decoded = ebus::decode(dt, field_data);
      if (!decoded || ebus::isNull(*decoded)) {
        writer.writeRaw("null");
      } else {
        bool numeric = ebus::isNumeric(dt);
        if (numeric) {
          float val = ebus::roundDigits(
              ebus::asFloat(*decoded) / getFieldDivider(i), getFieldDigits(i));
          writer.writeValueFloat(val);
        } else {
          char buf[32];
          size_t len =
              std::min(std::strlen(profile->datatype), sizeof(buf) - 1);
          std::memcpy(buf, profile->datatype, len);
          buf[len] = '\0';
          if (std::string_view(buf).find("HEX") == 0) {
            writer.writeHexValue(field_data);
          } else {
            writer.writeValue(ebus::asString(*decoded));
          }
        }
      }
    }
  }
}

ebus::Sequence Command::getVectorFromJson(std::string_view json) const {
  ebus::detail::JsonReader reader(json);
  if (reader.findKey("value")) {
    reader.next();
    return getVectorFromValue(reader.rawValue());
  }
  // Try parsing as full JSON object with field selection
  {
    ebus::detail::JsonReader r2(json);
    if (r2.next() == ebus::detail::JsonReader::Token::object_start) {
      if (r2.findKey("value")) {
        r2.next();
        return getVectorFromValue(r2.rawValue());
      }
    }
  }
  return ebus::Sequence{};
}

ebus::Sequence Command::getVectorFromValue(std::string_view val_view,
                                           size_t field_idx) const {
  if (val_view.empty() || field_idx >= fields_.size()) return {};

  if (val_view.size() >= 2 && val_view.front() == '{' &&
      val_view.back() == '}') {
    ebus::detail::JsonReader reader(val_view);
    if (reader.next() == ebus::detail::JsonReader::Token::object_start) {
      reader.forEachField(
          [&](std::string_view fkey, ebus::detail::JsonReader& fr) {
            fr.next();
            if (fkey == "field") {
              field_idx = getFieldIndex(fr.value());
            }
            return true;
          });
    }
  }

  if (field_idx >= fields_.size()) return {};
  const auto* profile = getFieldProfile(field_idx);
  if (!profile) return {};

  auto dt = getFieldDatatype(field_idx);
  bool is_num = ebus::isNumeric(dt);

  if (is_num) {
    double val = ebus::toNum<double>(val_view);
    float min_v = getFieldMin(field_idx);
    float max_v = getFieldMax(field_idx);
    if ((val >= min_v) && (val <= max_v)) {
      return getVectorFromDouble(val, field_idx);
    }
  } else {
    std::string_view s = val_view;
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
      s.remove_prefix(1);
      s.remove_suffix(1);
    }
    return getVectorFromString(s, field_idx);
  }
  return {};
}

ebus::Sequence Command::getVectorFromDouble(double value,
                                            size_t field_idx) const {
  if (field_idx >= fields_.size()) return {};
  const auto* profile = getFieldProfile(field_idx);
  if (!profile) return {};

  double scaledValue =
      ebus::roundDigits(value * profile->divider, profile->digits);
  ebus::DataValue dv;
  dv = static_cast<float>(scaledValue);
  return ebus::encode(getFieldDatatype(field_idx), dv);
}

ebus::Sequence Command::getVectorFromString(std::string_view value,
                                            size_t field_idx) const {
  if (field_idx >= fields_.size()) return {};
  const auto* profile = getFieldProfile(field_idx);
  if (!profile) return {};

  auto dt = getFieldDatatype(field_idx);
  ebus::DataValue dv;
  std::string dt_name = ebus::dataTypeToString(dt);
  if (dt_name.find("HEX") == 0) {
    uint8_t hex_buf[256];
    size_t hex_len = ebus::toBytes(value, hex_buf, sizeof(hex_buf));
    dv = ebus::byteToChar(ebus::ByteView(hex_buf, hex_len));
  } else {
    dv = std::string(value.substr(0, ebus::sizeOfDataType(dt)));
  }
  return ebus::encode(dt, dv);
}

double Command::getDoubleFromVector() const {
  if (fields_.empty() || data_.empty()) return 0.0;
  auto dt = getFieldDatatype(0);
  auto decoded = ebus::decode(dt, ebus::range(data_, 0, data_.size()));
  if (!decoded || ebus::isNull(*decoded)) return 0.0;
  return ebus::roundDigits(ebus::asFloat(*decoded) / getFieldDivider(0),
                           getFieldDigits(0));
}

const std::string Command::getStringFromVector() const {
  if (fields_.empty() || data_.empty()) return "";
  auto dt = getFieldDatatype(0);
  auto decoded = ebus::decode(dt, ebus::range(data_, 0, data_.size()));
  if (!decoded || ebus::isNull(*decoded)) return "";

  std::string dt_name = ebus::dataTypeToString(dt);
  if (dt_name.find("HEX") == 0) {
    return ebus::toHexString(*decoded, 0);
  }
  return ebus::asString(*decoded);
}

Command Command::fromJson(ebus::detail::JsonReader& reader) {
  Command command;
  if (reader.next() != ebus::detail::JsonReader::Token::object_start) {
    logger.error("Command: fromJson failed - expected object_start");
    return command;
  }

  reader.forEachField([&](std::string_view key, ebus::detail::JsonReader& r) {
    auto token = r.next();
    if (key == "key")
      command.key_id_ = StringPool::instance().intern(r.value());
    else if (key == "name")
      command.name_id_ = StringPool::instance().intern(r.value());
    else if (key == "read_cmd") {
      uint8_t hex_buf[64];
      size_t hex_len = ebus::toBytes(r.value(), hex_buf, sizeof(hex_buf));
      command.read_cmd_.assign(ebus::ByteView(hex_buf, hex_len));
    } else if (key == "write_cmd") {
      uint8_t hex_buf[64];
      size_t hex_len = ebus::toBytes(r.value(), hex_buf, sizeof(hex_buf));
      command.write_cmd_temp_.assign(ebus::ByteView(hex_buf, hex_len));
    } else if (key == "active")
      command.active_ = r.asBool();
    else if (key == "interval")
      command.interval_ = r.asNum<uint16_t>();
    else if (key == "ha_profile") {
      const HAProfile* p = findHAProfile(r.value());
      /* command-level ha_profile ignored - per-field HA in fields array */
      (void)p;
    } else if (key == "fields") {
      if (token == ebus::detail::JsonReader::Token::array_start) {
        while (true) {
          auto field_token = r.next();
          if (field_token == ebus::detail::JsonReader::Token::array_end ||
              field_token == ebus::detail::JsonReader::Token::end ||
              field_token == ebus::detail::JsonReader::Token::error)
            break;
          if (field_token != ebus::detail::JsonReader::Token::object_start)
            break;
          command_types::FieldRef field;
          r.forEachField(
              [&](std::string_view fkey, ebus::detail::JsonReader& fr) {
                fr.next();
                if (fkey == "name")
                  field.name_id = StringPool::instance().intern(fr.value());
                else if (fkey == "profile") {
                  const DataProfile* p = findDataProfile(fr.value());
                  field.profile_idx = p ? getProfileIndex(p) : 0;
                } else if (fkey == "position")
                  field.pos_master =
                      static_cast<uint8_t>(fr.asNum<size_t>() & 0x0F);
                else if (fkey == "master") {
                  if (fr.asBool()) field.pos_master |= 0x80;
                } else if (fkey == "ha_profile") {
                  const HAProfile* p = findHAProfile(fr.value());
                  field.ha_profile_idx = p ? getProfileIndexHA(p) : 0;
                }
                return true;
              });
          command.fields_.push_back(field);
        }
      }
    }
    return true;
  });

  command.last_ = 0;
  command.poll_id_ = 0;
  command.data_.clear();
  return command;
}

Command Command::fromTabular(ebus::detail::JsonReader& reader) {
  Command command;
  if (reader.next() != ebus::detail::JsonReader::Token::array_start)
    return command;

  int index = 0;
  while (true) {
    auto token = reader.next();
    if (token == ebus::detail::JsonReader::Token::array_end ||
        token == ebus::detail::JsonReader::Token::end ||
        token == ebus::detail::JsonReader::Token::error)
      break;

    switch (index) {
      case 0:
        command.key_id_ = StringPool::instance().intern(reader.value());
        break;
      case 1:
        command.name_id_ = StringPool::instance().intern(reader.value());
        break;
      case 2: {
        uint8_t hex_buf[64];
        size_t hex_len =
            ebus::toBytes(reader.value(), hex_buf, sizeof(hex_buf));
        command.read_cmd_.assign(ebus::ByteView(hex_buf, hex_len));
        break;
      }  // NOLINT(bugprone-branch-ctl-initializer)
      case 3: {
        uint8_t hex_buf[64];
        size_t hex_len =
            ebus::toBytes(reader.value(), hex_buf, sizeof(hex_buf));
        command.write_cmd_temp_.assign(ebus::ByteView(hex_buf, hex_len));
        break;
      }  // NOLINT(bugprone-branch-ctl-initializer)
      case 4:
        command.active_ = reader.asBool();
        break;
      case 5:
        command.interval_ = reader.asNum<uint16_t>();
        break;
      case 6: {
        if (token == ebus::detail::JsonReader::Token::array_start) {
          while (true) {
            auto ft = reader.next();
            if (ft == ebus::detail::JsonReader::Token::array_end ||
                ft == ebus::detail::JsonReader::Token::end ||
                ft == ebus::detail::JsonReader::Token::error)
              break;
            if (ft != ebus::detail::JsonReader::Token::object_start) break;
            command_types::FieldRef field;
            reader.forEachField(
                [&](std::string_view fkey, ebus::detail::JsonReader& fr) {
                  fr.next();
                  if (fkey == "name")
                    field.name_id = StringPool::instance().intern(fr.value());
                  else if (fkey == "profile") {
                    const DataProfile* p = findDataProfile(fr.value());
                    field.profile_idx = p ? getProfileIndex(p) : 0;
                  } else if (fkey == "position")
                    field.pos_master =
                        static_cast<uint8_t>(fr.asNum<size_t>() & 0x0F);
                  else if (fkey == "master") {
                    if (fr.asBool()) field.pos_master |= 0x80;
                  } else if (fkey == "ha_profile") {
                    const HAProfile* p = findHAProfile(fr.value());
                    field.ha_profile_idx = p ? getProfileIndexHA(p) : 0;
                  }
                  return true;
                });
            command.fields_.push_back(field);
          }
        } else if (token == ebus::detail::JsonReader::Token::string) {
          std::string_view fields_json = reader.value();
          if (!fields_json.empty()) {
            ebus::detail::JsonReader fr(fields_json);
            if (fr.next() == ebus::detail::JsonReader::Token::array_start) {
              while (true) {
                auto ft = fr.next();
                if (ft == ebus::detail::JsonReader::Token::array_end ||
                    ft == ebus::detail::JsonReader::Token::end ||
                    ft == ebus::detail::JsonReader::Token::error)
                  break;
                if (ft != ebus::detail::JsonReader::Token::object_start) break;
                command_types::FieldRef field;
                fr.forEachField([&](std::string_view fkey,
                                    ebus::detail::JsonReader& fr_inner) {
                  fr_inner.next();
                  if (fkey == "name")
                    field.name_id =
                        StringPool::instance().intern(fr_inner.value());
                  else if (fkey == "profile") {
                    const DataProfile* p = findDataProfile(fr_inner.value());
                    field.profile_idx = p ? getProfileIndex(p) : 0;
                  } else if (fkey == "position")
                    field.pos_master =
                        static_cast<uint8_t>(fr_inner.asNum<size_t>() & 0x0F);
                  else if (fkey == "master") {
                    if (fr_inner.asBool()) field.pos_master |= 0x80;
                  } else if (fkey == "ha_profile") {
                    const HAProfile* p = findHAProfile(fr_inner.value());
                    field.ha_profile_idx = p ? getProfileIndexHA(p) : 0;
                  }
                  return true;
                });
                command.fields_.push_back(field);
              }
            }
          }
        }
      } break;
      default:
        reader.skipComposite(token);
        break;
    }
    index++;
  }

  command.last_ = 0;
  command.poll_id_ = 0;
  command.data_.clear();
  return command;
}

const std::string Command::evaluate(ebus::detail::JsonReader& reader) {
  if (reader.next() != ebus::detail::JsonReader::Token::object_start)
    return "Record root is not a JSON object";

  struct {
    bool key = false, name = false, read_cmd = false, active = false,
         fields = false;
  } met;

  std::string error;
  reader.forEachField([&](std::string_view key, ebus::detail::JsonReader& r) {
    auto token = r.next();
    if (key == "key") {
      met.key = (token == ebus::detail::JsonReader::Token::string);
      if (!met.key) error = "Invalid type for field: key";
    } else if (key == "name") {
      met.name = (token == ebus::detail::JsonReader::Token::string);
      if (!met.name) error = "Invalid type for field: name";
    } else if (key == "read_cmd" || key == "write_cmd") {
      met.read_cmd = true;
      if (token == ebus::detail::JsonReader::Token::string) {
        std::string_view hex = r.value();
        if (hex.length() % 2 != 0)
          error = "Invalid hex string length: " + std::string(key);
        else
          for (char c : hex)
            if (!isxdigit((unsigned char)c)) {
              error = "Invalid hex character in: " + std::string(key);
              break;
            }
      } else
        error = "Invalid type for field: " + std::string(key);
    } else if (key == "active") {
      met.active = (token == ebus::detail::JsonReader::Token::boolean);
      if (!met.active) error = "Invalid type for field: active";
    } else if (key == "fields") {
      met.fields = (token == ebus::detail::JsonReader::Token::array_start);
      if (token == ebus::detail::JsonReader::Token::array_start) {
        int field_index = 0;
        while (true) {
          auto ft = r.next();
          if (ft == ebus::detail::JsonReader::Token::array_end ||
              ft == ebus::detail::JsonReader::Token::end ||
              ft == ebus::detail::JsonReader::Token::error)
            break;
          if (ft != ebus::detail::JsonReader::Token::object_start) {
            error = "Expected object in fields array at index " +
                    std::to_string(field_index);
            break;
          }
          bool has_name = false, has_profile = false, has_position = false;
          r.forEachField([&](std::string_view fkey,
                             ebus::detail::JsonReader& fr) {
            auto ftoken = fr.next();
            if (fkey == "name")
              has_name = (ftoken == ebus::detail::JsonReader::Token::string);
            else if (fkey == "profile") {
              has_profile = (ftoken == ebus::detail::JsonReader::Token::string);
              if (has_profile && findDataProfile(fr.value()) == nullptr)
                error = "Unknown data profile: " + std::string(fr.value());
            } else if (fkey == "position")
              has_position =
                  (ftoken == ebus::detail::JsonReader::Token::number);
            else if (fkey == "master") {
              if (ftoken != ebus::detail::JsonReader::Token::boolean)
                error = "Invalid type for field: master";
            } else if (fkey == "ha_profile") {
              if (ftoken != ebus::detail::JsonReader::Token::string)
                error = "Invalid type for field: ha_profile";
            }
            return true;
          });
          if (!has_name)
            error = "Missing required field: fields[" +
                    std::to_string(field_index) + "].name";
          else if (!has_profile)
            error = "Missing required field: fields[" +
                    std::to_string(field_index) + "].profile";
          else if (!has_position)
            error = "Missing required field: fields[" +
                    std::to_string(field_index) + "].position";
          field_index++;
        }
      } else if (token != ebus::detail::JsonReader::Token::end) {
        error = "Invalid type for field: fields";
      }
    } else if (key == "interval") {
      if (token != ebus::detail::JsonReader::Token::number)
        error = "Invalid type for field: interval";
    } else if (key == "ha_profile") {
      if (token != ebus::detail::JsonReader::Token::string)
        error = "Invalid type for field: ha_profile";
    }
    return true;
  });

  if (!error.empty()) return error;
  if (!met.key) return "Missing required field: key";
  if (!met.name) return "Missing required field: name";
  if (!met.read_cmd) return "Missing required field: read_cmd";
  if (!met.active) return "Missing required field: active";
  if (!met.fields) return "Missing required field: fields";

  return "";
}

#endif