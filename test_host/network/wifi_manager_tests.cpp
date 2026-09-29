#include <catch2/catch_test_macros.hpp>
#include <array>
#include <string>

#include "config/app_config.hpp"
#include "network/wifi_network_manager.hpp"
#include "system/logger.hpp"
#include "wifi_sdk.hpp"

Logger logger;

namespace {
AppConfig makeConfig() {
  AppConfig config;
  config.reset();
  config.network.wifi_ssid = "test-network";
  config.network.wifi_password = "synthetic-station-password";
  config.network.wifi_bssid = "02:00:00:00:00:01";
  config.network.ap_password = "synthetic-recovery-password";
  config.network.static_ip_enabled = true;
  config.network.ip_address = "192.0.2.7";
  config.network.netmask = "255.255.255.0";
  config.network.gateway = "192.0.2.1";
  config.network.dns1 = "192.0.2.2";
  config.network.dns2 = "192.0.2.3";
  return config;
}
void gotIp() {
  ip_event_got_ip_t event{};
  event.ip_info.ip.addr = ESP_IP4TOADDR(192, 0, 2, 7);
  event.ip_info.gw.addr = ESP_IP4TOADDR(192, 0, 2, 1);
  WifiNetworkManager::handle_event(nullptr, IP_EVENT, IP_EVENT_STA_GOT_IP, &event);
}
void disconnect() {
  WifiNetworkManager::handle_event(nullptr, WIFI_EVENT,
                                   WIFI_EVENT_STA_DISCONNECTED, nullptr);
}
struct ConnectedWifi {
  ConnectedWifi() {
    const AppConfig config = makeConfig();
    REQUIRE(WifiNetworkManager::begin(config));
    wifi_mock::mode_result = ESP_OK;
    wifi_mock::connect_result = ESP_OK;
    wifi_mock::get_ps_result = ESP_OK;
    wifi_mock::get_config_result = ESP_OK;
    wifi_mock::mode = WIFI_MODE_STA;
    WifiNetworkManager::setStaIpAssignedCallback(nullptr);
    gotIp();  // Reset the real retry counter and stop any pending timer.
    wifi_mock::connect_calls = 0;
    wifi_mock::mode_calls = 0;
  }
};
}

TEST_CASE("Wifi startup disables modem sleep and applies the typed boot snapshot", "[wifi][manager]") {
  // Inspect the initial startup policy before the fixture resets event state.
  const AppConfig config = makeConfig();
  REQUIRE(WifiNetworkManager::begin(config));
  REQUIRE(wifi_mock::ps_calls == 1);
  REQUIRE(wifi_mock::power_save == WIFI_PS_NONE);
  REQUIRE(wifi_mock::initial_connect_power_save == WIFI_PS_NONE);
  REQUIRE(wifi_mock::initial_connect_mode == WIFI_MODE_STA);
  REQUIRE(wifi_mock::sta_config.sta.scan_method == WIFI_ALL_CHANNEL_SCAN);
  REQUIRE(std::string(reinterpret_cast<char*>(wifi_mock::sta_config.sta.ssid)) == "test-network");
  REQUIRE(std::string(reinterpret_cast<char*>(wifi_mock::sta_config.sta.password)) == "synthetic-station-password");
  REQUIRE(std::string(reinterpret_cast<char*>(wifi_mock::ap_config.ap.password)) == "synthetic-recovery-password");
  REQUIRE(wifi_mock::ap_config.ap.authmode == WIFI_AUTH_WPA2_PSK);
  REQUIRE(wifi_mock::sta_config.sta.bssid_set);
  REQUIRE(wifi_mock::sta_config.sta.bssid[5] == 1);
}

TEST_CASE_METHOD(ConnectedWifi, "Wifi ignores overlapping event IDs from other families", "[wifi][manager]") {
  const int count = WifiNetworkManager::getReconnectCount();
  // WIFI_READY and GOT_IP intentionally share numeric ID zero.
  static_assert(WIFI_EVENT_WIFI_READY == IP_EVENT_STA_GOT_IP);
  WifiNetworkManager::handle_event(nullptr, WIFI_EVENT, WIFI_EVENT_WIFI_READY, nullptr);
  WifiNetworkManager::handle_event(nullptr, IP_EVENT, WIFI_EVENT_STA_DISCONNECTED, nullptr);
  WifiNetworkManager::handle_event(nullptr, nullptr, IP_EVENT_STA_GOT_IP, nullptr);
  REQUIRE(WifiNetworkManager::getReconnectCount() == count);
  REQUIRE(WifiNetworkManager::isStaConnected());
  REQUIRE_FALSE(wifi_mock::timer.active);
  REQUIRE(wifi_mock::mode_calls == 0);
  REQUIRE(wifi_mock::connect_calls == 0);
}

