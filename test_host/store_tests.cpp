#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>

#include "Command.hpp"
#include "Store.hpp"

using namespace ebus::detail;

Command makeCommand(const std::string& key, const std::string& name,
                    bool active, bool master, int position,
                    const std::string& datatype, const std::string& read_cmd) {
  std::string json =
      R"({"key":")" + key + R"(","name":")" + name + R"(","read_cmd":")" +
      read_cmd + R"(","write_cmd":"","active":)" + (active ? "true" : "false") +
      R"(,"interval":0,"master":)" + (master ? "true" : "false") +
      R"(,"position":)" + std::to_string(position) + R"(","datatype":")" +
      datatype +
      R"(","divider":1,"min":0,"max":0,"digits":0,"unit":"","ha":false,"ha_profile":""})";
  JsonReader reader(json);
  return Command::fromJson(reader);
}

TEST_CASE("Store insert and find by key", "[Store]") {
  store.wipeCommands();
  Command cmd = makeCommand("01", "Test", true, true, 1, "UINT8", "fe070009");
  store.insertCommand(cmd);

  Command* found = store.findCommand("01");
  REQUIRE(found != nullptr);
  REQUIRE(found->getKey() == "01");
  REQUIRE(found->getName() == "Test");

  Command* not_found = store.findCommand("99");
  REQUIRE(not_found == nullptr);
}

TEST_CASE("Store insert updates existing command", "[Store]") {
  store.wipeCommands();
  Command cmd1 = makeCommand("01", "First", true, true, 1, "UINT8", "fe070009");
  store.insertCommand(cmd1);

  Command cmd2 =
      makeCommand("01", "Updated", false, true, 1, "UINT8", "fe070009");
  store.insertCommand(cmd2);

  Command* found = store.findCommand("01");
  REQUIRE(found != nullptr);
  REQUIRE(found->getName() == "Updated");
  REQUIRE(found->getActive() == false);
}

TEST_CASE("Store remove command", "[Store]") {
  store.wipeCommands();
  Command cmd = makeCommand("01", "Test", true, true, 1, "UINT8", "fe070009");
  store.insertCommand(cmd);

  store.removeCommand("01");
  REQUIRE(store.findCommand("01") == nullptr);
}

TEST_CASE("Store getCommands returns all commands", "[Store]") {
  store.wipeCommands();
  for (int i = 0; i < 5; i++) {
    Command cmd = makeCommand(std::to_string(i), "Test " + std::to_string(i),
                              true, true, 1, "UINT8", "fe070009");
    store.insertCommand(cmd);
  }

  auto cmds = store.getCommands();
  REQUIRE(cmds.size() == 5);
}

TEST_CASE("Store getActiveCommands counts active only", "[Store]") {
  store.wipeCommands();
  Command cmd1 =
      makeCommand("01", "Active", true, true, 1, "UINT8", "fe070009");
  store.insertCommand(cmd1);

  Command cmd2 =
      makeCommand("02", "Inactive", false, true, 1, "UINT8", "fe070009");
  store.insertCommand(cmd2);

  REQUIRE(store.getActiveCommands() == 1);
  REQUIRE(store.getPassiveCommands() == 1);
}

TEST_CASE("Store findAllMatchingCommands matches read_cmd", "[Store]") {
  store.wipeCommands();
  Command cmd1 = makeCommand("01", "Match", true, true, 1, "UINT8", "fe070009");
  store.insertCommand(cmd1);

  Command cmd2 =
      makeCommand("02", "NoMatch", true, true, 1, "UINT8", "080b09010a00");
  store.insertCommand(cmd2);

  uint8_t master_bytes[] = {0x10, 0xfe, 0x07, 0x00, 0x09};
  ebus::ByteView master(master_bytes, 5);

  auto matches = store.findAllMatchingCommands(master);
  REQUIRE(matches.size() == 1);
  REQUIRE(matches[0]->getKey() == "01");
}

TEST_CASE("Store updateData sets data", "[Store]") {
  store.wipeCommands();
  Command cmd = makeCommand("01", "Test", true, true, 1, "UINT8", "fe070009");
  store.insertCommand(cmd);

  uint8_t master_bytes[] = {0x10, 0xfe, 0x07, 0x00, 0x09, 0x00};
  ebus::ByteView master(master_bytes, 6);

  store.updateData(nullptr, master, {});

  Command* found = store.findCommand("01");
  REQUIRE(found->getData().size() > 0);
}

