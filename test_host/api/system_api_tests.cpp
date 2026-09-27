#define private public
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "api/system_api.hpp"
#undef private

TEST_CASE("SystemApi heap endpoint returns current and trend fields",
          "[system_api]") {
  SystemApi api;
  httpd_req_t req{};

  REQUIRE(SystemApi::handleHeap(&req) == ESP_OK);
  REQUIRE(req.content_type == "application/json;charset=utf-8");
  std::string body;
  for (const auto& chunk : req.chunks) body += chunk;
  REQUIRE(body.find(R"("current")") != std::string::npos);
  REQUIRE(body.find(R"("trend")") != std::string::npos);
  REQUIRE(req.chunks.back().empty());
}
