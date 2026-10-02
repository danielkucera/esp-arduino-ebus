#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

// No ESP-IDF dependency: the exact parser used by the firmware is host-testable.
namespace mqtt_endpoint {

inline constexpr const char* patch_version = "wss-r2";

struct Endpoint {
  std::string uri;
  const char* transport = "invalid";
  const char* error = nullptr;  // Static text only: never echo credentials.
  bool tls = false;
  explicit operator bool() const { return error == nullptr && !uri.empty(); }
};

inline Endpoint invalid(const char* reason) {
  Endpoint result;
  result.error = reason;
  return result;
}

// A bare hostname keeps legacy MQTT/TCP behaviour. An explicit URI selects its
// own transport and port: NEVER append the legacy port to a ws/wss/mqtts URI.
// User/password must stay in the existing separate configuration fields.
inline Endpoint parse(std::string_view input, uint16_t legacy_port = 1883) {
  while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front())))
    input.remove_prefix(1);
  while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back())))
    input.remove_suffix(1);
  if (input.empty()) return invalid("MQTT Server / URI is empty");
  // NVS reads in this project use a 512-byte buffer including the terminator.
  if (input.size() > 500) return invalid("MQTT Server / URI is too long");
  for (unsigned char c : input) {
    if (c <= 0x20 || c >= 0x7f || c == '\\')
      return invalid("Use an ASCII hostname/URI without spaces or backslashes");
  }
  if (input.find_first_of("?#") != std::string_view::npos)
    return invalid("MQTT URI queries and fragments are not supported");

  Endpoint result;
  const auto separator = input.find("://");
  const bool explicit_uri = separator != std::string_view::npos;
  std::string scheme = "mqtt";
  std::string_view remainder = input;
  if (explicit_uri) {
    scheme.assign(input.substr(0, separator));
    for (char& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    remainder = input.substr(separator + 3);
  }
  if (scheme == "mqtt") result.transport = "mqtt";
  else if (scheme == "mqtts") { result.transport = "mqtts"; result.tls = true; }
  else if (scheme == "ws") result.transport = "ws";
  else if (scheme == "wss") { result.transport = "wss"; result.tls = true; }
  else return invalid("Supported schemes are mqtt://, mqtts://, ws:// and wss://");

  const bool websocket = scheme == "ws" || scheme == "wss";
  const auto slash = remainder.find('/');
  const std::string_view authority = remainder.substr(0, slash);
  const std::string_view path = slash == std::string_view::npos
                                    ? std::string_view{} : remainder.substr(slash);
  if (authority.empty()) return invalid("MQTT hostname is missing");
  if (authority.find('@') != std::string_view::npos)
    return invalid("Use the separate MQTT User and MQTT Password fields");
  if (!websocket && !path.empty() && path != "/")
    return invalid("A path is only supported with ws:// or wss://");

  std::string_view host;
  std::string_view port;
  bool explicit_port = false;
  if (authority.front() == '[') {
    const auto close = authority.find(']');
    if (close == std::string_view::npos || close <= 1)
      return invalid("Invalid bracketed IPv6 address");
    host = authority.substr(1, close - 1);
    if (host.find(':') == std::string_view::npos)
      return invalid("Brackets are reserved for IPv6 addresses");
    for (unsigned char c : host)
      if (!std::isxdigit(c) && c != ':' && c != '.')
        return invalid("Invalid character in IPv6 address");
    if (close + 1 < authority.size()) {
      if (authority[close + 1] != ':') return invalid("Invalid text after hostname");
      explicit_port = true;
      port = authority.substr(close + 2);
    }
  } else {
    const auto colon = authority.find(':');
    host = authority.substr(0, colon);
    if (colon != std::string_view::npos) {
      explicit_port = true;
      port = authority.substr(colon + 1);
    }
    if (host.empty()) return invalid("MQTT hostname is missing");
    for (unsigned char c : host)
      if (!std::isalnum(c) && c != '.' && c != '-' && c != '_')
        return invalid("Invalid character in MQTT hostname");
  }
  if (explicit_port) {
    if (port.empty() || port.size() > 5) return invalid("Invalid MQTT port");
    uint32_t number = 0;
    for (unsigned char c : port) {
      if (!std::isdigit(c)) return invalid("MQTT port must be a number");
      number = number * 10 + (c - '0');
    }
    if (number == 0 || number > 65535) return invalid("MQTT port must be 1..65535");
  }

  result.uri = scheme + "://" + std::string(authority);
  if (!explicit_uri && !explicit_port)
    result.uri += ":" + std::to_string(legacy_port ? legacy_port : 1883);
  if (websocket)
    result.uri += path.empty() ? "/mqtt" : std::string(path);
  else
    result.uri += path;  // Preserve a permitted trailing slash.
  return result;
}

}  // namespace mqtt_endpoint
