#pragma once

#include <string_view>

namespace web_admin {
inline constexpr std::string_view secret_placeholder = "********";

inline bool constantTimeEqual(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  unsigned char difference = 0;
  for (size_t i = 0; i < left.size(); ++i) {
    difference |= static_cast<unsigned char>(left[i] ^ right[i]);
  }
  return difference == 0;
}

inline bool isSensitiveKey(std::string_view key) {
  return key == "wifiPassword" || key == "mqttPass" || key == "apModePassword";
}

inline bool preserveSecret(std::string_view key, std::string_view value) {
  return isSensitiveKey(key) && value == secret_placeholder;
}

inline bool sameOrigin(std::string_view origin, std::string_view host) {
  if (origin.empty()) return true;
  constexpr std::string_view scheme = "http://";
  return !host.empty() && origin.size() <= 255 && host.size() <= 127 &&
         origin.substr(0, scheme.size()) == scheme &&
         origin.substr(scheme.size()) == host;
}
}  // namespace web_admin
