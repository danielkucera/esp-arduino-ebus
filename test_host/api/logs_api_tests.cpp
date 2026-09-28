#define private public
#include <catch2/catch_test_macros.hpp>
#include <ebus/detail/json_reader.hpp>
#include <string>

#include "api/logs_api.hpp"
#include "api/system_api_stub_state.hpp"
#undef private

TEST_CASE("LogsApi serves the logs page", "[logs_api]") {
  httpd_req_t req{};

  REQUIRE(LogsApi::handleLogsPage(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.content_type == "text/html");
  REQUIRE(req.final_body == "<html><body>logs</body></html>");
}

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

TEST_CASE("LogsApi streams time relation JSON", "[logs_api]") {
  httpd_req_t req{};

  REQUIRE(LogsApi::handleLogsTimeRelation(&req) == ESP_OK);
  REQUIRE(req.content_type == "application/json;charset=utf-8");
  REQUIRE(req.chunks.size() == 2);
  REQUIRE(req.chunks.front() == R"({"time_relation":true})");
  REQUIRE(req.chunks.back().empty());
}

TEST_CASE("LogsApi tap endpoint forwards since and streams tap JSON",
          "[logs_api]") {
  Logger logger_inst;
  LogsApi api(logger_inst);
  HostSystemApiStub::resetTap();
  httpd_req_t req{};
  req.query = "since=1700000000123";

  REQUIRE(LogsApi::handleTap(&req) == ESP_OK);
  REQUIRE(req.content_type == "application/json;charset=utf-8");
  REQUIRE(HostSystemApiStub::tapSinceMillis() == 1700000000123ULL);
  std::string body;
  for (const auto& chunk : req.chunks) body += chunk;
  REQUIRE(ebus::detail::JsonReader::validate(body));
  REQUIRE(body == R"({"tap":[],"dropped":0,"capacity":0})");
  REQUIRE(req.chunks.back().empty());
}
