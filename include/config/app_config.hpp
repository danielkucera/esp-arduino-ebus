#pragma once

#include <cstdint>
#include <ebus/detail/json_writer.hpp>
#include <ebus/types.hpp>

struct AppConfig {
  struct Network {
    ebus::FixedString<32> wifi_ssid;
    ebus::FixedString<64> wifi_password;
    ebus::FixedString<18> wifi_bssid;
    ebus::FixedString<32> ap_password;

    bool static_ip_enabled = false;
    ebus::FixedString<16> ip_address;
    ebus::FixedString<16> gateway;
    ebus::FixedString<16> netmask;
    ebus::FixedString<16> dns1;
    ebus::FixedString<16> dns2;
  } network;

  struct Sntp {
    bool enabled = false;
    ebus::FixedString<64> server;
    ebus::FixedString<32> timezone;
  } sntp;

  struct Pwm {
    uint8_t value;
  } pwm;

  struct Bus {
    uint16_t window_us;
    uint16_t offset_us;
    ebus::FixedString<8> address;
    bool system_inquiry = false;
    bool system_response = true;
    bool scan_on_startup = false;
  } bus;

  struct Mqtt {
    bool enabled = false;
    ebus::FixedString<64> server;
    ebus::FixedString<32> user;
    ebus::FixedString<32> pass;
    ebus::FixedString<32> root_topic;
  } mqtt;

  struct MqttHa {
    bool enabled = false;
    ebus::FixedString<32> thing_name;
  } mqtt_ha;

  struct Http {
    ebus::FixedString<512> headers;
  } http;

  void reset();

  void toJson(ebus::detail::JsonWriter& writer) const;

  /**
   * @brief Deserializes a JSON string into an AppConfig object.
   * @param json The JSON string to parse.
   * @return A populated AppConfig object. Defaults are used for missing keys.
   */
  static AppConfig fromJson(std::string_view json);

  /**
   * @brief Merges a partial JSON string into the current configuration.
   * Only keys present in the JSON are updated; others remain unchanged.
   * Unknown keys are safely ignored.
   * @return true if the JSON was partially or fully parsed, false on error.
   */
  bool mergeFromJson(std::string_view json);

  /**
   * @brief Performs a basic structural validation of a JSON string.
   * @return true if the string appears to be valid JSON, false otherwise.
   */
  static bool isValidJson(std::string_view json);
};