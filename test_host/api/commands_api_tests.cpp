#define private public
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <ebus/detail/json_reader.hpp>
#include <string>
#include <utility>

#include "api/commands_api.hpp"
#include "api/http_stub_state.hpp"
#include "app/command_manager.hpp"
#undef private

namespace {

std::string commandJson(const std::string& key, const std::string& name) {
  return R"({"key":")" + key + R"(","name":")" + name +
         R"(","read_cmd":"fe070009","write_cmd":"","interval":60,)"
         R"("master":true,"fields":[{"name":"value","profile":"uint8",)"
         R"("position":1,"ha_profile":""}]})";
}

void insertCommands(const std::string& commands) {
  httpd_req_t req{};
  req.body = R"({"commands":[)" + commands + "]}";
  req.content_len = static_cast<int>(req.body.size());
  REQUIRE(CommandsApi::handleCommandsInsert(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
}

}  // namespace

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

TEST_CASE("CommandsApi serves page and lists inserted commands",
          "[commands_api]") {
  commandManager.wipeCommands();
  MqttHA mqtt_ha;
  CommandsApi api(commandManager, mqtt_ha);

  httpd_req_t page_req{};
  REQUIRE(CommandsApi::handleCommandsPage(&page_req) == ESP_OK);
  REQUIRE(page_req.status == "200 OK");
  REQUIRE(page_req.content_type == "text/html");
  REQUIRE(page_req.final_body == "<html><body>commands</body></html>");

  insertCommands(commandJson("api01", "Api_Test"));
  httpd_req_t list_req{};
  REQUIRE(CommandsApi::handleCommands(&list_req) == ESP_OK);
  REQUIRE(list_req.content_type == "application/json;charset=utf-8");
  std::string body;
  for (const auto& chunk : list_req.chunks) body += chunk;
  REQUIRE(ebus::detail::JsonReader::validate(body));
  REQUIRE(body.find(R"("key":"api01")") != std::string::npos);
  REQUIRE(body.find(R"("name":"Api_Test")") != std::string::npos);
  REQUIRE(list_req.chunks.back().empty());
  commandManager.wipeCommands();
}

TEST_CASE("CommandsApi evaluates valid commands and rejects invalid ones",
          "[commands_api]") {
  commandManager.wipeCommands();
  MqttHA mqtt_ha;
  CommandsApi api(commandManager, mqtt_ha);

  httpd_req_t valid_req{};
  valid_req.body = R"({"commands":[)" + commandJson("api01", "Api_Test") + "]}";
  valid_req.content_len = static_cast<int>(valid_req.body.size());
  REQUIRE(CommandsApi::handleCommandsEvaluate(&valid_req) == ESP_OK);
  REQUIRE(valid_req.status == "200 OK");
  REQUIRE(valid_req.final_body.find(R"("id":"evaluate")") != std::string::npos);
  REQUIRE(valid_req.final_body.find(R"("status":"successful")") !=
          std::string::npos);

  httpd_req_t invalid_req{};
  invalid_req.body = R"({"commands":[{"key":"bad"}]})";
  invalid_req.content_len = static_cast<int>(invalid_req.body.size());
  REQUIRE(CommandsApi::handleCommandsEvaluate(&invalid_req) == ESP_OK);
  REQUIRE(invalid_req.status == "400 Bad Request");
  REQUIRE(invalid_req.final_body.find(R"("status":"failed")") !=
          std::string::npos);
  commandManager.wipeCommands();
}

TEST_CASE("CommandsApi removes selected commands", "[commands_api]") {
  commandManager.wipeCommands();
  MqttHA mqtt_ha;
  CommandsApi api(commandManager, mqtt_ha);
  insertCommands(commandJson("api01", "First") + "," +
                 commandJson("api02", "Second"));
  REQUIRE(commandManager.getCommandCount() == 2);

  httpd_req_t req{};
  req.body = R"({"keys":["api01"]})";
  req.content_len = static_cast<int>(req.body.size());
  REQUIRE(CommandsApi::handleCommandsRemove(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.final_body.find(R"("id":"remove")") != std::string::npos);
  REQUIRE(commandManager.findCommand("api01") == nullptr);
  REQUIRE(commandManager.findCommand("api02") != nullptr);
  commandManager.wipeCommands();
}

TEST_CASE("CommandsApi saves and loads commands", "[commands_api]") {
  commandManager.wipeCommands();
  MqttHA mqtt_ha;
  CommandsApi api(commandManager, mqtt_ha);
  insertCommands(commandJson("api01", "Api_Test"));

  httpd_req_t save_req{};
  REQUIRE(CommandsApi::handleCommandsSave(&save_req) == ESP_OK);
  REQUIRE(save_req.status == "200 OK");
  REQUIRE(save_req.final_body.find(R"("status":"successful")") !=
          std::string::npos);
  REQUIRE(save_req.final_body.find("Saved ") != std::string::npos);

  commandManager.removeAll();
  REQUIRE(commandManager.getCommandCount() == 0);
  httpd_req_t load_req{};
  REQUIRE(CommandsApi::handleCommandsLoad(&load_req) == ESP_OK);
  REQUIRE(load_req.status == "200 OK");
  REQUIRE(load_req.final_body.find(R"("status":"successful")") !=
          std::string::npos);
  REQUIRE(commandManager.findCommand("api01") != nullptr);

  httpd_req_t wipe_req{};
  REQUIRE(CommandsApi::handleCommandsWipe(&wipe_req) == ESP_OK);
  REQUIRE(commandManager.getCommandCount() == 0);
  REQUIRE(wipe_req.final_body.find(R"("id":"wipe")") != std::string::npos);
  if (wipe_req.status == "200 OK") {
    REQUIRE(wipe_req.final_body.find(R"("status":"successful")") !=
            std::string::npos);
    REQUIRE(wipe_req.final_body.find("Wiped ") != std::string::npos);
  } else {
    REQUIRE(wipe_req.status == "500 Internal Server Error");
    REQUIRE(wipe_req.final_body.find(R"("status":"failed")") !=
            std::string::npos);
    REQUIRE(wipe_req.final_body.find("Wipe failed") != std::string::npos);
  }
}

TEST_CASE("CommandsApi uploads a command file", "[commands_api]") {
  commandManager.wipeCommands();
  commandManager.removeAll();
  MqttHA mqtt_ha;
  CommandsApi api(commandManager, mqtt_ha);

  const std::string payload = "[" + commandJson("api01", "Uploaded_Test") + "]";
  httpd_req_t req{};
  req.method = HTTP_POST;
  req.body = payload;
  req.content_len = static_cast<int>(payload.size());

  REQUIRE(CommandsApi::handleCommandsUpload(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.final_body.find(R"("id":"upload")") != std::string::npos);
  REQUIRE(req.final_body.find("Uploaded ") != std::string::npos);
  REQUIRE(req.final_body.find("loaded 1 commands") != std::string::npos);
  REQUIRE(commandManager.findCommand("api01") != nullptr);
  commandManager.removeAll();
}

TEST_CASE("CommandsApi rejects non-POST uploads", "[commands_api]") {
  commandManager.wipeCommands();
  MqttHA mqtt_ha;
  CommandsApi api(commandManager, mqtt_ha);
  httpd_req_t req{};

  REQUIRE(CommandsApi::handleCommandsUpload(&req) == ESP_OK);
  REQUIRE(req.status == "405 Method Not Allowed");
  REQUIRE(req.final_body.find(R"("id":"upload")") != std::string::npos);
  commandManager.wipeCommands();
}

TEST_CASE("CommandsApi registers all command routes", "[commands_api]") {
  MqttHA mqtt_ha;
  CommandsApi api(commandManager, mqtt_ha);
  HostHttpStub::clearRoutes();

  REQUIRE_FALSE(CommandsApi::registerHandlers(nullptr));
  REQUIRE(HostHttpStub::routes().empty());
  REQUIRE(CommandsApi::registerHandlers(reinterpret_cast<httpd_handle_t>(1)));
  REQUIRE(HostHttpStub::routes().size() == 9);

  const auto& routes = HostHttpStub::routes();
  const std::pair<const char*, httpd_method_t> expected_routes[] = {
      {"/commands", HTTP_GET},
      {"/api/v1/app/commands", HTTP_GET},
      {"/api/v1/app/commands/evaluate", HTTP_POST},
      {"/api/v1/app/commands/insert", HTTP_POST},
      {"/api/v1/app/commands/upload", HTTP_POST},
      {"/api/v1/app/commands/remove", HTTP_POST},
      {"/api/v1/app/commands/load", HTTP_POST},
      {"/api/v1/app/commands/save", HTTP_POST},
      {"/api/v1/app/commands/wipe", HTTP_POST},
  };
  REQUIRE(sizeof(expected_routes) / sizeof(expected_routes[0]) ==
          routes.size());
  for (const auto& expected : expected_routes) {
    REQUIRE(std::count_if(routes.begin(), routes.end(),
                          [&expected](const auto& route) {
                            return route.uri == expected.first &&
                                   route.method == expected.second;
                          }) == 1);
  }
  HostHttpStub::clearRoutes();
}
