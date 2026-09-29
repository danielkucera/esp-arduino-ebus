#include "config/app_config.hpp"

#include <cstdlib>
#include <ebus/detail/json_reader.hpp>
#include <ebus/types.hpp>
#include <string>
#include <string_view>

#include "app/app_limits.hpp"

void AppConfig::reset() {
  *this = AppConfig{};
  // No configured station: boot into provisioning/recovery AP mode.
  network.ap_password = "ebusebus";
  sntp.server = "pool.ntp.org";
  sntp.timezone = "UTC0";
  bus.address = "ff";
  mqtt_ha.thing_name = "esp-eBus";
  pwm.value = 130;
  bus.window_us = 4400;
  bus.offset_us = 50;
  bus.system_inquiry = false;
  bus.system_response = true;
  bus.scan_on_startup = false;
}

bool AppConfig::isValid() const {
  // An empty SSID is valid: the adapter can operate in provisioning AP mode.

  // PWM
  if (pwm.value < app::limits::Pwm::min || pwm.value > app::limits::Pwm::max)
    return false;

  // eBUS address must be non-empty and fit the FixedString capacity.
  if (bus.address.empty()) return false;

  // Bus timing
  if (bus.window_us < app::limits::Bus::window_min_us ||
      bus.window_us > app::limits::Bus::window_max_us)
    return false;
  // Note: offset_min_us is 0 and offset_us unsigned, so only the upper
  // bound is checked (a `< min` comparison would be dead code).
  if (bus.offset_us > app::limits::Bus::offset_max_us) return false;

  return true;
}

std::string_view AppConfig::Network::recoveryApPassword() const {
  if (ap_password.size() < 8 || ap_password.size() > 63) return "ebusebus";
  return {ap_password.c_str(), ap_password.size()};
}

namespace {

bool parseFlatBool(std::string_view value) {
  return value == "selected" || value == "true" || value == "1" ||
         value == "on";
}

// Parses a decimal int from a non-terminated view via a bounded copy.
bool parseFlatInt(std::string_view value, int32_t& out) {
  if (value.empty() || value.size() > 10) return false;
  char buf[12]{};
  const size_t len =
      value.size() < sizeof(buf) - 1 ? value.size() : sizeof(buf) - 1;
  for (size_t i = 0; i < len; ++i) buf[i] = value[i];
  char* end = nullptr;
  const long parsed = std::strtol(buf, &end, 10);
  if (end == buf || *end != '\0') return false;
  out = static_cast<int32_t>(parsed);
  return true;
}

void assignFlatU8(uint8_t& dst, std::string_view value) {
  int32_t parsed = 0;
  if (parseFlatInt(value, parsed) && parsed >= 0 && parsed <= 255) {
    dst = static_cast<uint8_t>(parsed);
  }
}

void assignFlatU16(uint16_t& dst, std::string_view value) {
  int32_t parsed = 0;
  if (parseFlatInt(value, parsed) && parsed >= 0 && parsed <= 65535) {
    dst = static_cast<uint16_t>(parsed);
  }
}

}  // namespace

bool AppConfig::isKnownFlatKey(std::string_view key) {
  return key == "wifiSsid" || key == "wifiPassword" || key == "wifiBssid" ||
         key == "apModePassword" || key == "wifiFullScan" ||
         key == "staticIPEnabled" ||
         key == "ipAddress" || key == "gateway" || key == "netmask" ||
         key == "dns1" || key == "dns2" || key == "sntpEnabled" ||
         key == "sntpServer" || key == "sntpTimezone" || key == "pwmValue" ||
         key == "ebusAddress" || key == "busWindow" || key == "busOffset" ||
         key == "systemInquiry" || key == "systemResponse" ||
         key == "scanOnStartup" || key == "mqttEnabled" ||
         key == "mqttServer" || key == "mqttUser" || key == "mqttPass" ||
         key == "rootTopic" || key == "haEnabled" || key == "thingName" ||
         key == "httpHeaders";
}

