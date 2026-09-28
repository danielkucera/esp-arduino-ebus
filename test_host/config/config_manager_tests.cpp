#include <catch2/catch_test_macros.hpp>
#include <string>

#include "config/app_config_loader.hpp"
#include "config/config_manager.hpp"

// The ConfigManager instance backing these tests. NVS itself is the
// in-memory host mock (test_host/mocks/config/nvs_mock.cpp), so each case
// starts from resetConfig() for isolation.
ConfigManager configManager;

TEST_CASE("ConfigManager string roundtrip", "[config_nvs]") {
  configManager.resetConfig();

  REQUIRE(ConfigManager::writeString("wifiSsid", "my-ssid") == true);
  REQUIRE(std::string(ConfigManager::readString("wifiSsid")) == "my-ssid");

  // Overwrite works.
  REQUIRE(ConfigManager::writeString("wifiSsid", "other") == true);
  REQUIRE(std::string(ConfigManager::readString("wifiSsid")) == "other");

  // Missing keys yield the fallback.
  REQUIRE(std::string(ConfigManager::readString("nope", "fb")) == "fb");
  REQUIRE(std::string(ConfigManager::readString("nope")) == "");
}

TEST_CASE("ConfigManager int parsing matches firmware semantics",
          "[config_nvs]") {
  configManager.resetConfig();

  REQUIRE(ConfigManager::writeString("busWindow", "4300") == true);
  REQUIRE(ConfigManager::readInt("busWindow", 4400) == 4300);

  // Non-numeric content falls back (strtol failure path).
  REQUIRE(ConfigManager::writeString("busWindow", "abc") == true);
  REQUIRE(ConfigManager::readInt("busWindow", 4400) == 4400);

  // Missing keys fall back.
  REQUIRE(ConfigManager::readInt("busWindow", 4400) == 4400);
}

TEST_CASE("ConfigManager bool parsing matches firmware semantics",
          "[config_nvs]") {
  configManager.resetConfig();

  for (auto truthy : {"true", "1", "selected", "on"}) {
    REQUIRE(ConfigManager::writeString("sntpEnabled", truthy) == true);
    REQUIRE(ConfigManager::readBool("sntpEnabled") == true);
  }
  for (auto falsy : {"false", "0", "", "nope"}) {
    REQUIRE(ConfigManager::writeString("sntpEnabled", falsy) == true);
    REQUIRE(ConfigManager::readBool("sntpEnabled") == false);
  }
  // Missing keys yield the fallback.
  configManager.resetConfig();
  REQUIRE(ConfigManager::readBool("sntpEnabled", true) == true);
  REQUIRE(ConfigManager::readBool("sntpEnabled") == false);
}

TEST_CASE("ConfigManager resetConfig clears stored keys", "[config_nvs]") {
  REQUIRE(ConfigManager::writeString("wifiSsid", "my-ssid") == true);
  configManager.resetConfig();
  REQUIRE(std::string(ConfigManager::readString("wifiSsid", "fb")) == "fb");
}

TEST_CASE("ConfigManager fetchConfig dumps stored keys", "[config_nvs]") {
  configManager.resetConfig();
  REQUIRE(ConfigManager::writeString("wifiSsid", "my-ssid") == true);

  std::string out;
  ConfigManager::fetchConfig([&out](std::string_view chunk) {
    out.append(chunk.data(), chunk.size());
  });
  REQUIRE(out.find("my-ssid") != std::string::npos);
}

