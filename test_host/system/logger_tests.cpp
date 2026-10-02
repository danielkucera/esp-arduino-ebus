#include <esp_timer.h>

#include <catch2/catch_test_macros.hpp>
#include <ebus/detail/json_reader.hpp>
#include <string>

#include "system/logger.hpp"

namespace {

std::string fetch(Logger& logger, uint64_t since = 0) {
  std::string result;
  logger.fetchLogs([&result](std::string_view chunk) { result.append(chunk); },
                   since);
  return result;
}

}  // namespace

TEST_CASE("Logger ring retains newest entries in chronological order",
          "[logger]") {
  Logger logger(3);
  LoggerTestState::now_us = 1000000;
  logger.info("first");
  LoggerTestState::now_us = 2000000;
  logger.warn("second");
  LoggerTestState::now_us = 3000000;
  logger.error("third");
  LoggerTestState::now_us = 4000000;
  logger.debug("fourth");

  const std::string json = fetch(logger);
  REQUIRE(ebus::detail::JsonReader::validate(json));
  REQUIRE(logger.getRingOverwriteCount() == 1);
  REQUIRE(json.find(R"("message":"first")") == std::string::npos);
  const auto second = json.find(R"("message":"second")");
  const auto third = json.find(R"("message":"third")");
  const auto fourth = json.find(R"("message":"fourth")");
  REQUIRE(second != std::string::npos);
  REQUIRE(third != std::string::npos);
  REQUIRE(fourth != std::string::npos);
  REQUIRE(second < third);
  REQUIRE(third < fourth);
  REQUIRE(json.find(R"("level":"WARN")") != std::string::npos);
  REQUIRE(json.find(R"("level":"ERROR")") != std::string::npos);
  REQUIRE(json.find(R"("level":"DEBUG")") != std::string::npos);
}

TEST_CASE("Logger since filter includes entries at the boundary", "[logger]") {
  Logger logger(4);
  LoggerTestState::now_us = 1000000;
  logger.info("old");
  LoggerTestState::now_us = 2000000;
  logger.info("boundary");
  LoggerTestState::now_us = 3000000;
  logger.info("new");

  const std::string json = fetch(logger, 2000);
  REQUIRE(ebus::detail::JsonReader::validate(json));
  REQUIRE(json.find(R"("message":"old")") == std::string::npos);
  REQUIRE(json.find(R"("message":"boundary")") != std::string::npos);
  REQUIRE(json.find(R"("message":"new")") != std::string::npos);
}

TEST_CASE("Logger JSON-escapes text and preserves raw JSON messages",
          "[logger]") {
  Logger logger(4);
  LoggerTestState::now_us = 1234000;
  logger.info("quote \" and slash \\");
  LoggerTestState::now_us = 2345000;
  logger.info(R"({"ready":true,"count":2})", true, 17, 9);
  LoggerTestState::now_us = 3456000;
  logger.error("without ids");

  const std::string json = fetch(logger);
  REQUIRE(ebus::detail::JsonReader::validate(json));
  REQUIRE(json.find(R"("message":"quote \" and slash \\")") !=
          std::string::npos);
  REQUIRE(json.find(R"("message":{"ready":true,"count":2})") !=
          std::string::npos);
  REQUIRE(json.find(R"("sid":17)") != std::string::npos);
  REQUIRE(json.find(R"("pid":9)") != std::string::npos);
  REQUIRE(json.find(R"("message":"without ids")") != std::string::npos);
  REQUIRE(json.find(R"("sid":0)") == std::string::npos);
  REQUIRE(json.find(R"("pid":0)") == std::string::npos);
}

TEST_CASE("Logger ring capacity defaults safely for zero and oversized input",
          "[logger]") {
  Logger zero_capacity(0);
  LoggerTestState::now_us = 1000000;
  zero_capacity.info("kept");
  REQUIRE(zero_capacity.getRingOverwriteCount() == 0);
  REQUIRE(fetch(zero_capacity).find(R"("message":"kept")") !=
          std::string::npos);

  Logger oversized_capacity(5);
  for (int i = 0; i < 5; ++i) {
    oversized_capacity.info("bounded");
  }
  REQUIRE(oversized_capacity.getRingOverwriteCount() == 1);
  REQUIRE(fetch(oversized_capacity).find(R"("message":"bounded")") !=
          std::string::npos);
}
