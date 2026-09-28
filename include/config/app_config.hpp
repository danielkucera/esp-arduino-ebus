#pragma once

#include <cstdint>
#include <ebus/types.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

  /** @brief Validates field values against protocol limits. */
  bool isValid() const;

  /**
   * @brief Merges a flat NVS-key JSON object (as posted by /api/v1/config)
   * into the current configuration. Keys use NVS names (e.g. "wifiSsid",
   * "pwmValue"); all values arrive as strings, matching the previous
   * ConfigManager::writeConfigJson contract. Non-string values are rejected.
   * Unparseable int values leave the current field unchanged.
   * Unknown keys are optionally collected for NVS passthrough storage.
   * @param unknowns Optional out-vector receiving unknown key/value pairs.
   * @return true if the JSON was an object and all values were strings.
   */
  bool mergeFlatJson(
      std::string_view json,
      std::vector<std::pair<std::string, std::string>>* unknowns = nullptr);

  /** @brief True if the key is a known flat NVS config key. */
  static bool isKnownFlatKey(std::string_view key);

  /**
   * @brief Collects NVS key names whose values differ between the live
   * snapshot and a freshly NVS-loaded struct (staged-but-unapplied edits).
   * Passwords compare by value but only key *names* are reported, so the
   * output is safe to serve over HTTP.
   */
  static void collectDrift(const AppConfig& live, const AppConfig& requested,
                           std::vector<std::string>& keys);
};