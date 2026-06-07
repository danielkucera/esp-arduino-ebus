#if defined(EBUS_INTERNAL)
#include "Command.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <ebus/detail/json_writer.hpp>
#include <ebus/utils.hpp>
#include <limits>
#include <regex>
#include "Logger.hpp"

const uint32_t& Command::getPollId() const { return poll_id; }

void Command::setPollId(const uint32_t id) { poll_id = id; }

const uint32_t& Command::getLast() const { return last; }

void Command::setLast(const uint32_t time) { last = time; }

const ebus::Sequence& Command::getData() const { return data; }

void Command::setData(ebus::ByteView data) { this->data.assign(data); }

size_t Command::getLength() const { return length; }

bool Command::getNumeric() const { return numeric; }

const std::string& Command::getKey() const { return key; }

const std::string& Command::getName() const { return name; }

const ebus::Sequence& Command::getReadCmd() const { return read_cmd; }

const ebus::Sequence& Command::getWriteCmd() const { return write_cmd; }

const bool& Command::getActive() const { return active; }

const uint32_t& Command::getInterval() const { return interval; }

const bool& Command::getMaster() const { return master; }

const size_t& Command::getPosition() const { return position; }

const ebus::DataType& Command::getDatatype() const { return datatype; }

const float& Command::getDivider() const { return divider; }

const float& Command::getMin() const { return min; }

const float& Command::getMax() const { return max; }

const uint8_t& Command::getDigits() const { return digits; }

const std::string& Command::getUnit() const { return unit; }

const bool& Command::getHA() const { return ha; }

const std::string& Command::getHAComponent() const { return ha_component; }

const std::string& Command::getHADeviceClass() const { return ha_device_class; }

const std::string& Command::getHAEntityCategory() const {
  return ha_entity_category;
}

const std::string& Command::getHAMode() const { return ha_mode; }

const std::map<int, std::string>& Command::getHAKeyValueMap() const {
  return ha_key_value_map;
}

const int& Command::getHADefaultKey() const { return ha_default_key; }

const uint8_t& Command::getHAPayloadOn() const { return ha_payload_on; }

const uint8_t& Command::getHAPayloadOff() const { return ha_payload_off; }

const std::string& Command::getHAStateClass() const { return ha_state_class; }

const float& Command::getHAStep() const { return ha_step; }

bool Command::matches(ebus::ByteView master_view) const {
  // eBUS service identification (ZZ PB SB...) starts at index 1 
  // (Index 0 is Source). Fixed-offset matching is precise and fast.
  return ebus::matches(master_view, read_cmd, 1);
}

