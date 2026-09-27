#define private public
#include <catch2/catch_test_macros.hpp>

#include "api/config_api.hpp"
#include "http_stub_state.hpp"
#undef private

TEST_CASE("ConfigApi serves the configuration page", "[config_api]") {
  httpd_req_t req{};

  REQUIRE(ConfigApi::handleConfigPage(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.content_type == "text/html");
  REQUIRE(req.final_body == "<html><body>config</body></html>");
}

TEST_CASE("ConfigApi rejects a null server", "[config_api]") {
  REQUIRE_FALSE(ConfigApi::registerHandlers(nullptr));
}

TEST_CASE("ConfigApi registers the configuration page route", "[config_api]") {
  HostHttpStub::clearRoutes();
  ConfigApi api;
  auto* server = reinterpret_cast<httpd_handle_t>(1);

  REQUIRE(ConfigApi::registerHandlers(server));
  REQUIRE(HostHttpStub::routes().size() == 1);
  REQUIRE(HostHttpStub::routes()[0].uri == "/config");
  REQUIRE(HostHttpStub::routes()[0].method == HTTP_GET);
  REQUIRE(HostHttpStub::routes()[0].handler == ConfigApi::handleConfigPage);
}
