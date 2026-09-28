#define private public
#include <catch2/catch_test_macros.hpp>
#include <ebus/detail/json_reader.hpp>

#include "api/devices_api.hpp"
#include "app/ebus_test_helpers.hpp"
#undef private

TEST_CASE("DevicesApi serves the devices page", "[devices_api]") {
  DevicesApi api;
  httpd_req_t req{};

  REQUIRE(DevicesApi::handleDevicesPage(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.content_type == "text/html");
  REQUIRE(req.final_body == "<html><body>devices</body></html>");
}

TEST_CASE("DevicesApi streams a valid device list", "[devices_api]") {
  REQUIRE(configureHostEbusController());

  httpd_req_t req{};
  REQUIRE(DevicesApi::handleDevices(&req) == ESP_OK);
  REQUIRE(req.content_type == "application/json;charset=utf-8");

  std::string body;
  for (const auto& chunk : req.chunks) body += chunk;
  REQUIRE(ebus::detail::JsonReader::validate(body));
  REQUIRE(body == "[]");
}

TEST_CASE("DevicesApi scan endpoints return initiated responses",
          "[devices_api]") {
  DevicesApi api;

  httpd_req_t scan_req{};
  REQUIRE(DevicesApi::handleDevicesScan(&scan_req) == ESP_OK);
  REQUIRE(scan_req.status == "200 OK");
  REQUIRE(scan_req.content_type == "application/json;charset=utf-8");
  REQUIRE(scan_req.final_body == R"({"id":"scan","status":"initiated"})");

  httpd_req_t full_scan_req{};
  REQUIRE(DevicesApi::handleDevicesScanFull(&full_scan_req) == ESP_OK);
  REQUIRE(full_scan_req.status == "200 OK");
  REQUIRE(full_scan_req.content_type == "application/json;charset=utf-8");
  REQUIRE(full_scan_req.final_body ==
          R"({"id":"scan_full","status":"initiated"})");
}
