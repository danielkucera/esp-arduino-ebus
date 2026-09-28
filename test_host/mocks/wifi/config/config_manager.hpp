#pragma once
#include <map>
#include <string>
#include <string_view>

// Deliberately shares a read buffer, matching the production API's lifetime.
class ConfigManager {
 public:
  std::map<std::string, std::string> values;
  std::string_view readString(const char* key, const char* fallback = "") {
    const auto it = values.find(key);
    buffer_ = it == values.end() ? fallback : it->second;
    return buffer_;
  }
  bool readBool(const char* key, bool fallback = false) {
    const auto it = values.find(key);
    return it == values.end() ? fallback : it->second == "true";
  }
 private:
  std::string buffer_;
};