TEST_CASE_METHOD(ConnectedWifi, "Wifi disconnect backoff saturates and keeps recovery AP after reconnect", "[wifi][manager]") {
  const std::array<uint64_t, 9> delays = {
      1000000, 1000000, 5000000, 10000000, 15000000,
      20000000, 25000000, 30000000, 30000000};
  for (size_t i = 0; i < delays.size(); ++i) {
    disconnect();
    REQUIRE_FALSE(WifiNetworkManager::isStaConnected());
    REQUIRE(wifi_mock::timer.active);
    REQUIRE(wifi_mock::timer.delay_us == delays[i]);
    REQUIRE(wifi_mock::mode == (i < 2 ? WIFI_MODE_STA : WIFI_MODE_APSTA));
  }
  // More than 255 events must not wrap the saturated failure counter.
  for (int i = 0; i < 300; ++i) {
    disconnect();
    REQUIRE(wifi_mock::timer.delay_us == 30000000);
  }
  REQUIRE(wifi_mock::connect_calls == 0); // No blocking/immediate event-handler retry.
  REQUIRE(WifiNetworkManager::isRecoveryAccessPointReady());
  gotIp();
  REQUIRE(WifiNetworkManager::isStaConnected());
  REQUIRE_FALSE(wifi_mock::timer.active);
  REQUIRE(WifiNetworkManager::isRecoveryAccessPointReady());
  REQUIRE(wifi_mock::mode == WIFI_MODE_APSTA);
  disconnect();
  REQUIRE(wifi_mock::timer.delay_us == 1000000); // Successful IP assignment resets backoff.
}

TEST_CASE_METHOD(ConnectedWifi, "Wifi timer retries a failed driver connect without blocking", "[wifi][manager]") {
  disconnect();
  REQUIRE(wifi_mock::timer.callback != nullptr);
  wifi_mock::connect_result = ESP_FAIL;
  wifi_mock::timer.active = false; // The one-shot fired.
  wifi_mock::timer.callback(wifi_mock::timer.arg);
  REQUIRE(wifi_mock::connect_calls == 1);
  REQUIRE(wifi_mock::timer.active);
  REQUIRE(wifi_mock::timer.delay_us == 5000000);
  wifi_mock::connect_result = ESP_OK;
  wifi_mock::timer.active = false;
  wifi_mock::timer.callback(wifi_mock::timer.arg);
  REQUIRE(wifi_mock::connect_calls == 2);
  REQUIRE_FALSE(wifi_mock::timer.active);
}

TEST_CASE_METHOD(ConnectedWifi, "Wifi failed recovery AP enable is retried on next disconnect", "[wifi][manager]") {
  disconnect();
  disconnect();
  wifi_mock::mode_result = ESP_FAIL;
  disconnect();
  REQUIRE_FALSE(WifiNetworkManager::isRecoveryAccessPointReady());
  REQUIRE(wifi_mock::timer.delay_us == 5000000);
  wifi_mock::mode_result = ESP_OK;
  disconnect();
  REQUIRE(WifiNetworkManager::isRecoveryAccessPointReady());
  REQUIRE(wifi_mock::timer.delay_us == 10000000);
}

TEST_CASE_METHOD(ConnectedWifi, "Wifi status exposes driver policy and reports unavailable values", "[wifi][manager]") {
  REQUIRE(std::string(WifiNetworkManager::powerSaveMode()) == "disabled");
  REQUIRE(std::string(WifiNetworkManager::scanMethod()) == "all_channels");
  wifi_mock::get_ps_result = ESP_FAIL;
  wifi_mock::get_config_result = ESP_FAIL;
  REQUIRE(std::string(WifiNetworkManager::powerSaveMode()) == "unavailable");
  REQUIRE(std::string(WifiNetworkManager::scanMethod()) == "unavailable");
}

TEST_CASE_METHOD(ConnectedWifi, "Wifi status keeps its boot snapshot until restart", "[wifi][manager]") {
  auto staged = makeConfig();
  staged.network.static_ip_enabled = false;
  staged.network.ip_address = "192.0.2.99";
  staged.network.gateway = "192.0.2.98";
  staged.network.netmask = "255.255.0.0";
  staged.network.dns1 = "192.0.2.97";
  staged.network.dns2 = "192.0.2.96";
  staged.network.wifi_full_scan = false;
  // Calling begin again must not silently replace the applied configuration.
  REQUIRE(WifiNetworkManager::begin(staged));
  REQUIRE(WifiNetworkManager::isStaticIpEnabled());
  REQUIRE(WifiNetworkManager::getConfiguredIpAddress() == "192.0.2.7");
  REQUIRE(WifiNetworkManager::getConfiguredGateway() == "192.0.2.1");
  REQUIRE(WifiNetworkManager::getConfiguredNetmask() == "255.255.255.0");
  REQUIRE(WifiNetworkManager::getConfiguredDns1() == "192.0.2.2");
  REQUIRE(WifiNetworkManager::getConfiguredDns2() == "192.0.2.3");
  REQUIRE(std::string(WifiNetworkManager::scanMethod()) == "all_channels");
}
