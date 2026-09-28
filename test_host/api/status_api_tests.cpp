#define private public
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "api/http_stub_state.hpp"
#include "api/status_api.hpp"
#undef private

TEST_CASE("StatusApi serves the status page", "[status_api]") {
  httpd_req_t req{};

  REQUIRE(StatusApi::handleStatusPage(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.content_type == "text/html");
  REQUIRE(req.final_body == "<html><body>status</body></html>");
}

TEST_CASE("StatusApi rejects a null server", "[status_api]") {
  REQUIRE_FALSE(StatusApi::registerHandlers(nullptr));
}

TEST_CASE("StatusApi registers status and health routes", "[status_api]") {
  HostHttpStub::clearRoutes();
  StatusApi api;
  auto* server = reinterpret_cast<httpd_handle_t>(1);

  REQUIRE(StatusApi::registerHandlers(server));
  REQUIRE(HostHttpStub::routes().size() == 2);
  REQUIRE(HostHttpStub::routes()[0].uri == "/status");
  REQUIRE(HostHttpStub::routes()[0].method == HTTP_GET);
  REQUIRE(HostHttpStub::routes()[0].handler == StatusApi::handleStatusPage);
  REQUIRE(HostHttpStub::routes()[1].uri == "/api/v1/health");
  REQUIRE(HostHttpStub::routes()[1].method == HTTP_GET);
  REQUIRE(HostHttpStub::routes()[1].handler == StatusApi::handleHealth);
}

TEST_CASE("StatusApi health endpoint returns system health details",
          "[status_api]") {
  StatusApi api;
  httpd_req_t req{};

  REQUIRE(StatusApi::handleHealth(&req) == ESP_OK);
  REQUIRE(req.content_type == "application/json;charset=utf-8");

  std::string body;
  for (const auto& chunk : req.chunks) body += chunk;
  REQUIRE(body.find(R"("firmware":"host-test")") != std::string::npos);
  REQUIRE(body.find(R"("uptime":123456)") != std::string::npos);
  REQUIRE(body.find(R"("reset_code":0)") != std::string::npos);
  REQUIRE(body.find(R"("rssi":-50)") != std::string::npos);
  REQUIRE(body.find(R"("wifi_reconnects":0)") != std::string::npos);
  REQUIRE(body.find(R"("mqtt_connected":false)") != std::string::npos);
  REQUIRE(body.find(R"("error_rate_pct":0)") != std::string::npos);
  REQUIRE(body.find(R"("breaker_open":false)") != std::string::npos);
  REQUIRE(body.find(R"("messages":0)") != std::string::npos);
  REQUIRE(body.find(R"("errors":0)") != std::string::npos);
  REQUIRE(body.find(R"("logger":{"ring_overwrites":0,"print_drops":0})") !=
          std::string::npos);
  REQUIRE(req.chunks.back().empty());
}
