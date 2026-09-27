#include <catch2/catch_test_macros.hpp>
#include <string>

#include "config/app_config.hpp"

TEST_CASE("AppConfig reset provides migration-safe defaults", "[app_config]") {
  AppConfig cfg;
  cfg.reset();

  // Values relied upon by the main.cpp migration (must match old fallbacks)
  REQUIRE(cfg.pwm.value == 130);
  REQUIRE(cfg.bus.window_us == 4400);
  REQUIRE(cfg.bus.offset_us == 50);
  REQUIRE(std::string(cfg.bus.address.c_str()) == "ff");
  REQUIRE(cfg.bus.system_inquiry == false);
  REQUIRE(cfg.bus.system_response == true);
  REQUIRE(cfg.bus.scan_on_startup == false);
  REQUIRE(std::string(cfg.sntp.server.c_str()) == "pool.ntp.org");
  REQUIRE(std::string(cfg.sntp.timezone.c_str()) == "UTC0");
  REQUIRE(std::string(cfg.mqtt_ha.thing_name.c_str()) == "esp-eBus");

  // reset() ships test WiFi credentials, so the default snapshot validates
  REQUIRE(cfg.isValid() == true);
}

TEST_CASE("AppConfig isValid accepts a fully populated config",
          "[app_config]") {
  AppConfig cfg;
  cfg.reset();
  cfg.network.wifi_ssid.assign("my-ssid");

  REQUIRE(cfg.isValid() == true);
}

TEST_CASE("AppConfig isValid rejects out-of-range values", "[app_config]") {
  AppConfig cfg;
  cfg.reset();
  cfg.network.wifi_ssid.assign("my-ssid");

  AppConfig bad = cfg;
  bad.pwm.value = 0;
  REQUIRE(bad.isValid() == false);

  bad = cfg;
  bad.bus.window_us = 4000;
  REQUIRE(bad.isValid() == false);

  bad = cfg;
  bad.bus.offset_us = 500;
  REQUIRE(bad.isValid() == false);

  bad = cfg;
  bad.bus.address.assign("");
  REQUIRE(bad.isValid() == false);

  bad = cfg;
  bad.network.wifi_ssid.assign("");
  REQUIRE(bad.isValid() == false);
}

TEST_CASE("AppConfig mergeFlatJson applies NVS-keyed string values",
          "[app_config]") {
  AppConfig cfg;
  cfg.reset();

  REQUIRE(
      cfg.mergeFlatJson(
          R"({"wifiSsid":"my-ssid","pwmValue":"200","busWindow":"4300",)"
          R"("sntpEnabled":"true","mqttEnabled":"1","scanOnStartup":"on"})") ==
      true);
  REQUIRE(std::string(cfg.network.wifi_ssid.c_str()) == "my-ssid");
  REQUIRE(cfg.pwm.value == 200);
  REQUIRE(cfg.bus.window_us == 4300);
  REQUIRE(cfg.sntp.enabled == true);
  REQUIRE(cfg.mqtt.enabled == true);
  REQUIRE(cfg.bus.scan_on_startup == true);
  // Untouched keys keep their values
  REQUIRE(cfg.bus.offset_us == 50);
  REQUIRE(std::string(cfg.bus.address.c_str()) == "ff");

  // Boolean false spellings
  REQUIRE(cfg.mergeFlatJson(R"({"sntpEnabled":"false"})") == true);
  REQUIRE(cfg.sntp.enabled == false);

  // Non-string values are rejected, like the legacy write path
  REQUIRE(cfg.mergeFlatJson(R"({"pwmValue":200})") == false);

  // Non-object JSON is rejected
  REQUIRE(cfg.mergeFlatJson(R"(not json)") == false);
}

TEST_CASE("AppConfig mergeFlatJson ignores unparseable ints", "[app_config]") {
  AppConfig cfg;
  cfg.reset();

  REQUIRE(cfg.mergeFlatJson(R"({"pwmValue":"abc","busWindow":"999999"})") ==
          true);
  REQUIRE(cfg.pwm.value == 130);
  REQUIRE(cfg.bus.window_us == 4400);
}

TEST_CASE("AppConfig mergeFlatJson collects unknown keys", "[app_config]") {
  AppConfig cfg;
  cfg.reset();

  std::vector<std::pair<std::string, std::string>> unknowns;
  REQUIRE(cfg.mergeFlatJson(R"({"customKey":"customValue","pwmValue":"100"})",
                            &unknowns) == true);
  REQUIRE(cfg.pwm.value == 100);
  REQUIRE(unknowns.size() == 1);
  REQUIRE(unknowns[0].first == "customKey");
  REQUIRE(unknowns[0].second == "customValue");

  REQUIRE(AppConfig::isKnownFlatKey("wifiSsid") == true);
  REQUIRE(AppConfig::isKnownFlatKey("customKey") == false);
}

TEST_CASE("AppConfig::collectDrift reports staged keys only", "[app_config]") {
  AppConfig live;
  live.reset();
  AppConfig requested = live;

  std::vector<std::string> keys;
  AppConfig::collectDrift(live, requested, keys);
  REQUIRE(keys.empty());

  requested.bus.address.assign("30");
  requested.mqtt.enabled = true;
  AppConfig::collectDrift(live, requested, keys);
  REQUIRE(keys.size() == 2);
  REQUIRE(keys[0] == "ebusAddress");
  REQUIRE(keys[1] == "mqttEnabled");
}
