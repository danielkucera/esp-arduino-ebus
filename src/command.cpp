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

#include "ha_profile.hpp"
#include "logger.hpp"

const uint32_t& Command::getPollId() const { return poll_id_; }

void Command::setPollId(const uint32_t id) { poll_id_ = id; }

const uint32_t& Command::getLast() const { return last_; }

void Command::setLast(const uint32_t time) { last_ = time; }

const ebus::Sequence& Command::getData() const { return data_; }

void Command::setData(ebus::ByteView data) { this->data_.assign(data); }

size_t Command::getLength() const { return length_; }

bool Command::getNumeric() const { return numeric_; }

std::string_view Command::getKey() const { return key_; }

std::string_view Command::getName() const { return name_; }

const ebus::Sequence& Command::getReadCmd() const { return read_cmd_; }

const ebus::Sequence& Command::getWriteCmd() const { return write_cmd_; }

const bool& Command::getActive() const { return active_; }

const uint32_t& Command::getInterval() const { return interval_; }

const bool& Command::getMaster() const { return master_; }

const size_t& Command::getPosition() const { return position_; }

const ebus::DataType& Command::getDatatype() const { return datatype_; }

const float& Command::getDivider() const { return divider_; }

const float& Command::getMin() const { return min_; }

const float& Command::getMax() const { return max_; }

const uint8_t& Command::getDigits() const { return digits_; }

std::string_view Command::getUnit() const { return unit_; }

const bool& Command::getHA() const { return ha_; }

const command_types::ProfileFS& Command::getHAProfile() const {
  return ha_profile_;
}

bool Command::matches(ebus::ByteView master_view) const {
  // eBUS service identification (ZZ PB SB...) starts at index 1
  // (Index 0 is Source). Fixed-offset matching is precise and fast.
  return ebus::matches(master_view, read_cmd_, 1);
}

void Command::getValueJson(ebus::detail::JsonWriter& writer) const {
  auto decoded = ebus::decode(datatype_, data_);
  if (!decoded || ebus::isNull(*decoded)) {
    writer.writeRaw("null");
  } else {
    if (numeric_) {
      float val =
          ebus::roundDigits(ebus::asFloat(*decoded) / divider_, digits_);
      writer.writeValueFloat(val);
    } else {
      const auto meta = getMetaCached();
      if (meta && std::string_view(meta->name).find("HEX") == 0) {
        writer.writeHexValue(data_);
      } else {
        writer.writeValue(ebus::asString(*decoded));
      }
    }
  }
}

ebus::Sequence Command::getVectorFromJson(std::string_view json) const {
  ebus::detail::JsonReader reader(json);
  return reader.findKey("value") ? getVectorFromValue(reader.rawValue())
                                 : ebus::Sequence{};
}

ebus::Sequence Command::getVectorFromValue(std::string_view val_view) const {
  if (val_view.empty()) return {};

  if (numeric_) {
    double val = ebus::toNum<double>(val_view);
    if ((val >= min_) && (val <= max_)) {
      return getVectorFromDouble(val);
    }
  } else {
    std::string_view s = val_view;
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
      s.remove_prefix(1);
      s.remove_suffix(1);
    }
    return getVectorFromString(s);
  }
  return {};
}

double Command::getDoubleFromVector() const {
  if (data_.empty()) return 0.0;
  auto decoded = ebus::decode(datatype_, data_);
  if (!decoded || ebus::isNull(*decoded)) return 0.0;

  return ebus::roundDigits(ebus::asFloat(*decoded) / divider_, digits_);
}

const std::string Command::getStringFromVector() const {
  if (data_.empty()) return "";
  auto decoded = ebus::decode(datatype_, data_);
  if (!decoded || ebus::isNull(*decoded)) return "";

  const auto meta = getMetaCached();
  if (meta && std::string_view(meta->name).find("HEX") == 0) {
    return ebus::toHexString(*decoded, 0);
  }
  return ebus::asString(*decoded);
}

