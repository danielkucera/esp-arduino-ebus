#include "config/app_config_loader.hpp"

#include "config_manager.hpp"

namespace {

// Helper to copy a string_view into a FixedString, truncating if needed.
template <size_t Cap>
void assignFixedString(ebus::FixedString<Cap>& dst, std::string_view src) {
  dst.assign(src);
}

// Helper to copy a string_view into a FixedString only if non-empty.
template <size_t Cap>
void assignFixedStringIfNotEmpty(ebus::FixedString<Cap>& dst,
                                 std::string_view src) {
  if (!src.empty()) dst.assign(src);
}

// Helper to assign int only if non-zero (for missing keys).
void assignIntIfNonZero(uint8_t& dst, int32_t src) {
  if (src != 0) dst = static_cast<uint8_t>(src);
}
void assignIntIfNonZero(uint16_t& dst, int32_t src) {
  if (src != 0) dst = static_cast<uint16_t>(src);
}

}  // namespace

bool AppConfigLoader::load(AppConfig& config) {
  config.reset();

  // Network
  assignFixedStringIfNotEmpty(config.network.wifi_ssid,
                              config_manager_.readString("wifiSsid"));
  assignFixedStringIfNotEmpty(config.network.wifi_password,
                              config_manager_.readString("wifiPassword"));
  assignFixedStringIfNotEmpty(config.network.wifi_bssid,
                              config_manager_.readString("wifiBssid"));
  assignFixedStringIfNotEmpty(config.network.ap_password,
                              config_manager_.readString("apModePassword"));

  config.network.static_ip_enabled =
      config_manager_.readBool("staticIPEnabled");
  assignFixedStringIfNotEmpty(config.network.ip_address,
                              config_manager_.readString("ipAddress"));
  assignFixedStringIfNotEmpty(config.network.gateway,
                              config_manager_.readString("gateway"));
  assignFixedStringIfNotEmpty(config.network.netmask,
                              config_manager_.readString("netmask"));
  assignFixedStringIfNotEmpty(config.network.dns1,
                              config_manager_.readString("dns1"));
  assignFixedStringIfNotEmpty(config.network.dns2,
                              config_manager_.readString("dns2"));

  // SNTP
  config.sntp.enabled = config_manager_.readBool("sntpEnabled");
  assignFixedStringIfNotEmpty(config.sntp.server,
                              config_manager_.readString("sntpServer"));
  assignFixedStringIfNotEmpty(config.sntp.timezone,
                              config_manager_.readString("sntpTimezone"));

  // PWM
  assignIntIfNonZero(config.pwm.value, config_manager_.readInt("pwmValue"));

  // Bus
  assignFixedStringIfNotEmpty(config.bus.address,
                              config_manager_.readString("ebusAddress"));
  assignIntIfNonZero(config.bus.window_us,
                     config_manager_.readInt("busWindow"));
  assignIntIfNonZero(config.bus.offset_us,
                     config_manager_.readInt("busOffset"));

  config.bus.system_inquiry = config_manager_.readBool("systemInquiry");
  config.bus.system_response = config_manager_.readBool("systemResponse");
  config.bus.scan_on_startup = config_manager_.readBool("scanOnStartup");

  // MQTT
  config.mqtt.enabled = config_manager_.readBool("mqttEnabled");
  assignFixedStringIfNotEmpty(config.mqtt.server,
                              config_manager_.readString("mqttServer"));
  assignFixedStringIfNotEmpty(config.mqtt.user,
                              config_manager_.readString("mqttUser"));
  assignFixedStringIfNotEmpty(config.mqtt.pass,
                              config_manager_.readString("mqttPass"));
  assignFixedStringIfNotEmpty(config.mqtt.root_topic,
                              config_manager_.readString("rootTopic"));

  // MQTT HA
  config.mqtt_ha.enabled = config_manager_.readBool("haEnabled");
  assignFixedStringIfNotEmpty(config.mqtt_ha.thing_name,
                              config_manager_.readString("thingName"));

  // HTTP
  assignFixedStringIfNotEmpty(config.http.headers,
                              config_manager_.readString("httpHeaders"));

  return true;
}

bool AppConfigLoader::save(const AppConfig& config) {
  bool ok = true;

  // Network
  ok &=
      config_manager_.writeString("wifiSsid", config.network.wifi_ssid.c_str());
  ok &= config_manager_.writeString("wifiPassword",
                                    config.network.wifi_password.c_str());
  ok &= config_manager_.writeString("wifiBssid",
                                    config.network.wifi_bssid.c_str());
  ok &= config_manager_.writeString("apModePassword",
                                    config.network.ap_password.c_str());

  ok &= config_manager_.writeString(
      "staticIPEnabled", config.network.static_ip_enabled ? "true" : "false");
  ok &= config_manager_.writeString("ipAddress",
                                    config.network.ip_address.c_str());
  ok &= config_manager_.writeString("gateway", config.network.gateway.c_str());
  ok &= config_manager_.writeString("netmask", config.network.netmask.c_str());
  ok &= config_manager_.writeString("dns1", config.network.dns1.c_str());
  ok &= config_manager_.writeString("dns2", config.network.dns2.c_str());

  // SNTP
  ok &= config_manager_.writeString("sntpEnabled",
                                    config.sntp.enabled ? "true" : "false");
  ok &= config_manager_.writeString("sntpServer", config.sntp.server.c_str());
  ok &=
      config_manager_.writeString("sntpTimezone", config.sntp.timezone.c_str());

  // PWM
  ok &=
      config_manager_.writeString("pwmValue", std::to_string(config.pwm.value));

  // Bus
  ok &= config_manager_.writeString("ebusAddress", config.bus.address.c_str());
  ok &= config_manager_.writeString("busWindow",
                                    std::to_string(config.bus.window_us));
  ok &= config_manager_.writeString("busOffset",
                                    std::to_string(config.bus.offset_us));
  ok &= config_manager_.writeString(
      "systemInquiry", config.bus.system_inquiry ? "true" : "false");
  ok &= config_manager_.writeString(
      "systemResponse", config.bus.system_response ? "true" : "false");
  ok &= config_manager_.writeString(
      "scanOnStartup", config.bus.scan_on_startup ? "true" : "false");

  // MQTT
  ok &= config_manager_.writeString("mqttEnabled",
                                    config.mqtt.enabled ? "true" : "false");
  ok &= config_manager_.writeString("mqttServer", config.mqtt.server.c_str());
  ok &= config_manager_.writeString("mqttUser", config.mqtt.user.c_str());
  ok &= config_manager_.writeString("mqttPass", config.mqtt.pass.c_str());
  ok &=
      config_manager_.writeString("rootTopic", config.mqtt.root_topic.c_str());

  // MQTT HA
  ok &= config_manager_.writeString("haEnabled",
                                    config.mqtt_ha.enabled ? "true" : "false");
  ok &= config_manager_.writeString("thingName",
                                    config.mqtt_ha.thing_name.c_str());

  // HTTP
  ok &= config_manager_.writeString("httpHeaders", config.http.headers.c_str());

  return ok;
}