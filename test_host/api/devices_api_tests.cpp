#define private public
#include <catch2/catch_test_macros.hpp>

#include "api/devices_api.hpp"
#undef private

TEST_CASE("DevicesApi scan endpoints return initiated responses",
          "[devices_api]") {
  DevicesApi api;

  httpd_req_t scan_req{};
  REQUIRE(DevicesApi::handleDevicesScan(&scan_req) == ESP_OK);
  REQUIRE(scan_req.status == "200 OK");
  REQUIRE(scan_req.final_body.find(R"("id":"scan")") != std::string::npos);
  REQUIRE(scan_req.final_body.find(R"("status":"initiated")") !=
          std::string::npos);

  httpd_req_t full_scan_req{};
  REQUIRE(DevicesApi::handleDevicesScanFull(&full_scan_req) == ESP_OK);
  REQUIRE(full_scan_req.status == "200 OK");
  REQUIRE(full_scan_req.final_body.find(R"("id":"scan_full")") !=
          std::string::npos);
}
