#define private public
#include <catch2/catch_test_macros.hpp>

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
