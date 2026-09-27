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
    "write_cmd":"",
    "interval":0,
    "master":true,
    "fields":[
      {"name":"value","profile":"data2b_celsius","position":1,"ha_profile":"sensor_temperature"}
    ]
  })";
}

}  // namespace

TEST_CASE("ValuesApi reads and writes by key", "[values_api]") {
  commandManager.wipeCommands();
  ebus::detail::JsonReader reader(makeCommandJson());
  commandManager.insertCommand(Command::fromJson(reader));

  ValuesApi api(commandManager);

  httpd_req_t read_req{};
  read_req.body = R"({"key":"01"})";
  read_req.content_len = static_cast<int>(read_req.body.size());
  REQUIRE(ValuesApi::handleValuesRead(&read_req) == ESP_OK);

  httpd_req_t write_req{};
  write_req.body = R"({"key":"01","value":12.5})";
  write_req.content_len = static_cast<int>(write_req.body.size());
  REQUIRE(ValuesApi::handleValuesWrite(&write_req) == ESP_OK);

  httpd_req_t list_req{};
  REQUIRE(ValuesApi::handleValues(&list_req) == ESP_OK);
}

TEST_CASE("ValuesApi rejects unknown keys", "[values_api]") {
  commandManager.wipeCommands();
  ValuesApi api(commandManager);

  httpd_req_t req{};
  req.body = R"({"key":"missing"})";
  req.content_len = static_cast<int>(req.body.size());
  REQUIRE(ValuesApi::handleValuesRead(&req) == ESP_OK);
  REQUIRE(ValuesApi::handleValuesWrite(&req) == ESP_OK);
}
