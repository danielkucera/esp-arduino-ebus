#define private public
#include <catch2/catch_test_macros.hpp>
#include <ebus/detail/json_reader.hpp>
#include <string>

#include "api/system_api.hpp"
#include "ebus_test_helpers.hpp"
#include "system_api_stub_state.hpp"
#undef private

TEST_CASE("SystemApi system endpoint serializes firmware and chip details",
          "[system_api]") {
  SystemApi api;
  httpd_req_t req{};

  REQUIRE(SystemApi::handleSystem(&req) == ESP_OK);
  REQUIRE(req.content_type == "application/json;charset=utf-8");

  std::string body;
  for (const auto& chunk : req.chunks) body += chunk;
  REQUIRE(ebus::detail::JsonReader::validate(body));
  REQUIRE(body.find(R"("firmware":{"version":"host-test")") !=
          std::string::npos);
  REQUIRE(body.find(R"("esp_idf_version":"host-test")") != std::string::npos);
  REQUIRE(body.find(R"("unique_id":"host-test")") != std::string::npos);
  REQUIRE(body.find(R"("clock_speed":240)") != std::string::npos);
  REQUIRE(body.find(R"("apb_speed":240000000)") != std::string::npos);
  REQUIRE(body.find(R"("chip":{"chip_revision":1,"flash_size":4194304})") !=
          std::string::npos);
  REQUIRE(req.chunks.back().empty());
}

TEST_CASE("SystemApi tasks endpoint serializes thread and cpu arrays",
          "[system_api]") {
  REQUIRE(configureHostEbusController());

  SystemApi api;
  httpd_req_t req{};

  REQUIRE(SystemApi::handleTasks(&req) == ESP_OK);
  REQUIRE(req.content_type == "application/json;charset=utf-8");
  std::string body;
  for (const auto& chunk : req.chunks) body += chunk;
  REQUIRE(ebus::detail::JsonReader::validate(body));
  REQUIRE(body.find(R"("name":"ebus_bus","stack_size":-1,"stack_free":-1)") !=
          std::string::npos);
  REQUIRE(
      body.find(R"("name":"ebus_client","stack_size":-1,"stack_free":-1)") !=
      std::string::npos);
  REQUIRE(
      body.find(R"("name":"ebus_reactor","stack_size":-1,"stack_free":-1)") !=
      std::string::npos);
  REQUIRE(body.find(R"("name":"ebus_bus_syn")") == std::string::npos);
  REQUIRE(body.find(R"("cpu":[]})") != std::string::npos);
  REQUIRE(req.chunks.back().empty());
}

TEST_CASE("SystemApi heap endpoint serializes current and trend values",
          "[system_api]") {
  HostSystemApiStub::clearHeapTrend();
  SystemMonitor::HeapSample sample{};
  sample.uptime_seconds = 42;
  sample.free_bytes = 8192;
  sample.min_bytes = 4096;
  sample.largest_block = 2048;
  HostSystemApiStub::setHeapTrend(&sample, 1);

  SystemApi api;
  httpd_req_t req{};

  REQUIRE(SystemApi::handleHeap(&req) == ESP_OK);
  REQUIRE(req.content_type == "application/json;charset=utf-8");
  std::string body;
  for (const auto& chunk : req.chunks) body += chunk;
  REQUIRE(body.find(R"("current":{"free":1024,"largest":512,"min":256})") !=
          std::string::npos);
  REQUIRE(
      body.find(
          R"("trend":[{"uptime":42,"free":8192,"min":4096,"largest":2048}])") !=
      std::string::npos);
  REQUIRE(req.chunks.back().empty());
  HostSystemApiStub::clearHeapTrend();
}