TEST_CASE("AppConfigLoader save/load roundtrip", "[config_nvs]") {
  configManager.resetConfig();

  AppConfig written;
  written.reset();
  written.network.wifi_ssid.assign("my-ssid");
  written.pwm.value = 200;
  written.bus.window_us = 4300;
  written.bus.address.assign("01");
  written.sntp.enabled = true;
  written.mqtt.server.assign("mqtt.local");

  AppConfigLoader loader(configManager);
  REQUIRE(loader.save(written) == true);

  AppConfig loaded;
  REQUIRE(loader.load(loaded) == true);
  REQUIRE(std::string(loaded.network.wifi_ssid.c_str()) == "my-ssid");
  REQUIRE(loaded.pwm.value == 200);
  REQUIRE(loaded.bus.window_us == 4300);
  REQUIRE(std::string(loaded.bus.address.c_str()) == "01");
  REQUIRE(loaded.sntp.enabled == true);
  REQUIRE(std::string(loaded.mqtt.server.c_str()) == "mqtt.local");
  // Untouched keys carry reset defaults.
  REQUIRE(loaded.bus.offset_us == 50);
}

TEST_CASE("AppConfigLoader load overlays stored keys on defaults",
          "[config_nvs]") {
  configManager.resetConfig();
  REQUIRE(ConfigManager::writeString("pwmValue", "210") == true);

  AppConfig loaded;
  AppConfigLoader loader(configManager);
  REQUIRE(loader.load(loaded) == true);
  REQUIRE(loaded.pwm.value == 210);
  REQUIRE(loaded.bus.window_us == 4400);
}

TEST_CASE("AppConfigLoader preserves provisioning defaults and open station credentials",
          "[config_nvs]") {
  configManager.resetConfig();
  AppConfigLoader loader(configManager);
  AppConfig loaded;
  REQUIRE(loader.load(loaded));
  REQUIRE(loaded.network.wifi_ssid.empty());
  REQUIRE(loaded.network.wifi_password.empty());
  REQUIRE(loaded.network.wifi_full_scan);
  REQUIRE(loaded.isValid());

  REQUIRE(ConfigManager::writeString("wifiSsid", "open-test-network"));
  REQUIRE(ConfigManager::writeString("wifiPassword", ""));
  REQUIRE(ConfigManager::writeString("wifiPowerSave", "true"));
  REQUIRE(loader.load(loaded));
  REQUIRE(std::string(loaded.network.wifi_ssid.c_str()) == "open-test-network");
  REQUIRE(loaded.network.wifi_password.empty());
  REQUIRE(loaded.network.wifi_full_scan);
}

TEST_CASE("AppConfigLoader roundtrips Wi-Fi policy and full-length recovery credentials",
          "[config_nvs]") {
  configManager.resetConfig();
  AppConfigLoader loader(configManager);
  AppConfig written;
  written.reset();
  written.network.wifi_ssid = "test-network";
  written.network.wifi_password = "synthetic-station-password";
  written.network.wifi_bssid = "02:00:00:00:00:01";
  written.network.ap_password.assign(std::string(63, 'p'));
  written.network.wifi_full_scan = false;
  written.network.static_ip_enabled = true;
  written.network.ip_address = "192.0.2.7";
  written.network.gateway = "192.0.2.1";
  written.network.netmask = "255.255.255.0";
  written.network.dns1 = "192.0.2.2";
  written.network.dns2 = "192.0.2.3";
  REQUIRE(loader.save(written));
  REQUIRE_FALSE(ConfigManager::readBool("wifiFullScan", true));

  AppConfig loaded;
  REQUIRE(loader.load(loaded));
  std::vector<std::string> drift;
  // Restrict the comparison to network fields: unrelated upstream defaults
  // are outside this test's scope.
  AppConfig expected = loaded;
  expected.network = written.network;
  AppConfig::collectDrift(expected, loaded, drift);
  REQUIRE(drift.empty());
  REQUIRE(loaded.network.recoveryApPassword() == std::string(63, 'p'));
  REQUIRE_FALSE(loaded.network.wifi_full_scan);

  const AppConfig boot = loaded;
  loaded.network.wifi_full_scan = true;
  REQUIRE(loader.save(loaded));
  REQUIRE(loader.load(loaded));
  REQUIRE(loaded.network.wifi_full_scan);
  REQUIRE_FALSE(boot.network.wifi_full_scan);
  AppConfig::collectDrift(boot, loaded, drift);
  REQUIRE(drift == std::vector<std::string>{"wifiFullScan"});
}
