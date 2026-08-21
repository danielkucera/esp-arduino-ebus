#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>

#include "command.hpp"
#include "command_manager.hpp"

using namespace ebus::detail;

Command makeCommand(const std::string& key, const std::string& name,
                    bool active, bool master, int position,
                    const std::string& profile, const std::string& read_cmd) {
  std::string json =
      R"({"key":")" + key + R"(","name":")" + name + R"(","read_cmd":")" +
      read_cmd + R"(","write_cmd":"","interval":)" + (active ? "60" : "0") +
      R"(,"fields":[{"name":"value","profile":")" + profile +
      R"(","position":)" + std::to_string(position) + R"(,"master":)" +
      (master ? "true" : "false") +
      R"(,"ha":true,"ha_profile":"sensor_temperature"}],"ha":true,"ha_profile":"sensor_temperature"})";
  JsonReader reader(json);
  return Command::fromJson(reader);
}

Command makeCommandMultiField(const std::string& key, const std::string& name,
                              bool active, const std::string& read_cmd,
                              const std::string& field1_profile, int field1_pos,
                              bool field1_master,
                              const std::string& field2_profile, int field2_pos,
                              bool field2_master) {
  std::string json =
      R"({"key":")" + key + R"(","name":")" + name + R"(","read_cmd":")" +
      read_cmd + R"(","write_cmd":"","active":)" + (active ? "true" : "false") +
      R"(,"interval":0,"fields":[{"name":"field1","profile":")" +
      field1_profile + R"(","position":)" + std::to_string(field1_pos) +
      R"(,"master":)" + (field1_master ? "true" : "false") +
      R"(},{"name":"field2","profile":")" + field2_profile +
      R"(","position":)" + std::to_string(field2_pos) + R"(,"master":)" +
      (field2_master ? "true" : "false") + R"(}],"ha":false,"ha_profile":""})";
  JsonReader reader(json);
  return Command::fromJson(reader);
}

TEST_CASE("CommandManager insert and find by key", "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd = makeCommand("01", "Test", true, true, 1, "u8", "fe070009");
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
  Command cmd1 = makeCommand("01", "First", true, true, 1, "u8", "fe070009");
  commandManager.insertCommand(cmd1);

  Command cmd2 = makeCommand("01", "Updated", false, true, 1, "u8", "fe070009");
  commandManager.insertCommand(cmd2);

  Command* found = commandManager.findCommand("01");
  REQUIRE(found != nullptr);
  REQUIRE(found->getName() == "Updated");
  REQUIRE(found->getActive() == false);
}

TEST_CASE("CommandManager remove command", "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd = makeCommand("01", "Test", true, true, 1, "u8", "fe070009");
  commandManager.insertCommand(cmd);

  commandManager.removeCommand("01");
  REQUIRE(commandManager.findCommand("01") == nullptr);
}

TEST_CASE("CommandManager getCommands returns all commands",
          "[CommandManager]") {
  commandManager.wipeCommands();
  for (int i = 0; i < 5; i++) {
    Command cmd = makeCommand(std::to_string(i), "Test " + std::to_string(i),
                              true, true, 1, "u8", "fe070009");
    commandManager.insertCommand(cmd);
  }

  auto cmds = commandManager.getCommands();
  REQUIRE(cmds.size() == 5);
}

TEST_CASE("CommandManager getActiveCommands counts active only",
          "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd1 = makeCommand("01", "Active", true, true, 1, "u8", "fe070009");
  commandManager.insertCommand(cmd1);

  Command cmd2 =
      makeCommand("02", "Inactive", false, true, 1, "u8", "fe070009");
  commandManager.insertCommand(cmd2);

  REQUIRE(commandManager.getActiveCommands() == 1);
  REQUIRE(commandManager.getPassiveCommands() == 1);
}

TEST_CASE("CommandManager findAllMatchingCommands matches read_cmd",
          "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd1 = makeCommand("01", "Match", true, true, 1, "u8", "fe070009");
  commandManager.insertCommand(cmd1);

  Command cmd2 =
      makeCommand("02", "NoMatch", true, true, 1, "u8", "080b09010a00");
  commandManager.insertCommand(cmd2);

  uint8_t master_bytes[] = {0x10, 0xfe, 0x07, 0x00, 0x09};
  ebus::ByteView master(master_bytes, 5);

  auto matches = commandManager.findAllMatchingCommands(master);
  REQUIRE(matches.size() == 1);
  REQUIRE(matches[0]->getKey() == "01");
}

TEST_CASE("CommandManager updateData sets data", "[CommandManager]") {
  commandManager.wipeCommands();
  Command cmd = makeCommand("01", "Test", true, true, 1, "u8", "fe070009");
  commandManager.insertCommand(cmd);

  uint8_t master_bytes[] = {0x10, 0xfe, 0x07, 0x00, 0x09, 0x00};
  ebus::ByteView master(master_bytes, 6);

  commandManager.updateData(nullptr, master, {});

  Command* found = commandManager.findCommand("01");
  REQUIRE(found->getData().size() > 0);
}

TEST_CASE("CommandManager loadCommandsFrom streams JSON from file",
          "[CommandManager]") {
  commandManager.wipeCommands();

  const char* json = R"([
    {"key":"01","name":"Test1","read_cmd":"fe070009","write_cmd":"",
     "active":true,"interval":60,"fields":[{"name":"value","profile":"u8","position":1,"master":true,"ha":true,"ha_profile":"sensor_temperature"}],
     "ha":true,"ha_profile":"sensor_temperature"},
    {"key":"02","name":"Test2","read_cmd":"fe070009","write_cmd":"",
     "active":false,"interval":0,"fields":[{"name":"value","profile":"u8","position":1,"master":false,"ha":true,"ha_profile":"sensor_temperature"}],
     "ha":true,"ha_profile":"sensor_temperature"}
  ])";

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
    const char* active = (i % 5 == 0) ? "false" : "true";
    int len = snprintf(
        buf, sizeof(buf),
        "  {"
        "\"key\":\"%02d\",\"name\":\"Cmd_%02d\",\"read_cmd\":\"%02xb50903"
        "%02x%02x00\",\"write_cmd\":\"\",\"active\":%s,\"interval\":60,"
        "\"fields\":[{\"name\":\"value\",\"profile\":\"u8\","
        "\"position\":1,\"master\":false}],"
        "\"ha\":false,\"ha_profile\":\"\"}%s\n",
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

  const char* json = R"([
    {
      "key":"01","name":"Multi","read_cmd":"fe070009","write_cmd":"",
      "active":true,"interval":60,
      "fields":[
        {"name":"temp","profile":"d2c_c","position":1,"master":false},
        {"name":"sensor","profile":"u8_enum","position":3,"master":false}
      ],
      "ha":false,"ha_profile":""
    }
  ])";

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
      R"([["key","name","read_cmd","write_cmd","active","interval","fields","ha","ha_profile"],
        ["01","Test1","fe070009","","1","60",[{"name":"value","profile":"d2b_c","position":1,"master":true}],"1","sensor_temperature"],
        ["02","Test2","fe070009","","0","0",[{"name":"value","profile":"u8","position":1,"master":false}],"0",""]
      ])";

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
