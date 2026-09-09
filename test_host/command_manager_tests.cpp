#include <catch2/catch_all.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ebus/data_types.hpp>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>
#include <ebus/utils.hpp>
#include <iostream>

#include "command.hpp"
#include "command_manager.hpp"

using namespace ebus::detail;
using namespace ebus;

Command makeCommand(const std::string& key, const std::string& name,
                    bool active, bool master, int position,
                    const std::string& profile, const std::string& read_cmd) {
  std::string json = R"({"key":")" + key + R"(","name":")" + name +
                     R"(","read_cmd":")" + read_cmd +
                     R"(","write_cmd":"","interval":)" + (active ? "60" : "0") +
                     R"(,"master":)" + (master ? "true" : "false") +
                     R"(,"fields":[{"name":"value","profile":")" + profile +
                     R"(","position":)" + std::to_string(position) +
                     R"(,"ha_profile":"sensor_temperature"}]})";
  JsonReader reader(json);
  return Command::fromJson(reader);
}

Command makeCommandMultiField(const std::string& key, const std::string& name,
                              bool active, const std::string& read_cmd,
                              bool master, const std::string& field1_profile,
                              int field1_pos, const std::string& field1_ha,
                              const std::string& field2_profile, int field2_pos,
                              const std::string& field2_ha) {
  std::string json =
      R"({"key":")" + key + R"(","name":")" + name + R"(","read_cmd":")" +
      read_cmd + R"(","write_cmd":"","interval":)" + (active ? "60" : "0") +
      R"(,"master":)" + (master ? "true" : "false") +
      R"(,"fields":[{"name":"field1","profile":")" + field1_profile +
      R"(","position":)" + std::to_string(field1_pos) + R"(,"ha_profile":")" +
      field1_ha + R"("},{"name":"field2","profile":")" + field2_profile +
      R"(","position":)" + std::to_string(field2_pos) + R"(,"ha_profile":")" +
      field2_ha + R"("}]})";
  JsonReader reader(json);
  return Command::fromJson(reader);
}

TEST_CASE("CommandManager insert and find by key", "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd = makeCommand("01", "Test", true, true, 1, "uint8", "fe070009");
  commandManager.insertCommand(cmd);

  Command* found = commandManager.findCommand("01");
  REQUIRE(found != nullptr);
  REQUIRE(found->getKey() == "01");
  REQUIRE(found->getName() == "Test");

  Command* not_found = commandManager.findCommand("99");
  REQUIRE(not_found == nullptr);
}

TEST_CASE("CommandManager insert updates existing command",
          "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd1 = makeCommand("01", "First", true, true, 1, "uint8", "fe070009");
  commandManager.insertCommand(cmd1);

  Command cmd2 =
      makeCommand("01", "Updated", false, true, 1, "uint8", "fe070009");
  commandManager.insertCommand(cmd2);

  Command* found = commandManager.findCommand("01");
  REQUIRE(found != nullptr);
  REQUIRE(found->getName() == "Updated");
  REQUIRE(found->getActive() == false);
}

TEST_CASE("CommandManager remove command", "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd = makeCommand("01", "Test", true, true, 1, "uint8", "fe070009");
  commandManager.insertCommand(cmd);

  commandManager.removeCommand("01");
  REQUIRE(commandManager.findCommand("01") == nullptr);
}

TEST_CASE("CommandManager getCommands returns all commands",
          "[CommandManager]") {
  commandManager.wipeCommands();
  for (int i = 0; i < 5; i++) {
    Command cmd = makeCommand(std::to_string(i), "Test " + std::to_string(i),
                              true, true, 1, "uint8", "fe070009");
    commandManager.insertCommand(cmd);
  }

  size_t count = 0;
  commandManager.forEachCommand([&](const Command*) { ++count; });
  REQUIRE(count == 5);
}

