#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "esp_mac.h"
#include "identity_stub_state.hpp"
#include "system/device_identity.hpp"

TEST_CASE(
    "Device identity formats the low MAC bytes as six lowercase hex digits",
    "[device_identity]") {
  HostIdentityStub::reset();
  HostIdentityStub::setMac({0x12, 0x34, 0x56, 0x00, 0x0A, 0xBC});

  calcUniqueId();

  REQUIRE(std::string(getUniqueId()) == "000abc");
  REQUIRE(HostIdentityStub::lastMacType() == ESP_MAC_WIFI_STA);
}

TEST_CASE("Device identity formats a full low-byte MAC value",
          "[device_identity]") {
  HostIdentityStub::reset();
  HostIdentityStub::setMac({0xAA, 0xBB, 0xCC, 0x12, 0x34, 0x56});

  calcUniqueId();

  REQUIRE(std::string(getUniqueId()) == "123456");
}

TEST_CASE("Device identity produces six zeroes for a zero MAC",
          "[device_identity]") {
  HostIdentityStub::reset();

  calcUniqueId();

  REQUIRE(std::string(getUniqueId()) == "000000");
}
