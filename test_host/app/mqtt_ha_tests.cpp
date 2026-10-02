#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "app/command.hpp"
#include "app/command_manager.hpp"
#include "app/mqtt_ha.hpp"

namespace {

struct PublishedMessage {
  std::string topic;
  std::string payload;
};

struct Recorder {
  std::vector<PublishedMessage> messages;

  MqttHA::Transport transport() {
    MqttHA::Transport t;
    t.publish = [this](const char* topic, uint8_t qos, bool retain,
                       const char* payload, bool prefix) {
      (void)qos;
      (void)retain;
      (void)prefix;
      messages.push_back(
          {topic != nullptr ? topic : "", payload != nullptr ? payload : ""});
    };
    t.publish_stream =
        [this](
            const char* topic, uint8_t qos, bool retain,
            const std::function<void(const ebus::JsonChunkVisitor&)>& builder,
            bool prefix) {
          (void)qos;
          (void)retain;
          (void)prefix;
          std::string payload;
          if (builder) {
            builder([&payload](std::string_view chunk) {
              payload.append(chunk.data(), chunk.size());
            });
          }
          messages.push_back({topic != nullptr ? topic : "", payload});
        };
    return t;
  }
};

MqttHA makeHa(Recorder& recorder) {
  MqttHA ha;
  ha.setUniqueId("820b80");
  ha.setRootTopic("ebus/820b80/");
  ha.setWillTopic("ebus/820b80/state/available");
  ha.setThingName("esp-eBus");
  ha.setThingModel("esp-eBus Adapter");
  ha.setThingModelId("esp-ebus-adapter");
  ha.setThingHwVersion("7.0");
  ha.setEnabled(true);
  ha.setTransport(recorder.transport());
  return ha;
}

void registerTempCommand() {
  commandManager.wipeCommands();
  std::string json =
      R"({"key":"01","name":"Outside_Temperature","read_cmd":"fe070009",)"
      R"("write_cmd":"","interval":0,"master":true,)"
      R"("fields":[{"name":"value","profile":"data2b_celsius","position":1,)"
      R"("ha_profile":"sensor_temperature"}]})";
  ebus::detail::JsonReader reader(json);
  commandManager.insertCommand(Command::fromJson(reader));
}

bool payloadContains(const PublishedMessage& msg, std::string_view needle) {
  return msg.payload.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("MqttHA publishes sensor discovery config", "[mqttha]") {
  Recorder recorder;
  registerTempCommand();
  MqttHA ha = makeHa(recorder);

  const Command* cmd = commandManager.findCommand("01");
  REQUIRE(cmd != nullptr);
  ha.publishComponent(cmd, 0, false);

  REQUIRE(recorder.messages.size() == 1);
  const PublishedMessage& msg = recorder.messages[0];
  REQUIRE(msg.topic ==
          "homeassistant/sensor/ebus820b80/01_outside_temperature_value/"
          "config");
  REQUIRE(payloadContains(msg, "ebus820b80"));
  REQUIRE(payloadContains(msg, "unique_id"));
  REQUIRE(payloadContains(msg, "state_topic"));
  REQUIRE(payloadContains(msg, "device"));
}

TEST_CASE("MqttHA remove publishes empty retained payload", "[mqttha]") {
  Recorder recorder;
  registerTempCommand();
  MqttHA ha = makeHa(recorder);

  const Command* cmd = commandManager.findCommand("01");
  REQUIRE(cmd != nullptr);
  ha.publishComponent(cmd, 0, true);

  REQUIRE(recorder.messages.size() == 1);
  REQUIRE(recorder.messages[0].topic ==
          "homeassistant/sensor/ebus820b80/01_outside_temperature_value/"
          "config");
  REQUIRE(recorder.messages[0].payload.empty());
}

TEST_CASE("MqttHA publishes nothing when disabled", "[mqttha]") {
  Recorder recorder;
  registerTempCommand();
  MqttHA ha = makeHa(recorder);
  ha.setEnabled(false);

  const Command* cmd = commandManager.findCommand("01");
  REQUIRE(cmd != nullptr);
  ha.publishComponentIfEnabled(cmd, 0);

  REQUIRE(recorder.messages.empty());
}

TEST_CASE("MqttHA skips fields without HA profile", "[mqttha]") {
  Recorder recorder;
  commandManager.wipeCommands();
  std::string json =
      R"({"key":"02","name":"Plain","read_cmd":"fe070009","write_cmd":"",)"
      R"("interval":0,"master":true,)"
      R"("fields":[{"name":"value","profile":"uint8","position":1,)"
      R"("ha_profile":""}]})";
  ebus::detail::JsonReader reader(json);
  commandManager.insertCommand(Command::fromJson(reader));

  MqttHA ha = makeHa(recorder);
  const Command* cmd = commandManager.findCommand("02");
  REQUIRE(cmd != nullptr);
  ha.publishComponent(cmd, 0, false);

  REQUIRE(recorder.messages.empty());
}

TEST_CASE("MqttHA device info carries thing identity", "[mqttha]") {
  Recorder recorder;
  MqttHA ha = makeHa(recorder);
  ha.publishDeviceInfo();

  REQUIRE(!recorder.messages.empty());
  bool found_thing = false;
  for (const PublishedMessage& msg : recorder.messages) {
    if (payloadContains(msg, "esp-eBus") && payloadContains(msg, "danman.eu")) {
      found_thing = true;
    }
  }
  REQUIRE(found_thing == true);
}

TEST_CASE("MqttHA without transport drops silently", "[mqttha]") {
  registerTempCommand();
  MqttHA ha;
  ha.setEnabled(true);

  const Command* cmd = commandManager.findCommand("01");
  REQUIRE(cmd != nullptr);
  // No transport injected: must not crash, publishes nothing observable.
  ha.publishComponent(cmd, 0, false);
  ha.publishDeviceInfo();
  SUCCEED();
}
