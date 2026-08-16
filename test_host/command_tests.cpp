#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include <ebus/data_types.hpp>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>

#include "Command.hpp"

using namespace ebus::detail;

TEST_CASE("Command fromJson roundtrip preserves fields", "[Command]") {
  std::string json =
      R"({"key":"01","name":"Outside_Temperature","read_cmd":"fe070009","write_cmd":"","active":false,"interval":0,"master":true,"position":1,"datatype":"DATA2B","divider":1,"min":0,"max":0,"digits":2,"unit":"°C","ha":true,"ha_profile":"sensor_temperature"})";

  JsonReader reader(json);
  Command cmd = Command::fromJson(reader);

  REQUIRE(cmd.getKey() == "01");
  REQUIRE(cmd.getName() == "Outside_Temperature");
  REQUIRE(cmd.getActive() == false);
  REQUIRE(cmd.getMaster() == true);
  REQUIRE(cmd.getPosition() == 1);
  REQUIRE(cmd.getDatatype() == ebus::DataType::data2b);
  REQUIRE(cmd.getDivider() == 1);
  REQUIRE(cmd.getDigits() == 2);
  REQUIRE(cmd.getUnit() == "°C");
  REQUIRE(cmd.getHA() == true);
  REQUIRE(std::string(cmd.getHAProfile()) == "sensor_temperature");
}

TEST_CASE("Command fromTabular roundtrip preserves fields", "[Command]") {
  std::string json =
      R"(["01","Outside_Temperature","fe070009","",false,0,true,1,"DATA2B",1,0,0,2,"°C",true,"sensor_temperature",[],0])";

  JsonReader reader(json);
  Command cmd = Command::fromTabular(reader);

  REQUIRE(cmd.getKey() == "01");
  REQUIRE(cmd.getName() == "Outside_Temperature");
  REQUIRE(cmd.getDatatype() == ebus::DataType::data2b);
  REQUIRE(std::string(cmd.getHAProfile()) == "sensor_temperature");
}

TEST_CASE("Command evaluate accepts valid command", "[Command]") {
  std::string json =
      R"({"key":"01","name":"Outside_Temperature","read_cmd":"fe070009","active":true,"master":true,"position":1,"datatype":"DATA2B"})";

  JsonReader reader(json);
  std::string error = Command::evaluate(reader);
  REQUIRE(error.empty());
}

TEST_CASE("Command evaluate rejects missing key", "[Command]") {
  std::string json =
      R"({"name":"Outside_Temperature","read_cmd":"fe070009","active":true,"master":true,"position":1,"datatype":"DATA2B"})";

  JsonReader reader(json);
  std::string error = Command::evaluate(reader);
  REQUIRE_FALSE(error.empty());
  REQUIRE(error.find("key") != std::string::npos);
}

TEST_CASE("Command evaluate rejects invalid datatype", "[Command]") {
  std::string json =
      R"({"key":"01","name":"Outside_Temperature","read_cmd":"fe070009","active":true,"master":true,"position":1,"datatype":"INVALID_TYPE"})";

  JsonReader reader(json);
  std::string error = Command::evaluate(reader);
  REQUIRE_FALSE(error.empty());
  REQUIRE(error.find("datatype") != std::string::npos);
}

TEST_CASE("Command toJson serializes all fields", "[Command]") {
  std::string json =
      R"({"key":"01","name":"Test","read_cmd":"fe070009","write_cmd":"","active":true,"interval":60,"master":true,"position":1,"datatype":"UINT16","divider":1,"min":0,"max":100,"digits":0,"unit":"","ha":true,"ha_profile":"sensor_temperature"})";
  JsonReader reader(json);
  Command cmd = Command::fromJson(reader);

  std::string out;
  ebus::detail::JsonWriter writer([&out](std::string_view s) { out += s; });
  auto scope = writer.objectScope();
  cmd.toJson(writer);

  REQUIRE(out.find("\"key\":\"01\"") != std::string::npos);
  REQUIRE(out.find("\"name\":\"Test\"") != std::string::npos);
  REQUIRE(out.find("\"datatype\":\"UINT16\"") != std::string::npos);
  REQUIRE(out.find("\"ha_profile\":\"sensor_temperature\"") !=
          std::string::npos);
  REQUIRE(out.find("\"ha_key_value_map\"") == std::string::npos);
  REQUIRE(out.find("\"ha_default_key\"") == std::string::npos);
}

TEST_CASE("Command getStringFromVector decodes string type", "[Command]") {
  std::string json =
      R"({"key":"01","name":"Test","read_cmd":"fe070009","active":true,"master":true,"position":1,"datatype":"CHAR1","length":1})";
  JsonReader reader(json);
  Command cmd = Command::fromJson(reader);

  ebus::Sequence data;
  data.push_back(0x41);
  cmd.setData(ebus::ByteView(data));

  REQUIRE(cmd.getStringFromVector() == "A");
}

TEST_CASE("Command matches checks read_cmd at offset 1", "[Command]") {
  std::string json =
      R"({"key":"01","name":"Test","read_cmd":"fe070009","active":true,"master":true,"position":1,"datatype":"UINT8"})";
  JsonReader reader(json);
  Command cmd = Command::fromJson(reader);

  uint8_t bytes[] = {0x10, 0xfe, 0x07, 0x00, 0x09};
  ebus::ByteView master(bytes, 5);

  REQUIRE(cmd.matches(master) == true);
}
