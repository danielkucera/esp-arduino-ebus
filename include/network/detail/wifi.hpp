#pragma once

#include <cctype>
#include <string>

// Pure hostname helpers, exposed for host testing.
// (Zero ESP dependencies; mDNS/HA identity correctness matters.)
namespace network::detail::wifi {

inline std::string trimCopy(const std::string& value) {
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

inline std::string buildHostname(const std::string& source,
                                 const char* fallback) {
  std::string hostname = trimCopy(source);
  if (hostname.empty()) hostname = fallback;

  std::string sanitized;
  sanitized.reserve(hostname.size());
  for (size_t i = 0; i < hostname.size(); ++i) {
    const char c = hostname[i];
    if (std::isalnum(static_cast<unsigned char>(c))) {
      sanitized +=
          static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (c == '-' || c == '_' || c == ' ') {
      sanitized += '-';
    }
  }

  while (!sanitized.empty() && sanitized.front() == '-') {
    sanitized.erase(sanitized.begin());
  }
  while (!sanitized.empty() && sanitized.back() == '-') sanitized.pop_back();

  if (sanitized.empty()) sanitized = "esp-ebus";
  if (sanitized.size() > 63) sanitized.erase(63);
  return sanitized;
}

}  // namespace network::detail::wifi
