#define private public
#define protected public
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "api/adc_api.hpp"
#include "hardware/adc.hpp"
#undef private
#undef protected

TEST_CASE("AdcApi raw handler reports adc metadata and streams payload",
          "[adc_api]") {
  Adc adc_inst;
  AdcApi api(adc_inst);

  httpd_req_t req{};
  req.query = "sample_rate=30000&samples_per_channel=8&channels=0,1";
  req.content_len = 0;

  REQUIRE(AdcApi::handleAdcRaw(&req) == ESP_OK);
  REQUIRE(req.status == "200 OK");
  REQUIRE(req.content_type == "application/octet-stream");
  REQUIRE(std::any_of(req.headers.begin(), req.headers.end(),
                      [](const std::pair<std::string, std::string>& header) {
                        return header.first == "X-ADC-Format" &&
                               header.second == "esp32c3-ch12c3-le16";
                      }));
  REQUIRE(req.chunks.size() > 0);
}

TEST_CASE("AdcApi state and enable endpoints do not fail", "[adc_api]") {
  Adc adc_inst;
  AdcApi api(adc_inst);

  httpd_req_t state_req{};
  REQUIRE(AdcApi::handleAdcState(&state_req) == ESP_OK);

  httpd_req_t enable_req{};
  REQUIRE(AdcApi::handleAdcEnable(&enable_req) == ESP_OK);

  httpd_req_t disable_req{};
  REQUIRE(AdcApi::handleAdcDisable(&disable_req) == ESP_OK);
}
