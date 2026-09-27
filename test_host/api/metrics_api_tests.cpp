#define private public
#include <catch2/catch_test_macros.hpp>

#include "api/metrics_api.hpp"
#undef private

TEST_CASE("MetricsApi reset endpoints return success responses",
          "[metrics_api]") {
  MetricsApi api;

  httpd_req_t reset_req{};
  REQUIRE(MetricsApi::handleMetricsReset(&reset_req) == ESP_OK);
  REQUIRE(reset_req.status == "200 OK");
  REQUIRE(reset_req.final_body.find(R"("id":"reset")") != std::string::npos);
  REQUIRE(reset_req.final_body.find(R"("status":"successful")") !=
          std::string::npos);

  httpd_req_t breaker_req{};
  REQUIRE(MetricsApi::handleBreakerReset(&breaker_req) == ESP_OK);
  REQUIRE(breaker_req.status == "200 OK");
  REQUIRE(breaker_req.final_body.find(R"("id":"breaker_reset")") !=
          std::string::npos);
}
