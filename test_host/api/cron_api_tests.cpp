#define private public
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "api/cron_api.hpp"
#include "app/command.hpp"
#include "app/command_manager.hpp"
#undef private

namespace {

void insertWritableCommand() {
  commandManager.wipeCommands();
  const std::string command_json =
      R"({"key":"cron-test","name":"SetPoint","read_cmd":"50b509030d3300",)"
      R"("write_cmd":"50b509040e3300","interval":60,"master":false,)"
      R"("fields":[{"name":"value","profile":"data2b_celsius","position":1,)"
      R"("ha_profile":"sensor_temperature"}]})";
  ebus::detail::JsonReader command_reader(command_json);
  commandManager.insertCommand(Command::fromJson(command_reader));
  REQUIRE(commandManager.findCommand("cron-test") != nullptr);
}

}  // namespace

TEST_CASE("CronApi serves the cron page", "[cron_api]") {
  Cron cron_api_commands(commandManager);
  CronApi api(cron_api_commands);
  httpd_req_t req{};

  REQUIRE(CronApi::handleCronPage(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.content_type == "text/html");
  REQUIRE(req.final_body == "<html><body>cron</body></html>");
}

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

TEST_CASE("CronApi evaluates valid rules and rejects invalid schedules",
          "[cron_api]") {
  insertWritableCommand();
  Cron cron_api_commands(commandManager);
  CronApi api(cron_api_commands);

  httpd_req_t valid_req{};
  valid_req.body =
      R"([{"id":"r1","schedule":"* * * * *","command_key":"cron-test",)"
      R"("enabled":true,"value":25.5}])";
  valid_req.content_len = static_cast<int>(valid_req.body.size());

  REQUIRE(CronApi::handleCronEvaluate(&valid_req) == ESP_OK);
  REQUIRE(valid_req.status == "200 OK");
  REQUIRE(valid_req.final_body.find(R"("id":"evaluate")") != std::string::npos);
  REQUIRE(valid_req.final_body.find(R"("status":"successful")") !=
          std::string::npos);

  httpd_req_t invalid_req{};
  invalid_req.body =
      R"([{"id":"r2","schedule":"99 * * * *","command_key":"cron-test",)"
      R"("enabled":true,"value":25.5}])";
  invalid_req.content_len = static_cast<int>(invalid_req.body.size());

  REQUIRE(CronApi::handleCronEvaluate(&invalid_req) == ESP_OK);
  REQUIRE(invalid_req.status == "400 Bad Request");
  REQUIRE(invalid_req.final_body.find("Invalid minute field") !=
          std::string::npos);
  commandManager.wipeCommands();
}

TEST_CASE("CronApi accepts an empty evaluation array", "[cron_api]") {
  Cron cron_api_commands(commandManager);
  CronApi api(cron_api_commands);

  httpd_req_t req{};
  req.body = "[]";
  req.content_len = static_cast<int>(req.body.size());

  REQUIRE(CronApi::handleCronEvaluate(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.final_body.find(R"("status":"successful")") != std::string::npos);
}

TEST_CASE("CronApi rejects non-object evaluation entries", "[cron_api]") {
  Cron cron_api_commands(commandManager);
  CronApi api(cron_api_commands);

  httpd_req_t req{};
  req.body = "[null]";
  req.content_len = static_cast<int>(req.body.size());

  REQUIRE(CronApi::handleCronEvaluate(&req) == ESP_OK);
  REQUIRE(req.status == "400 Bad Request");
  REQUIRE(req.final_body.find("Each cron rule must be a JSON object") !=
          std::string::npos);
}

TEST_CASE("CronApi rejects non-array save payloads", "[cron_api]") {
  Cron cron_api_commands(commandManager);
  CronApi api(cron_api_commands);

  httpd_req_t req{};
  req.body = R"({"rules":[]})";
  req.content_len = static_cast<int>(req.body.size());

  REQUIRE(CronApi::handleCronSave(&req) == ESP_OK);
  REQUIRE(req.status == "500 Internal Server Error");
  REQUIRE(req.final_body.find(R"("id":"save")") != std::string::npos);
  REQUIRE(req.final_body.find(R"("status":"failed")") != std::string::npos);
}

TEST_CASE("CronApi saves rules and loads them from persistence", "[cron_api]") {
  insertWritableCommand();
  const std::string payload =
      R"([{"id":"saved-rule","schedule":"* * * * *",)"
      R"("command_key":"cron-test","enabled":true,"value":25.5}])";
  Cron save_cron(commandManager);
  CronApi save_api(save_cron);
  httpd_req_t save_req{};
  save_req.body = payload;
  save_req.content_len = static_cast<int>(payload.size());

  REQUIRE(CronApi::handleCronSave(&save_req) == ESP_OK);
  REQUIRE(save_req.status == "200 OK");
  REQUIRE(save_req.final_body.find(R"("id":"save")") != std::string::npos);
  REQUIRE(save_req.final_body.find(R"("status":"successful")") !=
          std::string::npos);
  REQUIRE(save_req.final_body.find("Saved ") != std::string::npos);

  Cron load_cron(commandManager);
  CronApi load_api(load_cron);
  httpd_req_t load_req{};
  REQUIRE(CronApi::handleCronLoad(&load_req) == ESP_OK);
  REQUIRE(load_req.status == "200 OK");
  REQUIRE(load_req.final_body.find(R"("id":"load")") != std::string::npos);
  REQUIRE(load_req.final_body.find(R"("status":"successful")") !=
          std::string::npos);
  REQUIRE(load_req.final_body.find("Loaded ") != std::string::npos);
  REQUIRE(load_cron.getRulesCount() == 1);

  httpd_req_t list_req{};
  REQUIRE(CronApi::handleCron(&list_req) == ESP_OK);
  std::string body;
  for (const auto& chunk : list_req.chunks) body += chunk;
  REQUIRE(body.find(R"("id":"saved-rule")") != std::string::npos);
  REQUIRE(body.find(R"("command_key":"cron-test")") != std::string::npos);
  REQUIRE(body.find(R"("value":25.5)") != std::string::npos);

  commandManager.wipeCommands();
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
