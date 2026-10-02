#include <catch2/catch_test_macros.hpp>
#include <string>

#include "app/ha_profile.hpp"

TEST_CASE("HAProfile lookup finds known entries", "[ha_profile]") {
  const HAProfile* temp = findHAProfile("sensor_temperature");
  REQUIRE(temp != nullptr);
  REQUIRE(std::string(temp->component) == "sensor");
  REQUIRE(std::string(temp->device_class) == "temperature");

  const HAProfile* running = findHAProfile("binary_sensor_running");
  REQUIRE(running != nullptr);
  REQUIRE(std::string(running->component) == "binary_sensor");
  REQUIRE(running->payload_on == 1);
  REQUIRE(running->payload_off == 0);
}

TEST_CASE("HAProfile enum mapping is intact", "[ha_profile]") {
  const HAProfile* mode = findHAProfile("select_enum_mode");
  REQUIRE(mode != nullptr);
  REQUIRE(mode->key_value_count == 5);
  REQUIRE(mode->default_key == 3);
  REQUIRE(std::string(mode->key_value_pairs[0].second) == "on");
}

TEST_CASE("HAProfile lookup rejects unknown names", "[ha_profile]") {
  REQUIRE(findHAProfile("no_such_profile") == nullptr);
  REQUIRE(findHAProfile("") == nullptr);
}

TEST_CASE("HAProfile index roundtrip is consistent", "[ha_profile]") {
  uint8_t count = 0;
  while (getHAProfileByIndex(static_cast<uint8_t>(count + 1)) != nullptr) {
    ++count;
    REQUIRE(count < 200);  // sanity guard against infinite loop
  }
  REQUIRE(count > 0);

  for (uint8_t i = 1; i <= count; ++i) {
    const HAProfile* p = getHAProfileByIndex(i);
    REQUIRE(p != nullptr);
    REQUIRE(getHaProfileIndex(p) == i);
    REQUIRE(findHAProfile(p->name) == p);
  }

  REQUIRE(getHAProfileByIndex(0) == nullptr);
  REQUIRE(getHaProfileIndex(nullptr) == 0);
}