TEST_CASE("CommandManager getActiveCommands counts active only",
          "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd1 =
      makeCommand("01", "Active", true, true, 1, "uint8", "fe070009");
  commandManager.insertCommand(cmd1);

  Command cmd2 =
      makeCommand("02", "Inactive", false, true, 1, "uint8", "fe070009");
  commandManager.insertCommand(cmd2);

  REQUIRE(commandManager.getActiveCommands() == 1);
  REQUIRE(commandManager.getPassiveCommands() == 1);
}

TEST_CASE("CommandManager updateData decodes multi-field slave correctly",
          "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd = makeCommandMultiField("09", "Buffer/Middle_Temperature", false,
                                      "08b50903290100", false, "data2c_celsius",
                                      1, "", "uint8", 3, "");
  cmd.setPollId(1);
  commandManager.insertCommand(cmd);
  Command* cmd_ptr = commandManager.findCommand("09");

  uint8_t slave_bytes[] = {0x05, 0x00, 0x00, 0x00, 0x01, 0x00};
  ebus::ByteView slave(slave_bytes, 6);

  ebus::ProtocolInfo info{};
  info.session_id = 0;
  info.poll_id = 1;
  info.slave_view = slave;
  commandManager.updateData(info);

  Command* found = commandManager.findCommand("09");
  REQUIRE(found != nullptr);
  REQUIRE(found->getData().size() == 5);

  size_t field0_pos = found->getFieldPosition(0) - 1;
  size_t field0_len = ebus::sizeOfDataType(found->getFieldDatatype(0));
  auto field0_data = ebus::range(found->getData(), field0_pos, field0_len);
  auto decoded0 = ebus::decode(found->getFieldDatatype(0), field0_data);
  REQUIRE(decoded0.has_value());

  size_t field1_pos = found->getFieldPosition(1) - 1;
  size_t field1_len = ebus::sizeOfDataType(found->getFieldDatatype(1));
  auto field1_data = ebus::range(found->getData(), field1_pos, field1_len);
  auto decoded1 = ebus::decode(found->getFieldDatatype(1), field1_data);
  REQUIRE(decoded1.has_value());
}

TEST_CASE("Command getValueJson outputs null for empty data", "[Command]") {
  Command cmd = makeCommand("01", "Test", true, true, 1, "uint8", "fe070009");

  std::string out;
  ebus::detail::JsonWriter writer([&out](std::string_view s) { out += s; });
  cmd.getValueJson(writer);
  writer.flush();

  REQUIRE(out == "null");
}

TEST_CASE("Command getValueJson outputs value for set data", "[Command]") {
  Command cmd = makeCommand("01", "Test", true, true, 1, "uint8", "fe070009");

  ebus::Sequence data;
  data.push_back(0x42);
  cmd.setData(ebus::ByteView(data));

  std::string out;
  ebus::detail::JsonWriter writer([&out](std::string_view s) { out += s; });
  cmd.getValueJson(writer);
  writer.flush();

  REQUIRE(out == "66");
}

TEST_CASE("Command profile names resolve correctly after rename", "[Command]") {
  Command cmd =
      makeCommand("01", "Test", true, true, 1, "data2b_celsius", "fe070009");
  REQUIRE(cmd.getFieldProfile(0) != nullptr);
  REQUIRE(std::string(cmd.getFieldProfile(0)->name) == "data2b_celsius");
  REQUIRE(cmd.getFieldDatatype(0) == ebus::DataType::data2b);
}

TEST_CASE("CommandManager findPassiveCommands matches passive read_cmd",
          "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd1 =
      makeCommand("01", "Match", false, true, 1, "uint8", "fe070009");
  commandManager.insertCommand(cmd1);

  Command cmd2 =
      makeCommand("02", "NoMatch", false, true, 1, "uint8", "080b09010a00");
  commandManager.insertCommand(cmd2);

  uint8_t master_bytes[] = {0x10, 0xfe, 0x07, 0x00, 0x09};
  ebus::ByteView master(master_bytes, 5);

  auto matches = commandManager.findPassiveCommands(master);
  REQUIRE(matches.size() == 1);
  REQUIRE(matches[0]->getKey() == "01");
}

