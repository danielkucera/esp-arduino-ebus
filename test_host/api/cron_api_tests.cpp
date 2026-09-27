#define private public
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "api/cron_api.hpp"
#include "app/command_manager.hpp"
#undef private

TEST_CASE("CronApi rejects a non-array evaluation payload", "[cron_api]") {
  Cron cron_api_commands(commandManager);
  CronApi api(cron_api_commands);

  httpd_req_t req{};
  req.body = R"({"rules":{}})";
  req.content_len = static_cast<int>(req.body.size());

  REQUIRE(CronApi::handleCronEvaluate(&req) == ESP_OK);
  REQUIRE(req.status == "400 Bad Request");
  REQUIRE(req.final_body.find(R"("id":"evaluate")") != std::string::npos);
  REQUIRE(req.final_body.find(R"("status":"failed")") != std::string::npos);
}

TEST_CASE("CronApi streams configured rules as JSON", "[cron_api]") {
  Cron cron_api_commands(commandManager);
  CronApi api(cron_api_commands);

  httpd_req_t req{};
  REQUIRE(CronApi::handleCron(&req) == ESP_OK);
  REQUIRE(req.content_type == "application/json;charset=utf-8");
  std::string body;
  for (const auto& chunk : req.chunks) body += chunk;
  REQUIRE(body == "[]");
  REQUIRE(req.chunks.back().empty());
}