void Command::getValueJson(ebus::detail::JsonWriter& writer) const {
  auto decoded = ebus::decode(datatype, data);
  if (!decoded || ebus::isNull(*decoded)) {
    writer.writeRaw("null");
  } else {
    if (numeric) {
      // Optimization: Calculate directly from the decoded value to avoid double
      // decoding
      float val = ebus::roundDigits(ebus::asFloat(*decoded) / divider, digits);
      writer.writeValueFloat(val);
    } else {
      const auto meta = getMetaCached();
      if (meta && std::string_view(meta->name).find("HEX") == 0) {
        writer.writeHexValue(data);
      } else {
        // asString returns a const reference to the string inside the variant;
        // zero-copy to JsonWriter
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

  if (numeric) {
    double val = ebus::toNum<double>(val_view);
    if ((val >= min) && (val <= max)) {
      return getVectorFromDouble(val);
    }
  } else {
    std::string_view s = val_view;
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
      s.remove_prefix(1);
      s.remove_suffix(1);
    }
    return getVectorFromString(std::string(s));
  }
  return {};
}

double Command::getDoubleFromVector() const {
  if (data.empty()) return 0.0;
  auto decoded = ebus::decode(datatype, data);
  if (!decoded || ebus::isNull(*decoded)) return 0.0;

  return ebus::roundDigits(ebus::asFloat(*decoded) / divider, digits);
}

const std::string Command::getStringFromVector() const {
  if (data.empty()) return "";
  auto decoded = ebus::decode(datatype, data);
  if (!decoded || ebus::isNull(*decoded)) return "";

  const auto meta = getMetaCached();
  if (meta && std::string_view(meta->name).find("HEX") == 0) {
    return ebus::toHexString(*decoded, 0);
  }
  return ebus::asString(*decoded);
}

void Command::toJson(ebus::detail::JsonWriter& writer) const {
  writer.startObject();
  // Command Fields
  writer.writeField("key", key);
  writer.writeField("name", name);
  writer.writeHexField("read_cmd", read_cmd);
  writer.writeHexField("write_cmd", write_cmd);
  writer.writeField("active", active);
  writer.writeField("interval", interval);

  // Data Fields
  writer.writeField("master", master);
  writer.writeField("position", position);
  writer.writeField("datatype", ebus::dataTypeToString(datatype));
  writer.writeFieldFloat("divider", divider);
  writer.writeFieldFloat("min", min);
  writer.writeFieldFloat("max", max);
  writer.writeField("digits", digits);
  writer.writeField("unit", unit);

  // Home Assistant
  writer.writeField("ha", ha);
  writer.writeField("ha_component", ha_component);
  writer.writeField("ha_device_class", ha_device_class);
  writer.writeField("ha_entity_category", ha_entity_category);
  writer.writeField("ha_mode", ha_mode);

  writer.appendKey("ha_key_value_map");
  writer.startObject();
  for (const auto& kv : ha_key_value_map) {
    // Optimization: Avoid std::to_string heap allocation for every map entry
    char keyBuf[12];
    auto [ptr, ec] = std::to_chars(keyBuf, keyBuf + sizeof(keyBuf), kv.first);
    if (ec == std::errc{}) {
      writer.writeField(std::string_view(keyBuf, ptr - keyBuf), kv.second);
    }
  }
  writer.endObject();

  writer.writeField("ha_default_key", ha_default_key);
  writer.writeField("ha_payload_on", ha_payload_on);
  writer.writeField("ha_payload_off", ha_payload_off);
  writer.writeField("ha_state_class", ha_state_class);
  writer.writeFieldFloat("ha_step", ha_step);
  writer.endObject();
}

Command Command::fromJson(ebus::detail::JsonReader& reader) {
  Command command;
  if (reader.next() != ebus::detail::JsonReader::Token::ObjectStart)
  {
    logger.error("Command: fromJson failed - expected ObjectStart");
    return command;
  }

  reader.forEachField([&](std::string_view key, ebus::detail::JsonReader& r) {
    auto token = r.next();
    if (key == "key")
      command.key = r.value();
    else if (key == "name")
      command.name = r.value();
    else if (key == "read_cmd")
      command.read_cmd.assign(ebus::toVector(std::string(r.value())));
    else if (key == "write_cmd")
      command.write_cmd.assign(ebus::toVector(std::string(r.value())));
    else if (key == "active")
      command.active = r.asBool();
    else if (key == "interval")
      command.interval = r.asNum<uint32_t>();
    else if (key == "master")
      command.master = r.asBool();
    else if (key == "position")
      command.position = r.asNum<size_t>();
    else if (key == "datatype") {
      command.datatype = ebus::stringToDataType(std::string(r.value()).c_str());
      command.length = ebus::sizeOfDataType(command.datatype);
      command.numeric = ebus::isNumeric(command.datatype);
    } else if (key == "divider")
      command.divider = r.asNum<float>();
    else if (key == "min")
      command.min = r.asNum<float>();
    else if (key == "max")
      command.max = r.asNum<float>();
    else if (key == "digits")
      command.digits = r.asNum<uint8_t>();
    else if (key == "unit")
      command.unit = r.value();
    else if (key == "ha")
      command.ha = r.asBool();
    else if (key == "ha_component")
      command.ha_component = r.value();
    else if (key == "ha_device_class")
      command.ha_device_class = r.value();
    else if (key == "ha_entity_category")
      command.ha_entity_category = r.value();
    else if (key == "ha_mode")
      command.ha_mode = r.value();
    else if (key == "ha_key_value_map") {
      if (token == ebus::detail::JsonReader::Token::ObjectStart) {
        r.forEachField([&](std::string_view k,
                           ebus::detail::JsonReader& map_r) {
          if (map_r.next() == ebus::detail::JsonReader::Token::String) {
            std::string key_str(k);
            char* endptr;
            errno = 0;
            long val = std::strtol(key_str.c_str(), &endptr, 10);
            if (endptr != key_str.c_str() && *endptr == '\0' && errno == 0) {
              command.ha_key_value_map[static_cast<int>(val)] = map_r.value();
            }
          }
          return true;
        });
      }
    } else if (key == "ha_default_key")
      command.ha_default_key = r.asNum<int>();
    else if (key == "ha_payload_on")
      command.ha_payload_on = r.asNum<uint8_t>();
    else if (key == "ha_payload_off")
      command.ha_payload_off = r.asNum<uint8_t>();
    else if (key == "ha_state_class")
      command.ha_state_class = r.value();
    else if (key == "ha_step")
      command.ha_step = r.asNum<float>();
    return true;
  });

  command.last = 0;
  command.poll_id = 0;
  command.data.clear();
  return command;
}

Command Command::fromTabular(ebus::detail::JsonReader& reader) {
  Command command;
  if (reader.next() != ebus::detail::JsonReader::Token::ArrayStart) return command;

  int index = 0;
  while (true) {
    auto token = reader.next();
    if (token == ebus::detail::JsonReader::Token::ArrayEnd ||
        token == ebus::detail::JsonReader::Token::End ||
        token == ebus::detail::JsonReader::Token::Error)
      break;

    switch (index) {
      case 0: command.key = reader.value(); break;
      case 1: command.name = reader.value(); break;
      case 2: command.read_cmd.assign(ebus::toVector(std::string(reader.value()))); break;
      case 3: command.write_cmd.assign(ebus::toVector(std::string(reader.value()))); break;
      case 4: command.active = reader.asBool(); break;
      case 5: command.interval = reader.asNum<uint32_t>(); break;
      case 6: command.master = reader.asBool(); break;
      case 7: command.position = reader.asNum<size_t>(); break;
      case 8: {
        command.datatype = ebus::stringToDataType(std::string(reader.value()).c_str());
        command.length = ebus::sizeOfDataType(command.datatype);
        command.numeric = ebus::isNumeric(command.datatype);
      } break;
      case 9: command.divider = reader.asNum<float>(); break;
      case 10: command.min = reader.asNum<float>(); break;
      case 11: command.max = reader.asNum<float>(); break;
      case 12: command.digits = reader.asNum<uint8_t>(); break;
      case 13: command.unit = reader.value(); break;
      case 14: command.ha = reader.asBool(); break;
      case 15: command.ha_component = reader.value(); break;
      case 16: command.ha_device_class = reader.value(); break;
      case 17: command.ha_entity_category = reader.value(); break;
      case 18: command.ha_mode = reader.value(); break;
      case 19: {
        if (token == ebus::detail::JsonReader::Token::ObjectStart) {
          reader.forEachField([&](std::string_view k, ebus::detail::JsonReader& r) {
            if (r.next() == ebus::detail::JsonReader::Token::String) {
              std::string key_str(k);
              char* endptr;
              errno = 0;
              long val = std::strtol(key_str.c_str(), &endptr, 10);
              if (endptr != key_str.c_str() && *endptr == '\0' && errno == 0) {
                command.ha_key_value_map[static_cast<int>(val)] = r.value();
              }
            }
            return true;
          });
        } else {
          reader.skipComposite(token);
        }
      } break;
      case 20: command.ha_default_key = reader.asNum<int>(); break;
      case 21: command.ha_payload_on = reader.asNum<uint8_t>(); break;
      case 22: command.ha_payload_off = reader.asNum<uint8_t>(); break;
      case 23: command.ha_state_class = reader.value(); break;
      case 24: command.ha_step = reader.asNum<float>(); break;
      default:
        reader.skipComposite(token);
        break;
    }
    index++;
  }

  command.last = 0;
  command.poll_id = 0;
  command.data.clear();
  return command;
}

const std::string Command::evaluate(ebus::detail::JsonReader& reader) {
  if (reader.next() != ebus::detail::JsonReader::Token::ObjectStart)
    return "Record root is not a JSON object";

  struct {
    bool key = false, name = false, read_cmd = false, active = false,
         master = false, position = false, datatype = false;
  } met;

  std::string error;
  reader.forEachField([&](std::string_view key, ebus::detail::JsonReader& r) {
    auto token = r.next();
    if (key == "key") {
      met.key = (token == ebus::detail::JsonReader::Token::String);
      if (!met.key) error = "Invalid type for field: key";
    } else if (key == "name") {
      met.name = (token == ebus::detail::JsonReader::Token::String);
      if (!met.name) error = "Invalid type for field: name";
    } else if (key == "read_cmd" || key == "write_cmd") {
      if (key == "read_cmd")
        met.read_cmd = (token == ebus::detail::JsonReader::Token::String);
      if (token == ebus::detail::JsonReader::Token::String) {
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
      met.active = (token == ebus::detail::JsonReader::Token::Boolean);
      if (!met.active) error = "Invalid type for field: active";
    } else if (key == "master") {
      met.master = (token == ebus::detail::JsonReader::Token::Boolean);
      if (!met.master) error = "Invalid type for field: master";
    } else if (key == "position") {
      met.position = (token == ebus::detail::JsonReader::Token::Number);
      if (!met.position) error = "Invalid type for field: position";
    } else if (key == "datatype") {
      met.datatype = (token == ebus::detail::JsonReader::Token::String &&
                      ebus::stringToDataType(std::string(r.value()).c_str()) !=
                          ebus::DataType::error);
      if (!met.datatype) error = "Invalid datatype for field: datatype";
    } else if (key == "ha_key_value_map") {
      if (token != ebus::detail::JsonReader::Token::ObjectStart)
        error = "Invalid type for field: ha_key_value_map";
      else
        error = isKeyValueMapValid(r);
    } else if (key == "interval" || key == "ha_default_key" ||
               key == "ha_payload_on" || key == "ha_payload_off") {
      if (token != ebus::detail::JsonReader::Token::Number)
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

const std::string Command::isKeyValueMapValid(
    ebus::detail::JsonReader& reader) {
  std::string error;
  reader.forEachField(
      [&](std::string_view key, ebus::detail::JsonReader& map_r) {
        // Check if the key can be converted to an integer
        std::string key_str(key);
        char* endptr = nullptr;
        errno = 0;
        long long keyValue = std::strtoll(key_str.c_str(), &endptr, 10);
        if (endptr == key_str.c_str() || *endptr != '\0' || errno == ERANGE) {
          error = "Invalid key: " + key_str;
          return false;
        }
        if (keyValue < std::numeric_limits<int>::min() ||
            keyValue > std::numeric_limits<int>::max()) {
          error = "Key out of range: " + key_str;
          return false;
        }
        if (map_r.next() != ebus::detail::JsonReader::Token::String) {
          error = "Invalid value type in map";
          return false;
        }
        return true;
      });
  return error;
}

const ebus::DataTypeInfo* Command::getMetaCached() const {
  if (!_cachedMeta.has_value()) {
    _cachedMeta = ebus::getMeta(datatype);
  }
  return _cachedMeta.has_value() ? &_cachedMeta.value() : nullptr;
}

ebus::Sequence Command::getVectorFromDouble(double value) const {
  double scaledValue = ebus::roundDigits(value * divider, digits);

  ebus::DataValue dv;
  dv = static_cast<float>(scaledValue);

  return ebus::encode(datatype, dv);
}

ebus::Sequence Command::getVectorFromString(const std::string& value) const {
  const auto meta = getMetaCached();
  if (!meta) return ebus::Sequence{};

  ebus::DataValue dv;
  if (std::string(meta->name).find("HEX") == 0) {
    dv = ebus::byteToChar(ebus::toVector(value));
  } else {
    dv = value.substr(0, length);
  }

  return ebus::encode(datatype, dv);
}

#endif