TEST_CASE("CommandManager updateData sets data", "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd = makeCommand("01", "Test", false, true, 1, "uint8", "fe070009");
  commandManager.insertCommand(cmd);

  uint8_t master_bytes[] = {0x10, 0xfe, 0x07, 0x00, 0x09, 0x00};
  ebus::ByteView master(master_bytes, 6);

  ebus::ProtocolInfo info{};
  info.session_id = 0;
  info.poll_id = 0;
  info.master_view = master;
  commandManager.updateData(info);

  Command* found = commandManager.findCommand("01");
  REQUIRE(found->getData().size() > 0);
}

TEST_CASE("CommandManager updateData stores full payload for position>1",
          "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd = makeCommand("08", "Buffer/Top_Temperature", false, false, 3,
                            "data2c_celsius", "25b50903290000");
  cmd.setPollId(1);
  commandManager.insertCommand(cmd);
  Command* cmd_ptr = commandManager.findCommand("08");

  uint8_t slave_bytes[] = {0x05, 0x00, 0x00, 0xef, 0x03, 0x00};
  ebus::ByteView slave(slave_bytes, 6);

  ebus::ProtocolInfo info{};
  info.session_id = 0;
  info.poll_id = 1;
  info.slave_view = slave;
  commandManager.updateData(info);

  Command* found = commandManager.findCommand("08");
  REQUIRE(found != nullptr);
  REQUIRE(found->getData().size() == 5);

  size_t field_pos = found->getFieldPosition(0) - 1;
  size_t field_len = ebus::sizeOfDataType(found->getFieldDatatype(0));
  auto field_data = ebus::range(found->getData(), field_pos, field_len);
  auto decoded = ebus::decode(found->getFieldDatatype(0), field_data);
  REQUIRE(decoded.has_value());
  REQUIRE(ebus::asFloat(*decoded) == Catch::Approx(62.9375f));
}

TEST_CASE("CommandManager updateData stores full payload for master position>1",
          "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd = makeCommand("08", "Buffer/Top_Temperature", true, true, 3,
                            "data2c_celsius", "25b50903290000");
  cmd.setPollId(1);
  commandManager.insertCommand(cmd);
  Command* cmd_ptr = commandManager.findCommand("08");

  uint8_t master_bytes[] = {0x10, 0x25, 0xb5, 0x09, 0x04,
                            0x00, 0x00, 0xef, 0x03, 0x00};
  ebus::ByteView master(master_bytes, 10);

  ebus::ProtocolInfo info{};
  info.session_id = 0;
  info.poll_id = 1;
  info.master_view = master;
  commandManager.updateData(info);

  Command* found = commandManager.findCommand("08");
  REQUIRE(found != nullptr);
  REQUIRE(found->getData().size() == 4);

  size_t field_pos = found->getFieldPosition(0) - 1;
  size_t field_len = ebus::sizeOfDataType(found->getFieldDatatype(0));
  auto field_data = ebus::range(found->getData(), field_pos, field_len);
  auto decoded = ebus::decode(found->getFieldDatatype(0), field_data);
  REQUIRE(decoded.has_value());
  REQUIRE(ebus::asFloat(*decoded) == Catch::Approx(62.9375f));
}

