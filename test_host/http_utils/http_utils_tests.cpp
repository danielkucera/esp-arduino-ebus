#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <ebus/detail/json_reader.hpp>
#include <string>

#include "logger_stub_state.hpp"
#include "network/http_utils.hpp"

TEST_CASE("StreamingReader feeds partial request reads", "[http_utils]") {
  const std::string payload = R"({"value":42})";
  httpd_req_t req{};
  req.body = payload;
  req.content_len = static_cast<int>(payload.size());
  req.recv_chunk_limit = 3;

  HttpUtils::StreamingReader reader(&req);
  REQUIRE(reader.isValid());
  REQUIRE(reader.feedAll());
  reader.endOfInput();

  auto& json = reader.jsonReader();
  REQUIRE(json.next() == ebus::detail::JsonReader::Token::object_start);
  REQUIRE(json.findKey("value"));
  REQUIRE(json.next() == ebus::detail::JsonReader::Token::number);
  REQUIRE(json.asNum<int>() == 42);
  REQUIRE(req.received_bytes == payload.size());
}

TEST_CASE("StreamingReader accepts a body larger than its stream buffer",
          "[http_utils]") {
  const std::string value(5000, 'x');
  const std::string payload = R"({"value":")" + value + R"("})";
  httpd_req_t req{};
  req.body = payload;
  req.content_len = static_cast<int>(payload.size());
  req.recv_chunk_limit = 257;

  HttpUtils::StreamingReader reader(&req);
  REQUIRE(reader.isValid());
  REQUIRE(reader.feedAll());
  reader.endOfInput();
  auto& json = reader.jsonReader();
  REQUIRE(json.next() == ebus::detail::JsonReader::Token::object_start);
  REQUIRE(json.findKey("value"));
  REQUIRE(json.next() == ebus::detail::JsonReader::Token::string);
  REQUIRE(json.value().size() == value.size());
  REQUIRE(req.received_bytes == payload.size());
}

TEST_CASE("StreamingReader boundary switches at stream buffer size",
          "[http_utils]") {
  const std::string prefix = R"({"x":")";
  const std::string suffix = R"("})";
  const std::string streaming_payload =
      prefix +
      std::string(
          HttpUtils::streaming_buffer_size - prefix.size() - suffix.size(),
          'a') +
      suffix;
  httpd_req_t streaming_req{};
  streaming_req.body = streaming_payload;
  streaming_req.content_len = static_cast<int>(streaming_payload.size());
  streaming_req.recv_chunk_limit = 193;
  {
    HttpUtils::StreamingReader reader(&streaming_req);
    REQUIRE(reader.isValid());
    REQUIRE(reader.feedAll());
    reader.endOfInput();
    auto& json = reader.jsonReader();
    REQUIRE(json.next() == ebus::detail::JsonReader::Token::object_start);
    REQUIRE(json.findKey("x"));
    REQUIRE(json.next() == ebus::detail::JsonReader::Token::string);
    REQUIRE(json.value().size() ==
            HttpUtils::streaming_buffer_size - prefix.size() - suffix.size());
  }
  REQUIRE(streaming_req.received_bytes == streaming_payload.size());

  const std::string fallback_payload =
      prefix +
      std::string(
          HttpUtils::streaming_buffer_size + 1 - prefix.size() - suffix.size(),
          'b') +
      suffix;
  httpd_req_t fallback_req{};
  fallback_req.body = fallback_payload;
  fallback_req.content_len = static_cast<int>(fallback_payload.size());
  fallback_req.recv_chunk_limit = 193;
  {
    HttpUtils::StreamingReader reader(&fallback_req);
    REQUIRE(reader.isValid());
    REQUIRE(reader.feedAll());
    auto& json = reader.jsonReader();
    REQUIRE(json.next() == ebus::detail::JsonReader::Token::object_start);
    REQUIRE(json.findKey("x"));
    REQUIRE(json.next() == ebus::detail::JsonReader::Token::string);
    REQUIRE(json.value().size() == HttpUtils::streaming_buffer_size + 1 -
                                       prefix.size() - suffix.size());
  }
  REQUIRE(fallback_req.received_bytes == fallback_payload.size());
}

