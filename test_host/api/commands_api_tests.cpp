#define private public
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "api/commands_api.hpp"
#include "app/command_manager.hpp"
#undef private

TEST_CASE("CommandsApi rejects malformed command arrays", "[commands_api]") {
  commandManager.wipeCommands();
  MqttHA mqtt_ha;
  CommandsApi api(commandManager, mqtt_ha);

  httpd_req_t req{};
  req.body = R"({"commands":{}})";
  req.content_len = static_cast<int>(req.body.size());

  REQUIRE(CommandsApi::handleCommandsEvaluate(&req) == ESP_OK);
  REQUIRE(req.status == "400 Bad Request");
  REQUIRE(req.final_body.find(R"("status":"failed")") != std::string::npos);
  REQUIRE(req.final_body.find(R"("id":"evaluate")") != std::string::npos);
}

TEST_CASE("CommandsApi inserts a valid command", "[commands_api]") {
  commandManager.wipeCommands();
  MqttHA mqtt_ha;
  CommandsApi api(commandManager, mqtt_ha);

  httpd_req_t req{};
  req.body =
      R"({"commands":[{"key":"api01","name":"Api_Test","read_cmd":"fe070009",)"
      R"("write_cmd":"","interval":0,"master":true,"fields":[)"
      R"({"name":"value","profile":"uint8","position":1,"ha_profile":""}]}]})";
  req.content_len = static_cast<int>(req.body.size());

  REQUIRE(CommandsApi::handleCommandsInsert(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.final_body.find(R"("status":"successful")") != std::string::npos);
  REQUIRE(commandManager.findCommand("api01") != nullptr);
  commandManager.wipeCommands();
}