void Command::toJson(ebus::detail::JsonWriter& writer) const {
  auto scope = writer.objectScope();
  // Command Fields
  writer.writeField("key", key_);
  writer.writeField("name", name_);
  writer.writeHexField("read_cmd", read_cmd_);
  writer.writeHexField("write_cmd", write_cmd_);
  writer.writeField("active", active_);
  writer.writeField("interval", interval_);

  // Data Fields
  writer.writeField("master", master_);
  writer.writeField("position", position_);
  writer.writeField("datatype", ebus::dataTypeToString(datatype_));
  writer.writeFieldFloat("divider", divider_);
  writer.writeFieldFloat("min", min_);
  writer.writeFieldFloat("max", max_);
  writer.writeField("digits", digits_);
  writer.writeField("unit", unit_);

  // Home Assistant
  writer.writeField("ha", ha_);
  writer.writeField("ha_profile", ha_profile_);
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
      command.key_.assign(r.value());
    else if (key == "name")
      command.name_.assign(r.value());
    else if (key == "read_cmd")
      command.read_cmd_.assign(ebus::toVector(r.value()));
    else if (key == "write_cmd")
      command.write_cmd_.assign(ebus::toVector(r.value()));
    else if (key == "active")
      command.active_ = r.asBool();
    else if (key == "interval")
      command.interval_ = r.asNum<uint32_t>();
    else if (key == "master")
      command.master_ = r.asBool();
    else if (key == "position")
      command.position_ = r.asNum<size_t>();
    else if (key == "datatype") {
      char dt_buf[32];
      size_t dt_len = std::min(r.value().size(), sizeof(dt_buf) - 1);
      std::memcpy(dt_buf, r.value().data(), dt_len);
      dt_buf[dt_len] = '\0';
      command.datatype_ = ebus::stringToDataType(dt_buf);
      command.length_ = ebus::sizeOfDataType(command.datatype_);
      command.numeric_ = ebus::isNumeric(command.datatype_);
    } else if (key == "divider")
      command.divider_ = r.asNum<float>();
    else if (key == "min")
      command.min_ = r.asNum<float>();
    else if (key == "max")
      command.max_ = r.asNum<float>();
    else if (key == "digits")
      command.digits_ = r.asNum<uint8_t>();
    else if (key == "unit")
      command.unit_.assign(r.value());
    else if (key == "ha")
      command.ha_ = r.asBool();
    else if (key == "ha_profile")
      command.ha_profile_.assign(r.value());
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
        command.key_.assign(reader.value());
        break;
      case 1:
        command.name_.assign(reader.value());
        break;
      case 2:
        command.read_cmd_.assign(ebus::toVector(reader.value()));
        break;
      case 3:
        command.write_cmd_.assign(ebus::toVector(reader.value()));
        break;
      case 4:
        command.active_ = reader.asBool();
        break;
      case 5:
        command.interval_ = reader.asNum<uint32_t>();
        break;
      case 6:
        command.master_ = reader.asBool();
        break;
      case 7:
        command.position_ = reader.asNum<size_t>();
        break;
      case 8: {
        char dt_buf[32];
        size_t dt_len = std::min(reader.value().size(), sizeof(dt_buf) - 1);
        std::memcpy(dt_buf, reader.value().data(), dt_len);
        dt_buf[dt_len] = '\0';
        command.datatype_ = ebus::stringToDataType(dt_buf);
        command.length_ = ebus::sizeOfDataType(command.datatype_);
        command.numeric_ = ebus::isNumeric(command.datatype_);
      } break;
      case 9:
        command.divider_ = reader.asNum<float>();
        break;
      case 10:
        command.min_ = reader.asNum<float>();
        break;
      case 11:
        command.max_ = reader.asNum<float>();
        break;
      case 12:
        command.digits_ = reader.asNum<uint8_t>();
        break;
      case 13:
        command.unit_.assign(reader.value());
        break;
      case 14:
        command.ha_ = reader.asBool();
        break;
      case 15:
        command.ha_profile_.assign(reader.value());
        break;
      case 16:
        reader.skipComposite(token);
        break;
      case 17:
        reader.skipComposite(token);
        break;
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
         master = false, position = false, datatype = false;
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
      if (key == "read_cmd")
        met.read_cmd = (token == ebus::detail::JsonReader::Token::string);
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
      } else if (key == "read_cmd")
        error = "Invalid type for field: read_cmd";
    } else if (key == "active") {
      met.active = (token == ebus::detail::JsonReader::Token::boolean);
      if (!met.active) error = "Invalid type for field: active";
    } else if (key == "master") {
      met.master = (token == ebus::detail::JsonReader::Token::boolean);
      if (!met.master) error = "Invalid type for field: master";
    } else if (key == "position") {
      met.position = (token == ebus::detail::JsonReader::Token::number);
      if (!met.position) error = "Invalid type for field: position";
    } else if (key == "datatype") {
      char dt_buf[32];
      size_t dt_len = std::min(r.value().size(), sizeof(dt_buf) - 1);
      std::memcpy(dt_buf, r.value().data(), dt_len);
      dt_buf[dt_len] = '\0';
      met.datatype = (token == ebus::detail::JsonReader::Token::string &&
                      ebus::stringToDataType(dt_buf) != ebus::DataType::error);
      if (!met.datatype) error = "Invalid datatype for field: datatype";
    } else if (key == "ha_profile") {
      if (token != ebus::detail::JsonReader::Token::string)
        error = "Invalid type for field: ha_profile";
    } else if (key == "interval") {
      if (token != ebus::detail::JsonReader::Token::number)
        error = "Invalid type for field: " + std::string(key);
    }
    return error.empty();
  });

  if (!error.empty()) return error;
  if (!met.key) return "Missing required field: key";
  if (!met.name) return "Missing required field: name";
  if (!met.read_cmd) return "Missing required field: read_cmd";
  if (!met.active) return "Missing required field: active";
  if (!met.master) return "Missing required field: master";
  if (!met.position) return "Missing required field: position";
  if (!met.datatype) return "Missing required field: datatype";

  return "";
}

const ebus::DataTypeInfo* Command::getMetaCached() const {
  if (!_cachedMeta.has_value()) {
    _cachedMeta = ebus::getMeta(datatype_);
  }
  return _cachedMeta.has_value() ? &_cachedMeta.value() : nullptr;
}

ebus::Sequence Command::getVectorFromDouble(double value) const {
  double scaledValue = ebus::roundDigits(value * divider_, digits_);

  ebus::DataValue dv;
  dv = static_cast<float>(scaledValue);

  return ebus::encode(datatype_, dv);
}

ebus::Sequence Command::getVectorFromString(std::string_view value) const {
  const auto meta = getMetaCached();
  if (!meta) return ebus::Sequence{};

  ebus::DataValue dv;
  if (std::string(meta->name).find("HEX") == 0) {
    dv = ebus::byteToChar(ebus::toVector(value));
  } else {
    dv = std::string(value.substr(0, length_));
  }

  return ebus::encode(datatype_, dv);
}

#endif
