#define private public
#define protected public
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "api/values_api.hpp"
#include "app/command.hpp"
#include "app/command_manager.hpp"
#undef private
#undef protected

namespace {

std::string makeCommandJson() {
  return R"({
    "key":"01",
    "name":"Outside_Temperature",
    "read_cmd":"fe070009",
    "write_cmd":"50b509040e3300",
    "interval":60,
    "master":false,
    "fields":[
      {"name":"value","profile":"data2b_celsius","position":1,"ha_profile":"sensor_temperature"}
    ]
  })";
}

void insertTestCommand() {
  command_manager.wipeCommands();
  const std::string command_json = makeCommandJson();
  ebus::detail::JsonReader reader(command_json);
  Command command = Command::fromJson(reader);
  command_manager.insertCommand(std::move(command));
}

}  // namespace

TEST_CASE("ValuesApi serves the values page", "[values_api]") {
  command_manager.wipeCommands();
  ValuesApi api(command_manager);
  httpd_req_t req{};

  REQUIRE(ValuesApi::handleValuesPage(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.content_type == "text/html");
  REQUIRE(req.final_body == "<html><body>values</body></html>");
}

TEST_CASE("ValuesApi reads and writes by key", "[values_api]") {
  insertTestCommand();
  REQUIRE(command_manager.findCommand("01") != nullptr);

  ValuesApi api(command_manager);

  httpd_req_t read_req{};
  read_req.body = R"({"key":"01"})";
  read_req.content_len = static_cast<int>(read_req.body.size());
  REQUIRE(ValuesApi::handleValuesRead(&read_req) == ESP_OK);
  REQUIRE(read_req.status == "200 OK");
  REQUIRE(read_req.content_type == "application/json;charset=utf-8");
  REQUIRE(read_req.final_body.find(R"("id":"read")") != std::string::npos);
  REQUIRE(read_req.final_body.find(R"("status":"requested")") !=
          std::string::npos);

  httpd_req_t write_req{};
  write_req.body = R"({"key":"01","value":12.5})";
  write_req.content_len = static_cast<int>(write_req.body.size());
  REQUIRE(ValuesApi::handleValuesWrite(&write_req) == ESP_OK);
  REQUIRE(write_req.status == "200 OK");
  REQUIRE(write_req.final_body.find(R"("id":"write")") != std::string::npos);
  REQUIRE(write_req.final_body.find(R"("status":"successful")") !=
          std::string::npos);

  httpd_req_t list_req{};
  REQUIRE(ValuesApi::handleValues(&list_req) == ESP_OK);
  REQUIRE(list_req.content_type == "application/json;charset=utf-8");
  std::string values_body;
  for (const auto& chunk : list_req.chunks) values_body += chunk;
  REQUIRE(values_body.find(R"("key":"01")") != std::string::npos);
  REQUIRE(values_body.find(R"("name":"Outside_Temperature")") !=
          std::string::npos);
  REQUIRE(list_req.chunks.back().empty());

  command_manager.wipeCommands();
}

TEST_CASE("ValuesApi rejects unknown keys", "[values_api]") {
  command_manager.wipeCommands();
  ValuesApi api(command_manager);

  httpd_req_t read_req{};
  read_req.body = R"({"key":"missing"})";
  read_req.content_len = static_cast<int>(read_req.body.size());
  REQUIRE(ValuesApi::handleValuesRead(&read_req) == ESP_OK);
  REQUIRE(read_req.status == "404 Not Found");
  REQUIRE(read_req.final_body.find(R"("id":"read")") != std::string::npos);
  REQUIRE(read_req.final_body.find("Key 'missing' not found") !=
          std::string::npos);

  httpd_req_t write_req{};
  write_req.body = R"({"key":"missing","value":12.5})";
  write_req.content_len = static_cast<int>(write_req.body.size());
  REQUIRE(ValuesApi::handleValuesWrite(&write_req) == ESP_OK);
  REQUIRE(write_req.status == "404 Not Found");
  REQUIRE(write_req.final_body.find(R"("id":"write")") != std::string::npos);
  REQUIRE(write_req.final_body.find("Key 'missing' not found") !=
          std::string::npos);
}

TEST_CASE("ValuesApi rejects malformed and invalid writes", "[values_api]") {
  insertTestCommand();
  ValuesApi api(command_manager);

  httpd_req_t malformed_req{};
  malformed_req.body = R"({"key":"01","value":)";
  malformed_req.content_len = static_cast<int>(malformed_req.body.size());
  REQUIRE(ValuesApi::handleValuesWrite(&malformed_req) == ESP_OK);
  REQUIRE(malformed_req.status == "400 Bad Request");
  REQUIRE(malformed_req.final_body.find(R"("status":"failed")") !=
          std::string::npos);

  httpd_req_t invalid_value_req{};
  invalid_value_req.body = R"({"key":"01","value":"not-a-number"})";
  invalid_value_req.content_len =
      static_cast<int>(invalid_value_req.body.size());
  REQUIRE(ValuesApi::handleValuesWrite(&invalid_value_req) == ESP_OK);
  REQUIRE(invalid_value_req.status == "400 Bad Request");
  REQUIRE(invalid_value_req.final_body.find(R"("id":"write")") !=
          std::string::npos);
  REQUIRE(invalid_value_req.final_body.find("Invalid value for key '01'") !=
          std::string::npos);

  command_manager.wipeCommands();
}
