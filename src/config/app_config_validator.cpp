#include "config/app_config_validator.hpp"

#include <ebus/detail/json_reader.hpp>

#include "app/app_limits.hpp"

namespace config {

bool AppConfigValidator::validate(const AppConfig& config) {
  // 1. Required network credentials
  if (config.network.wifi_ssid.empty()) return false;

  // 2. PWM
  if (config.pwm.value < app::limits::Pwm::min ||
      config.pwm.value > app::limits::Pwm::max)
    return false;

  // 3. eBUS address must be non-empty and fit the FixedString capacity.
  if (config.bus.address.empty()) return false;

  // 4. Bus timing
  if (config.bus.window_us < app::limits::Bus::window_min_us ||
      config.bus.window_us > app::limits::Bus::window_max_us)
    return false;
  if (config.bus.offset_us < app::limits::Bus::offset_min_us ||
      config.bus.offset_us > app::limits::Bus::offset_max_us)
    return false;

  return true;
}

bool AppConfigValidator::validateJson(std::string_view json) {
  if (!ebus::detail::JsonReader::validate(json)) return false;
  ebus::detail::JsonReader reader(json);

  auto ssid_token = reader.get("network.wifi_ssid");
  if (ssid_token == ebus::detail::JsonReader::Token::string) {
    if (reader.value().empty()) return false;
  }

  auto pwm_token = reader.get("pwm");
  if (pwm_token == ebus::detail::JsonReader::Token::number ||
      pwm_token == ebus::detail::JsonReader::Token::string) {
    auto val = reader.asNumStrict<uint8_t>();
    if (!val || *val < app::limits::Pwm::min || *val > app::limits::Pwm::max)
      return false;
  }

  auto window_token = reader.get("bus.window_us");
  if (window_token == ebus::detail::JsonReader::Token::number ||
      window_token == ebus::detail::JsonReader::Token::string) {
    auto val = reader.asNumStrict<uint16_t>();
    if (!val || *val < app::limits::Bus::window_min_us ||
        *val > app::limits::Bus::window_max_us)
      return false;
  }

  auto offset_token = reader.get("bus.offset_us");
  if (offset_token == ebus::detail::JsonReader::Token::number ||
      offset_token == ebus::detail::JsonReader::Token::string) {
    auto val = reader.asNumStrict<uint16_t>();
    if (!val || *val < app::limits::Bus::offset_min_us ||
        *val > app::limits::Bus::offset_max_us)
      return false;
  }

  return true;
}

}  // namespace config