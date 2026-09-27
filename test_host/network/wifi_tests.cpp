#include <catch2/catch_test_macros.hpp>
#include <string>

#include "network/detail/wifi.hpp"

TEST_CASE("Wifi trimCopy strips surrounding whitespace", "[wifi]") {
  using network::detail::wifi::trimCopy;
  REQUIRE(trimCopy("  esp-ebus  ") == "esp-ebus");
  REQUIRE(trimCopy("\t\r\nabc\n") == "abc");
  REQUIRE(trimCopy("abc") == "abc");
  REQUIRE(trimCopy("") == "");
  REQUIRE(trimCopy("   ") == "");
  // Inner whitespace is preserved.
  REQUIRE(trimCopy("a b") == "a b");
}

TEST_CASE("Wifi buildHostname sanitizes for mDNS", "[wifi]") {
  using network::detail::wifi::buildHostname;
  REQUIRE(buildHostname("MyEbus_01", "fb") == "myebus-01");
  REQUIRE(buildHostname("Living Room!", "fb") == "living-room");
  REQUIRE(buildHostname("", "fb") == "fb");
  REQUIRE(buildHostname("   ", "fb") == "fb");
  // Nothing usable left after sanitizing falls back to esp-ebus.
  REQUIRE(buildHostname("!!!", "fb") == "esp-ebus");
  REQUIRE(buildHostname("---", "fb") == "esp-ebus");
  // Leading/trailing dashes are stripped.
  REQUIRE(buildHostname("-abc-", "fb") == "abc");
  // Uppercase is lowered.
  REQUIRE(buildHostname("ESP-EBUS", "fb") == "esp-ebus");
}

TEST_CASE("Wifi buildHostname caps length at 63 chars", "[wifi]") {
  using network::detail::wifi::buildHostname;
  const std::string long_name(80, 'a');
  std::string result = buildHostname(long_name, "fb");
  REQUIRE(result.size() == 63);
  REQUIRE(result == std::string(63, 'a'));
}
