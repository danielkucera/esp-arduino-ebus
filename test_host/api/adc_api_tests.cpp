#define private public
#define protected public
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "api/adc_api.hpp"
#include "api/adc_stub_state.hpp"
#include "hardware/adc.hpp"
#undef private
#undef protected

TEST_CASE("AdcApi serves the ADC page", "[adc_api]") {
  httpd_req_t req{};

  REQUIRE(AdcApi::handleAdcPage(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.content_type == "text/html");
  REQUIRE(req.final_body == "<html><body>adc</body></html>");
}

TEST_CASE("AdcApi raw handler reports adc metadata and streams payload",
          "[adc_api]") {
  HostAdcStub::reset();
  Adc adc_inst;
  AdcApi api(adc_inst);

  httpd_req_t req{};
  req.query = "sample_rate=30000&samples_per_channel=8&channels=0,1";
  req.content_len = 0;

  REQUIRE(AdcApi::handleAdcRaw(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.content_type == "application/octet-stream");
  REQUIRE(HostAdcStub::state().stream_calls == 1);
  REQUIRE(HostAdcStub::state().stream_sample_rate == 30000);
  REQUIRE(HostAdcStub::state().stream_samples_per_channel == 8);
  REQUIRE(HostAdcStub::state().stream_channel_mask == 0x03);
  REQUIRE(std::any_of(req.headers.begin(), req.headers.end(),
                      [](const std::pair<std::string, std::string>& header) {
                        return header.first == "X-ADC-Format" &&
                               header.second == "esp32c3-ch12c3-le16";
                      }));
  REQUIRE(req.chunks.size() > 0);
  REQUIRE(req.chunks.front() == std::string("\x01\x02\x03\x04", 4));
  REQUIRE(req.chunks.back().empty());
}

TEST_CASE("AdcApi uses query aliases and defaults", "[adc_api]") {
  HostAdcStub::reset();
  Adc adc_inst;
  AdcApi api(adc_inst);

  httpd_req_t req{};
  req.query = "sample_count=4&channels=2,4";

  REQUIRE(AdcApi::handleAdcRaw(&req) == ESP_OK);
  REQUIRE(HostAdcStub::state().stream_sample_rate == 30000);
  REQUIRE(HostAdcStub::state().stream_samples_per_channel == 4);
  REQUIRE(HostAdcStub::state().stream_channel_mask == 0x14);
  REQUIRE(HostAdcStub::state().effective_sample_rate == 30000);
}

TEST_CASE("AdcApi reports ADC startup and streaming failures", "[adc_api]") {
  HostAdcStub::reset();
  Adc adc_inst;
  AdcApi api(adc_inst);

  HostAdcStub::state().begin_result = false;
  httpd_req_t startup_req{};
  REQUIRE(AdcApi::handleAdcRaw(&startup_req) == ESP_OK);
  REQUIRE(startup_req.status == "500 Internal Server Error");
  REQUIRE(startup_req.final_body.find("ADC not running or failed to start") !=
          std::string::npos);
  REQUIRE(HostAdcStub::state().stream_calls == 0);

  HostAdcStub::reset();
  HostAdcStub::state().running = true;
  HostAdcStub::state().stream_result = false;
  httpd_req_t stream_req{};
  REQUIRE(AdcApi::handleAdcRaw(&stream_req) == ESP_FAIL);
  REQUIRE(HostAdcStub::state().stream_calls == 1);
}

TEST_CASE("AdcApi state and enable endpoints reflect ADC state", "[adc_api]") {
  HostAdcStub::reset();
  Adc adc_inst;
  AdcApi api(adc_inst);

  httpd_req_t state_req{};
  REQUIRE(AdcApi::handleAdcState(&state_req) == ESP_OK);
  std::string state_body;
  for (const auto& chunk : state_req.chunks) state_body += chunk;
  REQUIRE(state_body.find(R"("running":false)") != std::string::npos);

  httpd_req_t enable_req{};
  REQUIRE(AdcApi::handleAdcEnable(&enable_req) == ESP_OK);
  REQUIRE(enable_req.status == "200 OK");
  REQUIRE(HostAdcStub::state().running);

  httpd_req_t disable_req{};
  REQUIRE(AdcApi::handleAdcDisable(&disable_req) == ESP_OK);
  REQUIRE(disable_req.status == "200 OK");
  REQUIRE_FALSE(HostAdcStub::state().running);
  REQUIRE(HostAdcStub::state().stop_calls == 1);
}
