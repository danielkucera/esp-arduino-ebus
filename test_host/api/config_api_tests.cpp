#define private public
#include <catch2/catch_test_macros.hpp>

#include "api/config_api.hpp"
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
