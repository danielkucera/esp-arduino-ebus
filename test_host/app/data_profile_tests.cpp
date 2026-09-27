#include <catch2/catch_test_macros.hpp>
#include <string>

#include "app/data_profile.hpp"

TEST_CASE("DataProfile lookup finds known entries", "[data_profile]") {
  const DataProfile* temp = findDataProfile("data2b_celsius");
  REQUIRE(temp != nullptr);
  REQUIRE(std::string(temp->datatype) == "DATA2B");
  REQUIRE(temp->divider == 1.0f);
  REQUIRE(temp->digits == 1);

  const DataProfile* pressure = findDataProfile("data2b_bar");
  REQUIRE(pressure != nullptr);
  REQUIRE(std::string(pressure->unit) == "bar");
  REQUIRE(pressure->divider == 1000.0f);
}

TEST_CASE("DataProfile lookup rejects unknown names", "[data_profile]") {
  REQUIRE(findDataProfile("no_such_profile") == nullptr);
  REQUIRE(findDataProfile("") == nullptr);
}

TEST_CASE("DataProfile index roundtrip is consistent", "[data_profile]") {
  // Count entries without hardcoding the generated total (user overlays
  // change it).
  uint8_t count = 0;
  while (getProfileByIndex(static_cast<uint8_t>(count + 1)) != nullptr) {
    ++count;
    REQUIRE(count < 200);  // sanity guard against infinite loop
  }
  REQUIRE(count > 0);

  for (uint8_t i = 1; i <= count; ++i) {
    const DataProfile* p = getProfileByIndex(i);
    REQUIRE(p != nullptr);
    REQUIRE(getProfileIndex(p) == i);
    REQUIRE(findDataProfile(p->name) == p);
  }

  REQUIRE(getProfileByIndex(0) == nullptr);
  REQUIRE(getProfileIndex(nullptr) == 0);
}