TEST_CASE("CommandManager loadCommandsFrom streams JSON from file",
          "[CommandManager]") {
  commandManager.wipeCommands();

  const char* json =
      R"([{"key":"01","name":"Test1","read_cmd":"fe070009","write_cmd":"","interval":60,)"
      R"("fields":[{"name":"value","profile":"uint8","interval":0,"position":1,"ha_profile":"sensor_temperature"}]},)"
      R"({"key":"02","name":"Test2","read_cmd":"fe070009","write_cmd":"",)"
      R"("fields":[{"name":"value","profile":"uint8","position":1,"ha_profile":"sensor_temperature"}]}])";

  const char* tmp_path = "/tmp/test_commands_stream.json";
  FILE* f = std::fopen(tmp_path, "wb");
  REQUIRE(f != nullptr);
  std::fwrite(json, 1, std::strlen(json), f);
  std::fclose(f);

  int64_t bytes = commandManager.loadCommandsFrom(tmp_path);
  REQUIRE(bytes >= 0);
  REQUIRE(commandManager.getCommandCount() == 2);
  REQUIRE(commandManager.findCommand("01") != nullptr);
  REQUIRE(commandManager.findCommand("02") != nullptr);

  std::remove(tmp_path);
}

TEST_CASE("CommandManager loadCommandsFrom streams large JSON (47 commands)",
          "[CommandManager]") {
  commandManager.wipeCommands();

  FILE* f = std::fopen("/tmp/test_large_stream.json", "wb");
  REQUIRE(f != nullptr);

  std::fputs("[\n", f);
  for (int i = 1; i <= 47; i++) {
    char buf[1024];
    const char* active = (i % 5 == 0) ? "0" : "60";
    int len = snprintf(
        buf, sizeof(buf),
        "  {"
        "\"key\":\"%02d\",\"name\":\"Cmd_%02d\",\"read_cmd\":\"%02xb50903"
        "%02x%02x00\",\"write_cmd\":\"\",\"interval\":%s,"
        "\"fields\":[{\"name\":\"value\",\"profile\":\"uint8\","
        "\"position\":1,\"master\":false}],"
        "\"ha_profile\":\"\"}%s\n",
        i, i, (i * 10) % 256, (i * 10 + 1) % 256, (i * 10 + 2) % 256, active,
        (i < 47) ? "," : "");
    std::fwrite(buf, 1, len, f);
  }
  std::fputs("]\n", f);
  std::fclose(f);

  int64_t bytes =
      commandManager.loadCommandsFrom("/tmp/test_large_stream.json");
  REQUIRE(bytes >= 0);
  REQUIRE(commandManager.getCommandCount() == 47);

  for (int i = 1; i <= 47; i++) {
    char key[4];
    snprintf(key, sizeof(key), "%02d", i);
    REQUIRE(commandManager.findCommand(key) != nullptr);
  }

  std::remove("/tmp/test_large_stream.json");
}

TEST_CASE("CommandManager loadCommandsFrom loads multi-field commands",
          "[CommandManager]") {
  commandManager.wipeCommands();

  const char* json =
      R"([{"key":"01","name":"Multi","read_cmd":"fe070009","write_cmd":"","interval":60,"master":true)"
      R"(,"fields":[{"name":"temp","profile":"data2c_celsius",)"
      R"("position":1,"ha_profile":""},)"
      R"({"name":"sensor","profile":"uint8",)"
      R"("position":3,"ha_profile":""}]}])";

  const char* tmp_path = "/tmp/test_multifield_stream.json";
  FILE* f = std::fopen(tmp_path, "wb");
  REQUIRE(f != nullptr);
  std::fwrite(json, 1, std::strlen(json), f);
  std::fclose(f);

  int64_t bytes = commandManager.loadCommandsFrom(tmp_path);
  REQUIRE(bytes >= 0);
  REQUIRE(commandManager.getCommandCount() == 1);
  REQUIRE(commandManager.findCommand("01") != nullptr);

  Command* found = commandManager.findCommand("01");
  REQUIRE(found->getFieldCount() == 2);
  REQUIRE(found->getFieldName(0) == std::string_view("temp"));
  REQUIRE(found->getFieldName(1) == std::string_view("sensor"));
  REQUIRE(found->getFieldDatatype(0) == ebus::DataType::data2c);
  REQUIRE(found->getFieldDatatype(1) == ebus::DataType::uint8);

  std::remove(tmp_path);
}