TEST_CASE("HTTP body size limit accepts exact maximum and rejects one over",
          "[http_utils]") {
  const std::string prefix = R"({"x":")";
  const std::string suffix = R"("})";
  const std::string accepted = prefix +
                               std::string(HttpUtils::max_request_body_size -
                                               prefix.size() - suffix.size(),
                                           'c') +
                               suffix;
  httpd_req_t accepted_req{};
  accepted_req.body = accepted;
  accepted_req.content_len = static_cast<int>(accepted.size());
  accepted_req.recv_chunk_limit = 509;
  {
    HttpUtils::StreamingReader reader(&accepted_req);
    REQUIRE(reader.isValid());
    REQUIRE(reader.feedAll());
    auto& json = reader.jsonReader();
    REQUIRE(json.next() == ebus::detail::JsonReader::Token::object_start);
    REQUIRE(json.findKey("x"));
    REQUIRE(json.next() == ebus::detail::JsonReader::Token::string);
    REQUIRE(json.value().size() ==
            HttpUtils::max_request_body_size - prefix.size() - suffix.size());
  }
  REQUIRE(accepted_req.received_bytes == accepted.size());

  const std::string rejected =
      prefix +
      std::string(
          HttpUtils::max_request_body_size + 1 - prefix.size() - suffix.size(),
          'd') +
      suffix;
  httpd_req_t rejected_req{};
  rejected_req.body = rejected;
  rejected_req.content_len = static_cast<int>(rejected.size());
  HttpUtilsTestState::clearLoggerMessages();
  HttpUtils::StreamingReader reader(&rejected_req);
  REQUIRE_FALSE(reader.isValid());
  REQUIRE(rejected_req.received_bytes == 0);
  REQUIRE(HttpUtilsTestState::last_warning.find("Request body too large") !=
          std::string::npos);
}

TEST_CASE("StreamingReader rejects oversized bodies and receive failures",
          "[http_utils]") {
  HttpUtilsTestState::clearLoggerMessages();
  httpd_req_t oversized{};
  oversized.content_len =
      static_cast<int>(HttpUtils::max_request_body_size + 1);
  HttpUtils::StreamingReader oversized_reader(&oversized);
  REQUIRE_FALSE(oversized_reader.isValid());
  REQUIRE(HttpUtilsTestState::last_warning.find("Request body too large") !=
          std::string::npos);

  httpd_req_t failed{};
  failed.body = R"({"value":42})";
  failed.content_len = static_cast<int>(failed.body.size());
  failed.recv_fail_after = 4;
  failed.recv_chunk_limit = 2;
  {
    HttpUtils::StreamingReader failed_reader(&failed);
    REQUIRE(failed_reader.isValid());
    REQUIRE_FALSE(failed_reader.feedAll());
    REQUIRE(failed.received_bytes == 4);
  }

  httpd_req_t next{};
  next.body = "[]";
  next.content_len = static_cast<int>(next.body.size());
  HttpUtils::StreamingReader next_reader(&next);
  REQUIRE(next_reader.isValid());
  REQUIRE(next_reader.feedAll());
}

TEST_CASE("readBody handles chunked reads, limits and failures",
          "[http_utils]") {
  std::string payload(HttpUtils::streaming_buffer_size + 17, 'a');
  httpd_req_t req{};
  req.body = payload;
  req.content_len = static_cast<int>(payload.size());
  req.recv_chunk_limit = 111;
  REQUIRE(HttpUtils::readBody(&req) == payload);
  REQUIRE(req.received_bytes == payload.size());

  HttpUtilsTestState::clearLoggerMessages();
  httpd_req_t oversized{};
  oversized.content_len =
      static_cast<int>(HttpUtils::max_request_body_size + 1);
  REQUIRE(HttpUtils::readBody(&oversized).empty());
  REQUIRE(HttpUtilsTestState::last_warning.find("Request body too large") !=
          std::string::npos);

  httpd_req_t failed{};
  failed.body = "partial";
  failed.content_len = 20;
  failed.recv_fail_after = 3;
  failed.recv_chunk_limit = 3;
  REQUIRE(HttpUtils::readBody(&failed).empty());
}