TEST_CASE("Store loadCommandsFrom streams JSON from file", "[Store]") {
  store.wipeCommands();

  const char* json = R"([
    {"key":"01","name":"Test1","read_cmd":"fe070009","write_cmd":"",
     "active":true,"interval":60,"master":true,"position":1,
     "datatype":"UINT8","divider":1,"min":0,"max":0,"digits":0,"unit":"",
     "ha":false,"ha_profile":""},
    {"key":"02","name":"Test2","read_cmd":"fe070009","write_cmd":"",
     "active":false,"interval":0,"master":false,"position":1,
     "datatype":"UINT8","divider":1,"min":0,"max":0,"digits":0,"unit":"",
     "ha":false,"ha_profile":""}
  ])";

  const char* tmp_path = "/tmp/test_commands_stream.json";
  FILE* f = std::fopen(tmp_path, "wb");
  REQUIRE(f != nullptr);
  std::fwrite(json, 1, std::strlen(json), f);
  std::fclose(f);

  int64_t bytes = store.loadCommandsFrom(tmp_path);
  REQUIRE(bytes >= 0);
  REQUIRE(store.getCommandCount() == 2);
  REQUIRE(store.findCommand("01") != nullptr);
  REQUIRE(store.findCommand("02") != nullptr);

  std::remove(tmp_path);
}

TEST_CASE("Store loadCommandsFrom streams large JSON (47 commands)",
          "[Store]") {
  store.wipeCommands();

  // Write a 4000+ byte JSON that spans multiple 1024-byte buffer fills.
  FILE* f = std::fopen("/tmp/test_large_stream.json", "wb");
  REQUIRE(f != nullptr);

  std::fputs("[\n", f);
  for (int i = 1; i <= 47; i++) {
    char buf[1024];
    if (i == 33) {
      int len = snprintf(
          buf, sizeof(buf),
          "  {\"key\":\"%02d\",\"name\":\"Cmd_%02d\",\"read_cmd\":\"%02xb50903"
          "%02x%02x00\",\"write_cmd\":\"\",\"active\":true,\"interval\":60,"
          "\"master\":false,\"position\":1,\"datatype\":\"UINT8\",\"divider\":"
          "1,"
          "\"min\":0,\"max\":0,\"digits\":0,\"unit\":\"\",\"ha\":true,"
          "\"ha_profile\":\"select_enum\"}%s\n",
          i, i, (i * 10) % 256, (i * 10 + 1) % 256, (i * 10 + 2) % 256,
          (i < 47) ? "," : "");
      std::fwrite(buf, 1, len, f);
    } else if (i == 44) {
      int len = snprintf(
          buf, sizeof(buf),
          "  {\"key\":\"%02d\",\"name\":\"Cmd_%02d\",\"read_cmd\":\"%02xb50903"
          "%02x%02x00\",\"write_cmd\":\"\",\"active\":true,\"interval\":60,"
          "\"master\":false,\"position\":1,\"datatype\":\"UINT8\",\"divider\":"
          "1,"
          "\"min\":0,\"max\":0,\"digits\":0,\"unit\":\"\",\"ha\":true,"
          "\"ha_profile\":\"sensor_enum_ok_error\"}%s\n",
          i, i, (i * 10) % 256, (i * 10 + 1) % 256, (i * 10 + 2) % 256,
          (i < 47) ? "," : "");
      std::fwrite(buf, 1, len, f);
    } else {
      int len = snprintf(
          buf, sizeof(buf),
          "  {\"key\":\"%02d\",\"name\":\"Cmd_%02d\",\"read_cmd\":\"%02xb50903"
          "%02x%02x00\",\"write_cmd\":\"\",\"active\":%s,\"interval\":60,"
          "\"master\":false,\"position\":1,\"datatype\":\"UINT8\",\"divider\":"
          "1,"
          "\"min\":0,\"max\":0,\"digits\":0,\"unit\":\"\",\"ha\":false,"
          "\"ha_profile\":\"\"}%"
          "s\n",
          i, i, (i * 10) % 256, (i * 10 + 1) % 256, (i * 10 + 2) % 256,
          (i % 5 == 0) ? "false" : "true", (i < 47) ? "," : "");
      std::fwrite(buf, 1, len, f);
    }
  }
  std::fputs("]\n", f);
  std::fclose(f);

  int64_t bytes = store.loadCommandsFrom("/tmp/test_large_stream.json");
  REQUIRE(bytes >= 0);
  REQUIRE(store.getCommandCount() == 47);

  for (int i = 1; i <= 47; i++) {
    char key[4];
    snprintf(key, sizeof(key), "%02d", i);
    REQUIRE(store.findCommand(key) != nullptr);
  }

  std::remove("/tmp/test_large_stream.json");
}

