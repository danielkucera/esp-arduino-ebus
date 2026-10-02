#define private public
#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "api/http_stub_state.hpp"
#include "api/network_api.hpp"
#undef private
#include <esp_wifi.h>

#include <string>

TEST_CASE("NetworkApi serializes network status sections", "[network_api]") {
  NetworkApi api;
  httpd_req_t req{};

  REQUIRE(NetworkApi::handleNetwork(&req) == ESP_OK);
  REQUIRE(req.content_type == "application/json;charset=utf-8");
  std::string body;
  for (const auto& chunk : req.chunks) body += chunk;
  REQUIRE(
      body ==
      R"({"wifi":{"connected":false},"mqtt":{"connected":false,"published":0,"publish_failed":0,"queue_dropped":0,"connects":0},"sockets":{"detected":0,"connected":0,"max":16},"sntp":{"enabled":false}})");
  REQUIRE(req.chunks.back().empty());
}

TEST_CASE("NetworkApi registers status and Wi-Fi scan routes",
          "[network_api]") {
  HostHttpStub::clearRoutes();
  NetworkApi api;
  auto* server = reinterpret_cast<httpd_handle_t>(1);

  REQUIRE(NetworkApi::registerHandlers(server));
  REQUIRE(HostHttpStub::routes().size() == 2);
  REQUIRE(HostHttpStub::routes()[0].uri == "/api/v1/network");
  REQUIRE(HostHttpStub::routes()[0].method == HTTP_GET);
  REQUIRE(HostHttpStub::routes()[0].handler == NetworkApi::handleNetwork);
  REQUIRE(HostHttpStub::routes()[1].uri == "/api/v1/network/wifi/scan");
  REQUIRE(HostHttpStub::routes()[1].method == HTTP_POST);
  REQUIRE(HostHttpStub::routes()[1].handler == NetworkApi::handleWifiScan);
}

TEST_CASE("NetworkApi reports Wi-Fi scan failure", "[network_api]") {
  NetworkApi api;
  host_wifi_scan_start_result = ESP_FAIL;

  httpd_req_t req{};
  REQUIRE(NetworkApi::handleWifiScan(&req) == ESP_OK);
  REQUIRE(req.status == "500 Internal Server Error");
  REQUIRE(req.final_body.find(R"("id":"wifi_scan")") != std::string::npos);
  REQUIRE(req.final_body.find(R"("status":"failed")") != std::string::npos);
  host_wifi_scan_start_result = ESP_OK;
}

TEST_CASE("NetworkApi serializes discovered Wi-Fi access points",
          "[network_api]") {
  NetworkApi api;
  host_wifi_scan_start_result = ESP_OK;
  host_wifi_scan_ap_count = 1;
  auto& ap = host_wifi_scan_records[0];
  ap = {};
  const char ssid[] = "test-net";
  std::memcpy(ap.ssid, ssid, sizeof(ssid) - 1);
  ap.bssid[0] = 0x01;
  ap.bssid[5] = 0x06;
  ap.rssi = -42;
  ap.primary = 11;
  ap.authmode = WIFI_AUTH_WPA2_PSK;
  const uint32_t clears_before = host_wifi_scan_clear_count;

  httpd_req_t req{};
  REQUIRE(NetworkApi::handleWifiScan(&req) == ESP_OK);
  REQUIRE(req.content_type == "application/json;charset=utf-8");
  std::string body;
  for (const auto& chunk : req.chunks) body += chunk;
  REQUIRE(body.find(R"("ssid":"test-net")") != std::string::npos);
  REQUIRE(body.find(R"("bssid":"01:00:00:00:00:06")") != std::string::npos);
  REQUIRE(body.find(R"("authMode":"WPA2_PSK")") != std::string::npos);
  REQUIRE(host_wifi_scan_clear_count == clears_before + 1);

  host_wifi_scan_ap_count = 0;
}