bool AppConfig::mergeFlatJson(
    std::string_view json,
    std::vector<std::pair<std::string, std::string>>* unknowns) {
  ebus::detail::JsonReader reader(json);
  if (reader.next() != ebus::detail::JsonReader::Token::object_start) {
    return false;
  }

  while (true) {
    auto token = reader.next();
    if (token == ebus::detail::JsonReader::Token::object_end ||
        token == ebus::detail::JsonReader::Token::end) {
      break;
    }
    if (token != ebus::detail::JsonReader::Token::key) continue;
    std::string key(reader.value());
    if (reader.next() != ebus::detail::JsonReader::Token::string) {
      return false;
    }
    std::string_view value = reader.value();

    if (key == "wifiSsid") {
      network.wifi_ssid.assign(value);
    } else
      // Credentials: an empty value means "keep the stored one" (the GET
      // endpoint never serves passwords, so untouched fields always arrive
      // empty). Deliberate consequence: passwords cannot be cleared to empty
      // via the UI; erase NVS to go back to compiled defaults.
      if (key == "wifiPassword") {
        if (!value.empty()) network.wifi_password.assign(value);
      } else if (key == "wifiBssid") {
        network.wifi_bssid.assign(value);
      } else if (key == "apModePassword") {
        if (!value.empty()) network.ap_password.assign(value);
      } else if (key == "wifiFullScan") {
        network.wifi_full_scan = parseFlatBool(value);
      } else if (key == "staticIPEnabled") {
        network.static_ip_enabled = parseFlatBool(value);
      } else if (key == "ipAddress") {
        network.ip_address.assign(value);
      } else if (key == "gateway") {
        network.gateway.assign(value);
      } else if (key == "netmask") {
        network.netmask.assign(value);
      } else if (key == "dns1") {
        network.dns1.assign(value);
      } else if (key == "dns2") {
        network.dns2.assign(value);
      } else if (key == "sntpEnabled") {
        sntp.enabled = parseFlatBool(value);
      } else if (key == "sntpServer") {
        sntp.server.assign(value);
      } else if (key == "sntpTimezone") {
        sntp.timezone.assign(value);
      } else if (key == "pwmValue") {
        assignFlatU8(pwm.value, value);
      } else if (key == "ebusAddress") {
        bus.address.assign(value);
      } else if (key == "busWindow") {
        assignFlatU16(bus.window_us, value);
      } else if (key == "busOffset") {
        assignFlatU16(bus.offset_us, value);
      } else if (key == "systemInquiry") {
        bus.system_inquiry = parseFlatBool(value);
      } else if (key == "systemResponse") {
        bus.system_response = parseFlatBool(value);
      } else if (key == "scanOnStartup") {
        bus.scan_on_startup = parseFlatBool(value);
      } else if (key == "mqttEnabled") {
        mqtt.enabled = parseFlatBool(value);
      } else if (key == "mqttServer") {
        mqtt.server.assign(value);
      } else if (key == "mqttUser") {
        mqtt.user.assign(value);
      } else if (key == "mqttPass") {
        if (!value.empty()) mqtt.pass.assign(value);
      } else if (key == "rootTopic") {
        mqtt.root_topic.assign(value);
      } else if (key == "haEnabled") {
        mqtt_ha.enabled = parseFlatBool(value);
      } else if (key == "thingName") {
        mqtt_ha.thing_name.assign(value);
      } else if (key == "httpHeaders") {
        http.headers.assign(value);
      } else if (unknowns != nullptr) {
        unknowns->emplace_back(key, std::string(value));
      }
  }

  return true;
}

namespace {
// FixedString capacities differ per field; compare by content view.
template <size_t N, size_t M>
bool driftStr(const ebus::FixedString<N>& a, const ebus::FixedString<M>& b) {
  return std::string_view(a.c_str(), a.size()) !=
         std::string_view(b.c_str(), b.size());
}
}  // namespace

void AppConfig::collectDrift(const AppConfig& live, const AppConfig& requested,
                             std::vector<std::string>& keys) {
  // Mirrors isKnownFlatKey order; NVS key names are reported (safe for HTTP).
  if (driftStr(live.network.wifi_ssid, requested.network.wifi_ssid))
    keys.emplace_back("wifiSsid");
  if (driftStr(live.network.wifi_password, requested.network.wifi_password))
    keys.emplace_back("wifiPassword");
  if (driftStr(live.network.wifi_bssid, requested.network.wifi_bssid))
    keys.emplace_back("wifiBssid");
  if (driftStr(live.network.ap_password, requested.network.ap_password))
    keys.emplace_back("apModePassword");
  if (live.network.wifi_full_scan != requested.network.wifi_full_scan)
    keys.emplace_back("wifiFullScan");
  if (live.network.static_ip_enabled != requested.network.static_ip_enabled)
    keys.emplace_back("staticIPEnabled");
  if (driftStr(live.network.ip_address, requested.network.ip_address))
    keys.emplace_back("ipAddress");
  if (driftStr(live.network.gateway, requested.network.gateway))
    keys.emplace_back("gateway");
  if (driftStr(live.network.netmask, requested.network.netmask))
    keys.emplace_back("netmask");
  if (driftStr(live.network.dns1, requested.network.dns1))
    keys.emplace_back("dns1");
  if (driftStr(live.network.dns2, requested.network.dns2))
    keys.emplace_back("dns2");
  if (live.sntp.enabled != requested.sntp.enabled)
    keys.emplace_back("sntpEnabled");
  if (driftStr(live.sntp.server, requested.sntp.server))
    keys.emplace_back("sntpServer");
  if (driftStr(live.sntp.timezone, requested.sntp.timezone))
    keys.emplace_back("sntpTimezone");
  if (live.pwm.value != requested.pwm.value) keys.emplace_back("pwmValue");
  if (driftStr(live.bus.address, requested.bus.address))
    keys.emplace_back("ebusAddress");
  if (live.bus.window_us != requested.bus.window_us)
    keys.emplace_back("busWindow");
  if (live.bus.offset_us != requested.bus.offset_us)
    keys.emplace_back("busOffset");
  if (live.bus.system_inquiry != requested.bus.system_inquiry)
    keys.emplace_back("systemInquiry");
  if (live.bus.system_response != requested.bus.system_response)
    keys.emplace_back("systemResponse");
  if (live.bus.scan_on_startup != requested.bus.scan_on_startup)
    keys.emplace_back("scanOnStartup");
  if (live.mqtt.enabled != requested.mqtt.enabled)
    keys.emplace_back("mqttEnabled");
  if (driftStr(live.mqtt.server, requested.mqtt.server))
    keys.emplace_back("mqttServer");
  if (driftStr(live.mqtt.user, requested.mqtt.user))
    keys.emplace_back("mqttUser");
  if (driftStr(live.mqtt.pass, requested.mqtt.pass))
    keys.emplace_back("mqttPass");
  if (driftStr(live.mqtt.root_topic, requested.mqtt.root_topic))
    keys.emplace_back("rootTopic");
  if (live.mqtt_ha.enabled != requested.mqtt_ha.enabled)
    keys.emplace_back("haEnabled");
  if (driftStr(live.mqtt_ha.thing_name, requested.mqtt_ha.thing_name))
    keys.emplace_back("thingName");
  if (driftStr(live.http.headers, requested.http.headers))
    keys.emplace_back("httpHeaders");
}
