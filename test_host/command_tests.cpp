#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include <ebus/data_types.hpp>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>

#include "command.hpp"
#include "command_manager.hpp"

using namespace ebus::detail;

TEST_CASE("Command fromJson roundtrip preserves fields", "[Command]") {
  std::string json =
      R"({"key":"01","name":"Outside_Temperature","read_cmd":"fe070009",)"
      R"("write_cmd":"","interval":0,"master":true,)"
      R"("fields":[{"name":"value","profile":"data2b_celsius",)"
      R"("position":1,"ha_profile":"sensor_temperature"}]})";

  JsonReader reader(json);
  Command cmd = Command::fromJson(reader);

  REQUIRE(cmd.getKey() == "01");
  REQUIRE(cmd.getName() == "Outside_Temperature");
  REQUIRE(cmd.getActive() == false);
  REQUIRE(cmd.getFieldCount() == 1);
  REQUIRE(cmd.getFieldPosition(0) == 1);
  REQUIRE(cmd.getMaster() == true);
  REQUIRE(cmd.getFieldDatatype(0) == ebus::DataType::data2b);
  REQUIRE(cmd.getFieldDivider(0) == 1);
  REQUIRE(cmd.getFieldDigits(0) == 1);
  REQUIRE(std::string(cmd.getFieldUnit(0)) == "\u00b0C");
  REQUIRE(cmd.hasFieldHA(0) == true);
  REQUIRE(std::string(cmd.getFieldHAProfileName(0)) == "sensor_temperature");
}

TEST_CASE("Command fromTabular roundtrip preserves fields", "[Command]") {
  std::string json = R"(["01","Outside_Temperature","fe070009","",0,true,)"
                     R"([{"name":"value","profile":"data2b_celsius",)"
                     R"("position":1,"ha_profile":"sensor_temperature"}])";

  JsonReader reader(json);
  Command cmd = Command::fromTabular(reader);

  REQUIRE(cmd.getKey() == "01");
  REQUIRE(cmd.getName() == "Outside_Temperature");
  REQUIRE(cmd.getFieldCount() == 1);
  REQUIRE(cmd.getFieldDatatype(0) == ebus::DataType::data2b);
  REQUIRE(std::string(cmd.getFieldHAProfileName(0)) == "sensor_temperature");
}

TEST_CASE("Command evaluate accepts valid command", "[Command]") {
  std::string json =
      R"({"key":"01","name":"Outside_Temperature","read_cmd":"fe070009",)"
      R"("interval":0,"master":true,)"
      R"("fields":[{"name":"value","profile":"data2b_celsius",)"
      R"("position":1,"ha_profile":"sensor_temperature"}]})";

  JsonReader reader(json);
  std::string_view error = Command::evaluate(reader);
  REQUIRE(error.empty());
}

TEST_CASE("Command evaluate rejects missing key", "[Command]") {
  std::string json = R"({"name":"Outside_Temperature","read_cmd":"fe070009",)"
                     R"("interval":0,"master":true,)"
                     R"("fields":[{"name":"value","profile":"data2b_celsius",)"
                     R"("position":1,"ha_profile":"sensor_temperature"}]})";

  JsonReader reader(json);
  std::string_view error = Command::evaluate(reader);
  REQUIRE_FALSE(error.empty());
  REQUIRE(error.find("key") != std::string_view::npos);
}

