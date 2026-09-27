#define private public
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "api/logs_api.hpp"
#undef private

TEST_CASE("LogsApi passes the since query to the logger", "[logs_api]") {
  Logger logger_inst;
  LogsApi api(logger_inst);
  httpd_req_t req{};
  req.query = "since=123456";

  REQUIRE(LogsApi::handleLogs(&req) == ESP_OK);
  REQUIRE(req.content_type == "application/json;charset=utf-8");
  REQUIRE(logger_inst.lastSinceMillis() == 123456);
  REQUIRE(logger_inst.fetchCount() == 1);
  REQUIRE(req.chunks.size() == 2);
  REQUIRE(req.chunks.front() == R"({"logs":[]})");
  REQUIRE(req.chunks.back().empty());
}

TEST_CASE("LogsApi rejects oversized queries without fetching logs",
          "[logs_api]") {
  Logger logger_inst;
  LogsApi api(logger_inst);
  httpd_req_t req{};
  req.query = std::string(256, 'x');

  REQUIRE(LogsApi::handleLogs(&req) == ESP_OK);
  REQUIRE(req.status == "414 URI Too Long");
  REQUIRE(logger_inst.fetchCount() == 0);
}
