#define private public
#include <catch2/catch_test_macros.hpp>
#include <ebus/detail/json_reader.hpp>

#include "api/metrics_api.hpp"
#include "ebus_test_helpers.hpp"
#undef private

TEST_CASE("MetricsApi streams valid metrics JSON", "[metrics_api]") {
  REQUIRE(configureHostEbusController());

  httpd_req_t req{};
  REQUIRE(MetricsApi::handleMetrics(&req) == ESP_OK);
  REQUIRE(req.content_type == "application/json;charset=utf-8");

  std::string body;
  for (const auto& chunk : req.chunks) body += chunk;
  REQUIRE(ebus::detail::JsonReader::validate(body));
  REQUIRE(body.size() > 2);
  REQUIRE(body.front() == '{');
  REQUIRE(body.back() == '}');
}

TEST_CASE("MetricsApi serves the metrics page", "[metrics_api]") {
  httpd_req_t req{};

  REQUIRE(MetricsApi::handleMetricsPage(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.content_type == "text/html");
  REQUIRE(req.final_body == "<html><body>metrics</body></html>");
}

TEST_CASE("MetricsApi reset endpoints return success responses",
          "[metrics_api]") {
  MetricsApi api;

  httpd_req_t reset_req{};
  REQUIRE(MetricsApi::handleMetricsReset(&reset_req) == ESP_OK);
  REQUIRE(reset_req.status == "200 OK");
  REQUIRE(reset_req.content_type == "application/json;charset=utf-8");
  REQUIRE(reset_req.final_body == R"({"id":"reset","status":"successful"})");

  httpd_req_t breaker_req{};
  REQUIRE(MetricsApi::handleBreakerReset(&breaker_req) == ESP_OK);
  REQUIRE(breaker_req.status == "200 OK");
  REQUIRE(breaker_req.content_type == "application/json;charset=utf-8");
  REQUIRE(breaker_req.final_body ==
          R"({"id":"breaker_reset","status":"successful"})");
}