TEST_CASE("CommandManager loadCommandsFrom streams tabular format with fields",
          "[CommandManager]") {
  commandManager.wipeCommands();

  const char* json =
      R"([["key","name","read_cmd","write_cmd","interval","master","fields"],)"
      R"(["01","Test1","fe070009","","60",true,)"
      R"([{"name":"value","profile":"data2b_celsius",)"
      R"("position":1,"ha_profile":"sensor_temperature"}]],)"
      R"(["02","Test2","fe070009","","0",false,)"
      R"([{"name":"value","profile":"uint8",)"
      R"("position":1,"ha_profile":""}]])";

  const char* tmp_path = "/tmp/test_tabular_stream.json";
  FILE* f = std::fopen(tmp_path, "wb");
  REQUIRE(f != nullptr);
  std::fwrite(json, 1, std::strlen(json), f);
  std::fclose(f);

  int64_t bytes = commandManager.loadCommandsFrom(tmp_path);
  REQUIRE(bytes >= 0);
  REQUIRE(commandManager.getCommandCount() == 2);
  REQUIRE(commandManager.findCommand("01") != nullptr);
  REQUIRE(commandManager.findCommand("02") != nullptr);

  std::remove(tmp_path);
}

TEST_CASE("CommandManager loadCommandsFrom preserves min/max overrides",
          "[CommandManager]") {
  commandManager.wipeCommands();

  const char* json =
      R"([{"key":"32","name":"Basement/SetPoint","read_cmd":"50b509030d3300","write_cmd":"50b509040e3300","interval":60,"master":false)"
      R"(,"fields":[{"name":"value","profile":"data1c_celsius","position":1,"ha_profile":"number_temperature","min":15,"max":20}]}])";

  const char* tmp_path = "/tmp/test_minmax_stream.json";
  FILE* f = std::fopen(tmp_path, "wb");
  REQUIRE(f != nullptr);
  std::fwrite(json, 1, std::strlen(json), f);
  std::fclose(f);

  int64_t bytes = commandManager.loadCommandsFrom(tmp_path);
  REQUIRE(bytes >= 0);
  REQUIRE(commandManager.getCommandCount() == 1);

  Command* found = commandManager.findCommand("32");
  REQUIRE(found != nullptr);
  REQUIRE(found->getFieldCount() == 1);
  REQUIRE(commandManager.getFieldMinOverride(found->getKeyId(), 0) ==
          Catch::Approx(15.0f));
  REQUIRE(commandManager.getFieldMaxOverride(found->getKeyId(), 0) ==
          Catch::Approx(20.0f));
  REQUIRE(found->getFieldMin(0) == Catch::Approx(15.0f));
  REQUIRE(found->getFieldMax(0) == Catch::Approx(20.0f));

  std::remove(tmp_path);
}

TEST_CASE("CommandManager removeFieldOverrides drains pool for a key",
          "[CommandManager]") {
  commandManager.wipeCommands();

  Command cmd = makeCommand("01", "Test", true, true, 1, "uint8", "fe070009");
  commandManager.insertCommand(cmd);
  Command* found = commandManager.findCommand("01");
  REQUIRE(found != nullptr);

  commandManager.setFieldMinOverride(found->getKeyId(), 0, 42.0f);
  commandManager.setFieldMaxOverride(found->getKeyId(), 0, 84.0f);
  REQUIRE(commandManager.getFieldMinOverride(found->getKeyId(), 0) ==
          Catch::Approx(42.0f));
  REQUIRE(commandManager.getFieldMaxOverride(found->getKeyId(), 0) ==
          Catch::Approx(84.0f));

  commandManager.removeFieldOverrides(found->getKeyId());
  REQUIRE(std::isnan(commandManager.getFieldMinOverride(found->getKeyId(), 0)));
  REQUIRE(std::isnan(commandManager.getFieldMaxOverride(found->getKeyId(), 0)));

  commandManager.wipeCommands();
}
