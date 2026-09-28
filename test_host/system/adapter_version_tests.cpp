#include <catch2/catch_test_macros.hpp>

#include "identity_stub_state.hpp"
#include "system/adapter_version.hpp"

TEST_CASE("Adapter hardware version decodes pre-7.0 and numeric versions",
          "[adapter_version]") {
  HostIdentityStub::reset();

  HostIdentityStub::setAdapterVersionRaw(0x00);
  loadAdapterHwVersionFromEfuse();
  REQUIRE(getAdapterHwVersionRaw() == 0x00);
  REQUIRE(getAdapterHwVersionString() == "pre-7.0");

  HostIdentityStub::setAdapterVersionRaw(0x70);
  loadAdapterHwVersionFromEfuse();
  REQUIRE(getAdapterHwVersionRaw() == 0x70);
  REQUIRE(getAdapterHwVersionString() == "7.0");

  HostIdentityStub::setAdapterVersionRaw(0x93);
  loadAdapterHwVersionFromEfuse();
  REQUIRE(getAdapterHwVersionRaw() == 0x93);
  REQUIRE(getAdapterHwVersionString() == "9.3");
}

TEST_CASE(
    "Adapter hardware version falls back to hexadecimal for invalid nibbles",
    "[adapter_version]") {
  HostIdentityStub::reset();

  HostIdentityStub::setAdapterVersionRaw(0xA1);
  loadAdapterHwVersionFromEfuse();
  REQUIRE(getAdapterHwVersionRaw() == 0xA1);
  REQUIRE(getAdapterHwVersionString() == "0xA1");

  HostIdentityStub::setAdapterVersionRaw(0x7A);
  loadAdapterHwVersionFromEfuse();
  REQUIRE(getAdapterHwVersionRaw() == 0x7A);
  REQUIRE(getAdapterHwVersionString() == "0x7A");
}

TEST_CASE("Adapter hardware version reports eFuse read failures",
          "[adapter_version]") {
  HostIdentityStub::reset();
  HostIdentityStub::setAdapterVersionRaw(0x72);
  HostIdentityStub::setAdapterVersionReadResult(ESP_FAIL);

  loadAdapterHwVersionFromEfuse();

  REQUIRE(getAdapterHwVersionRaw() == 0xEE);
  REQUIRE(getAdapterHwVersionString() == "reading error");
}

TEST_CASE("Adapter software version is stable", "[adapter_version]") {
  const auto version = getAdapterSwVersion();
  REQUIRE(version.first == 0x07);
  REQUIRE(version.second == 0x02);
}