TEST_CASE("Command evaluate rejects unknown profile", "[Command]") {
  std::string json =
      R"({"key":"01","name":"Outside_Temperature","read_cmd":"fe070009",)"
      R"("interval":0,)"
      R"("fields":[{"name":"value","profile":"invalid_profile",)"
      R"("position":1,"ha_profile":"sensor_temperature"}]})";

  JsonReader reader(json);
  std::string_view error = Command::evaluate(reader);
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("Command evaluate rejects missing fields", "[Command]") {
  std::string json =
      R"({"key":"01","name":"Outside_Temperature","read_cmd":"fe070009",)"
      R"("interval":0})";

  JsonReader reader(json);
  std::string_view error = Command::evaluate(reader);
  REQUIRE_FALSE(error.empty());
  REQUIRE(error.find("fields") != std::string_view::npos);
}

TEST_CASE("Command toJson serializes fields", "[Command]") {
  std::string json =
      R"({"key":"01","name":"Test","read_cmd":"fe070009","write_cmd":"",)"
      R"("interval":60,"master":true,)"
      R"("fields":[{"name":"value","profile":"data2b_celsius",)"
      R"("position":1,"ha_profile":"sensor_temperature"}]})";
  JsonReader reader(json);
  Command cmd = Command::fromJson(reader);

  std::string out;
  ebus::detail::JsonWriter writer([&out](std::string_view s) { out += s; });
  {
    auto scope = writer.objectScope();
    writer.writeField("key", cmd.getKey());
    writer.writeField("name", cmd.getName());
    writer.writeHexField("read_cmd", cmd.getReadCmd());
    CommandManager commandManager;
    writer.writeHexField("write_cmd", cmd.getWriteCmd(commandManager));
    writer.writeField("interval", cmd.getInterval());

    {
      auto arr = writer.arrayScope("fields");
      for (size_t i = 0; i < cmd.getFieldCount(); i++) {
        auto field_obj = writer.objectScope();
        writer.writeField("name", cmd.getFieldName(i));
        writer.writeField("profile", cmd.getFieldProfile(i)
                                         ? cmd.getFieldProfile(i)->name
                                         : "");
        writer.writeField("position",
                          static_cast<uint32_t>(cmd.getFieldPosition(i)));
        writer.writeField("master", cmd.getMaster());
        writer.writeField("ha_profile", cmd.getFieldHAProfileName(i));
      }
    }
  }

  REQUIRE(out.find("\"key\":\"01\"") != std::string::npos);
  REQUIRE(out.find("\"name\":\"Test\"") != std::string::npos);
  REQUIRE(out.find("\"fields\"") != std::string::npos);
  REQUIRE(out.find("\"profile\":\"data2b_celsius\"") != std::string::npos);
  REQUIRE(out.find("\"position\":1") != std::string::npos);
  REQUIRE(out.find("\"master\":true") != std::string::npos);
  REQUIRE(out.find("\"ha_profile\":\"sensor_temperature\"") !=
          std::string::npos);
}

TEST_CASE("Command matches checks read_cmd at offset 1", "[Command]") {
  std::string json = R"({"key":"01","name":"Test","read_cmd":"fe070009",)"
                     R"("interval":0,"master":true,)"
                     R"("fields":[{"name":"value","profile":"uint8",)"
                     R"("position":1,"ha_profile":"sensor_temperature"}]})";
  JsonReader reader(json);
  Command cmd = Command::fromJson(reader);

  uint8_t bytes[] = {0x10, 0xfe, 0x07, 0x00, 0x09};
  ebus::ByteView master(bytes, 5);

  REQUIRE(cmd.matches(master) == true);
}

TEST_CASE("Command writeValuePayload formats single and multi fields flat",
          "[Command]") {
  SECTION("Single field") {
    std::string json = R"({"key":"09","name":"Buffer/Middle_Temperature",)"
                       R"("read_cmd":"08b50903290100","master":false,)"
                       R"("fields":[{"name":"middle_temperature",)"
                       R"("profile":"data2c_celsius","position":1,)"
                       R"("ha_profile":"sensor_temperature"}]})";
    JsonReader reader(json);
    Command cmd = Command::fromJson(reader);

    ebus::Sequence data;
    data.push_back(0x50);
    data.push_back(0x01);
    cmd.setData(ebus::ByteView(data));

    std::string out;
    ebus::detail::JsonWriter writer([&out](std::string_view s) { out += s; });
    {
      auto scope = writer.objectScope();
      cmd.writeValuePayload(writer);
    }

    REQUIRE(out.find("\"middle_temperature\":") != std::string::npos);
  }

  SECTION("Multi field") {
    std::string json = R"({"key":"09","name":"Buffer/Middle_Temperature",)"
                       R"("read_cmd":"08b50903290100","master":false,)"
                       R"("fields":[{"name":"middle_temperature",)"
                       R"("profile":"data2c_celsius","position":1,)"
                       R"("ha_profile":"sensor_temperature"},)"
                       R"({"name":"state","profile":"uint8","position":3,)"
                       R"("ha_profile":"sensor_enum_state"}]})";
    JsonReader reader(json);
    Command cmd = Command::fromJson(reader);

    ebus::Sequence data;
    data.push_back(0x50);
    data.push_back(0x01);
    data.push_back(0x55);  // 85
    cmd.setData(ebus::ByteView(data));

    std::string out;
    ebus::detail::JsonWriter writer([&out](std::string_view s) { out += s; });
    {
      auto scope = writer.objectScope();
      cmd.writeValuePayload(writer);
    }

    REQUIRE(out.find("\"middle_temperature\":") != std::string::npos);
    REQUIRE(out.find("\"state\":85") != std::string::npos);
  }
}