TEST_CASE("Store loadCommandsFrom streams JSON with select_enum profile",
          "[Store]") {
  store.wipeCommands();

  const char* json = R"([
    {
      "key":"01","name":"Test1","read_cmd":"fe070009","write_cmd":"",
      "active":true,"interval":60,"master":true,"position":1,
      "datatype":"UINT8","divider":1,"min":0,"max":0,"digits":0,"unit":"",
      "ha":true,"ha_profile":"select_enum"
    }
  ])";

  const char* tmp_path = "/tmp/test_nested_stream.json";
  FILE* f = std::fopen(tmp_path, "wb");
  REQUIRE(f != nullptr);
  std::fwrite(json, 1, std::strlen(json), f);
  std::fclose(f);

  int64_t bytes = store.loadCommandsFrom(tmp_path);
  REQUIRE(bytes >= 0);
  REQUIRE(store.getCommandCount() == 1);
  REQUIRE(store.findCommand("01") != nullptr);

  std::remove(tmp_path);
}

TEST_CASE("Store loadCommandsFrom loads large commands", "[Store]") {
  store.wipeCommands();

  const char* path = "/tmp/test_kvm_stream.json";
  FILE* f = std::fopen(path, "wb");
  REQUIRE(f != nullptr);

  std::fputs("[\n", f);
  for (int i = 1; i <= 47; i++) {
    char buf[1024];
    if (i == 33) {
      int len = snprintf(
          buf, sizeof(buf),
          "  "
          "{\"key\":\"%02d\",\"name\":\"Cmd_%02d\",\"read_cmd\":"
          "\"50b509030d2b00\","
          "\"write_cmd\":\"\",\"active\":true,\"interval\":60,"
          "\"master\":false,\"position\":1,\"datatype\":\"UINT8\",\"divider\":"
          "1,"
          "\"min\":0,\"max\":0,\"digits\":0,\"unit\":\"\",\"ha\":true,"
          "\"ha_profile\":\"select_enum\"}",
          i, i, (i < 47) ? "," : "");
      len += snprintf(buf + len, sizeof(buf) - len, "%s\n", "");
      std::fwrite(buf, 1, len, f);
    } else if (i == 44) {
      int len =
          snprintf(buf, sizeof(buf),
                   "  "
                   "{\"key\":\"%02d\",\"name\":\"Cmd_%02d\",\"read_cmd\":"
                   "\"b8b50903b9ba00\","
                   "\"write_cmd\":\"\",\"active\":true,\"interval\":60,"
                   "\"master\":false,\"position\":1,\"datatype\":\"UINT8\","
                   "\"divider\":1,"
                   "\"min\":0,\"max\":0,\"digits\":0,\"unit\":\"\",\"ha\":true,"
                   "\"ha_profile\":\"sensor_enum_ok_error\"}",
                   i, i, (i < 47) ? "," : "");
      len += snprintf(buf + len, sizeof(buf) - len, "%s\n", "");
      std::fwrite(buf, 1, len, f);
    } else {
      int len = snprintf(
          buf, sizeof(buf),
          "  {\"key\":\"%02d\",\"name\":\"Cmd_%02d\",\"read_cmd\":\"%02xb50903"
          "%02x%02x00\",\"write_cmd\":\"\",\"active\":%s,\"interval\":60,"
          "\"master\":false,\"position\":1,\"datatype\":\"UINT8\",\"divider\":"
          "1,"
          "\"min\":0,\"max\":0,\"digits\":0,\"unit\":\"\",\"ha\":false,"
          "\"ha_profile\":\"\"}%"
          "s\n",
          i, i, (i * 10) % 256, (i * 10 + 1) % 256, (i * 10 + 2) % 256,
          (i % 5 == 0) ? "false" : "true", (i < 47) ? "," : "");
      std::fwrite(buf, 1, len, f);
    }
  }
  std::fputs("]\n", f);
  std::fclose(f);

  int64_t bytes = store.loadCommandsFrom(path);
  REQUIRE(bytes >= 0);
  REQUIRE(store.getCommandCount() == 47);

  for (int i = 1; i <= 47; i++) {
    char key[4];
    snprintf(key, sizeof(key), "%02d", i);
    REQUIRE(store.findCommand(key) != nullptr);
  }

  std::remove(path);

  store.wipeCommands();

  const char* json =
      R"([["key","name","read_cmd","write_cmd","active","interval","master","position","datatype","divider","min","max","digits","unit","ha","ha_profile"],
        ["01","Test1","fe070009","","1","60","1","1","UINT8","1","0","0","0","","0"],
        ["02","Test2","fe070009","","0","0","0","1","UINT8","1","0","0","0","","0"]
      ])";

  const char* tmp_path = "/tmp/test_tabular_stream.json";
  f = std::fopen(tmp_path, "wb");
  REQUIRE(f != nullptr);
  std::fwrite(json, 1, std::strlen(json), f);
  std::fclose(f);

  bytes = store.loadCommandsFrom(tmp_path);
  REQUIRE(bytes >= 0);
  REQUIRE(store.getCommandCount() == 2);
  REQUIRE(store.findCommand("01") != nullptr);
  REQUIRE(store.findCommand("02") != nullptr);

  std::remove(tmp_path);
}