TEST_CASE("prepareJsonReaderForArray supports arrays and keyed arrays",
          "[http_utils]") {
  std::string error;
  ebus::detail::JsonReader direct(R"([1,2])");
  REQUIRE(HttpUtils::prepareJsonReaderForArray(direct, "items", error));
  REQUIRE(direct.next() == ebus::detail::JsonReader::Token::number);
  REQUIRE(direct.asNum<int>() == 1);

  ebus::detail::JsonReader keyed(R"({"items":[{"id":"a"}]})");
  REQUIRE(HttpUtils::prepareJsonReaderForArray(keyed, "items", error));
  REQUIRE(keyed.next() == ebus::detail::JsonReader::Token::object_start);

  ebus::detail::JsonReader missing(R"({"other":[]})");
  REQUIRE_FALSE(HttpUtils::prepareJsonReaderForArray(missing, "items", error));
  REQUIRE(error.find("must contain") != std::string::npos);

  error.clear();
  ebus::detail::JsonReader wrong_type(R"({"items":{}})");
  REQUIRE_FALSE(
      HttpUtils::prepareJsonReaderForArray(wrong_type, "items", error));
  REQUIRE(error == "Expected a JSON array.");

  error.clear();
  ebus::detail::JsonReader scalar("42");
  REQUIRE_FALSE(HttpUtils::prepareJsonReaderForArray(scalar, "items", error));
  REQUIRE(error == "JSON root must be an object or a direct array.");
}

TEST_CASE("custom headers are parsed and applied to responses",
          "[http_utils]") {
  HttpUtils::setCustomHeaders(
      "X-One: alpha\r\nMalformed\n:empty\nX-Two: beta\nX-Empty:\n");

  httpd_req_t req{};
  HttpUtils::sendResponse(&req, "201 Created", "text/plain", "ok");
  REQUIRE(req.status == "201 Created");
  REQUIRE(req.content_type == "text/plain");
  REQUIRE(req.final_body == "ok");
  REQUIRE(req.headers.size() == 2);
  REQUIRE(req.headers[0] ==
          std::pair<std::string, std::string>("X-One", "alpha"));
  REQUIRE(req.headers[1] ==
          std::pair<std::string, std::string>("X-Two", "beta"));

  HttpUtils::setCustomHeaders("");
}

TEST_CASE("standard JSON responses include expected fields and headers",
          "[http_utils]") {
  HttpUtils::setCustomHeaders("X-Test: yes");
  httpd_req_t success{};
  HttpUtils::sendSuccessResponse(&success, "saved", "successful", "done");
  REQUIRE(success.status == "200 OK");
  REQUIRE(success.content_type == "application/json;charset=utf-8");
  REQUIRE(success.final_body ==
          R"({"id":"saved","status":"successful","message":"done"})");
  REQUIRE(success.headers.size() == 1);
  REQUIRE(success.headers[0].second == "yes");

  httpd_req_t failure{};
  HttpUtils::sendErrorResponse(&failure, "400 Bad Request", "save", "invalid");
  REQUIRE(failure.status == "400 Bad Request");
  REQUIRE(failure.final_body ==
          R"({"id":"save","status":"failed","error":"invalid"})");
  REQUIRE(ebus::detail::JsonReader::validate(failure.final_body));
  HttpUtils::setCustomHeaders("");
}

TEST_CASE("registerRoute reports HTTP registration failures", "[http_utils]") {
  HttpUtilsTestState::clearLoggerMessages();
  HttpUtilsTestState::route_result = ESP_OK;
  REQUIRE(HttpUtils::registerRoute(reinterpret_cast<httpd_handle_t>(1), "/test",
                                   HTTP_GET, nullptr));

  HttpUtilsTestState::route_result = ESP_FAIL;
  REQUIRE_FALSE(HttpUtils::registerRoute(reinterpret_cast<httpd_handle_t>(1),
                                         "/failed", HTTP_POST, nullptr));
  REQUIRE(HttpUtilsTestState::last_error.find("/failed") != std::string::npos);
  REQUIRE(HttpUtilsTestState::last_error.find("MOCK_ESP_ERR") !=
          std::string::npos);
  HttpUtilsTestState::route_result = ESP_OK;
}
