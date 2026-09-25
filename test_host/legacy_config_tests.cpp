#include <array>
#include <cstdint>
#include <iostream>
#include <string>

#include "legacy_config.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  ++failures;
}

void writeField(std::array<uint8_t, 177>& data, size_t offset,
                const std::string& value) {
  for (size_t i = 0; i < value.size(); ++i) data[offset + i] = value[i];
  data[offset + value.size()] = 0;
}

std::array<uint8_t, 177> validBlob() {
  std::array<uint8_t, 177> data{};
  data[0] = 'e';
  data[1] = 'e';
  data[2] = 'a';
  writeField(data, 4, "esp-ebus");
  writeField(data, 37, "admin-secret");
  writeField(data, 70, "example-network");
  writeField(data, 103, "wifi-secret");
  writeField(data, 136, "30");
  writeField(data, 169, "147");
  return data;
}

void testValidLegacyConfig() {
  auto data = validBlob();
  LegacyIotWebConfConfig config;
  check(parseLegacyIotWebConfConfig(data.data(), data.size(), config),
        "valid legacy data should parse");
  check(config.thing_name == "esp-ebus", "thing name should be preserved");
  check(config.admin_password == "admin-secret",
        "admin password should be preserved");
  check(config.wifi_ssid == "example-network",
        "WiFi SSID should be preserved");
  check(config.wifi_password == "wifi-secret",
        "WiFi password should be preserved");
}

void testLegacyConfigWithoutOptionalPwmField() {
  auto data = validBlob();
  LegacyIotWebConfConfig config;
  check(parseLegacyIotWebConfConfig(data.data(), 169, config),
        "169-byte legacy data should parse");
  check(config.wifi_ssid == "example-network",
        "system fields should not depend on the optional PWM field");
}

void testOpenNetworkConfig() {
  auto data = validBlob();
  data[103] = 0;
  LegacyIotWebConfConfig config;
  check(parseLegacyIotWebConfConfig(data.data(), 169, config),
        "an open network configuration should parse");
  check(config.wifi_password.empty(),
        "an empty WiFi password should be preserved");
}

void testRejectsOtherVersion() {
  auto data = validBlob();
  data[2] = 'b';
  LegacyIotWebConfConfig config;
  check(!parseLegacyIotWebConfConfig(data.data(), data.size(), config),
        "unknown legacy versions should be rejected");
}

void testRejectsTruncatedData() {
  auto data = validBlob();
  LegacyIotWebConfConfig config;
  check(!parseLegacyIotWebConfConfig(data.data(), 168, config),
        "truncated system fields should be rejected");
}

void testRejectsUnterminatedFields() {
  auto data = validBlob();
  for (size_t i = 70; i < 103; ++i) data[i] = 'x';
  LegacyIotWebConfConfig config;
  check(!parseLegacyIotWebConfConfig(data.data(), data.size(), config),
        "unterminated fields should be rejected");
}

void testLegacyValueValidation() {
  check(legacyPwmIsValid(1), "PWM 1 should be accepted");
  check(legacyPwmIsValid(255), "PWM 255 should be accepted");
  check(!legacyPwmIsValid(0), "PWM 0 should be rejected");
  check(!legacyPwmIsValid(256), "PWM 256 should be rejected");
  check(legacyAdminPasswordIsValid("12345678"),
        "eight-character admin passwords should be accepted");
  check(!legacyAdminPasswordIsValid("1234567"),
        "short admin passwords should be rejected");
}

}  // namespace

int main() {
  testValidLegacyConfig();
  testLegacyConfigWithoutOptionalPwmField();
  testOpenNetworkConfig();
  testRejectsOtherVersion();
  testRejectsTruncatedData();
  testRejectsUnterminatedFields();
  testLegacyValueValidation();
  if (failures == 0) std::cout << "Legacy configuration tests passed\n";
  return failures == 0 ? 0 : 1;
}
