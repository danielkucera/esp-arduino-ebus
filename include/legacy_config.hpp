#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

struct LegacyIotWebConfConfig {
  std::string thing_name;
  std::string admin_password;
  std::string wifi_ssid;
  std::string wifi_password;
};

inline bool legacyPwmIsValid(uint32_t value) {
  return value >= 1 && value <= 255;
}

inline bool legacyAdminPasswordIsValid(const std::string& value) {
  return value.size() >= 8;
}

inline bool parseLegacyIotWebConfConfig(const uint8_t* data, size_t size,
                                        LegacyIotWebConfConfig& config) {
  constexpr size_t version_length = 4;
  constexpr size_t field_length = 33;
  constexpr size_t required_size = version_length + 5 * field_length;
  constexpr uint8_t expected_version[version_length] = {'e', 'e', 'a', 0};

  if (data == nullptr || size < required_size ||
      std::memcmp(data, expected_version, version_length) != 0) {
    return false;
  }

  auto readField = [&](size_t offset, std::string& value) {
    const char* begin = reinterpret_cast<const char*>(data + offset);
    const void* terminator = std::memchr(begin, 0, field_length);
    if (terminator == nullptr) return false;
    const char* end = static_cast<const char*>(terminator);
    value.assign(begin, static_cast<size_t>(end - begin));
    return true;
  };

  return readField(version_length, config.thing_name) &&
         readField(version_length + field_length, config.admin_password) &&
         readField(version_length + 2 * field_length, config.wifi_ssid) &&
         readField(version_length + 3 * field_length, config.wifi_password);
}
